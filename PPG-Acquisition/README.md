
# MAX30102 + ESP32-S3 Raw PPG Acquisition System

A high-performance, research-grade photoplethysmography (PPG) data acquisition system designed for the **ESP32-S3** microcontroller and the **MAX30102** optical pulse oximeter sensor. 

The system acquires **raw, unsmoothed, un-normalized optical counts** from both Red (660 nm) and Infrared (880 nm) channels at a nominal sampling rate of **100 Hz**, streams data over high-throughput WebSockets to a local browser oscilloscope dashboard, and records lossless raw telemetry to computer-side CSV files with microsecond timestamps and sequence gap tracking.

---

## 1. System Architecture & Theory of Operation

```
+--------------------------------------------------------------------------------------------------+
|                                    ESP32-S3-WROOM-1-N16R8                                        |
|                                                                                                  |
|   +---------------------------------------+       +------------------------------------------+   |
|   |          CORE 1 (Acquisition)         |       |            CORE 0 (Networking)           |   |
|   |  - FreeRTOS Task (Priority 10)        |       |  - FreeRTOS Task (Priority 2)            |   |
|   |  - I2C Fast-Mode (400 kHz, Pins 8/9)  |       |  - Wi-Fi SoftAP ("PPG-ESP32")            |   |
|   |  - MAX30102 FIFO Drain (5ms cadence)  |       |  - AsyncWebServer (Port 80)              |   |
|   |  - Monotonic Sequence Counter (seq)   |       |  - WebSocket (/ws) Batch Broadcaster     |   |
|   |  - Microsecond Timing Extrapolation   |       |  - REST Endpoints (/api/status, config)  |   |
|   |  - Hardware Overflow Detection (0x05) |       |  - Static Assets (LittleFS / PROGMEM)    |   |
|   +-------------------+-------------------+       +--------------------+---------------------+   |
|                       |                                                ^                         |
|                       |   +----------------------------------------+   |                         |
|                       +-->|  PPGRingBuffer<1024> (Spinlock Guard)  +---+                         |
|                           |  Lock-free / thread-safe SPSC Buffer   |                             |
|                           +----------------------------------------+                             |
+--------------------------------------------------------------------------------------------------+
                               |                                        |
                  I2C Bus (Pins 8 & 9)                          Wi-Fi 802.11 b/g/n
                               v                                        v
                    +--------------------+             +----------------------------------+
                    | MAX30102 Sensor    |             | Clients (Dual Simultaneous Sync) |
                    | - Part ID: 0x15    |             |  1. Web Dashboard (Canvas Scope) |
                    | - 18-bit ADC       |             |  2. Python Recorder (record.py)  |
                    | - Red (660nm)      |             +----------------------------------+
                    | - IR (880nm)       |
                    +--------------------+
```

### Key Design Principles
1. **Asymmetric Multi-Processing (Dual-Core FreeRTOS Decoupling):**
   - **Core 1:** Exclusively dedicated to time-critical I2C sensor servicing at priority 10.
   - **Core 0:** Manages Wi-Fi PHY, AsyncTCP network stacks, HTTP, and WebSocket packet broadcasting. Heavy network traffic or TCP retransmissions cannot stall optical sample collection.
2. **True Raw Signal Integrity:**
   - Optical samples represent un-normalized 18-bit ADC output codes ($0 \text{ to } 262{,}143$ counts).
   - No moving-average filters, digital Butterworth filters, or SpO2/HR algorithms alter the telemetry stream on the device.
3. **Rigorous Per-Sample Microsecond Timestamping:**
   - Microsecond timestamps are derived directly from the hardware timer (`esp_timer_get_time()`).
   - When batch-draining $K$ samples from the sensor FIFO, timestamps are backward-extrapolated using the nominal interval ($10{,}000\,\mu\text{s}$) to preserve continuous inter-sample spacing without artificial burst-clustering.
4. **Data Loss & Overflow Auditability:**
   - Monotonically increasing sequence numbers (`uint32_t seq`) detect single-sample dropouts.
   - MAX30102 hardware register `0x05` (`OVF_COUNTER`) is continuously audited to catch on-chip FIFO overflows.
   - Internal ring buffer drop counters report any backpressure conditions.

---

## 2. Hardware Wiring & Power Verification

### Pin Assignments
| ESP32-S3 GPIO | MAX30102 Breakout Pin | Signal Function | Electrical Notes |
|:---|:---|:---|:---|
| **GPIO 8** | **SDA** | I2C Serial Data | Hardware I2C (400 kHz Fast-Mode) |
| **GPIO 9** | **SCL** | I2C Serial Clock | Hardware I2C (400 kHz Fast-Mode) |
| **5V / VBUS** | **VIN** | Power Supply Input | *See Breakout Board Power Analysis below* |
| **GND** | **GND** | Ground Reference | Common ground plane |
| *(Optional GPIO 10)* | **INT** | Active-low Interrupt | Open-drain interrupt line (leave floating if unused) |

---

### Critical Analysis: MAX30102 "Black Breakout Board" Power & Pull-Ups

> [!WARNING]
> Many generic black breakout boards have significant hardware quirks that prevent communication or degrade optical performance if not wired correctly. Please read this section before applying power.

#### 1. Supply Voltage Selection (`VIN`)
The MAX30102 integrated circuit requires two distinct supply rails:
- **$V_{DD}$ (Analog & Digital Logic):** $1.8\text{ V}$ nominal (Absolute maximum: $2.2\text{ V}$). Connecting 3.3V or 5V directly to a raw $V_{DD}$ pin will permanently destroy the chip.
- **$V_{LED+}$ (LED Anode Rail):** $3.1\text{ V to } 5.25\text{ V}$. Because the forward voltage drop of the internal LEDs requires $> 3.0\text{ V}$, this rail cannot operate at 1.8V.

**Breakout Board LDO Configurations:**
- **Dual-LDO Boards (Most common):** Inspect the module. If it contains **two** small 3-pin ICs (one 3.3V LDO e.g., marked `662K`, and one 1.8V LDO):
  - **Connect `VIN` to ESP32 `5V` (or `VBUS`).**
  - *Reasoning:* A 3.3V LDO requires $\sim 150\text{–}250\text{ mV}$ of dropout voltage. If you feed 3.3V into `VIN`, the regulator output sags to $\sim 3.05\text{–}3.15\text{ V}$, causing inconsistent LED emission. Supplying 5V guarantees clean, regulated 3.3V to the LEDs and 1.8V to the sensor logic.
- **Single-LDO Boards (1.8V LDO only):** If the board only has one 3-pin regulator, `VIN` is connected directly to the LED rail and the 1.8V regulator input:
  - **Connect `VIN` to ESP32 `3.3V` ONLY.** Supplying 5V could damage logic pull-ups.

#### 2. The Infamous 1.8V I2C Pull-Up Resistor Issue
Many clone breakout boards wire their 4.7 kΩ I2C pull-up resistors to the onboard **1.8V rail** instead of 3.3V:
- The ESP32-S3 input high threshold ($V_{IH}$) is $0.75 \times V_{DDIO} \approx 2.475\text{ V}$.
- When the bus is pulled up only to 1.8V, the ESP32-S3 cannot reliably read a logic `HIGH`. The bus appears held `LOW`, causing `Wire.endTransmission()` to return error code `2` or `5` (NACK / Bus Timeout).
- **Verification:** Measure the voltage between `SDA` (or `SCL`) and `GND` using a multimeter with the sensor powered and unattached to the ESP32:
  - If the multimeter reads **$\approx 1.8\text{ V}$**, the board has 1.8V pull-ups.
  - **Remedy:** Either add two external **$2.2\text{ k}\Omega \text{ to } 4.7\text{ k}\Omega$ pull-up resistors** between `SDA` $\rightarrow$ `3.3V` and `SCL` $\rightarrow$ `3.3V`, or remove the onboard 4-resistor array (often labeled `472`). The firmware additionally enables the ESP32-S3 internal pull-ups on GPIO 8 and 9 to assist weak buses.

---

## 3. Project Directory Structure

```
PPG-Acquisition/
├── platformio.ini         # PlatformIO project configuration for ESP32-S3-WROOM-1-N16R8
├── README.md              # Complete technical manual and validation guide
├── data/
│   └── index.html         # Research oscilloscope dashboard (Vanilla HTML5/CSS/JS)
├── include/
│   ├── config.h           # Pins, registers, Wi-Fi credentials, and FreeRTOS settings
│   ├── ppg_types.h        # PPGSample, SensorConfig, and PPGSystemStatus struct definitions
│   ├── ring_buffer.h      # SPSC thread-safe FreeRTOS ring buffer with spinlock guard
│   ├── sensor_manager.h   # Sensor driver, timestamping engine, and overflow detector
│   ├── web_assets.h       # PROGMEM fallback for dashboard (enables instant operation)
│   └── web_server_manager.h # AsyncWebServer, WebSocket stream, and REST API controller
├── scripts/
│   └── record_ppg.py      # Python 3 continuous CSV recorder with sequence gap detection
└── src/
    ├── main.cpp           # FreeRTOS dual-core task orchestrator and diagnostic monitor
    ├── sensor_manager.cpp # MAX30102 acquisition, I2C scan, and timing extrapolation
    └── web_server_manager.cpp # Network routes, WebSocket streaming, and client servicing
```

---

## 4. Software Setup & PlatformIO Guide

### Prerequisites
1. **VS Code:** Install [Visual Studio Code](https://code.visualstudio.com/).
2. **PlatformIO IDE Extension:** In VS Code, open Extensions (`Ctrl+Shift+X`), search for **PlatformIO IDE**, and install.
3. **Python 3:** Ensure Python 3.9+ is installed on your computer.

### Step-by-Step Build & Flash
1. Open VS Code.
2. Select **File $\rightarrow$ Open Folder...** and select `PPG-Acquisition`.
3. Allow PlatformIO a moment to index the project and download toolchains (`espressif32` platform, ESP32-S3 board definitions, and required libraries).
4. Connect your ESP32-S3 board to your computer via USB:
   > [!NOTE]
   > ESP32-S3 DevKit boards typically feature two USB ports: **USB** (Native USB CDC) and **UART** (COM / CP2102 / CH343). The firmware is configured with `-D ARDUINO_USB_CDC_ON_BOOT=1` and `-D ARDUINO_USB_MODE=1`, enabling Serial communication over either port.
5. In the PlatformIO toolbar at the bottom of VS Code:
   - Click **Build** (checkmark icon $\checkmark$) or press `Ctrl+Alt+B`.
   - Click **Upload** (right arrow icon $\rightarrow$) or press `Ctrl+Alt+U`.
   - Click **Serial Monitor** (plug icon) or press `Ctrl+Alt+S` (configured at **115200 baud**).
6. *(Optional)* **Upload Filesystem Image:**
   - Click the PlatformIO Alien icon on the left navigation bar $\rightarrow$ `esp32-s3-devkitc-1` $\rightarrow$ **Platform** $\rightarrow$ **Upload Filesystem Image** (`uploadfs`).
   - *Note:* Even if you skip this step, the firmware automatically detects that LittleFS is uninitialized and serves the embedded `INDEX_HTML_PROGMEM` dashboard.

---

## 5. Verification & Telemetry Testing

### Step 1: Serial Monitor Verification
Upon booting, the Serial Monitor output will display:
```
========================================================
  MAX30102 + ESP32-S3 Raw PPG Acquisition System
  Research-Grade High-Speed Optical Telemetry
========================================================
[HARDWARE] Chip Model:    ESP32-S3 (Rev 2)
[HARDWARE] CPU Cores:     2 @ 240 MHz
[HARDWARE] Flash Size:    16 MB
[HARDWARE] PSRAM Size:    8192 KB
[HARDWARE] Free Heap:     294 KB

[SENSOR] Initializing I2C bus for MAX30102...
[SENSOR] Configured pins: SDA = GPIO 8, SCL = GPIO 9, Bus Clock = 400000 Hz
[SENSOR] I2C 0x57 ACK received. Initializing MAX3010x library driver...
[SENSOR] Device Part ID: 0x15 (Expected: 0x15 for MAX30102), Rev: 0x01
[SENSOR] Configuring acquisition registers:
  - Sample Rate: 100 Hz (Inter-sample interval: 10000 us)
  - ADC Resolution: 18-bit (Pulse Width: 411 us)
  - ADC Range: 4096 nA
  - Averaging: 1 (Raw, no averaging)
  - LED Currents: Red = 0x1F (~6.4mA), IR = 0x1F (~6.4mA)
[SENSOR] MAX30102 configured and acquiring raw optical signals.

[NETWORK] Configuring Wi-Fi Access Point...
[NETWORK] Wi-Fi SoftAP started successfully.
  - SSID:     PPG-ESP32
  - Password: PPGsensor8
  - IP:       192.168.4.1
  - URL:      http://192.168.4.1/
[STORAGE] LittleFS filesystem mounted.
[HTTP] AsyncWebServer and WebSocket listening on port 80.
[TASK] Acquisition Task started on Core 1 (Priority 10)
[TASK] Network Task started on Core 0 (Priority 2)
```

During operation, a 1 Hz telemetry heartbeat reports live statistics:
```
[TELEMETRY] Fs: 100.1 Hz | Samples:     4500 | Red:  142050 | IR:  189320 | Drops:  0 | OVF:  0 | Clients: 1 | Heap: 288 KB
```

---

### Step 2: Web Dashboard Connection
1. On your computer or mobile device, connect to the Wi-Fi network:
   - **SSID:** `PPG-ESP32`
   - **Password:** `PPGsensor8`
2. Open a modern browser (Chrome, Firefox, Edge, Safari) and navigate to:
   - **`http://192.168.4.1`**
3. Verify the dashboard elements:
   - The status pill will display **Live (100 Hz)** with a glowing green indicator.
   - Dual oscilloscopes render live waveforms:
     - **Cyan trace:** Infrared optical signal (880 nm)
     - **Crimson trace:** Red optical signal (660 nm)
   - Real-time digital readouts display instantaneous raw 18-bit ADC counts ($0 \text{ to } 262{,}143$).
   - Measured sampling rate displays between **$99.0\text{ Hz}$ and $101.0\text{ Hz}$**.

---

### Step 3: Optical Contact & Signal Validation

| Test Condition | Expected Observation | Technical Rationale |
|:---|:---|:---|
| **No Contact (Sensor uncovered in room light)** | Red: $\sim 2{,}000\text{--}8{,}000$<br>IR: $\sim 2{,}000\text{--}8{,}000$ | Baseline ambient photo-current only. The sensor should produce a flat line with minimal optical reflection. |
| **Direct Light Exposure (Torch / Direct sun)** | Red: $262{,}143$ (Maxed)<br>IR: $262{,}143$ (Maxed) | Strong ambient infrared and visible photons saturate the internal 18-bit transimpedance amplifier. |
| **Stationary Fingertip (Gentle, stable pressure)** | Red: $\sim 110{,}000\text{--}170{,}000$<br>IR: $\sim 140{,}000\text{--}210{,}000$ | DC optical backscatter places the baseline in the linear dynamic range. A clear, periodic cardiac pulsatile waveform ($\sim 1{,}000\text{--}4{,}000\text{ counts}$ peak-to-peak) emerges at $\sim 1\text{--}1.5\text{ Hz}$ (60–90 BPM). |
| **Finger Motion / Tapping** | High-amplitude baseline shifts ($\pm 30{,}000\text{ counts}$) | Motion artifacts alter the physical optical path length and tissue geometry. |

> [!TIP]
> **Observing the Dicrotic Notch:** To visually inspect the small pulsatile AC component and aortic dicrotic notch without altering the raw data stream, click the **Visual: Raw Counts** toggle on the dashboard. This switches to **Visual: AC Pulsatile Zoom**, dynamically subtracting the moving DC baseline exclusively on the canvas while leaving the underlying raw stream completely uncompressed and unfiltered.

---

## 6. Continuous CSV Recording (Python)

The dedicated Python script `scripts/record_ppg.py` establishes a direct WebSocket connection to the ESP32-S3, streams all raw telemetry samples, audits sequence continuity, flushes periodically to disk, and saves companion research provenance metadata.

### Running the Recorder
Open a terminal and run:
```bash
# Basic indefinite recording (Ctrl+C to stop)
python scripts/record_ppg.py --host 192.168.4.1

# Specific output path and timed duration (e.g. 60 seconds)
python scripts/record_ppg.py --host 192.168.4.1 -o recordings/trial_01.csv -d 60
```

### Live Terminal HUD
While recording, the script displays a non-scrolling live telemetry HUD:
```
[REC] 00:01:30 | Samples:    9,000 | Rate: 100.1 Hz | Red: 142,310 | IR: 185,420 | Loss: 0 (0 lost) |    480 KB
```

### Final Summary Report
Upon stopping (`Ctrl+C` or duration timeout), a complete provenance report is displayed:
```
====================================================================
  RECORDING SUMMARY REPORT
====================================================================
Session ID:                   session_20261013_145020
Duration:                     60.02 seconds (1.00 minutes)
Total Samples Received:       6,005
Total Samples Missing (Lost): 0
Sequence Gap Events:          0
Duplicate Packets Dropped:    0
Packet Loss Rate:             0.000 %
Effective Sampling Rate:      100.05 Hz (Nominal: 100 Hz)
CSV File:                     recordings/trial_01.csv (412.3 KB)
Metadata File:                recordings/trial_01.meta.json
====================================================================
```

### CSV File Format
The generated CSV file starts with provenance metadata comments followed by raw columnar data:
```csv
# ====================================================================
# MAX30102 + ESP32-S3 Raw Optical PPG Telemetry Recording
# ====================================================================
# Session ID:           session_20261013_145020
# Start Time UTC:       2026-10-13T10:15:30.123456Z
# Host Address:         192.168.4.1:80
# Nominal Sample Rate:  100 Hz
# Nominal Period:       10000 us
# ADC Resolution:       18-bit (411 us pulse)
# ADC Dynamic Range:    4096 nA
# Sample Averaging:     1 (Raw, no averaging)
# LED Currents:         Red=0x1F, IR=0x1F
# Data Type:            Unsmoothed, unnormalized raw optical counts
# Columns:              session_id, seq, timestamp_us, timestamp_iso, raw_red, raw_ir
# ====================================================================
session_id,seq,timestamp_us,timestamp_iso,raw_red,raw_ir
session_20261013_145020,0,1024000,2026-10-13T10:15:30.123456Z,142050,189320
session_20261013_145020,1,1034000,2026-10-13T10:15:30.133456Z,142080,189350
session_20261013_145020,2,1044000,2026-10-13T10:15:30.143456Z,142110,189390
...
```

---

## 7. Timing Strategy, Precision & Technical Limitations

### Timestamping Precision Strategy
1. **Clock Source:** The firmware derives timestamps from `esp_timer_get_time()`, a 64-bit hardware timer running at 1 MHz (1 microsecond resolution). This hardware timer never overflows during the operational lifespan of the device ($> 292\text{ years}$).
2. **Backward Extrapolation in Batches:**
   - The MAX30102 stores samples in its internal 32-sample hardware FIFO.
   - Core 1 polls the FIFO every $5\text{ ms}$. If $K$ samples have accumulated:
     $$\Delta t_{\text{nominal}} = 10{,}000\,\mu\text{s}$$
     $$t_k = t_{\text{poll}} - (K - 1 - k) \times \Delta t_{\text{nominal}} \quad \text{for } k \in [0, K-1]$$
   - Monotonicity checks prevent timestamp regression or duplicate millisecond timestamps.

### Technical Limitations & Lossless Considerations
- **Sensor Internal RC Oscillator Drift:** The MAX30102 utilizes an internal RC oscillator rather than an external crystal. The manufacturer datasheet specifies an internal clock tolerance of approximately **$\pm 2\%$** over operating temperatures ($0^\circ\text{C}$ to $50^\circ\text{C}$). As a result, the physical sampling frequency can range between **$98\text{ Hz}$ and $102\text{ Hz}$**. The sequence number (`seq`) represents the true index of sensor conversions.
- **Wi-Fi Medium Contention:** Wi-Fi (802.11 b/g/n) is an unbonded, CSMA/CA wireless medium susceptible to RF interference and transmission retries. While the dual-core architecture and ring buffer insulate the I2C sensor from network stalls, sustained Wi-Fi disconnections longer than the ring buffer duration ($\sim 10.24\text{ seconds}$) will eventually cause buffer backpressure. If this occurs, the ring buffer flags dropped samples, increments the drop counter, and reports the event across all APIs.

---

## 8. Troubleshooting Guide

| Issue / Symptom | Probable Cause | Diagnostic & Resolution |
|:---|:---|:---|
| **`Device at I2C 0x57 did not acknowledge!`** | Weak I2C pull-ups to 1.8V or incorrect pins. | 1. Confirm SDA = GPIO 8, SCL = GPIO 9.<br>2. Measure idle voltage on SDA/SCL. If $< 2.4\text{V}$, add $2.2\text{k}\Omega\text{--}4.7\text{k}\Omega$ external pull-ups to 3.3V.<br>3. Verify board supply: feed 5V into VIN if dual LDOs exist. |
| **`Hardware FIFO Overflow!` reported in logs** | Acquisition loop blocked or delayed. | Ensure no blocking calls (`delay()`, complex logging) are introduced into `acquisitionTask`. Core 1 should remain dedicated to I2C reads. |
| **Flatline signal at 262,143 counts** | Photodiode amplifier saturated by excessive optical power or ambient light. | Reduce ambient light or decrease LED current in `config.h` (`MAX30102_LED_CURRENT_RED` and `_IR` from `0x1F` to `0x14` or `0x0F`). |
| **Flatline signal at $< 10,000$ counts with finger on sensor** | LEDs not emitting or supply voltage too low. | Verify that $V_{LED+}$ is receiving $\ge 3.1\text{V}$. If using a breakout board with an onboard 3.3V regulator, connect `VIN` to `5V` (VBUS). |
| **Browser dashboard reports "Connecting..." and does not graph** | Device connected to wrong Wi-Fi or IP blocked. | Verify computer is connected to SSID `PPG-ESP32`. Navigate directly to `http://192.168.4.1/`. Check browser console (`F12`) for WebSocket errors. |
| **Python recorder reports `ModuleNotFoundError: websockets`** | Missing Python dependency. | Install with: `pip install websockets`. |

---

## 9. Research Integrity & Non-Clinical Disclaimer

This system is engineered strictly for **scientific research, laboratory data collection, and signal processing education**. 

> [!CAUTION]
> **Non-Clinical Disclaimer:** This device is **not** a certified medical instrument and must **not** be used for medical diagnosis, clinical treatment, physiological screening, or therapeutic decision-making. No algorithms for diabetes detection, disease diagnosis, or clinical oxygen saturation claims are provided or implied.
