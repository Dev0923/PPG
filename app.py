"""
MAX30102 + ESP32-S3 PPG Acquisition Server & Dashboard
Single-command launcher: Runs FastAPI backend + WebSocket + Web Dashboard
and automatically opens the web browser.
"""

import time
import asyncio
import threading
import webbrowser
from pathlib import Path
from typing import Optional, Dict, Any, List, Set

import serial
import serial.tools.list_ports
from fastapi import FastAPI, WebSocket, WebSocketDisconnect, HTTPException
from fastapi.responses import HTMLResponse, FileResponse, JSONResponse
from fastapi.staticfiles import StaticFiles
from fastapi.middleware.cors import CORSMiddleware
import uvicorn

# Base directory
BASE_DIR = Path(__file__).resolve().parent
STATIC_DIR = BASE_DIR / "static"
RECORDINGS_DIR = BASE_DIR / "recordings"
STATIC_DIR.mkdir(parents=True, exist_ok=True)
RECORDINGS_DIR.mkdir(parents=True, exist_ok=True)

from contextlib import asynccontextmanager

main_loop: Optional[asyncio.AbstractEventLoop] = None

@asynccontextmanager
async def lifespan(app: FastAPI):
    global main_loop
    main_loop = asyncio.get_running_loop()
    yield

app = FastAPI(title="MAX30102 PPG Acquisition System", lifespan=lifespan)

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# Mount static folder
app.mount("/static", StaticFiles(directory=str(STATIC_DIR)), name="static")

# ==============================================================================
# HARDWARE SERIAL MANAGER
# ==============================================================================
class PPGHardwareManager:
    def __init__(self):
        self.ser: Optional[serial.Serial] = None
        self.port: Optional[str] = None
        self.baudrate: int = 115200
        self.is_running: bool = False
        self.thread: Optional[threading.Thread] = None
        self.client_queues: Set[asyncio.Queue] = set()
        self.lock = threading.Lock()
        
        # Telemetry metrics
        self.latest_sample: Dict[str, Any] = {
            "timestamp_us": 0,
            "seq": 0,
            "red": 0,
            "ir": 0,
            "finger": 0,
            "bpm": 0,
            "rate_hz": 0.0,
        }
        self.sample_count: int = 0
        self.start_time: float = 0
        
        # CSV Recording
        self.is_recording: bool = False
        self.record_file = None
        self.record_filename: Optional[str] = None
        self.record_duration: int = 0
        self.record_start_time: float = 0.0
        self.record_timer: Optional[threading.Timer] = None
        
        # BPM calculation buffer
        self.ir_buffer: List[float] = []
        self.ir_timestamps: List[float] = []
        self.last_beat_time: float = 0
        self.current_bpm: float = 0.0
        self.recent_bpms: List[float] = []

    def register_client(self, q: asyncio.Queue):
        with self.lock:
            self.client_queues.add(q)

    def unregister_client(self, q: asyncio.Queue):
        with self.lock:
            self.client_queues.discard(q)

    def broadcast(self, payload: Dict[str, Any]):
        """Thread-safe broadcast to all connected WebSocket client queues."""
        global main_loop
        if not self.client_queues or not main_loop or not main_loop.is_running():
            return
            
        with self.lock:
            queues = list(self.client_queues)
            
        for q in queues:
            try:
                if q.full():
                    try:
                        q.get_nowait()
                    except Exception:
                        pass
                main_loop.call_soon_threadsafe(q.put_nowait, payload)
            except Exception:
                pass

    def scan_ports(self) -> List[Dict[str, Any]]:
        """Scan all system serial ports and identify likely ESP32 / USB-UART bridges."""
        ports = []
        for p in serial.tools.list_ports.comports():
            # Exclude standard Bluetooth incoming/outgoing links
            is_bluetooth = "BTHENUM" in (p.hwid or "") or "Bluetooth" in (p.description or "")
            
            # Identify ESP32 / CH343 / CH340 / CP210x / FTDI
            is_esp32 = any(sig in (p.description or "").upper() or sig in (p.hwid or "").upper()
                           for sig in ["1A86", "303A", "CH343", "CH340", "CP210", "FTDI", "ESP32", "USB SERIAL"])
            
            ports.append({
                "device": p.device,
                "description": p.description,
                "hwid": p.hwid,
                "is_esp32_candidate": is_esp32,
                "is_bluetooth": is_bluetooth,
            })
        return ports

    def find_esp32_port(self, preferred_port: Optional[str] = None) -> Optional[str]:
        """Find the most likely ESP32 serial port."""
        available_ports = serial.tools.list_ports.comports()
        all_devices = [p.device for p in available_ports]
        
        # 1. If preferred port provided and exists
        if preferred_port and preferred_port in all_devices:
            return preferred_port
            
        # 2. Check COM9 specifically (known CH343 port on this machine)
        if "COM9" in all_devices:
            return "COM9"
            
        # 3. Look for CH343 / ESP32 VID:PID
        for p in available_ports:
            hwid = (p.hwid or "").upper()
            desc = (p.description or "").upper()
            if "1A86:55D3" in hwid or "303A:4001" in hwid or "CH343" in desc or "ESP32" in desc:
                return p.device
                
        # 4. Any non-bluetooth USB serial port
        for p in available_ports:
            hwid = (p.hwid or "").upper()
            desc = (p.description or "").upper()
            if "BTHENUM" not in hwid and "BLUETOOTH" not in desc:
                return p.device
                
        return None

    def start_acquisition(self, port_name: Optional[str] = None) -> Dict[str, Any]:
        """Verify hardware connection and begin continuous PPG stream."""
        with self.lock:
            if self.is_running and self.ser and self.ser.is_open:
                return {"success": True, "message": f"Already connected and measuring on {self.port}."}
                
            target_port = self.find_esp32_port(port_name)
            
            if not target_port:
                ports = self.scan_ports()
                bt_count = sum(1 for p in ports if p["is_bluetooth"])
                msg = "Hardware Not Found! No ESP32-S3 or MAX30102 device detected on your USB ports."
                if bt_count > 0 and len(ports) == bt_count:
                    msg += " Only Bluetooth COM ports were found. Please plug in the ESP32-S3 USB cable."
                else:
                    msg += " Please connect your ESP32-S3 board with a USB-C cable."
                    
                return {
                    "success": False,
                    "error_code": "HARDWARE_NOT_FOUND",
                    "message": msg,
                    "available_ports": [p["device"] for p in ports],
                }
                
            try:
                # Open serial port
                self.ser = serial.Serial(
                    port=target_port,
                    baudrate=self.baudrate,
                    timeout=1.0,
                    write_timeout=1.0
                )
                self.port = target_port
                
                # Ensure DTR and RTS are inactive so ESP32 runs freely without reset hold
                self.ser.dtr = False
                self.ser.rts = False
                time.sleep(0.05)
                
            except serial.SerialException as e:
                err_str = str(e)
                if "could not open port" in err_str.lower() or "permissionerror" in err_str.lower():
                    msg = f"Cannot open port {target_port}: Port is busy or in use by another program (e.g., VS Code Serial Monitor)."
                else:
                    msg = f"Serial communication error on {target_port}: {err_str}"
                    
                return {
                    "success": False,
                    "error_code": "PORT_OPEN_FAILED",
                    "message": msg,
                    "target_port": target_port,
                }
                
            # Read first few lines to verify sensor health
            sensor_fault_detected = False
            fault_details = ""
            start_wait = time.time()
            
            while time.time() - start_wait < 1.5:
                try:
                    line = self.ser.readline().decode("utf-8", errors="ignore").strip()
                    if not line:
                        continue
                    if "MAX30102 communication FAILED" in line or "NO I2C devices acknowledged" in line:
                        sensor_fault_detected = True
                        fault_details = "MAX30102 sensor communication failed! The ESP32 is connected, but the MAX30102 sensor is not responding on I2C (SDA=GPIO8, SCL=GPIO9)."
                        break
                    parts = line.split(",")
                    if len(parts) >= 4 and parts[0].isdigit() and parts[1].isdigit():
                        break
                except Exception:
                    pass
                    
            if sensor_fault_detected:
                self.ser.close()
                self.ser = None
                return {
                    "success": False,
                    "error_code": "SENSOR_COMMUNICATION_FAILED",
                    "message": fault_details,
                    "target_port": target_port,
                }
                
            # Start background reading thread
            self.is_running = True
            self.sample_count = 0
            self.start_time = time.time()
            self.thread = threading.Thread(target=self._reader_worker, daemon=True)
            self.thread.start()
            
            return {
                "success": True,
                "message": f"Connected to ESP32 on {target_port} at 115200 baud. PPG streaming active.",
                "port": target_port,
            }

    def stop_acquisition(self) -> Dict[str, Any]:
        """Stop serial acquisition and close port."""
        with self.lock:
            self.is_running = False
            if self.record_timer:
                try:
                    self.record_timer.cancel()
                except Exception:
                    pass
                self.record_timer = None
            if self.is_recording:
                self.stop_recording()
                
            if self.ser and self.ser.is_open:
                try:
                    self.ser.close()
                except Exception:
                    pass
            self.ser = None
            return {"success": True, "message": "Measurement stopped."}

    def start_recording(self, duration_sec: int = 0) -> Dict[str, Any]:
        """Start recording raw samples to CSV file with optional auto-stop duration."""
        if not self.is_running:
            return {"success": False, "message": "Cannot record: Hardware measurement is not active."}
            
        timestamp_str = time.strftime("%Y%m%d_%H%M%S")
        self.record_filename = f"ppg_session_{timestamp_str}.csv"
        filepath = RECORDINGS_DIR / self.record_filename
        
        try:
            self.record_file = open(filepath, "w", buffering=1, encoding="utf-8")
            self.record_file.write("timestamp_us,sample_seq,red_adc,ir_adc,finger_detected\n")
            self.is_recording = True
            self.record_duration = max(0, int(duration_sec))
            self.record_start_time = time.time()
            
            # Cancel any lingering previous timer
            if self.record_timer:
                try:
                    self.record_timer.cancel()
                except Exception:
                    pass
                self.record_timer = None
                
            # If duration preset is specified (> 0), schedule auto-stop timer
            if self.record_duration > 0:
                self.record_timer = threading.Timer(self.record_duration, self._auto_stop_recording)
                self.record_timer.daemon = True
                self.record_timer.start()
                
            return {
                "success": True,
                "filename": self.record_filename,
                "path": str(filepath),
                "duration": self.record_duration
            }
        except Exception as e:
            return {"success": False, "message": f"Failed to create recording file: {e}"}

    def _auto_stop_recording(self):
        """Callback invoked by thread timer when recording duration expires."""
        with self.lock:
            if not self.is_recording:
                return
            filename = self.record_filename
            duration = self.record_duration
            self.is_recording = False
            if self.record_file:
                try:
                    self.record_file.flush()
                    self.record_file.close()
                except Exception:
                    pass
                self.record_file = None
            self.record_timer = None

        print(f"\n[TIMER] Recording session complete ({duration}s). Saved to {filename}")
        # Broadcast termination event to connected WebSockets
        self.broadcast({
            "type": "recording_stopped",
            "reason": "timer_expired",
            "filename": filename,
            "duration": duration,
        })

    def stop_recording(self) -> Dict[str, Any]:
        """Stop CSV recording."""
        if self.record_timer:
            try:
                self.record_timer.cancel()
            except Exception:
                pass
            self.record_timer = None

        if not self.is_recording:
            return {"success": False, "message": "No active recording."}
            
        filename = self.record_filename
        self.is_recording = False
        if self.record_file:
            try:
                self.record_file.flush()
                self.record_file.close()
            except Exception:
                pass
            self.record_file = None
            
        return {"success": True, "message": f"Recording saved to {filename}", "filename": filename}

    def _update_bpm(self, ir_val: float, now: float, finger: int):
        """Simple, robust real-time heart rate detector."""
        if finger == 0 or ir_val < 15000:
            self.current_bpm = 0.0
            self.ir_buffer.clear()
            self.ir_timestamps.clear()
            return
            
        self.ir_buffer.append(ir_val)
        self.ir_timestamps.append(now)
        
        # Keep 4-second sliding window (~400 samples at 100Hz)
        while len(self.ir_timestamps) > 0 and (now - self.ir_timestamps[0]) > 4.0:
            self.ir_buffer.pop(0)
            self.ir_timestamps.pop(0)
            
        if len(self.ir_buffer) < 150:
            return
            
        vals = [-x for x in self.ir_buffer]
        mean_val = sum(vals) / len(vals)
        norm_vals = [v - mean_val for v in vals]
        
        threshold = max(norm_vals) * 0.45
        if threshold > 200:
            curr_val = norm_vals[-1]
            if curr_val > threshold and curr_val > norm_vals[-2] and (now - self.last_beat_time) > 0.4:
                # Local peak found
                if self.last_beat_time > 0:
                    delta = now - self.last_beat_time
                    instant_bpm = 60.0 / delta
                    if 45.0 <= instant_bpm <= 180.0:
                        self.recent_bpms.append(instant_bpm)
                        if len(self.recent_bpms) > 5:
                            self.recent_bpms.pop(0)
                        self.current_bpm = round(sum(self.recent_bpms) / len(self.recent_bpms), 1)
                self.last_beat_time = now

    def _reader_worker(self):
        """Worker thread that continuously polls serial data and broadcasts."""
        last_rate_time = time.time()
        samples_in_sec = 0
        current_rate_hz = 0.0
        
        while self.is_running and self.ser and self.ser.is_open:
            try:
                line = self.ser.readline().decode("utf-8", errors="ignore").strip()
                if not line:
                    continue
                    
                # Skip diagnostic comments
                if line.startswith("#") or line.startswith("["):
                    continue
                    
                parts = line.split(",")
                if len(parts) >= 4:
                    try:
                        ts_us = int(parts[0])
                        seq = int(parts[1])
                        red = int(parts[2])
                        ir = int(parts[3])
                        finger = int(parts[4]) if len(parts) > 4 else (1 if ir > 20000 else 0)
                        
                        samples_in_sec += 1
                        self.sample_count += 1
                        now = time.time()
                        
                        if now - last_rate_time >= 1.0:
                            current_rate_hz = round(samples_in_sec / (now - last_rate_time), 1)
                            samples_in_sec = 0
                            last_rate_time = now
                            
                        # Update BPM calculation
                        self._update_bpm(ir, now, finger)
                        
                        payload = {
                            "timestamp_us": ts_us,
                            "seq": seq,
                            "red": red,
                            "ir": ir,
                            "finger": finger,
                            "bpm": self.current_bpm,
                            "rate_hz": current_rate_hz or 100.0,
                        }
                        self.latest_sample = payload
                        
                        # Write to CSV if recording (timestamp_us,sample_seq,red_adc,ir_adc,finger_detected)
                        if self.is_recording and self.record_file:
                            self.record_file.write(f"{ts_us},{seq},{red},{ir},{finger}\n")
                            
                        # Broadcast thread-safely to WebSocket queues
                        self.broadcast(payload)
                                    
                    except (ValueError, IndexError):
                        continue
                        
            except (serial.SerialException, OSError):
                # Disconnection happened while streaming!
                self.is_running = False
                break
            except Exception:
                pass
                
        # If loop exited due to disconnection
        if self.ser and self.ser.is_open:
            try:
                self.ser.close()
            except Exception:
                pass
        self.ser = None
        self.is_running = False

hw = PPGHardwareManager()

# ==============================================================================
# API ENDPOINTS
# ==============================================================================
@app.get("/", response_class=HTMLResponse)
async def get_index():
    """Serve the main web dashboard."""
    index_file = STATIC_DIR / "index.html"
    if index_file.exists():
        return FileResponse(str(index_file))
    return HTMLResponse("<h1>PPG Acquisition Server Running. Static files loading...</h1>")

@app.get("/api/status")
async def get_status():
    """Get current hardware and streaming status."""
    ports = hw.scan_ports()
    elapsed = round(time.time() - hw.record_start_time, 1) if hw.is_recording else 0.0
    remaining = max(0.0, round(hw.record_duration - elapsed, 1)) if (hw.is_recording and hw.record_duration > 0) else None
    return {
        "is_measuring": hw.is_running,
        "connected_port": hw.port if hw.is_running else None,
        "is_recording": hw.is_recording,
        "recording_file": hw.record_filename,
        "recording_duration": hw.record_duration,
        "recording_elapsed": elapsed,
        "recording_remaining": remaining,
        "sample_count": hw.sample_count,
        "latest_sample": hw.latest_sample,
        "available_ports": ports,
    }

@app.get("/api/ports")
async def get_ports():
    """Scan and list all detected serial ports."""
    return {"ports": hw.scan_ports()}

@app.post("/api/measure/start")
async def start_measure(payload: Optional[Dict[str, Any]] = None):
    """Start PPG acquisition (probes hardware and returns error if not found)."""
    port = payload.get("port") if payload else None
    result = hw.start_acquisition(port)
    return JSONResponse(status_code=200 if result.get("success") else 400, content=result)

@app.post("/api/measure/stop")
async def stop_measure():
    """Stop PPG acquisition."""
    result = hw.stop_acquisition()
    return result

@app.post("/api/recording/start")
async def start_record(payload: Optional[Dict[str, Any]] = None):
    """Start CSV file recording with optional duration timer in seconds."""
    duration = 0
    if payload and "duration" in payload:
        try:
            duration = int(payload["duration"])
        except (ValueError, TypeError):
            duration = 0
    result = hw.start_recording(duration_sec=duration)
    return JSONResponse(status_code=200 if result.get("success") else 400, content=result)

@app.post("/api/recording/stop")
async def stop_record():
    """Stop CSV file recording."""
    result = hw.stop_recording()
    return result

@app.get("/api/recordings/{filename}")
async def download_recording(filename: str):
    """Download saved CSV recording."""
    file_path = RECORDINGS_DIR / filename
    if not file_path.exists():
        raise HTTPException(status_code=404, detail="Recording file not found")
    return FileResponse(
        path=str(file_path),
        filename=filename,
        media_type="text/csv"
    )

@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket):
    """Real-time high-speed PPG sample stream via async queue."""
    await websocket.accept()
    queue = asyncio.Queue(maxsize=300)
    hw.register_client(queue)
    try:
        while True:
            # Get latest sample from queue and transmit
            sample = await queue.get()
            await websocket.send_json(sample)
    except WebSocketDisconnect:
        pass
    except Exception:
        pass
    finally:
        hw.unregister_client(queue)


# ==============================================================================
# MAIN LAUNCHER ENTRY POINT
# ==============================================================================
def open_browser():
    """Wait for uvicorn to bind and launch the web browser."""
    time.sleep(1.2)
    url = "http://127.0.0.1:8000"
    print(f"\n[LAUNCHER] Opening web browser at {url} ...")
    webbrowser.open(url)

if __name__ == "__main__":
    port = 8000
    host = "127.0.0.1"
    
    print("=" * 70)
    print("   MAX30102 + ESP32-S3 Raw PPG Acquisition System")
    print("   Backend & Frontend Live Server")
    print("=" * 70)
    print(f"[*] Starting local server on http://{host}:{port}")
    print("[*] Browser will automatically open in a moment...")
    print("[*] Press Ctrl+C in this terminal to stop the server.\n")
    
    # Start browser opener in background thread
    threading.Thread(target=open_browser, daemon=True).start()
    
    # Start uvicorn server
    uvicorn.run("app:app", host=host, port=port, log_level="info")
