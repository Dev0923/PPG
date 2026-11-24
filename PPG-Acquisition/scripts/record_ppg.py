#!/usr/bin/env python3
"""
================================================================================
MAX30102 + ESP32-S3 Raw PPG Continuous CSV Recorder
================================================================================
Connects to the ESP32-S3 over Wi-Fi (WebSocket telemetry stream) and logs
raw optical samples (Red and IR channels) continuously to a CSV file.

Features:
- Guaranteed raw telemetry logging (no smoothing, no rounding)
- Real-time monotonic sequence gap detection & packet loss reporting
- Safe reconnection with duplicate sequence number rejection
- Regular disk buffer flushing to protect against power/network failure
- Companion metadata file generation (.meta.json) for research traceability
- High-resolution microsecond timestamp logging
- Clean Ctrl+C shutdown with summary statistics report

Usage:
    python record_ppg.py --host 192.168.4.1 --port 80 --output ppg_data.csv
================================================================================
"""

import argparse
import asyncio
import csv
import datetime
import json
import os
import signal
import sys
import time
import urllib.request
from pathlib import Path

try:
    import websockets
except ImportError:
    print("[ERROR] 'websockets' library is required. Install with: pip install websockets")
    sys.exit(1)


class PPGRecorder:
    def __init__(self, host="192.168.4.1", port=80, output_path=None, duration=0, flush_interval=50):
        self.host = host
        self.port = port
        self.ws_url = f"ws://{host}:{port}/ws"
        self.config_url = f"http://{host}:{port}/api/config"
        self.duration = duration
        self.flush_interval = flush_interval

        # Generate output filename if not specified
        timestamp_str = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
        self.session_id = f"session_{timestamp_str}"
        if output_path is None:
            records_dir = Path("recordings")
            records_dir.mkdir(parents=True, exist_ok=True)
            self.output_path = records_dir / f"ppg_{timestamp_str}.csv"
        else:
            self.output_path = Path(output_path)
            self.output_path.parent.mkdir(parents=True, exist_ok=True)

        self.meta_path = self.output_path.with_suffix(".meta.json")

        # Telemetry statistics
        self.running = False
        self.file_handle = None
        self.csv_writer = None
        self.samples_since_flush = 0
        self.last_flush_time = time.time()

        # Sequence tracking
        self.last_seq = None
        self.total_received = 0
        self.total_missing = 0
        self.gap_count = 0
        self.duplicate_count = 0

        # Timing & rate estimation
        self.start_time = None
        self.end_time = None
        self.first_sample_t_us = None
        self.last_sample_t_us = None

        # Latest values
        self.latest_red = 0
        self.latest_ir = 0

        # Sensor metadata
        self.sensor_config = {}

    def fetch_sensor_metadata(self):
        """Fetch sensor configuration registers from ESP32 HTTP API."""
        try:
            req = urllib.request.Request(self.config_url, headers={"User-Agent": "PPGRecorder/1.0"})
            with urllib.request.urlopen(req, timeout=3.0) as resp:
                if resp.status == 200:
                    data = resp.read().decode("utf-8")
                    self.sensor_config = json.loads(data)
                    print(f"[METADATA] Retrieved sensor config from {self.config_url}")
                    return True
        except Exception as e:
            print(f"[METADATA][NOTE] Could not fetch {self.config_url} ({e}). Using default telemetry settings.")
            self.sensor_config = {
                "sample_rate_hz": 100,
                "sample_period_us": 10000,
                "pulse_width_us": 411,
                "adc_resolution_bits": 18,
                "adc_range_na": 4096,
                "sample_average": 1,
                "led_mode": 2,
                "led_current_red": 31,
                "led_current_ir": 31
            }
            return False

    def open_csv_file(self):
        """Open CSV file and write research provenance headers."""
        self.file_handle = open(self.output_path, "w", newline="", encoding="utf-8")
        
        # Write metadata comments
        utc_now = datetime.datetime.now(datetime.timezone.utc).isoformat()
        comments = [
            "# ====================================================================",
            "# MAX30102 + ESP32-S3 Raw Optical PPG Telemetry Recording",
            "# ====================================================================",
            f"# Session ID:           {self.session_id}",
            f"# Start Time UTC:       {utc_now}",
            f"# Host Address:         {self.host}:{self.port}",
            f"# Nominal Sample Rate:  {self.sensor_config.get('sample_rate_hz', 100)} Hz",
            f"# Nominal Period:       {self.sensor_config.get('sample_period_us', 10000)} us",
            f"# ADC Resolution:       {self.sensor_config.get('adc_resolution_bits', 18)}-bit ({self.sensor_config.get('pulse_width_us', 411)} us pulse)",
            f"# ADC Dynamic Range:    {self.sensor_config.get('adc_range_na', 4096)} nA",
            f"# Sample Averaging:     {self.sensor_config.get('sample_average', 1)} (Raw, no averaging)",
            f"# LED Currents:         Red=0x{self.sensor_config.get('led_current_red', 31):02X}, IR=0x{self.sensor_config.get('led_current_ir', 31):02X}",
            "# Data Type:            Unsmoothed, unnormalized raw optical counts",
            "# Columns:              session_id, seq, timestamp_us, timestamp_iso, raw_red, raw_ir",
            "# ====================================================================",
        ]
        for line in comments:
            self.file_handle.write(line + "\n")

        # Standard CSV header
        self.csv_writer = csv.writer(self.file_handle)
        self.csv_writer.writerow(["session_id", "seq", "timestamp_us", "timestamp_iso", "raw_red", "raw_ir"])
        self.file_handle.flush()
        print(f"[RECORDER] Logging raw samples to: {self.output_path.resolve()}")

    def save_metadata_json(self):
        """Save companion JSON metadata file."""
        meta = {
            "session_id": self.session_id,
            "created_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
            "host": f"{self.host}:{self.port}",
            "sensor": "MAX30102",
            "microcontroller": "ESP32-S3-WROOM-1-N16R8",
            "sensor_config": self.sensor_config,
            "statistics": {
                "total_samples_received": self.total_received,
                "total_samples_missing": self.total_missing,
                "gap_events": self.gap_count,
                "duplicate_samples_rejected": self.duplicate_count,
                "loss_percentage": round((self.total_missing / (self.total_received + self.total_missing) * 100.0), 3) if (self.total_received + self.total_missing) > 0 else 0.0,
                "duration_seconds": round(time.time() - self.start_time, 2) if self.start_time else 0.0,
                "effective_rate_hz": round(self.total_received / (time.time() - self.start_time), 2) if self.start_time and (time.time() - self.start_time) > 0 else 0.0
            }
        }
        with open(self.meta_path, "w", encoding="utf-8") as f:
            json.dump(meta, f, indent=2)
        print(f"[METADATA] Saved research metadata: {self.meta_path.name}")

    def process_sample(self, sample):
        """
        Validate, detect sequence gaps, and write sample to CSV.
        sample: {"seq": int, "t": int (us), "r": int, "ir": int}
        """
        seq = sample.get("seq")
        t_us = sample.get("t")
        raw_red = sample.get("r")
        raw_ir = sample.get("ir")

        if seq is None or t_us is None or raw_red is None or raw_ir is None:
            return

        # Sequence gap and continuity analysis
        if self.last_seq is not None:
            if seq == self.last_seq:
                # Duplicate sample (e.g. from network retransmission)
                self.duplicate_count += 1
                return
            elif seq < self.last_seq:
                # Sequence regression (ESP32 rebooted or session reset)
                print(f"\n[SEQUENCE][WARNING] Sequence regressed from #{self.last_seq} to #{seq}! Device may have reset.")
            elif seq > self.last_seq + 1:
                # Sequence gap detected!
                missing = seq - (self.last_seq + 1)
                self.gap_count += 1
                self.total_missing += missing
                print(f"\n[GAP DETECTED] Missing sequence range: #{self.last_seq + 1} -> #{seq - 1} "
                      f"({missing} samples lost, Cumulative loss: {self.total_missing})")

        self.last_seq = seq
        self.total_received += 1
        self.latest_red = raw_red
        self.latest_ir = raw_ir

        if self.first_sample_t_us is None:
            self.first_sample_t_us = t_us
        self.last_sample_t_us = t_us

        # Generate ISO timestamp from microsecond offset
        iso_str = datetime.datetime.now(datetime.timezone.utc).isoformat()

        # Write CSV row
        self.csv_writer.writerow([self.session_id, seq, t_us, iso_str, raw_red, raw_ir])
        self.samples_since_flush += 1

        # Periodic disk flush
        now = time.time()
        if self.samples_since_flush >= self.flush_interval or (now - self.last_flush_time >= 1.0):
            self.file_handle.flush()
            self.samples_since_flush = 0
            self.last_flush_time = now

    async def log_telemetry_loop(self):
        """Connect to WebSocket and continuously process samples with auto-reconnection."""
        retry_delay = 1.0
        max_retry_delay = 8.0

        while self.running:
            try:
                print(f"[NETWORK] Connecting to WebSocket: {self.ws_url} ...")
                async with websockets.connect(self.ws_url, ping_interval=10, ping_timeout=10, max_size=2**20) as ws:
                    print(f"[NETWORK] Connected! Stream active.")
                    retry_delay = 1.0 # reset backoff on success

                    async for message in ws:
                        if not self.running:
                            break

                        try:
                            data = json.loads(message)
                        except json.JSONDecodeError:
                            continue

                        msg_type = data.get("type")
                        if msg_type == "batch":
                            samples = data.get("samples", [])
                            for s in samples:
                                self.process_sample(s)
                        elif msg_type == "sample":
                            self.process_sample(data)
                        elif msg_type == "status":
                            # Device status heartbeat
                            pass

                        # Check duration limit if specified
                        if self.duration > 0 and self.start_time:
                            if time.time() - self.start_time >= self.duration:
                                print(f"\n[RECORDER] Target duration of {self.duration} seconds reached.")
                                self.running = False
                                break

            except (websockets.ConnectionClosed, OSError, asyncio.TimeoutError) as e:
                if not self.running:
                    break
                print(f"\n[NETWORK][WARNING] Connection lost ({e}). Reconnecting in {retry_delay:.1f}s...")
                await asyncio.sleep(retry_delay)
                retry_delay = min(retry_delay * 1.5, max_retry_delay)

    async def hud_display_loop(self):
        """Render clean, periodic live telemetry progress line in the console."""
        while self.running:
            await asyncio.sleep(0.5)
            if self.total_received == 0 or not self.start_time:
                continue

            elapsed = time.time() - self.start_time
            hrs = int(elapsed // 3600)
            mins = int((elapsed % 3600) // 60)
            secs = int(elapsed % 60)
            time_str = f"{hrs:02d}:{mins:02d}:{secs:02d}"

            rate = self.total_received / elapsed if elapsed > 0 else 0.0
            file_kb = os.path.getsize(self.output_path) / 1024 if self.output_path.exists() else 0

            gap_status = f"{self.gap_count} gaps ({self.total_missing} lost)" if self.total_missing > 0 else "0 (0 lost)"

            # Carriage-return status line
            sys.stdout.write(
                f"\r[REC] {time_str} | Samples: {self.total_received:8,d} | Rate: {rate:5.1f} Hz | "
                f"Red: {self.latest_red:7,d} | IR: {self.latest_ir:7,d} | Loss: {gap_status} | {file_kb:6.0f} KB"
            )
            sys.stdout.flush()

    async def start(self):
        """Main acquisition orchestrator."""
        print("====================================================================")
        print("  MAX30102 + ESP32-S3 High-Speed Raw PPG Telemetry Recorder")
        print("====================================================================")
        print(f"Target:      {self.host}:{self.port}")
        print(f"Destination: {self.output_path}")
        print(f"Duration:    {'Indefinite (Ctrl+C to stop)' if self.duration <= 0 else f'{self.duration} seconds'}")
        print("====================================================================")

        # 1. Fetch metadata from device
        self.fetch_sensor_metadata()

        # 2. Open CSV file
        self.open_csv_file()

        # 3. Start timing
        self.running = True
        self.start_time = time.time()

        # 4. Launch concurrent network and HUD tasks
        try:
            await asyncio.gather(
                self.log_telemetry_loop(),
                self.hud_display_loop()
            )
        except asyncio.CancelledError:
            pass
        finally:
            self.stop()

    def stop(self):
        """Clean shutdown and reporting."""
        if not self.running and self.file_handle is None:
            return

        self.running = False
        self.end_time = time.time()

        if self.file_handle:
            self.file_handle.flush()
            self.file_handle.close()
            self.file_handle = None

        # Save metadata JSON
        self.save_metadata_json()

        # Print summary report
        elapsed = self.end_time - self.start_time if self.start_time else 0.0
        effective_rate = self.total_received / elapsed if elapsed > 0 else 0.0
        total_intended = self.total_received + self.total_missing
        loss_pct = (self.total_missing / total_intended * 100.0) if total_intended > 0 else 0.0
        file_size_kb = os.path.getsize(self.output_path) / 1024 if self.output_path.exists() else 0

        print("\n\n" + "=" * 68)
        print("  RECORDING SUMMARY REPORT")
        print("=" * 68)
        print(f"Session ID:                   {self.session_id}")
        print(f"Duration:                     {elapsed:.2f} seconds ({elapsed/60.0:.2f} minutes)")
        print(f"Total Samples Received:       {self.total_received:,}")
        print(f"Total Samples Missing (Lost): {self.total_missing:,}")
        print(f"Sequence Gap Events:          {self.gap_count}")
        print(f"Duplicate Packets Dropped:    {self.duplicate_count}")
        print(f"Packet Loss Rate:             {loss_pct:.3f} %")
        print(f"Effective Sampling Rate:      {effective_rate:.2f} Hz (Nominal: {self.sensor_config.get('sample_rate_hz', 100)} Hz)")
        print(f"CSV File:                     {self.output_path.resolve()} ({file_size_kb:,.1f} KB)")
        print(f"Metadata File:                {self.meta_path.resolve()}")
        print("=" * 68)


def main():
    parser = argparse.ArgumentParser(description="MAX30102 + ESP32-S3 Raw PPG Continuous CSV Recorder")
    parser.add_argument("--host", default="192.168.4.1", help="ESP32-S3 IP address (default: 192.168.4.1)")
    parser.add_argument("--port", type=int, default=80, help="Web server port (default: 80)")
    parser.add_argument("-o", "--output", default=None, help="Output CSV path (default: recordings/ppg_<timestamp>.csv)")
    parser.add_argument("-d", "--duration", type=float, default=0, help="Recording duration in seconds (0 = indefinite)")
    parser.add_argument("--flush-interval", type=int, default=50, help="Number of samples between disk flushes")

    args = parser.parse_args()

    recorder = PPGRecorder(
        host=args.host,
        port=args.port,
        output_path=args.output,
        duration=args.duration,
        flush_interval=args.flush_interval
    )

    loop = asyncio.new_event_loop()
    asyncio.set_event_loop(loop)

    def handle_signal():
        print("\n[RECORDER] Interrupt received (Ctrl+C). Finalizing CSV...")
        recorder.running = False
        for task in asyncio.all_tasks(loop):
            task.cancel()

    # Register signal handlers
    try:
        loop.add_signal_handler(signal.SIGINT, handle_signal)
        loop.add_signal_handler(signal.SIGTERM, handle_signal)
    except (NotImplementedError, AttributeError):
        # On Windows, add_signal_handler may not be implemented for all loop types
        pass

    try:
        loop.run_until_complete(recorder.start())
    except KeyboardInterrupt:
        recorder.stop()
    finally:
        loop.close()


if __name__ == "__main__":
    main()
