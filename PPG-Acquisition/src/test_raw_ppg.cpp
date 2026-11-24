#include <Arduino.h>
#include <Wire.h>
#include <esp_timer.h>
#include "MAX30105.h"

// ============================================================================
// HARDWARE PIN DEFINITIONS & I2C CONFIGURATION
// ============================================================================
#define I2C_SDA_PIN    8     // ESP32-S3 Hardware I2C SDA
#define I2C_SCL_PIN    9     // ESP32-S3 Hardware I2C SCL
#define I2C_CLOCK_HZ   400000 // Fast-mode 400 kHz

// MAX30102 Default 7-bit I2C Address
#define MAX30102_I2C_ADDR 0x57

// Threshold for optical finger presence detection (Baseline in air is ~600, tissue contact > 20000)
#define FINGER_THRESHOLD_IR 20000 // Raw 18-bit ADC counts (0 - 262143)

// ============================================================================
// GLOBAL OBJECTS
// ============================================================================
static MAX30105 particleSensor;

// Monotonic sample sequence counter & timing metrics
static uint32_t g_sampleSeq = 0;
static uint32_t g_lastPrintTimeMs = 0;
static uint32_t g_samplesInWindow = 0;
static float g_currentSampleRateHz = 0.0f;
static bool g_fingerDetected = false;

// ============================================================================
// I2C BUS SCANNER HELPER (For Hardware Diagnostics)
// ============================================================================
void scanI2CBus() {
    Serial.println("\n--------------------------------------------------");
    Serial.printf("[I2C SCAN] Scanning bus on SDA=GPIO%d, SCL=GPIO%d...\n", I2C_SDA_PIN, I2C_SCL_PIN);
    byte count = 0;
    for (byte addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        byte error = Wire.endTransmission();
        if (error == 0) {
            Serial.printf("  -> Found active device at 7-bit address 0x%02X", addr);
            if (addr == MAX30102_I2C_ADDR) {
                Serial.print(" (MATCH: MAX30102 Optical Sensor)");
            }
            Serial.println();
            count++;
        }
    }
    if (count == 0) {
        Serial.println("  [!] NO I2C devices acknowledged!");
        Serial.println("  Troubleshooting Checklist:");
        Serial.println("   1. Verify SDA is connected to GPIO 8, SCL to GPIO 9.");
        Serial.println("   2. Verify Power: Many clone 'black breakout boards' require 5V on VIN");
        Serial.println("      because of an onboard 3.3V LDO dropout.");
        Serial.println("   3. Check Pull-Ups: Many boards connect 4.7k pull-ups to 1.8V.");
        Serial.println("      The ESP32-S3 needs VIH >= 2.48V. Add 2.2k-4.7k external pull-ups");
        Serial.println("      between SDA->3.3V and SCL->3.3V if bus fails.");
    } else {
        Serial.printf("[I2C SCAN] Found %d device(s) on bus.\n", count);
    }
    Serial.println("--------------------------------------------------\n");
}

// ============================================================================
// ARDUINO SETUP
// ============================================================================
void setup() {
    // 1. Initialize Serial Communication
    Serial.begin(115200);
    // Allow USB CDC time to attach
    delay(1500);

    Serial.println("\n\n========================================================");
    Serial.println("   MAX30102 + ESP32-S3 Raw PPG Acquisition Test");
    Serial.println("   Step 4: Real-Time Hardware Communication & Signal Test");
    Serial.println("========================================================");
    Serial.printf("[SYSTEM] ESP32-S3 Chip Rev: %d | CPU: %d MHz\n", ESP.getChipRevision(), getCpuFrequencyMhz());
    Serial.printf("[SYSTEM] Flash Size: %u MB | PSRAM Size: %u KB\n",
                  ESP.getFlashChipSize() / (1024 * 1024), ESP.getPsramSize() / 1024);

    // 2. Initialize Hardware I2C on GPIO 8 (SDA) and GPIO 9 (SCL)
    Serial.printf("\n[STEP 1] Initializing I2C bus (SDA=GPIO%d, SCL=GPIO%d, %d Hz)...\n",
                  I2C_SDA_PIN, I2C_SCL_PIN, I2C_CLOCK_HZ);
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, I2C_CLOCK_HZ);
    Wire.setTimeOut(50); // 50ms bus timeout to prevent hanging

    // 3. Scan I2C bus to verify electrical connectivity
    scanI2CBus();

    // 4. Initialize MAX30102 sensor
    Serial.println("[STEP 2] Attempting connection to MAX30102 at 0x57...");
    if (!particleSensor.begin(Wire, I2C_CLOCK_HZ, MAX30102_I2C_ADDR)) {
        Serial.println("\n[ERROR] MAX30102 communication FAILED!");
        Serial.println("[ERROR] The sensor was not detected or did not respond.");
        Serial.println("[ACTION REQUIRED] Check your wiring and power, then press Reset button.");
        while (true) {
            delay(1000);
        }
    }

    // 5. Query and verify Part ID (0x15 = MAX30102)
    uint8_t partID = particleSensor.readPartID();
    uint8_t revID = particleSensor.getRevisionID();
    Serial.printf("[SUCCESS] MAX30102 detected! Part ID: 0x%02X (Expected: 0x15), Rev ID: 0x%02X\n", partID, revID);

    if (partID != 0x15) {
        Serial.printf("[WARNING] Sensor Part ID (0x%02X) does not match MAX30102 (0x15). Check sensor variant!\n", partID);
    }

    // 6. Configure MAX30102 for High-Precision Raw PPG Acquisition
    // Settings optimized for research-grade raw PPG data collection:
    // - ledBrightness: 0x1F (~6.4 mA) for balanced photodiode SNR without saturation
    // - sampleAverage: 1 (No digital averaging, pure unsmoothed raw samples)
    // - ledMode: 2 (Multi-LED mode: Red + IR enabled)
    // - sampleRate: 100 (100 samples/sec nominal acquisition rate)
    // - pulseWidth: 411 (411 us pulse width -> 18-bit ADC resolution)
    // - adcRange: 4096 (4096 nA full-scale range)
    Serial.println("[STEP 3] Configuring sensor registers for 100Hz 18-bit Raw PPG acquisition...");
    byte ledBrightness = 0x28; // ~8.0mA for clear arterial absorption
    byte sampleAverage = 1;    // 1 sample per FIFO entry (raw)
    byte ledMode = 2;          // 2 = Red + IR
    int sampleRate = 100;      // 100 Hz
    int pulseWidth = 411;      // 411 us -> 18-bit resolution
    int adcRange = 4096;       // 4096 nA

    particleSensor.setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange);

    // Set LED pulse amplitudes explicitly
    particleSensor.setPulseAmplitudeRed(0x28);
    particleSensor.setPulseAmplitudeIR(0x28);
    particleSensor.clearFIFO();

    Serial.println("[STEP 4] Sensor configuration locked. Commencing continuous stream.");
    Serial.println("\n--------------------------------------------------------------------------------");
    Serial.println("CSV Header: timestamp_us,seq,red,ir,finger_detected,rate_hz");
    Serial.println("Or plot in VS Code Teleplot / Arduino Serial Plotter:");
    Serial.println("Format: red:VALUE,ir:VALUE");
    Serial.println("--------------------------------------------------------------------------------\n");

    g_lastPrintTimeMs = millis();
}

// ============================================================================
// ARDUINO MAIN LOOP (Continuous Streaming)
// ============================================================================
void loop() {
    // Poll the sensor FIFO for newly digitized samples
    particleSensor.check();

    while (particleSensor.available()) {
        uint64_t timestamp_us = esp_timer_get_time();
        uint32_t red = particleSensor.getFIFORed();
        uint32_t ir = particleSensor.getFIFOIR();
        particleSensor.nextSample();

        g_sampleSeq++;
        g_samplesInWindow++;

        // Determine if finger is in contact (IR signal above baseline)
        g_fingerDetected = (ir > FINGER_THRESHOLD_IR);

        // Continuous timestamped CSV line for data collection & serial plotter
        // Format: timestamp_us,seq,red,ir,finger_status
        Serial.printf("%llu,%lu,%lu,%lu,%d\n",
                      timestamp_us, g_sampleSeq, red, ir, g_fingerDetected ? 1 : 0);
    }

    // Every 1.0 second: Print diagnostic status report
    uint32_t nowMs = millis();
    if (nowMs - g_lastPrintTimeMs >= 1000) {
        float elapsedSec = (nowMs - g_lastPrintTimeMs) / 1000.0f;
        g_currentSampleRateHz = g_samplesInWindow / elapsedSec;
        g_samplesInWindow = 0;
        g_lastPrintTimeMs = nowMs;

        // Print human-readable diagnostic banner without breaking CSV parsers (prefixed with #)
        Serial.printf("# [DIAGNOSTIC] Total Samples: %lu | Measured Rate: %.1f Hz | %s\n",
                      g_sampleSeq,
                      g_currentSampleRateHz,
                      g_fingerDetected ? "Finger Detected (PULSATILE)" : "[!] NO FINGER DETECTED (Touch sensor)");
    }

    // Small yield to prevent CPU lock and feed FreeRTOS watchdog
    delay(2);
}
