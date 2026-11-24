#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <LittleFS.h>
#include "config.h"
#include "ppg_types.h"
#include "ring_buffer.h"

// Forward declaration
class SensorManager;

/**
 * @brief Manages Wi-Fi Access Point mode, Async HTTP web server,
 * and high-throughput non-blocking WebSocket telemetry streaming.
 */
class WebServerManager {
public:
    WebServerManager(SensorManager& sensorMgr, PPGRingBuffer<RING_BUFFER_CAPACITY>& ringBuffer);

    /**
     * @brief Initialize Wi-Fi SoftAP and configure HTTP / WebSocket endpoints.
     */
    bool begin();

    /**
     * @brief Periodic network servicing routine called from Core 0 network task.
     * Batches samples from the ring buffer and streams them to active WebSocket clients.
     */
    void processStreaming();

    /**
     * @brief Number of active WebSocket clients (browsers and python scripts).
     */
    uint32_t getClientCount() const;

private:
    SensorManager& _sensorMgr;
    PPGRingBuffer<RING_BUFFER_CAPACITY>& _ringBuffer;

    AsyncWebServer _server;
    AsyncWebSocket _ws;

    uint32_t _last_status_broadcast_ms;
    bool _littlefs_mounted;

    void setupRoutes();
    void onWebSocketEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
                        AwsEventType type, void* arg, uint8_t* data, size_t len);
    void broadcastStatus();
    void streamBatchToClients(PPGSample* batch, size_t count);
};
