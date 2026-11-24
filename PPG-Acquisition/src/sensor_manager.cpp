#include "sensor_manager.h"
#include <esp_timer.h>

SensorManager::SensorManager(PPGRingBuffer<RING_BUFFER_CAPACITY>& ringBuffer)
    : _ringBuffer(ringBuffer),
      _sensor_ok(false),
      _acquiring(true),
      _part_id(0),
      _revision_id(0),
      _seq_counter(0),
      _total_samples(0),
      _fifo_overflow_count(0),
      _last_raw_red(0),
      _last_raw_ir(0),
      _session_start_us(0),
      _last_sample_timestamp_us(0),
      _rate_calc_window_start_us(0),
      _rate_calc_sample_count(0),
      _measured_rate_hz(0.0f)
{
}

bool SensorManager::begin() {
    Serial.println("\n[SENSOR] Initializing I2C bus for MAX30102...");
    Serial.printf("[SENSOR] Configured pins: SDA = GPIO %d, SCL = GPIO %d, Bus Clock = %d Hz\n",
                  I2C_SDA_PIN, I2C_SCL_PIN, I2C_FREQ_HZ);

    // Initialize I2C with ESP32-S3 pins and 400kHz clock
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, I2C_FREQ_HZ);
    Wire.setTimeOut(I2C_TIMEOUT_MS);

    // Scan for sensor address 0x57
    const uint8_t MAX30102_ADDR = 0x57;
    if (!scanI2CAddress(MAX30102_ADDR)) {
        Serial.println("[SENSOR][ERROR] Device at I2C 0x57 did not acknowledge!");
        Serial.println("[SENSOR][HARDWARE CHECK]");
        Serial.println("  1. Verify SDA (GPIO 8) and SCL (GPIO 9) wiring.");
        Serial.println("  2. Verify sensor power: For black breakout boards, supply 5V to VIN if dual LDOs exist,");
        Serial.println("     or 3.3V if only 1.8V regulator is present.");
        Serial.println("  3. Check I2C pull-ups: Many black breakout boards tie 4.7k pullups to 1.8V.");
        Serial.println("     If the bus voltage is 1.8V, the ESP32-S3 cannot read logic HIGH (VIH >= 2.48V).");
        Serial.println("     Add external 2.2k-4.7k pull-ups between SDA/SCL and 3.3V.");
        _sensor_ok = false;
        return false;
    }

    Serial.println("[SENSOR] I2C 0x57 ACK received. Initializing MAX3010x library driver...");

    // Initialize library communication
    if (!_sensor.begin(Wire, I2C_FREQ_HZ, MAX30102_ADDR)) {
        Serial.println("[SENSOR][ERROR] MAX3010x driver begin() failed.");
        _sensor_ok = false;
        return false;
    }

    // Read Part ID and Revision
    _part_id = _sensor.readPartID();
    _revision_id = _sensor.getRevisionID();
    Serial.printf("[SENSOR] Device Part ID: 0x%02X (Expected: 0x15 for MAX30102), Rev: 0x%02X\n",
                  _part_id, _revision_id);

    // Apply optical configuration
    Serial.println("[SENSOR] Configuring acquisition registers:");
    Serial.printf("  - Sample Rate: %d Hz (Inter-sample interval: %d us)\n", PPG_SAMPLE_RATE_HZ, PPG_NOMINAL_PERIOD_US);
    Serial.printf("  - ADC Resolution: %d-bit (Pulse Width: %d us)\n", MAX30102_ADC_BITS, MAX30102_PULSE_WIDTH);
    Serial.printf("  - ADC Range: %d nA\n", MAX30102_ADC_RANGE);
    Serial.printf("  - Averaging: %d (Raw, no averaging)\n", MAX30102_SAMPLE_AVG);
    Serial.printf("  - LED Currents: Red = 0x%02X (~6.4mA), IR = 0x%02X (~6.4mA)\n",
                  MAX30102_LED_CURRENT_RED, MAX30102_LED_CURRENT_IR);

    // Setup sensor registers via library helper:
    // powerLevel, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange
    _sensor.setup(MAX30102_LED_CURRENT_RED,
                  MAX30102_SAMPLE_AVG,
                  MAX30102_LED_MODE,
                  PPG_SAMPLE_RATE_HZ,
                  MAX30102_PULSE_WIDTH,
                  MAX30102_ADC_RANGE);

    // Disable FIFO rollover so the hardware overflow counter increments on lost samples
    _sensor.disableFIFORollover();

    // Clear stale FIFO data
    _sensor.clearFIFO();

    // Initialize session timing
    int64_t now_us = esp_timer_get_time();
    _session_start_us = now_us;
    _last_sample_timestamp_us = now_us;
    _rate_calc_window_start_us = now_us;
    _rate_calc_sample_count = 0;
    _measured_rate_hz = (float)PPG_SAMPLE_RATE_HZ;

    _sensor_ok = true;
    Serial.println("[SENSOR] MAX30102 configured and acquiring raw optical signals.");
    return true;
}

bool SensorManager::scanI2CAddress(uint8_t address) {
    Wire.beginTransmission(address);
    byte error = Wire.endTransmission();
    return (error == 0);
}

size_t SensorManager::pollFIFO() {
    if (!_sensor_ok || !_acquiring) {
        return 0;
    }

    // Query sensor FIFO. check() fetches unread bytes from MAX30102 FIFO
    uint16_t available_bytes = _sensor.check();
    if (available_bytes == 0 && !_sensor.available()) {
        return 0;
    }

    // Temporary storage for samples in this batch (MAX30102 FIFO holds up to 32 samples)
    const size_t MAX_BATCH = 32;
    uint32_t reds[MAX_BATCH];
    uint32_t irs[MAX_BATCH];
    size_t count = 0;

    while (_sensor.available() && count < MAX_BATCH) {
        reds[count] = _sensor.getFIFORed();
        irs[count]  = _sensor.getFIFOIR();
        _sensor.nextSample();
        count++;
    }

    if (count == 0) {
        return 0;
    }

    // Capture batch drain time in microseconds
    int64_t now_us = esp_timer_get_time();

    // Check hardware FIFO overflow register
    checkHardwareOverflow();

    // Assign sequence numbers and backward-extrapolated microsecond timestamps.
    // For a batch of N samples, sample index k arrived at:
    // T_k = T_now - (N - 1 - k) * T_nominal_period
    // This distributes timestamps according to sample arrival interval without burst-stacking.
    for (size_t i = 0; i < count; i++) {
        int64_t sample_offset_us = (int64_t)(count - 1 - i) * (int64_t)PPG_NOMINAL_PERIOD_US;
        int64_t sample_t_us = now_us - sample_offset_us;

        // Ensure strictly monotonic timestamps
        if (sample_t_us <= _last_sample_timestamp_us) {
            sample_t_us = _last_sample_timestamp_us + 1;
        }
        _last_sample_timestamp_us = sample_t_us;

        PPGSample sample;
        sample.seq = _seq_counter++;
        sample.timestamp_us = (uint64_t)sample_t_us;
        sample.raw_red = reds[i];
        sample.raw_ir = irs[i];

        _last_raw_red = sample.raw_red;
        _last_raw_ir  = sample.raw_ir;
        _total_samples++;

        // Push to ring buffer (thread-safe for Core 0 consumption)
        if (!_ringBuffer.push(sample)) {
            // Buffer was full; ring buffer tracks dropped count internally
        }

        // Sample rate estimation tracking
        _rate_calc_sample_count++;
    }

    // Calculate rolling sampling rate every 100 samples (~1 second at 100Hz)
    if (_rate_calc_sample_count >= 100) {
        int64_t elapsed_us = now_us - _rate_calc_window_start_us;
        if (elapsed_us > 0) {
            _measured_rate_hz = ((float)_rate_calc_sample_count * 1000000.0f) / (float)elapsed_us;
        }
        _rate_calc_window_start_us = now_us;
        _rate_calc_sample_count = 0;
    }

    return count;
}

void SensorManager::checkHardwareOverflow() {
    // MAX30102 Register 0x05 is OVF_COUNTER.
    // It increments whenever a new sample is generated while FIFO is full (rollover disabled).
    uint8_t ovf = _sensor.readRegister8(MAX30105_ADDRESS, 0x05);
    if (ovf > 0) {
        _fifo_overflow_count += ovf;
        // Writing to OVF_COUNTER or clearFIFO clears the register
        _sensor.writeRegister8(MAX30105_ADDRESS, 0x05, 0x00);
        Serial.printf("[SENSOR][WARNING] Hardware FIFO Overflow! +%d samples lost in sensor FIFO (Total: %u)\n",
                      ovf, _fifo_overflow_count);
    }
}

PPGSystemStatus SensorManager::getStatus() const {
    PPGSystemStatus st;
    portENTER_CRITICAL(const_cast<portMUX_TYPE*>(&_status_mutex));
    st.sensor_ok = _sensor_ok;
    st.part_id = _part_id;
    st.revision_id = _revision_id;
    st.total_samples = _total_samples;
    st.dropped_samples = _ringBuffer.getDroppedCount();
    st.fifo_overflow_count = _fifo_overflow_count;
    st.nominal_rate_hz = (float)PPG_SAMPLE_RATE_HZ;
    st.measured_rate_hz = _measured_rate_hz;
    st.uptime_seconds = (uint32_t)((esp_timer_get_time() - _session_start_us) / 1000000ULL);
    st.connected_clients = 0; // populated by WebServerManager
    st.free_heap = esp_get_free_heap_size();
    st.last_raw_red = _last_raw_red;
    st.last_raw_ir = _last_raw_ir;
    portEXIT_CRITICAL(const_cast<portMUX_TYPE*>(&_status_mutex));
    return st;
}

SensorConfig SensorManager::getConfig() const {
    SensorConfig cfg;
    cfg.sample_rate_hz = PPG_SAMPLE_RATE_HZ;
    cfg.sample_period_us = PPG_NOMINAL_PERIOD_US;
    cfg.pulse_width_us = MAX30102_PULSE_WIDTH;
    cfg.adc_resolution_bits = MAX30102_ADC_BITS;
    cfg.adc_range_na = MAX30102_ADC_RANGE;
    cfg.sample_average = MAX30102_SAMPLE_AVG;
    cfg.led_mode = MAX30102_LED_MODE;
    cfg.led_current_red = MAX30102_LED_CURRENT_RED;
    cfg.led_current_ir = MAX30102_LED_CURRENT_IR;
    return cfg;
}

void SensorManager::resetSession() {
    portENTER_CRITICAL(&_status_mutex);
    _seq_counter = 0;
    _total_samples = 0;
    _fifo_overflow_count = 0;
    _ringBuffer.clear();
    int64_t now_us = esp_timer_get_time();
    _session_start_us = now_us;
    _last_sample_timestamp_us = now_us;
    _rate_calc_window_start_us = now_us;
    _rate_calc_sample_count = 0;
    _sensor.clearFIFO();
    portEXIT_CRITICAL(&_status_mutex);
    Serial.println("[SENSOR] Session sequence counter and ring buffer reset.");
}

void SensorManager::setAcquiring(bool enable) {
    portENTER_CRITICAL(&_status_mutex);
    _acquiring = enable;
    portEXIT_CRITICAL(&_status_mutex);
    if (enable) {
        _sensor.wakeUp();
        _sensor.clearFIFO();
        Serial.println("[SENSOR] Optical acquisition resumed (sensor wakeUp).");
    } else {
        _sensor.shutDown();
        Serial.println("[SENSOR] Optical acquisition paused (sensor shutDown).");
    }
}

bool SensorManager::isAcquiring() const {
    return _acquiring;
}

