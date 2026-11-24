# MAX30102 + ESP32-S3 Raw PPG Acquisition System

High-precision, dual-channel optical PPG acquisition workstation for research-grade heart rate, pulse waveform analysis, and continuous CSV data logging.

---

## 🚀 Quick Start (Single Terminal Command)

To launch both the backend server and frontend web application and automatically open the live dashboard in your browser:

```bash
python app.py
```
*(Or double-click [`run.bat`](file:///c:/Users/devsa/Desktop/Hardware/run.bat))*

1. The terminal starts the FastAPI server and WebSocket telemetry stream.
2. The browser automatically opens to `http://127.0.0.1:8000`.
3. Click the glowing **"Measure PPG"** button:
   - **If hardware is connected:** Live 100 Hz dual-wavelength waveforms (IR and Red), BPM pulse gauge, finger detection, and recording controls immediately start streaming.
   - **Recording Timers:** Choose between **Manual**, **3 min**, **5 min**, **8 min**, or **10 min** presets. Active countdowns, a live progress bar, automated Web Audio chimes, and instant CSV downloads are handled automatically upon timer completion.
   - **If hardware is disconnected:** A clear, user-friendly alert message dialog pops up with a step-by-step hardware connection checklist (USB-C cable, I²C SDA/SCL pins, power).

---

## 📁 System Architecture

- **Unified Launcher & Backend:** [`app.py`](file:///c:/Users/devsa/Desktop/Hardware/app.py) (FastAPI, PySerial background telemetry loop, async WebSocket queue, CSV recorder)
- **Web Dashboard Frontend:**
  - HTML structure: [`static/index.html`](file:///c:/Users/devsa/Desktop/Hardware/static/index.html)
  - Interactive logic & 60 FPS Canvas: [`static/app.js`](file:///c:/Users/devsa/Desktop/Hardware/static/app.js)
  - Modern dark-mode styling: [`static/styles.css`](file:///c:/Users/devsa/Desktop/Hardware/static/styles.css)
- **ESP32 Firmware:**
  - High-precision 100 Hz raw stream: [`PPG-Acquisition/src/test_raw_ppg.cpp`](file:///c:/Users/devsa/Desktop/Hardware/PPG-Acquisition/src/test_raw_ppg.cpp)
  - PlatformIO configuration: [`PPG-Acquisition/platformio.ini`](file:///c:/Users/devsa/Desktop/Hardware/PPG-Acquisition/platformio.ini)

---

## 🛠️ Hardware Connections (MAX30102 to ESP32-S3)

| MAX30102 Sensor Pin | ESP32-S3 Pin | Note |
| :--- | :--- | :--- |
| **VIN / VCC** | **3.3V** or **5V** | 5V recommended for breakout boards with onboard 3.3V LDO regulator |
| **GND** | **GND** | Common ground |
| **SDA** | **GPIO 8** | I²C Data (Hardware I2C) |
| **SCL** | **GPIO 9** | I²C Clock (Hardware I2C, 400 kHz) |
| **USB Port** | **CH343 USB-C** | Enumerates as `COM9` on Windows |
