#include <Arduino.h>
#include <esp_system.h>
#include "config.h"
#include "ppg_types.h"
#include "ring_buffer.h"
#include "sensor_manager.h"
#include "web_server_manager.h"

// ============================================================================
// GLOBAL SYSTEM INSTANCES
// ============================================================================
// Single-Producer Single-Consumer lock-free ring buffer
static PPGRingBuffer<RING_BUFFER_CAPACITY> g_ringBuffer;

// Optical sensor driver & acquisition engine
static SensorManager g_sensorMgr(g_ringBuffer);

// Wi-Fi SoftAP, Web Server, and WebSocket streaming manager
static WebServerManager g_webServerMgr(g_sensorMgr, g_ringBuffer);

// FreeRTOS Task Handles
static TaskHandle_t g_acqTaskHandle = nullptr;
static TaskHandle_t g_netTaskHandle = nullptr;

// ============================================================================
// FREERTOS TASKS
// ============================================================================

/**
 * @brief Real-time Optical Acquisition Task pinned to Core 1.
 * High priority (10) ensures that Wi-Fi, TCP, and web rendering never
 * introduce jitter or delay I2C FIFO servicing.
 */
void acquisitionTask(void* pvParameters) {
    Serial.printf("[TASK] Acquisition Task started on Core %d (Priority %d)\n",
                  xPortGetCoreID(), uxTaskPriorityGet(NULL));

    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(ACQ_POLL_INTERVAL_MS);

    while (true) {
        // Poll sensor FIFO and push new samples to ring buffer
        g_sensorMgr.pollFIFO();

        // Delay until next poll interval (5ms ensures 2x oversampling of 10ms FIFO rate)
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

/**
 * @brief Network & Telemetry Streaming Task pinned to Core 0.
 * Priority 2 ensures that network processing runs smoothly without starving
 * FreeRTOS Wi-Fi system stacks.
 */
void networkTask(void* pvParameters) {
    Serial.printf("[TASK] Network Task started on Core %d (Priority %d)\n",
                  xPortGetCoreID(), uxTaskPriorityGet(NULL));

    while (true) {
        // Drain ring buffer and broadcast JSON batches to WebSocket clients
        g_webServerMgr.processStreaming();

        // Brief yield to prevent CPU hogging on Core 0
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

// ============================================================================
// SYSTEM SETUP
// ============================================================================
void setup() {
    // Initialize Serial interface (115200 baud)
    Serial.begin(115200);

    // Wait up to 1.5 seconds for USB CDC terminal if connected
    uint32_t serialStart = millis();
    while (!Serial && (millis() - serialStart < 1500)) {
        delay(10);
    }

    Serial.println("\n\n========================================================");
    Serial.println("  MAX30102 + ESP32-S3 Raw PPG Acquisition System");
    Serial.println("  Research-Grade High-Speed Optical Telemetry");
    Serial.println("========================================================");

    // Print Chip & System Diagnostics
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    Serial.printf("[HARDWARE] Chip Model:    ESP32-S3 (Rev %d)\n", chip_info.revision);
    Serial.printf("[HARDWARE] CPU Cores:     %d @ %d MHz\n", chip_info.cores, getCpuFrequencyMhz());
    Serial.printf("[HARDWARE] Flash Size:    %u MB\n", ESP.getFlashChipSize() / (1024 * 1024));
    Serial.printf("[HARDWARE] PSRAM Size:    %u KB\n", ESP.getPsramSize() / 1024);
    Serial.printf("[HARDWARE] Free Heap:     %u KB\n", esp_get_free_heap_size() / 1024);

    // Step 1: Initialize Sensor Manager (I2C on GPIO 8 & 9)
    bool sensorInitialized = g_sensorMgr.begin();
    if (!sensorInitialized) {
        Serial.println("[SYSTEM][WARNING] Sensor initialization reported failure.");
        Serial.println("[SYSTEM][NOTE] Web server and Wi-Fi will still launch so you can");
        Serial.println("               inspect telemetry and hardware status at http://192.168.4.1");
    }

    // Step 2: Initialize Wi-Fi SoftAP and Async Web Server
    bool netInitialized = g_webServerMgr.begin();
    if (!netInitialized) {
        Serial.println("[SYSTEM][ERROR] Network initialization failed!");
    }

    // Step 3: Launch FreeRTOS Tasks with Core Affinity
    // Core 1: Dedicated to I2C acquisition (Priority 10)
    xTaskCreatePinnedToCore(
        acquisitionTask,
        "AcquisitionTask",
        TASK_ACQ_STACK_SIZE,
        nullptr,
        TASK_ACQ_PRIORITY,
        &g_acqTaskHandle,
        TASK_ACQ_CORE
    );

    // Core 0: Dedicated to Network streaming (Priority 2)
    xTaskCreatePinnedToCore(
        networkTask,
        "NetworkTask",
        TASK_NET_STACK_SIZE,
        nullptr,
        TASK_NET_PRIORITY,
        &g_netTaskHandle,
        TASK_NET_CORE
    );

    Serial.println("\n[SYSTEM] Both FreeRTOS tasks active. System running.");
    Serial.println("--------------------------------------------------------");
    Serial.printf("  Connect your PC/phone to Wi-Fi SSID: '%s'\n", WIFI_AP_SSID);
    Serial.printf("  Password:                           '%s'\n", WIFI_AP_PASSWORD);
    Serial.println("  Open browser:                       http://192.168.4.1");
    Serial.println("--------------------------------------------------------\n");
}

// ============================================================================
// MAIN LOOP (Diagnostic Heartbeat)
// ============================================================================
void loop() {
    // Print 1 Hz telemetry heartbeat to Serial Monitor
    static uint32_t lastPrintMs = 0;
    uint32_t nowMs = millis();

    if (nowMs - lastPrintMs >= DIAGNOSTIC_PRINT_INTERVAL_MS) {
        lastPrintMs = nowMs;

        PPGSystemStatus st = g_sensorMgr.getStatus();
        uint32_t clients = g_webServerMgr.getClientCount();

        if (st.sensor_ok) {
            Serial.printf("[TELEMETRY] Fs: %5.1f Hz | Samples: %8u | Red: %7u | IR: %7u | Drops: %2u | OVF: %2u | Clients: %u | Heap: %3u KB\n",
                          st.measured_rate_hz,
                          st.total_samples,
                          st.last_raw_red,
                          st.last_raw_ir,
                          st.dropped_samples,
                          st.fifo_overflow_count,
                          clients,
                          st.free_heap / 1024);
        } else {
            Serial.printf("[STATUS] Sensor not detected at I2C 0x57. Check wiring/pullups. Clients: %u\n", clients);
        }
    }

    // Brief delay to yield loop task
    vTaskDelay(pdMS_TO_TICKS(50));
}
