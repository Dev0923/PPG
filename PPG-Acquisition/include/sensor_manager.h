#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <MAX30105.h>
#include "config.h"
#include "ppg_types.h"
#include "ring_buffer.h"

/**
 * @brief Manages the MAX30102 optical sensor initialization,
 * I2C communication, FIFO reading, precise per-sample timestamping,
 * and overflow detection.
 */
class SensorManager {
public:
    SensorManager(PPGRingBuffer<RING_BUFFER_CAPACITY>& ringBuffer);

    /**
     * @brief Initialize I2C bus and the MAX30102 sensor.
     * Performs bus scan, part ID verification, and register setup.
     * @return true if MAX30102 detected (Part ID 0x15) and initialized; false otherwise.
     */
    bool begin();

    /**
     * @brief Polling routine called continuously by Core 1 acquisition task.
     * Drains the MAX30102 hardware FIFO, assigns monotonic sequence numbers
     * and accurate microsecond timestamps, and pushes to the ring buffer.
     * @return Number of new samples acquired during this poll.
     */
    size_t pollFIFO();

    /**
     * @brief Get current system status and diagnostic metrics.
     * Thread-safe snapshot.
     */
    PPGSystemStatus getStatus() const;

    /**
     * @brief Get active sensor configuration parameters.
     */
    SensorConfig getConfig() const;

    /**
     * @brief Check whether sensor is healthy and responding.
     */
    /**
     * @brief Check whether sensor is healthy and responding.
     */
    bool isHealthy() const { return _sensor_ok; }

    /**
     * @brief Start or stop optical sensor acquisition.
     */
    void setAcquiring(bool enable);
    bool isAcquiring() const;

    /**
     * @brief Reset sequence counter, statistics, and ring buffer.
     */
    void resetSession();

private:
    PPGRingBuffer<RING_BUFFER_CAPACITY>& _ringBuffer;
    MAX30105 _sensor;

    bool     _sensor_ok;
    volatile bool _acquiring;
    uint8_t  _part_id;
    uint8_t  _revision_id;
    uint32_t _seq_counter;
    uint32_t _total_samples;
    uint32_t _fifo_overflow_count;
    uint32_t _last_raw_red;
    uint32_t _last_raw_ir;

    // Timing and sample-rate estimation variables
    int64_t  _session_start_us;
    int64_t  _last_sample_timestamp_us;
    int64_t  _rate_calc_window_start_us;
    uint32_t _rate_calc_sample_count;
    float    _measured_rate_hz;

    // Mutex for protecting status read across cores
    mutable portMUX_TYPE _status_mutex = portMUX_INITIALIZER_UNLOCKED;

    /**
     * @brief Scan I2C bus for address 0x57.
     * @return true if device responded with ACK.
     */
    bool scanI2CAddress(uint8_t address);

    /**
     * @brief Query MAX30102 hardware overflow counter register.
     */
    void checkHardwareOverflow();
};
