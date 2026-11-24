#pragma once

#include <Arduino.h>

/**
 * @brief Embedded fallback PROGMEM HTML for the research web dashboard.
 * Serves automatically if LittleFS filesystem image has not been uploaded yet.
 */
static const char INDEX_HTML_PROGMEM[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>MAX30102 + ESP32-S3 Raw PPG Acquisition System</title>
    <style>
        :root {
            --bg-base: #080c14;
            --bg-surface: #0f172a;
            --bg-card: #141e33;
            --border: #1e293b;
            --border-highlight: #334155;
            --text-primary: #f8fafc;
            --text-secondary: #94a3b8;
            --text-muted: #64748b;
            --color-ir: #00e5ff;
            --color-red: #ff3366;
            --color-success: #10b981;
            --color-warning: #f59e0b;
            --color-danger: #ef4444;
            --color-accent: #3b82f6;
            --font-mono: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, "Liberation Mono", "Courier New", monospace;
            --font-sans: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
        }

        * {
            box-sizing: border-box;
            margin: 0;
            padding: 0;
        }

        body {
            background-color: var(--bg-base);
            color: var(--text-primary);
            font-family: var(--font-sans);
            font-size: 14px;
            line-height: 1.5;
            min-height: 100vh;
            display: flex;
            flex-direction: column;
            overflow-x: hidden;
        }

        /* Top Navigation Bar */
        header {
            background-color: var(--bg-surface);
            border-bottom: 1px solid var(--border);
            padding: 12px 24px;
            display: flex;
            align-items: center;
            justify-content: space-between;
            flex-wrap: wrap;
            gap: 12px;
        }

        .header-title-group {
            display: flex;
            align-items: center;
            gap: 14px;
        }

        .brand-badge {
            background: linear-gradient(135deg, #2563eb, #1d4ed8);
            color: white;
            padding: 4px 8px;
            border-radius: 4px;
            font-size: 11px;
            font-weight: 700;
            letter-spacing: 0.5px;
            text-transform: uppercase;
        }

        h1 {
            font-size: 17px;
            font-weight: 600;
            letter-spacing: -0.2px;
        }

        .header-status-group {
            display: flex;
            align-items: center;
            gap: 16px;
        }

        .status-pill {
            display: flex;
            align-items: center;
            gap: 8px;
            background-color: var(--bg-card);
            border: 1px solid var(--border);
            padding: 5px 12px;
            border-radius: 9999px;
            font-size: 12px;
            font-weight: 500;
        }

        .status-dot {
            width: 8px;
            height: 8px;
            border-radius: 50%;
            background-color: var(--color-warning);
        }

        .status-dot.connected {
            background-color: var(--color-success);
            box-shadow: 0 0 8px var(--color-success);
        }

        .status-dot.disconnected {
            background-color: var(--color-danger);
            box-shadow: 0 0 8px var(--color-danger);
        }

        /* Main Layout */
        main {
            flex: 1;
            padding: 20px 24px;
            max-width: 1600px;
            width: 100%;
            margin: 0 auto;
            display: flex;
            flex-direction: column;
            gap: 20px;
        }

        /* Notice Banner */
        .telemetry-banner {
            background-color: rgba(30, 41, 59, 0.5);
            border: 1px solid var(--border);
            border-left: 4px solid var(--color-accent);
            padding: 10px 16px;
            border-radius: 6px;
            display: flex;
            justify-content: space-between;
            align-items: center;
            font-size: 12px;
            color: var(--text-secondary);
        }

        .telemetry-banner strong {
            color: var(--text-primary);
        }

        /* Metrics Row */
        .metrics-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
            gap: 14px;
        }

        .metric-card {
            background-color: var(--bg-surface);
            border: 1px solid var(--border);
            border-radius: 8px;
            padding: 14px 16px;
            display: flex;
            flex-direction: column;
            justify-content: space-between;
            position: relative;
            overflow: hidden;
        }

        .metric-card.ir-card {
            border-top: 3px solid var(--color-ir);
        }

        .metric-card.red-card {
            border-top: 3px solid var(--color-red);
        }

        .metric-label {
            font-size: 11px;
            text-transform: uppercase;
            letter-spacing: 0.5px;
            color: var(--text-secondary);
            margin-bottom: 6px;
            display: flex;
            justify-content: space-between;
            align-items: center;
        }

        .metric-value {
            font-family: var(--font-mono);
            font-size: 22px;
            font-weight: 700;
            color: var(--text-primary);
        }

        .metric-value.ir {
            color: var(--color-ir);
        }

        .metric-value.red {
            color: var(--color-red);
        }

        .metric-sub {
            font-size: 11px;
            color: var(--text-muted);
            margin-top: 4px;
            font-family: var(--font-mono);
        }

        .metric-value.error-badge {
            color: var(--color-danger);
        }

        /* Oscilloscope Card & Canvas */
        .chart-container-card {
            background-color: var(--bg-surface);
            border: 1px solid var(--border);
            border-radius: 8px;
            padding: 16px;
            display: flex;
            flex-direction: column;
            gap: 14px;
        }

        .chart-toolbar {
            display: flex;
            justify-content: space-between;
            align-items: center;
            flex-wrap: wrap;
            gap: 12px;
            border-bottom: 1px solid var(--border);
            padding-bottom: 12px;
        }

        .chart-legend {
            display: flex;
            align-items: center;
            gap: 20px;
        }

        .legend-item {
            display: flex;
            align-items: center;
            gap: 8px;
            font-size: 12px;
            font-weight: 600;
            cursor: pointer;
            user-select: none;
        }

        .legend-indicator {
            width: 12px;
            height: 12px;
            border-radius: 2px;
        }

        .legend-indicator.ir { background-color: var(--color-ir); box-shadow: 0 0 6px var(--color-ir); }
        .legend-indicator.red { background-color: var(--color-red); box-shadow: 0 0 6px var(--color-red); }

        .toolbar-controls {
            display: flex;
            align-items: center;
            gap: 10px;
            flex-wrap: wrap;
        }

        button, .btn {
            background-color: var(--bg-card);
            border: 1px solid var(--border-highlight);
            color: var(--text-primary);
            padding: 6px 14px;
            border-radius: 6px;
            font-size: 12px;
            font-weight: 500;
            cursor: pointer;
            transition: all 0.15s ease;
            display: inline-flex;
            align-items: center;
            gap: 6px;
            text-decoration: none;
        }

        button:hover, .btn:hover {
            background-color: #1e293b;
            border-color: #475569;
        }

        button.primary {
            background-color: #2563eb;
            border-color: #3b82f6;
            color: white;
        }

        button.primary:hover {
            background-color: #1d4ed8;
        }

        button.danger {
            background-color: #991b1b;
            border-color: #ef4444;
            color: white;
        }

        button.danger:hover {
            background-color: #b91c1c;
        }

        button.active {
            background-color: #1e3a8a;
            border-color: #60a5fa;
            color: #93c5fd;
        }

        select {
            background-color: var(--bg-card);
            border: 1px solid var(--border-highlight);
            color: var(--text-primary);
            padding: 6px 10px;
            border-radius: 6px;
            font-size: 12px;
            cursor: pointer;
            outline: none;
        }

        /* Canvas Wrapper */
        .canvas-stack {
            display: flex;
            flex-direction: column;
            gap: 12px;
        }

        .canvas-pane {
            position: relative;
            background-color: #05080e;
            border: 1px solid #1a2234;
            border-radius: 6px;
            height: 250px;
            width: 100%;
            overflow: hidden;
        }

        canvas {
            display: block;
            width: 100%;
            height: 100%;
        }

        .canvas-overlay-labels {
            position: absolute;
            top: 8px;
            left: 12px;
            font-size: 11px;
            font-family: var(--font-mono);
            color: var(--text-muted);
            pointer-events: none;
            display: flex;
            gap: 16px;
        }

        .canvas-y-axis {
            position: absolute;
            right: 8px;
            top: 8px;
            bottom: 8px;
            display: flex;
            flex-direction: column;
            justify-content: space-between;
            font-size: 10px;
            font-family: var(--font-mono);
            color: var(--text-muted);
            pointer-events: none;
            text-align: right;
        }

        /* Controls & Recording Panel */
        .control-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(320px, 1fr));
            gap: 16px;
        }

        .control-card {
            background-color: var(--bg-surface);
            border: 1px solid var(--border);
            border-radius: 8px;
            padding: 16px;
            display: flex;
            flex-direction: column;
            gap: 12px;
        }

        .control-card h3 {
            font-size: 14px;
            font-weight: 600;
            display: flex;
            align-items: center;
            gap: 8px;
            color: var(--text-primary);
        }

        .btn-row {
            display: flex;
            gap: 10px;
            flex-wrap: wrap;
        }

        .info-table {
            width: 100%;
            border-collapse: collapse;
            font-size: 12px;
        }

        .info-table td {
            padding: 6px 0;
            border-bottom: 1px solid rgba(255, 255, 255, 0.05);
        }

        .info-table td:first-child {
            color: var(--text-secondary);
            width: 45%;
        }

        .info-table td:last-child {
            font-family: var(--font-mono);
            color: var(--text-primary);
            text-align: right;
        }

        /* Footer */
        footer {
            background-color: var(--bg-surface);
            border-top: 1px solid var(--border);
            padding: 14px 24px;
            text-align: center;
            font-size: 12px;
            color: var(--text-muted);
        }

        /* Responsive tweaks */
        @media (max-width: 768px) {
            header { padding: 10px 16px; }
            main { padding: 12px 16px; }
            .canvas-pane { height: 200px; }
            .metric-value { font-size: 18px; }
        /* Live Monitor Hero Action Bar */
        .live-monitor-hero {
            background: linear-gradient(135deg, rgba(15, 23, 42, 0.95), rgba(20, 30, 51, 0.95));
            border: 1px solid var(--border-highlight);
            border-radius: 12px;
            padding: 16px 20px;
            display: flex;
            align-items: center;
            justify-content: space-between;
            flex-wrap: wrap;
            gap: 16px;
            box-shadow: 0 8px 24px rgba(0, 0, 0, 0.4);
        }

        .hero-left, .hero-right {
            display: flex;
            align-items: center;
            gap: 14px;
            flex-wrap: wrap;
        }

        .btn-live-monitor {
            background: linear-gradient(135deg, #10b981, #059669);
            color: #ffffff;
            font-size: 14px;
            font-weight: 700;
            padding: 10px 22px;
            border-radius: 8px;
            border: none;
            cursor: pointer;
            display: flex;
            align-items: center;
            gap: 10px;
            transition: all 0.2s ease;
            box-shadow: 0 0 15px rgba(16, 185, 129, 0.4);
        }

        .btn-live-monitor:hover {
            transform: translateY(-1px);
            box-shadow: 0 0 20px rgba(16, 185, 129, 0.6);
        }

        .btn-live-monitor.paused {
            background: linear-gradient(135deg, #334155, #1e293b);
            color: #94a3b8;
            box-shadow: none;
            border: 1px solid var(--border-highlight);
        }

        .pulse-beacon {
            width: 10px;
            height: 10px;
            border-radius: 50%;
            background-color: #ffffff;
            box-shadow: 0 0 8px #ffffff;
            animation: pulse-ring 1.5s infinite;
        }

        .btn-live-monitor.paused .pulse-beacon {
            animation: none;
            background-color: #64748b;
            box-shadow: none;
        }

        @keyframes pulse-ring {
            0% { transform: scale(0.95); opacity: 1; }
            50% { transform: scale(1.3); opacity: 0.7; }
            100% { transform: scale(0.95); opacity: 1; }
        }

        .finger-guide {
            font-size: 13px;
            display: flex;
            align-items: center;
            gap: 8px;
            padding: 8px 14px;
            border-radius: 6px;
            background: rgba(30, 41, 59, 0.6);
            border: 1px solid var(--border);
            color: var(--text-primary);
        }

        .heart-rate-badge {
            background: rgba(239, 68, 68, 0.12);
            border: 1px solid rgba(239, 68, 68, 0.35);
            border-radius: 8px;
            padding: 6px 16px;
            display: flex;
            align-items: center;
            gap: 10px;
        }

        .heart-icon {
            font-size: 20px;
            display: inline-block;
        }

        .heart-icon.beating {
            animation: heart-beat 0.6s ease;
        }

        @keyframes heart-beat {
            0% { transform: scale(1); }
            30% { transform: scale(1.35); }
            60% { transform: scale(1); }
        }
    </style>
</head>
<body>

    <!-- Header -->
    <header>
        <div class="header-title-group">
            <span class="brand-badge">PPG Core</span>
            <div>
                <h1>MAX30102 + ESP32-S3 Raw Optical Acquisition System</h1>
                <div style="font-size: 11px; color: var(--text-muted);">Hardware: ESP32-S3-WROOM-1-N16R8 | I2C (SDA=8, SCL=9) | 100 Hz Sync</div>
            </div>
        </div>
        <div class="header-status-group">
            <div class="status-pill">
                <span class="status-dot" id="wsStatusDot"></span>
                <span id="wsStatusText">Connecting...</span>
            </div>
            <div class="status-pill" style="font-family: var(--font-mono);">
                <span id="headerSessionTime">00:00:00</span>
            </div>
        </div>
    </header>

    <!-- Main Content -->
    <main>
        <!-- Telemetry Notice -->
        <div class="telemetry-banner">
            <div>
                <strong>Research Telemetry Notice:</strong> Waveform plots and counters display raw 18-bit optical ADC counts directly from the MAX30102 FIFO. <em>No baseline normalization, digital smoothing, or clinical filtering applied.</em>
            </div>
            <div id="deviceIpLabel">AP: 192.168.4.1</div>
        </div>

        <!-- Live Monitor Hero Action Bar -->
        <section class="live-monitor-hero">
            <div class="hero-left">
                <button id="btnMainLiveMonitor" class="btn-live-monitor" title="Click to start/pause live optical waveform streaming">
                    <span class="pulse-beacon"></span>
                    <span id="btnMainLiveMonitorText">Live Monitor: ACTIVE</span>
                </button>
                <div class="finger-guide" id="heroFingerGuide">
                    <span id="heroFingerIcon">👆</span>
                    <span id="heroFingerText">Place fingertip gently over sensor glass to measure PPG</span>
                </div>
            </div>
            <div class="hero-right">
                <div class="heart-rate-badge">
                    <span class="heart-icon" id="heartIcon">❤️</span>
                    <div>
                        <div style="font-size: 10px; color: #fca5a5; font-weight: 600;">ESTIMATED PULSE</div>
                        <div style="font-family: var(--font-mono); font-size: 18px; font-weight: 700; color: #f87171;" id="valLiveBPM">-- BPM</div>
                    </div>
                </div>
                <button id="btnQuickRecord" class="btn primary" style="padding: 10px 16px; font-weight: 600;">
                    ⏺ Start Recording
                </button>
                <button id="btnQuickExport" class="btn" style="padding: 10px 16px; font-weight: 600;" disabled>
                    📥 Export CSV
                </button>
            </div>
        </section>

        <!-- Metrics Row -->
        <section class="metrics-grid">
            <!-- Infrared Metric -->
            <div class="metric-card ir-card">
                <div class="metric-label">
                    <span>IR Optical Signal (880 nm)</span>
                    <span style="font-family: var(--font-mono); font-size: 10px;">18-bit ADC</span>
                </div>
                <div class="metric-value ir" id="valRawIR">------</div>
                <div class="metric-sub" id="subIRMinMax">Min: -- | Max: -- | P-P: --</div>
            </div>

            <!-- Red Metric -->
            <div class="metric-card red-card">
                <div class="metric-label">
                    <span>Red Optical Signal (660 nm)</span>
                    <span style="font-family: var(--font-mono); font-size: 10px;">18-bit ADC</span>
                </div>
                <div class="metric-value red" id="valRawRed">------</div>
                <div class="metric-sub" id="subRedMinMax">Min: -- | Max: -- | P-P: --</div>
            </div>

            <!-- Sampling Rate Metric -->
            <div class="metric-card">
                <div class="metric-label">
                    <span>Sampling Rate</span>
                    <span style="font-family: var(--font-mono); font-size: 10px;">Nominal: 100 Hz</span>
                </div>
                <div class="metric-value" id="valSampleRate">--.- Hz</div>
                <div class="metric-sub" id="subInterval">Period: ~10.00 ms (Jitter: <0.5ms)</div>
            </div>

            <!-- Total Samples Metric -->
            <div class="metric-card">
                <div class="metric-label">
                    <span>Total Samples</span>
                    <span style="font-family: var(--font-mono); font-size: 10px;">Seq Counter</span>
                </div>
                <div class="metric-value" id="valTotalSamples">0</div>
                <div class="metric-sub" id="subSeq">Latest Seq: #0</div>
            </div>

            <!-- Dropped Samples / FIFO Overflows Metric -->
            <div class="metric-card">
                <div class="metric-label">
                    <span>Integrity / Data Loss</span>
                    <span style="font-family: var(--font-mono); font-size: 10px;">Buffer & FIFO</span>
                </div>
                <div class="metric-value" id="valLoss">0</div>
                <div class="metric-sub" id="subLossDetails">Ring Drops: 0 | FIFO OVF: 0</div>
            </div>
        </section>

        <!-- Waveform Oscilloscopes -->
        <section class="chart-container-card">
            <div class="chart-toolbar">
                <div class="chart-legend">
                    <div class="legend-item" id="legendToggleIR">
                        <span class="legend-indicator ir"></span>
                        <span>IR Channel (880 nm)</span>
                    </div>
                    <div class="legend-item" id="legendToggleRed">
                        <span class="legend-indicator red"></span>
                        <span>Red Channel (660 nm)</span>
                    </div>
                </div>

                <div class="toolbar-controls">
                    <!-- Hardware Sensor Acquisition Toggle -->
                    <button id="btnAcqToggle" class="primary" title="Start or Stop MAX30102 optical sensor acquisition">
                        Acquisition: Active
                    </button>

                    <!-- Zoom/Display View -->
                    <button id="btnVisualFilter" title="Toggle AC-zoom view on the canvas. Raw data stream remains untouched!">
                        Visual: Raw Counts
                    </button>

                    <!-- Time Window -->
                    <label style="font-size: 12px; color: var(--text-secondary);" for="selectTimeWindow">Window:</label>
                    <select id="selectTimeWindow">
                        <option value="3">3 Seconds (300 samples)</option>
                        <option value="5" selected>5 Seconds (500 samples)</option>
                        <option value="10">10 Seconds (1000 samples)</option>
                    </select>

                    <!-- Pause Rendering -->
                    <button id="btnPauseRender">Pause Scope</button>
                    <!-- Clear Buffer -->
                    <button id="btnClearScope">Clear Scope</button>
                </div>
            </div>

            <div class="canvas-stack">
                <!-- IR Channel Pane -->
                <div class="canvas-pane" id="paneIR">
                    <canvas id="canvasIR"></canvas>
                    <div class="canvas-overlay-labels">
                        <span style="color: var(--color-ir); font-weight: 600;">INFRARED (880nm)</span>
                        <span id="canvasIRInfo">Raw Counts (0 - 262143)</span>
                    </div>
                    <div class="canvas-y-axis" id="axisY_IR">
                        <span id="axisIR_Max">262k</span>
                        <span id="axisIR_Mid">131k</span>
                        <span id="axisIR_Min">0</span>
                    </div>
                </div>

                <!-- Red Channel Pane -->
                <div class="canvas-pane" id="paneRed">
                    <canvas id="canvasRed"></canvas>
                    <div class="canvas-overlay-labels">
                        <span style="color: var(--color-red); font-weight: 600;">RED (660nm)</span>
                        <span id="canvasRedInfo">Raw Counts (0 - 262143)</span>
                    </div>
                    <div class="canvas-y-axis" id="axisY_Red">
                        <span id="axisRed_Max">262k</span>
                        <span id="axisRed_Mid">131k</span>
                        <span id="axisRed_Min">0</span>
                    </div>
                </div>
            </div>
        </section>

        <!-- Controls, Recording, and Sensor Status -->
        <section class="control-grid">
            <!-- Recording Controls -->
            <div class="control-card">
                <h3>
                    <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="10"/><circle cx="12" cy="12" r="4" fill="currentColor"/></svg>
                    Data Recording & Export
                </h3>
                <p style="font-size: 12px; color: var(--text-secondary);">
                    Record telemetry directly in your browser or trigger the dedicated Python recording script (<code style="color: #93c5fd;">scripts/record_ppg.py</code>) on your computer.
                </p>

                <div class="btn-row" style="margin-top: 6px;">
                    <button class="primary" id="btnToggleRecord">
                        Start Browser Recording
                    </button>
                    <button id="btnExportCSV" disabled>
                        Export CSV (<span id="recordCountLabel">0</span>)
                    </button>
                    <a class="btn" href="/api/download" download="esp32_ppg_buffer.csv" title="Download recent ring buffer directly from ESP32 RAM">
                        Download Device Buffer CSV
                    </a>
                </div>

                <div id="recordingStatusBanner" style="font-size: 11px; font-family: var(--font-mono); color: var(--text-muted); padding: 8px; background: rgba(0,0,0,0.2); border-radius: 4px;">
                    Status: Standby. Ready to record raw optical stream.
                </div>
            </div>

            <!-- Hardware & Acquisition Diagnostics -->
            <div class="control-card">
                <h3>
                    <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="2" y="2" width="20" height="8" rx="2"/><rect x="2" y="14" width="20" height="8" rx="2"/><line x1="6" y1="6" x2="6.01" y2="6"/><line x1="6" y1="18" x2="6.01" y2="18"/></svg>
                    Sensor & System Status
                </h3>
                <table class="info-table">
                    <tr>
                        <td>Sensor Model / Part ID</td>
                        <td id="diagPartID">MAX30102 (0x15)</td>
                    </tr>
                    <tr>
                        <td>Optical Configuration</td>
                        <td>100 Hz | 18-bit (411 &mu;s) | 4096 nA</td>
                    </tr>
                    <tr>
                        <td>LED Pulse Amplitude</td>
                        <td>Red: 0x1F (~6.4mA) | IR: 0x1F (~6.4mA)</td>
                    </tr>
                    <tr>
                        <td>I2C Bus & Timing</td>
                        <td>400 kHz Fast-Mode | Core 1 Pinned</td>
                    </tr>
                    <tr>
                        <td>Connected WebSocket Clients</td>
                        <td id="diagClients">1 client</td>
                    </tr>
                    <tr>
                        <td>ESP32-S3 Free Heap</td>
                        <td id="diagHeap">-- KB</td>
                    </tr>
                </table>
            </div>
        </section>
    </main>

    <!-- Footer -->
    <footer>
        Research-grade Photoplethysmography Telemetry System &bull; ESP32-S3-WROOM-1-N16R8 & MAX30102 &bull; Unsmoothed Raw Signal Acquisition
    </footer>

    <!-- JavaScript Application Logic -->
    <script>
        (function() {
            // Configuration & State
            const CONFIG = {
                wsUrl: `ws://${window.location.host}/ws`,
                nominalFs: 100,
                maxBufferSize: 1200 // Up to 12s of history at 100Hz
            };

            const state = {
                ws: null,
                connected: false,
                paused: false,
                visualAcZoom: false, // Visual-only AC subtraction toggle
                timeWindowSec: 5,
                samples: [], // Array of { seq, t, r, ir }
                startTimeMs: Date.now(),
                totalSamples: 0,
                droppedCount: 0,
                overflowCount: 0,
                measuredFs: 100.0,
                // Client-side recording
                recording: false,
                recordedData: [],
                recordStartTime: 0
            };

            // DOM Elements
            const el = {
                wsDot: document.getElementById('wsStatusDot'),
                wsText: document.getElementById('wsStatusText'),
                sessionTime: document.getElementById('headerSessionTime'),
                valRawIR: document.getElementById('valRawIR'),
                valRawRed: document.getElementById('valRawRed'),
                subIRMinMax: document.getElementById('subIRMinMax'),
                subRedMinMax: document.getElementById('subRedMinMax'),
                valSampleRate: document.getElementById('valSampleRate'),
                valTotalSamples: document.getElementById('valTotalSamples'),
                subSeq: document.getElementById('subSeq'),
                valLoss: document.getElementById('valLoss'),
                subLossDetails: document.getElementById('subLossDetails'),
                btnVisualFilter: document.getElementById('btnVisualFilter'),
                selectTimeWindow: document.getElementById('selectTimeWindow'),
                btnPauseRender: document.getElementById('btnPauseRender'),
                btnClearScope: document.getElementById('btnClearScope'),
                canvasIR: document.getElementById('canvasIR'),
                canvasRed: document.getElementById('canvasRed'),
                axisIR_Max: document.getElementById('axisIR_Max'),
                axisIR_Mid: document.getElementById('axisIR_Mid'),
                axisIR_Min: document.getElementById('axisIR_Min'),
                axisRed_Max: document.getElementById('axisRed_Max'),
                axisRed_Mid: document.getElementById('axisRed_Mid'),
                axisRed_Min: document.getElementById('axisRed_Min'),
                canvasIRInfo: document.getElementById('canvasIRInfo'),
                canvasRedInfo: document.getElementById('canvasRedInfo'),
                btnMainLiveMonitor: document.getElementById('btnMainLiveMonitor'),
                btnMainLiveMonitorText: document.getElementById('btnMainLiveMonitorText'),
                heroFingerGuide: document.getElementById('heroFingerGuide'),
                heroFingerIcon: document.getElementById('heroFingerIcon'),
                heroFingerText: document.getElementById('heroFingerText'),
                heartIcon: document.getElementById('heartIcon'),
                valLiveBPM: document.getElementById('valLiveBPM'),
                btnQuickRecord: document.getElementById('btnQuickRecord'),
                btnQuickExport: document.getElementById('btnQuickExport'),
                btnToggleRecord: document.getElementById('btnToggleRecord'),
                btnExportCSV: document.getElementById('btnExportCSV'),
                recordCountLabel: document.getElementById('recordCountLabel'),
                recordingBanner: document.getElementById('recordingStatusBanner'),
                diagPartID: document.getElementById('diagPartID'),
                diagClients: document.getElementById('diagClients'),
                diagHeap: document.getElementById('diagHeap')
            };

            // Canvas 2D Contexts & High-DPI Scaling
            const ctxIR = el.canvasIR.getContext('2d');
            const ctxRed = el.canvasRed.getContext('2d');

            function resizeCanvases() {
                [el.canvasIR, el.canvasRed].forEach(cv => {
                    const rect = cv.getBoundingClientRect();
                    const dpr = window.devicePixelRatio || 1;
                    cv.width = rect.width * dpr;
                    cv.height = rect.height * dpr;
                });
            }
            window.addEventListener('resize', resizeCanvases);
            resizeCanvases();

            // WebSocket Connection Manager
            function connectWebSocket() {
                const url = (window.location.protocol === 'file:' || !window.location.host) 
                    ? 'ws://192.168.4.1/ws' 
                    : `ws://${window.location.host}/ws`;

                el.wsText.textContent = 'Connecting...';
                el.wsDot.className = 'status-dot';

                state.ws = new WebSocket(url);

                state.ws.onopen = () => {
                    state.connected = true;
                    el.wsDot.className = 'status-dot connected';
                    el.wsText.textContent = 'Live (100 Hz)';
                };

                state.ws.onclose = () => {
                    state.connected = false;
                    el.wsDot.className = 'status-dot disconnected';
                    el.wsText.textContent = 'Disconnected (Reconnecting...)';
                    setTimeout(connectWebSocket, 2000);
                };

                state.ws.onerror = () => {
                    state.ws.close();
                };

                state.ws.onmessage = (event) => {
                    try {
                        const msg = JSON.parse(event.data);
                        handleIncomingMessage(msg);
                    } catch (e) {
                        console.error('JSON parse error:', e);
                    }
                };
            }

            function handleIncomingMessage(msg) {
                if (msg.type === 'batch' && Array.isArray(msg.samples)) {
                    for (let i = 0; i < msg.samples.length; i++) {
                        const s = msg.samples[i];
                        processSample(s);
                    }
                } else if (msg.type === 'sample') {
                    processSample(msg);
                } else if (msg.type === 'status') {
                    updateSystemStatus(msg);
                }
            }

            function processSample(sample) {
                // sample: { seq, t, r, ir }
                state.totalSamples++;
                
                // Add to rolling display buffer
                state.samples.push(sample);
                if (state.samples.length > CONFIG.maxBufferSize) {
                    state.samples.shift();
                }

                // If browser recording is active, append to recordedData
                if (state.recording) {
                    state.recordedData.push(sample);
                    el.recordCountLabel.textContent = state.recordedData.length.toLocaleString();
                }

                // Update latest digital readouts
                el.valRawIR.textContent = sample.ir.toLocaleString();
                el.valRawRed.textContent = sample.r.toLocaleString();
                el.valTotalSamples.textContent = sample.seq.toLocaleString();
                el.subSeq.textContent = `Latest Seq: #${sample.seq}`;

                // Real-time finger presence detection & guidance update
                const isFinger = (sample.ir > 40000);
                if (isFinger) {
                    el.heroFingerIcon.textContent = '💚';
                    el.heroFingerText.textContent = 'Finger detected: Recording pulsatile arterial pulse wave';
                    el.heroFingerGuide.style.borderColor = 'rgba(16, 185, 129, 0.4)';
                } else {
                    el.heroFingerIcon.textContent = '👆';
                    el.heroFingerText.textContent = 'Place fingertip gently on sensor glass (do not press hard)';
                    el.heroFingerGuide.style.borderColor = 'var(--border)';
                    el.valLiveBPM.textContent = '-- BPM';
                    recentRrIntervals = [];
                }

                // Online systolic pulse detector
                if (isFinger) {
                    if (irBaseline === 0) irBaseline = sample.ir;
                    irBaseline = irBaseline * 0.995 + sample.ir * 0.005;
                    const ac = sample.ir - irBaseline;

                    if (!irPeakArm && ac > 800) {
                        irPeakArm = true;
                        const now = Date.now();
                        const dt = now - lastBeatMs;
                        if (lastBeatMs > 0 && dt >= 320 && dt <= 1500) {
                            recentRrIntervals.push(dt);
                            if (recentRrIntervals.length > 4) recentRrIntervals.shift();
                            const avgDt = recentRrIntervals.reduce((a, b) => a + b, 0) / recentRrIntervals.length;
                            const bpm = Math.round(60000 / avgDt);
                            el.valLiveBPM.textContent = `${bpm} BPM`;

                            el.heartIcon.classList.remove('beating');
                            void el.heartIcon.offsetWidth;
                            el.heartIcon.classList.add('beating');
                        }
                        lastBeatMs = now;
                    } else if (irPeakArm && ac < 200) {
                        irPeakArm = false;
                    }
                }
            }

            function updateSystemStatus(st) {
                if (st.fs_act !== undefined) {
                    state.measuredFs = st.fs_act;
                    el.valSampleRate.textContent = `${st.fs_act.toFixed(1)} Hz`;
                }
                if (st.dropped !== undefined) {
                    state.droppedCount = st.dropped;
                }
                if (st.overflow !== undefined) {
                    state.overflowCount = st.overflow;
                }
                const totalLoss = state.droppedCount + state.overflowCount;
                el.valLoss.textContent = totalLoss.toLocaleString();
                if (totalLoss > 0) {
                    el.valLoss.classList.add('error-badge');
                } else {
                    el.valLoss.classList.remove('error-badge');
                }
                el.subLossDetails.textContent = `Ring Drops: ${state.droppedCount} | FIFO OVF: ${state.overflowCount}`;

                if (st.clients !== undefined) {
                    el.diagClients.textContent = `${st.clients} client${st.clients === 1 ? '' : 's'}`;
                }
                if (st.free_heap !== undefined) {
                    el.diagHeap.textContent = `${Math.round(st.free_heap / 1024)} KB`;
                }
            }

            // Rendering Engine for Oscilloscopes
            function render() {
                if (!state.paused && state.samples.length > 2) {
                    const count = Math.min(state.samples.length, state.timeWindowSec * CONFIG.nominalFs);
                    const visibleSamples = state.samples.slice(-count);

                    drawWaveform(ctxIR, el.canvasIR, visibleSamples, 'ir', '#00e5ff', el.axisIR_Max, el.axisIR_Mid, el.axisIR_Min, el.subIRMinMax);
                    drawWaveform(ctxRed, el.canvasRed, visibleSamples, 'r', '#ff3366', el.axisRed_Max, el.axisRed_Mid, el.axisRed_Min, el.subRedMinMax);
                }

                // Session Duration Timer
                const elapsedSec = Math.floor((Date.now() - state.startTimeMs) / 1000);
                const hrs = String(Math.floor(elapsedSec / 3600)).padStart(2, '0');
                const mins = String(Math.floor((elapsedSec % 3600) / 60)).padStart(2, '0');
                const secs = String(elapsedSec % 60).padStart(2, '0');
                el.sessionTime.textContent = `${hrs}:${mins}:${secs}`;

                requestAnimationFrame(render);
            }

            function drawWaveform(ctx, canvas, samples, field, strokeColor, elMax, elMid, elMin, elStats) {
                const w = canvas.width;
                const h = canvas.height;
                const dpr = window.devicePixelRatio || 1;

                ctx.clearRect(0, 0, w, h);

                // Draw background grid lines
                ctx.strokeStyle = 'rgba(255, 255, 255, 0.05)';
                ctx.lineWidth = 1 * dpr;
                const gridYSteps = 4;
                for (let i = 1; i < gridYSteps; i++) {
                    const y = (h / gridYSteps) * i;
                    ctx.beginPath();
                    ctx.moveTo(0, y);
                    ctx.lineTo(w, y);
                    ctx.stroke();
                }

                const gridXSteps = state.timeWindowSec;
                for (let i = 1; i < gridXSteps; i++) {
                    const x = (w / gridXSteps) * i;
                    ctx.beginPath();
                    ctx.moveTo(x, 0);
                    ctx.lineTo(x, h);
                    ctx.stroke();
                }

                if (samples.length < 2) return;

                // Extract values (or compute AC-zoomed values)
                let values = new Float64Array(samples.length);
                let min = Infinity;
                let max = -Infinity;

                if (state.visualAcZoom) {
                    // Running DC baseline subtraction for visual AC pulse inspection
                    // Note: This purely affects display; raw data is untouched
                    let sum = 0;
                    for (let i = 0; i < samples.length; i++) {
                        sum += samples[i][field];
                    }
                    const mean = sum / samples.length;
                    for (let i = 0; i < samples.length; i++) {
                        const val = samples[i][field] - mean;
                        values[i] = val;
                        if (val < min) min = val;
                        if (val > max) max = val;
                    }
                } else {
                    for (let i = 0; i < samples.length; i++) {
                        const val = samples[i][field];
                        values[i] = val;
                        if (val < min) min = val;
                        if (val > max) max = val;
                    }
                }

                // Autoscale with margins
                let range = max - min;
                if (range < 1) range = 1;
                const padding = range * 0.1;
                const scaleMin = min - padding;
                const scaleMax = max + padding;
                const scaleRange = scaleMax - scaleMin;

                // Update Y-axis readout labels
                if (state.visualAcZoom) {
                    elMax.textContent = `+${Math.round(max).toLocaleString()}`;
                    elMid.textContent = `0 (AC)`;
                    elMin.textContent = `${Math.round(min).toLocaleString()}`;
                    elStats.textContent = `AC Peak-to-Peak: ${Math.round(max - min).toLocaleString()} counts`;
                } else {
                    elMax.textContent = Math.round(max).toLocaleString();
                    elMid.textContent = Math.round((min + max) / 2).toLocaleString();
                    elMin.textContent = Math.round(min).toLocaleString();
                    elStats.textContent = `Min: ${min.toLocaleString()} | Max: ${max.toLocaleString()} | P-P: ${(max - min).toLocaleString()}`;
                }

                // Render trace
                ctx.beginPath();
                ctx.strokeStyle = strokeColor;
                ctx.lineWidth = 2 * dpr;
                ctx.lineCap = 'round';
                ctx.lineJoin = 'round';

                const xStep = w / (samples.length - 1);
                for (let i = 0; i < samples.length; i++) {
                    const x = i * xStep;
                    const normY = (values[i] - scaleMin) / scaleRange;
                    const y = h - (normY * h);

                    if (i === 0) {
                        ctx.moveTo(x, y);
                    } else {
                        ctx.lineTo(x, y);
                    }
                }
                ctx.stroke();

                // Glow effect under line
                ctx.shadowColor = strokeColor;
                ctx.shadowBlur = 4 * dpr;
                ctx.stroke();
                ctx.shadowBlur = 0; // reset
            }

            // Toolbar Controls & Event Listeners
            const btnAcq = document.getElementById('btnAcqToggle');
            if (btnAcq) {
                btnAcq.addEventListener('click', async () => {
                    try {
                        const res = await fetch('/api/control?action=toggle');
                        const data = await res.json();
                        if (data.acquiring) {
                            btnAcq.textContent = 'Acquisition: Active';
                            btnAcq.className = 'primary';
                        } else {
                            btnAcq.textContent = 'Acquisition: Stopped';
                            btnAcq.className = 'danger';
                        }
                    } catch (e) {
                        console.error('Failed to toggle sensor acquisition:', e);
                    }
                });
            }

            el.selectTimeWindow.addEventListener('change', (e) => {
                state.timeWindowSec = parseInt(e.target.value, 10);
            });

            el.btnPauseRender.addEventListener('click', () => {
                state.paused = !state.paused;
                el.btnPauseRender.textContent = state.paused ? 'Resume Scope' : 'Pause Scope';
                el.btnPauseRender.classList.toggle('active', state.paused);
            });

            el.btnClearScope.addEventListener('click', () => {
                state.samples = [];
                resizeCanvases();
            });

            // Hero Live Monitor Start/Pause Control
            el.btnMainLiveMonitor.addEventListener('click', () => {
                state.paused = !state.paused;
                if (state.paused) {
                    el.btnMainLiveMonitor.className = 'btn-live-monitor paused';
                    el.btnMainLiveMonitorText.textContent = 'Live Monitor: PAUSED (Click to Start)';
                    el.btnPauseRender.textContent = 'Resume Scope';
                    el.btnPauseRender.classList.add('active');
                } else {
                    el.btnMainLiveMonitor.className = 'btn-live-monitor';
                    el.btnMainLiveMonitorText.textContent = 'Live Monitor: ACTIVE';
                    el.btnPauseRender.textContent = 'Pause Scope';
                    el.btnPauseRender.classList.remove('active');
                }
            });

            // Quick Record & Quick Export synchronizers
            el.btnQuickRecord.addEventListener('click', () => {
                el.btnToggleRecord.click();
                if (state.recording) {
                    el.btnQuickRecord.textContent = '⏹ Stop Recording';
                    el.btnQuickRecord.className = 'btn danger';
                    el.btnQuickExport.disabled = true;
                } else {
                    el.btnQuickRecord.textContent = '⏺ Start Recording';
                    el.btnQuickRecord.className = 'btn primary';
                    el.btnQuickExport.disabled = (state.recordedData.length === 0);
                }
            });

            el.btnQuickExport.addEventListener('click', () => {
                el.btnExportCSV.click();
            });

            el.btnVisualFilter.addEventListener('click', () => {
                state.visualAcZoom = !state.visualAcZoom;
                if (state.visualAcZoom) {
                    el.btnVisualFilter.textContent = 'Visual: AC Pulsatile Zoom';
                    el.btnVisualFilter.classList.add('active');
                    el.canvasIRInfo.textContent = 'AC Dynamic Component (Mean Removed for Visual Inspection)';
                    el.canvasRedInfo.textContent = 'AC Dynamic Component (Mean Removed for Visual Inspection)';
                } else {
                    el.btnVisualFilter.textContent = 'Visual: Raw Counts';
                    el.btnVisualFilter.classList.remove('active');
                    el.canvasIRInfo.textContent = 'Raw Counts (0 - 262143)';
                    el.canvasRedInfo.textContent = 'Raw Counts (0 - 262143)';
                }
            });

            // Browser Recording & CSV Export
            el.btnToggleRecord.addEventListener('click', () => {
                if (!state.recording) {
                    // Start Recording
                    state.recording = true;
                    state.recordedData = [];
                    state.recordStartTime = Date.now();
                    el.btnToggleRecord.textContent = 'Stop Recording';
                    el.btnToggleRecord.className = 'danger';
                    el.btnExportCSV.disabled = true;
                    el.recordingBanner.innerHTML = `<span style="color: #ef4444; font-weight: bold;">&#x25CF; RECORDING IN PROGRESS:</span> Logging raw optical stream at 100 Hz...`;
                } else {
                    // Stop Recording
                    state.recording = false;
                    el.btnToggleRecord.textContent = 'Start Browser Recording';
                    el.btnToggleRecord.className = 'primary';
                    el.btnExportCSV.disabled = (state.recordedData.length === 0);
                    const durSec = ((Date.now() - state.recordStartTime) / 1000).toFixed(1);
                    el.recordingBanner.innerHTML = `<strong>Recording Complete:</strong> Captured ${state.recordedData.length.toLocaleString()} samples over ${durSec} seconds. Click 'Export CSV' to save.`;
                }
            });

            el.btnExportCSV.addEventListener('click', () => {
                if (state.recordedData.length === 0) return;

                const sessionId = 'rec_' + new Date().toISOString().replace(/[:.]/g, '-');
                let csv = `# MAX30102 Raw Optical PPG Telemetry\n`;
                csv += `# Session ID: ${sessionId}\n`;
                csv += `# Host: ${window.location.host || '192.168.4.1'}\n`;
                csv += `# Start Time UTC: ${new Date(state.recordStartTime).toISOString()}\n`;
                csv += `# Nominal Sample Rate: ${CONFIG.nominalFs} Hz\n`;
                csv += `# ADC Resolution: 18-bit (411 us pulse width)\n`;
                csv += `# LED Currents: Red = 0x1F (~6.4mA), IR = 0x1F (~6.4mA)\n`;
                csv += `session_id,seq,timestamp_us,raw_red,raw_ir\n`;

                for (let i = 0; i < state.recordedData.length; i++) {
                    const s = state.recordedData[i];
                    csv += `${sessionId},${s.seq},${s.t},${s.r},${s.ir}\n`;
                }

                const blob = new Blob([csv], { type: 'text/csv;charset=utf-8;' });
                const url = URL.createObjectURL(blob);
                const a = document.createElement('a');
                a.href = url;
                a.download = `ppg_${sessionId}.csv`;
                document.body.appendChild(a);
                a.click();
                document.body.removeChild(a);
                URL.revokeObjectURL(url);
            });

            // Start initialization
            connectWebSocket();
            requestAnimationFrame(render);
        })();
    </script>
</body>
</html>
)rawliteral";
