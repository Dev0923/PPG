#pragma once

#include <Arduino.h>

/**
 * ============================================================================
 * MAX30102 + ESP32-S3 Raw PPG Acquisition System Configuration
 * ============================================================================
 * 
 * Hardware Target: ESP32-S3-WROOM-1-N16R8 (16MB Flash, 8MB PSRAM)
 * Sensor: MAX30102 Heart Rate and Pulse Oximeter Breakout Board
 * Objective: Raw, unsmoothed, research-grade photoplethysmography acquisition.
 * 
 * IMPORTANT HARDWARE POWER & I2C NOTICE:
 * ----------------------------------------------------------------------------
 * 1. MAX30102 Internal Rails:
 *    - VDD (Analog/Digital core): 1.8V nominal (Absolute maximum 2.2V!)
 *    - VLED (LED Anode supply):  3.1V to 5.25V
 * 2. Breakout Board Supply Configuration:
 *    - "Black Breakout Boards" commonly include an onboard 3.3V LDO and a 1.8V LDO.
 *    - If your board has dual LDOs (XC6206 / 662K chips), connect VIN to the ESP32
 *      5V (VBUS) pin or 3.3V pin. Connecting to 5V provides sufficient dropout headroom
 *      for the onboard 3.3V LDO to output a stable 3.3V to the LEDs.
 *    - If your board has only a single 1.8V LDO, connect VIN to 3.3V ONLY.
 * 3. Breakout Board Pull-Up Resistor Issue:
 *    - Many clone black boards wire their 4.7k I2C pullup resistors to the 1.8V rail!
 *    - Because the ESP32-S3 input high threshold (VIH) is ~2.48V (0.75 * 3.3V), 1.8V
 *      idle levels can cause I2C communication failures or NACK timeouts.
 *    - If Wire.begin() fails to detect 0x57, verify bus voltage with a multimeter.
 *      Remedy: Enable internal pullups (configured below) or add 2.2k-4.7k external
 *      pullups between SDA/SCL and ESP32 3.3V.
 * ============================================================================
 */

// ============================================================================
// 1. PIN ASSIGNMENTS & I2C BUS CONFIGURATION
// ============================================================================
#define I2C_SDA_PIN                 8       // ESP32-S3 GPIO 8 for I2C Data
#define I2C_SCL_PIN                 9       // ESP32-S3 GPIO 9 for I2C Clock
#define I2C_FREQ_HZ                 400000  // 400 kHz I2C Fast-Mode
#define I2C_TIMEOUT_MS              50      // Bus transaction timeout (ms)

// Optional hardware interrupt pin (-1 if unused)
#define MAX30102_INT_PIN            -1      // Connect to GPIO 10 if hardware INT is used

// ============================================================================
// 2. WI-FI ACCESS POINT & WEB SERVER CONFIGURATION
// ============================================================================
#define WIFI_AP_SSID                "PPG-ESP32"
#define WIFI_AP_PASSWORD            "PPGsensor8"
#define WIFI_AP_CHANNEL             1
#define WIFI_AP_MAX_CLIENTS         4
#define WIFI_AP_SSID_HIDDEN         false

// Static IP configuration for predictable research connection
#define WIFI_AP_LOCAL_IP            IPAddress(192, 168, 4, 1)
#define WIFI_AP_GATEWAY             IPAddress(192, 168, 4, 1)
#define WIFI_AP_SUBNET              IPAddress(255, 255, 255, 0)

#define HTTP_SERVER_PORT            80
#define WS_ENDPOINT_PATH            "/ws"

// ============================================================================
// 3. OPTICAL SENSOR (MAX30102) ACQUISITION CONFIGURATION
// ============================================================================
// Nominal sampling frequency (Hz)
#define PPG_SAMPLE_RATE_HZ          100     // 100 samples per second
#define PPG_NOMINAL_PERIOD_US       10000   // 10,000 microseconds per sample (100 Hz)

// LED Mode: 2 = SpO2 mode (Red + IR optical channels active)
#define MAX30102_LED_MODE           2

// Sample Averaging: 1 = No on-chip averaging (Preserves raw signal dynamics)
// Options in SparkFun library: 1, 2, 4, 8, 16, 32
#define MAX30102_SAMPLE_AVG         1

// Pulse Width: 411 us provides full 18-bit ADC resolution (0 to 262,143 counts)
// Options: 69 (15-bit), 118 (16-bit), 215 (17-bit), 411 (18-bit)
#define MAX30102_PULSE_WIDTH        411
#define MAX30102_ADC_BITS           18
#define MAX30102_MAX_ADC_VALUE      262143

// ADC Dynamic Range: 4096 nA (15.63 pA per LSB)
// Options: 2048, 4096, 8192, 16384 nA
#define MAX30102_ADC_RANGE          4096

// LED Current Amplitude (0x00 to 0xFF, where 0x1F ~= 6.4mA, 0x24 ~= 7.2mA, 0xFF = 50mA)
// Selected to place raw photoplethysmogram values safely within the 18-bit linear range
// (~100,000 to ~220,000 counts) without saturating the ADC photodiode front-end.
#define MAX30102_LED_CURRENT_RED    0x1F    // ~6.4 mA for Red channel (660 nm)
#define MAX30102_LED_CURRENT_IR     0x1F    // ~6.4 mA for IR channel (880 nm)

// FIFO Configuration
#define MAX30102_ROLLOVER_ENABLE    false   // Do NOT roll over FIFO; detect overflow instead
#define MAX30102_FIFO_ALMOST_FULL   15      // Interrupt / threshold when 17 unread samples remain

// ============================================================================
// 4. MEMORY & BUFFERING ARCHITECTURE
// ============================================================================
// Thread-safe circular queue between Core 1 (Acquisition) and Core 0 (Network)
// 1024 samples @ 100 Hz = 10.24 seconds of unread sample buffering
#define RING_BUFFER_CAPACITY        1024

// WebSocket streaming batching:
// Broadcasting 5 samples per batch at 100 Hz yields 20 WebSocket messages/sec.
// This preserves sub-50ms visual responsiveness while dramatically cutting Wi-Fi TCP overhead.
#define WS_STREAM_BATCH_SIZE        5

// History buffer size for browser /api/download endpoint
#define CSV_EXPORT_BUFFER_SIZE      2000    // Last 20 seconds of raw telemetry

// ============================================================================
// 5. FREERTOS DUAL-CORE TASK SCHEDULING
// ============================================================================
// Core 1: Dedicated real-time I2C optical sampling task
#define TASK_ACQ_CORE               1
#define TASK_ACQ_PRIORITY           10
#define TASK_ACQ_STACK_SIZE         4096
#define ACQ_POLL_INTERVAL_MS        5       // Poll sensor FIFO every 5ms (Nyquist rate for 100Hz = 10ms)

// Core 0: Wi-Fi, AsyncTCP, WebServer, and WebSocket client servicing
#define TASK_NET_CORE               0
#define TASK_NET_PRIORITY           2
#define TASK_NET_STACK_SIZE         8192

// Diagnostic reporting interval in Serial Monitor
#define DIAGNOSTIC_PRINT_INTERVAL_MS 1000
