#pragma once

#include <Arduino.h>

/**
 * @brief Represents a single synchronous optical photoplethysmogram sample.
 * Preserves the un-normalized, un-smoothed 18-bit ADC optical counts.
 */
struct PPGSample {
    uint32_t seq;           ///< Monotonically increasing sequence number (0 to 2^32-1)
    uint64_t timestamp_us;  ///< High-precision microsecond hardware timestamp (esp_timer_get_time)
    uint32_t raw_red;       ///< Raw 18-bit optical count from Red channel (660 nm)
    uint32_t raw_ir;        ///< Raw 18-bit optical count from Infrared channel (880 nm)
};

/**
 * @brief Optical sensor hardware and register configuration record.
 */
struct SensorConfig {
    uint16_t sample_rate_hz;      ///< Configured nominal sample rate (e.g. 100 Hz)
    uint16_t sample_period_us;    ///< Nominal inter-sample interval in microseconds (10,000 us)
    uint16_t pulse_width_us;      ///< LED pulse width (411 us for 18-bit resolution)
    uint8_t  adc_resolution_bits;  ///< Effective ADC resolution (18 bits)
    uint16_t adc_range_na;        ///< ADC full scale range (4096 nA)
    uint8_t  sample_average;      ///< FIFO sample averaging (1 = raw, no averaging)
    uint8_t  led_mode;            ///< Optical mode (2 = Red + IR)
    uint8_t  led_current_red;     ///< Red LED DAC amplitude register value
    uint8_t  led_current_ir;      ///< IR LED DAC amplitude register value
};

/**
 * @brief Live system diagnostic telemetry and performance status.
 */
struct PPGSystemStatus {
    bool     sensor_ok;            ///< True if MAX30102 ACKed I2C transactions
    uint8_t  part_id;              ///< Sensor Part ID (expected 0x15 for MAX30102)
    uint8_t  revision_id;          ///< Silicon revision ID
    uint32_t total_samples;        ///< Cumulative samples acquired since boot
    uint32_t dropped_samples;      ///< Samples lost due to ring buffer backpressure
    uint32_t fifo_overflow_count;  ///< Hardware FIFO overflow events reported by MAX30102
    float    nominal_rate_hz;      ///< Target nominal sampling rate (100.0 Hz)
    float    measured_rate_hz;     ///< Empirically measured rolling sampling frequency
    uint32_t uptime_seconds;       ///< ESP32-S3 uptime in seconds
    uint32_t connected_clients;    ///< Number of active WebSocket clients
    size_t   free_heap;            ///< Free heap memory in bytes
    uint32_t last_raw_red;         ///< Most recent Red optical count
    uint32_t last_raw_ir;          ///< Most recent IR optical count
};
