#pragma once

#include <Arduino.h>
#include "ppg_types.h"
#include "config.h"

/**
 * @brief Thread-safe Single-Producer Single-Consumer (SPSC) Ring Buffer
 * optimized for cross-core FreeRTOS operations on ESP32-S3.
 * 
 * Producer: Core 1 (ppg_acquisition_task)
 * Consumer: Core 0 (network_task / web_server)
 */
template <size_t Capacity>
class PPGRingBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2 for fast masking");

public:
    PPGRingBuffer() : _head(0), _tail(0), _dropped_count(0) {
        portMUX_INITIALIZE(&_mutex);
    }

    /**
     * @brief Push a sample into the ring buffer.
     * Called exclusively by Core 1 acquisition task.
     * @param sample The raw PPG sample to enqueue.
     * @return true if enqueued successfully; false if buffer was full (sample dropped).
     */
    bool push(const PPGSample& sample) {
        portENTER_CRITICAL(&_mutex);
        size_t next_head = (_head + 1) & (Capacity - 1);
        if (next_head == _tail) {
            // Buffer is full: cannot enqueue without overwriting unread data
            _dropped_count++;
            portEXIT_CRITICAL(&_mutex);
            return false;
        }
        _buffer[_head] = sample;
        _head = next_head;
        portEXIT_CRITICAL(&_mutex);
        return true;
    }

    /**
     * @brief Pop a sample from the ring buffer.
     * Called exclusively by Core 0 network/web task.
     * @param sample Output reference to receive the sample.
     * @return true if a sample was retrieved; false if buffer is empty.
     */
    bool pop(PPGSample& sample) {
        portENTER_CRITICAL(&_mutex);
        if (_head == _tail) {
            portEXIT_CRITICAL(&_mutex);
            return false; // Empty
        }
        sample = _buffer[_tail];
        _tail = (_tail + 1) & (Capacity - 1);
        portEXIT_CRITICAL(&_mutex);
        return true;
    }

    /**
     * @brief Pop multiple samples into an array in a single critical section.
     * @param dest Destination array.
     * @param max_count Maximum number of samples to pop.
     * @return Number of samples actually retrieved.
     */
    size_t popBatch(PPGSample* dest, size_t max_count) {
        if (!dest || max_count == 0) return 0;

        portENTER_CRITICAL(&_mutex);
        size_t count = 0;
        while (_head != _tail && count < max_count) {
            dest[count++] = _buffer[_tail];
            _tail = (_tail + 1) & (Capacity - 1);
        }
        portEXIT_CRITICAL(&_mutex);
        return count;
    }

    /**
     * @brief Peek at the most recent sample pushed without removing it.
     */
    bool peekLatest(PPGSample& sample) {
        portENTER_CRITICAL(&_mutex);
        if (_head == _tail) {
            portEXIT_CRITICAL(&_mutex);
            return false;
        }
        size_t latest_idx = (_head - 1 + Capacity) & (Capacity - 1);
        sample = _buffer[latest_idx];
        portEXIT_CRITICAL(&_mutex);
        return true;
    }

    /**
     * @brief Current number of unread samples waiting in the buffer.
     */
    size_t available() const {
        portENTER_CRITICAL(const_cast<portMUX_TYPE*>(&_mutex));
        size_t avail = (_head - _tail + Capacity) & (Capacity - 1);
        portEXIT_CRITICAL(const_cast<portMUX_TYPE*>(&_mutex));
        return avail;
    }

    /**
     * @brief Total number of samples dropped due to buffer overflow.
     */
    uint32_t getDroppedCount() const {
        return _dropped_count;
    }

    /**
     * @brief Clear the buffer and reset pointers.
     */
    void clear() {
        portENTER_CRITICAL(&_mutex);
        _head = 0;
        _tail = 0;
        portEXIT_CRITICAL(&_mutex);
    }

private:
    PPGSample _buffer[Capacity];
    volatile size_t _head;
    volatile size_t _tail;
    volatile uint32_t _dropped_count;
    mutable portMUX_TYPE _mutex = portMUX_INITIALIZER_UNLOCKED;
};
