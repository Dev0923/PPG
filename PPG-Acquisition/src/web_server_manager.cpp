#include "web_server_manager.h"
#include "sensor_manager.h"
#include "web_assets.h"
#include <ArduinoJson.h>

WebServerManager::WebServerManager(SensorManager& sensorMgr, PPGRingBuffer<RING_BUFFER_CAPACITY>& ringBuffer)
    : _sensorMgr(sensorMgr),
      _ringBuffer(ringBuffer),
      _server(HTTP_SERVER_PORT),
      _ws(WS_ENDPOINT_PATH),
      _last_status_broadcast_ms(0),
      _littlefs_mounted(false)
{
}

bool WebServerManager::begin() {
    Serial.println("\n[NETWORK] Configuring Wi-Fi Access Point...");
    WiFi.mode(WIFI_AP);

    // Static IP configuration
    if (!WiFi.softAPConfig(WIFI_AP_LOCAL_IP, WIFI_AP_GATEWAY, WIFI_AP_SUBNET)) {
        Serial.println("[NETWORK][ERROR] softAPConfig failed!");
    }

    // Start SoftAP
    bool ap_ok = WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD, WIFI_AP_CHANNEL, WIFI_AP_SSID_HIDDEN, WIFI_AP_MAX_CLIENTS);
    if (!ap_ok) {
        Serial.println("[NETWORK][ERROR] Failed to start SoftAP!");
        return false;
    }

    Serial.printf("[NETWORK] Wi-Fi SoftAP started successfully.\n");
    Serial.printf("  - SSID:     %s\n", WIFI_AP_SSID);
    Serial.printf("  - Password: %s\n", WIFI_AP_PASSWORD);
    Serial.printf("  - IP:       %s\n", WiFi.softAPIP().toString().c_str());
    Serial.printf("  - URL:      http://%s/\n", WiFi.softAPIP().toString().c_str());

    // Initialize LittleFS
    if (LittleFS.begin(true)) {
        _littlefs_mounted = true;
        Serial.println("[STORAGE] LittleFS filesystem mounted.");
    } else {
        Serial.println("[STORAGE][WARNING] LittleFS mount failed; using embedded PROGMEM fallback.");
    }

    // Set up WebSocket handler and HTTP routes
    setupRoutes();

    // Start server
    _server.begin();
    Serial.println("[HTTP] AsyncWebServer and WebSocket listening on port 80.");
    return true;
}

void WebServerManager::setupRoutes() {
    // 1. WebSocket handler
    _ws.onEvent([this](AsyncWebSocket* server, AsyncWebSocketClient* client,
                       AwsEventType type, void* arg, uint8_t* data, size_t len) {
        this->onWebSocketEvent(server, client, type, arg, data, len);
    });
    _server.addHandler(&_ws);

    // 2. Root Dashboard HTML endpoint
    _server.on("/", HTTP_GET, [this](AsyncWebServerRequest* request) {
        if (_littlefs_mounted && LittleFS.exists("/index.html")) {
            request->send(LittleFS, "/index.html", "text/html");
        } else {
            // Serve embedded PROGMEM copy
            request->send(200, "text/html", INDEX_HTML_PROGMEM);
        }
    });

    // 3. API: Status telemetry endpoint (JSON)
    _server.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest* request) {
        PPGSystemStatus st = _sensorMgr.getStatus();
        st.connected_clients = _ws.count();

        JsonDocument doc;
        doc["sensor_ok"] = st.sensor_ok;
        doc["part_id"] = st.part_id;
        doc["revision_id"] = st.revision_id;
        doc["total_samples"] = st.total_samples;
        doc["dropped_samples"] = st.dropped_samples;
        doc["fifo_overflow_count"] = st.fifo_overflow_count;
        doc["nominal_rate_hz"] = st.nominal_rate_hz;
        doc["measured_rate_hz"] = st.measured_rate_hz;
        doc["uptime_seconds"] = st.uptime_seconds;
        doc["connected_clients"] = st.connected_clients;
        doc["free_heap"] = st.free_heap;
        doc["last_raw_red"] = st.last_raw_red;
        doc["last_raw_ir"] = st.last_raw_ir;

        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    // 4. API: Sensor configuration endpoint (JSON)
    _server.on("/api/config", HTTP_GET, [this](AsyncWebServerRequest* request) {
        SensorConfig cfg = _sensorMgr.getConfig();

        JsonDocument doc;
        doc["sample_rate_hz"] = cfg.sample_rate_hz;
        doc["sample_period_us"] = cfg.sample_period_us;
        doc["pulse_width_us"] = cfg.pulse_width_us;
        doc["adc_resolution_bits"] = cfg.adc_resolution_bits;
        doc["adc_range_na"] = cfg.adc_range_na;
        doc["sample_average"] = cfg.sample_average;
        doc["led_mode"] = cfg.led_mode;
        doc["led_current_red"] = cfg.led_current_red;
        doc["led_current_ir"] = cfg.led_current_ir;

        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    // 5. API: Control Acquisition (Start / Stop / Toggle)
    _server.on("/api/control", HTTP_ANY, [this](AsyncWebServerRequest* request) {
        if (request->hasParam("action")) {
            String act = request->getParam("action")->value();
            if (act == "start") {
                _sensorMgr.setAcquiring(true);
            } else if (act == "stop") {
                _sensorMgr.setAcquiring(false);
            } else if (act == "toggle") {
                _sensorMgr.setAcquiring(!_sensorMgr.isAcquiring());
            }
        }
        JsonDocument doc;
        doc["status"] = "ok";
        doc["acquiring"] = _sensorMgr.isAcquiring();
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    // 6. API: Reset Session sequence endpoint
    _server.on("/api/reset", HTTP_POST, [this](AsyncWebServerRequest* request) {
        _sensorMgr.resetSession();
        request->send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Session reset\"}");
    });

    // 6. API: Download RAM buffer as CSV
    _server.on("/api/download", HTTP_GET, [this](AsyncWebServerRequest* request) {
        AsyncResponseStream* response = request->beginResponseStream("text/csv");
        response->addHeader("Content-Disposition", "attachment; filename=\"esp32_ppg_buffer.csv\"");

        PPGSystemStatus st = _sensorMgr.getStatus();
        SensorConfig cfg = _sensorMgr.getConfig();

        response->printf("# MAX30102 Raw Optical PPG Telemetry Export\n");
        response->printf("# Uptime: %u s | Nominal Fs: %u Hz | Measured Fs: %.2f Hz\n",
                         st.uptime_seconds, cfg.sample_rate_hz, st.measured_rate_hz);
        response->printf("# ADC: %u-bit (%u us) | Range: %u nA\n",
                         cfg.adc_resolution_bits, cfg.pulse_width_us, cfg.adc_range_na);
        response->printf("# Red LED: 0x%02X | IR LED: 0x%02X\n",
                         cfg.led_current_red, cfg.led_current_ir);
        response->printf("session_id,seq,timestamp_us,raw_red,raw_ir\n");

        // Drain up to 200 samples currently in buffer
        PPGSample sample;
        size_t dumped = 0;
        while (_ringBuffer.pop(sample) && dumped < 500) {
            response->printf("buffer_export,%u,%llu,%u,%u\n",
                             sample.seq, sample.timestamp_us, sample.raw_red, sample.raw_ir);
            dumped++;
        }

        request->send(response);
    });

    // 404 Handler
    _server.onNotFound([](AsyncWebServerRequest* request) {
        request->send(404, "text/plain", "404 Not Found");
    });
}

void WebServerManager::onWebSocketEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
                                        AwsEventType type, void* arg, uint8_t* data, size_t len) {
    switch (type) {
        case WS_EVT_CONNECT: {
            IPAddress clientIp = client->remoteIP();
            Serial.printf("[WS] Client #%u connected from %s (Total clients: %u)\n",
                          client->id(), clientIp.toString().c_str(), _ws.count());

            // Send initial configuration and status greeting to client
            PPGSystemStatus st = _sensorMgr.getStatus();
            char greetingBuf[256];
            snprintf(greetingBuf, sizeof(greetingBuf),
                     "{\"type\":\"status\",\"fs_nom\":%.1f,\"fs_act\":%.2f,\"total\":%u,\"dropped\":%u,\"overflow\":%u,\"uptime\":%u,\"clients\":%u,\"free_heap\":%u}",
                     st.nominal_rate_hz, st.measured_rate_hz, st.total_samples,
                     st.dropped_samples, st.fifo_overflow_count, st.uptime_seconds,
                     (uint32_t)_ws.count(), (uint32_t)st.free_heap);
            client->text(greetingBuf);
            break;
        }

        case WS_EVT_DISCONNECT: {
            Serial.printf("[WS] Client #%u disconnected (Remaining: %u)\n", client->id(), _ws.count());
            break;
        }

        case WS_EVT_DATA: {
            // Client sent data (e.g. command)
            AwsFrameInfo* info = (AwsFrameInfo*)arg;
            if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
                data[len] = 0; // null-terminate
                char* text = (char*)data;
                if (strstr(text, "\"cmd\":\"reset\"") != nullptr) {
                    _sensorMgr.resetSession();
                    client->text("{\"type\":\"notice\",\"msg\":\"Session reset executed\"}");
                }
            }
            break;
        }

        case WS_EVT_ERROR: {
            Serial.printf("[WS] Error on client #%u\n", client->id());
            break;
        }

        case WS_EVT_PONG:
            break;
    }
}

void WebServerManager::processStreaming() {
    // Clean up disconnected WebSocket clients
    _ws.cleanupClients();

    // Drain samples from ring buffer
    const size_t BATCH_SIZE = WS_STREAM_BATCH_SIZE;
    PPGSample batch[BATCH_SIZE];

    while (_ringBuffer.available() >= BATCH_SIZE) {
        size_t popped = _ringBuffer.popBatch(batch, BATCH_SIZE);
        if (popped > 0 && _ws.count() > 0) {
            streamBatchToClients(batch, popped);
        }
    }

    // If remaining samples exist and no more are coming immediately, flush them
    size_t remaining = _ringBuffer.available();
    if (remaining > 0 && remaining < BATCH_SIZE) {
        size_t popped = _ringBuffer.popBatch(batch, remaining);
        if (popped > 0 && _ws.count() > 0) {
            streamBatchToClients(batch, popped);
        }
    }

    // Periodic 1Hz status broadcast
    uint32_t now_ms = millis();
    if (now_ms - _last_status_broadcast_ms >= DIAGNOSTIC_PRINT_INTERVAL_MS) {
        _last_status_broadcast_ms = now_ms;
        if (_ws.count() > 0) {
            broadcastStatus();
        }
    }
}

void WebServerManager::streamBatchToClients(PPGSample* batch, size_t count) {
    if (count == 0 || _ws.count() == 0) return;

    // Build compact JSON batch message:
    // {"type":"batch","samples":[{"seq":1,"t":10000,"r":120000,"ir":180000},...]}
    char jsonBuf[1024];
    int len = snprintf(jsonBuf, sizeof(jsonBuf), "{\"type\":\"batch\",\"samples\":[");

    for (size_t i = 0; i < count; i++) {
        int written = snprintf(jsonBuf + len, sizeof(jsonBuf) - len,
                               "%s{\"seq\":%u,\"t\":%llu,\"r\":%u,\"ir\":%u}",
                               (i > 0 ? "," : ""),
                               batch[i].seq,
                               batch[i].timestamp_us,
                               batch[i].raw_red,
                               batch[i].raw_ir);
        if (written > 0 && (len + written) < (int)sizeof(jsonBuf) - 4) {
            len += written;
        } else {
            break; // prevent buffer overflow
        }
    }

    snprintf(jsonBuf + len, sizeof(jsonBuf) - len, "]}");
    _ws.textAll(jsonBuf);
}

void WebServerManager::broadcastStatus() {
    PPGSystemStatus st = _sensorMgr.getStatus();
    char statusBuf[320];
    snprintf(statusBuf, sizeof(statusBuf),
             "{\"type\":\"status\",\"fs_nom\":%.1f,\"fs_act\":%.2f,\"total\":%u,\"dropped\":%u,\"overflow\":%u,\"uptime\":%u,\"clients\":%u,\"free_heap\":%u}",
             st.nominal_rate_hz, st.measured_rate_hz, st.total_samples,
             st.dropped_samples, st.fifo_overflow_count, st.uptime_seconds,
             (uint32_t)_ws.count(), (uint32_t)st.free_heap);
    _ws.textAll(statusBuf);
}

uint32_t WebServerManager::getClientCount() const {
    return _ws.count();
}
