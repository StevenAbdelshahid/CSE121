// Lab 6.1 - Ultrasonic Range Finder with Temperature Compensation
// ESP32-C3 with RCWL-1601 (SR04-compatible) ultrasonic sensor

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "hal/cpu_hal.h"

static const char* TAG = "LAB6_1";

// ===== Ultrasonic Sensor Pins =====
#define TRIG_PIN  GPIO_NUM_2
#define ECHO_PIN  GPIO_NUM_3

// ===== I2C Configuration (for temperature sensor) =====
#define I2C_PORT      I2C_NUM_0
#define SDA_PIN       10
#define SCL_PIN       8

// ===== Temperature Sensor Addresses =====
#define ADDR_SHTC3    0x70
#define ADDR_SHT3X_1  0x44
#define ADDR_SHT3X_2  0x45
#define ADDR_AHT20    0x38
#define ADDR_SI7021   0x40

// ===== Sensor Detection =====
typedef enum {
    SENSOR_NONE,
    SENSOR_SHTC3,
    SENSOR_SHT3X,
    SENSOR_AHT20,
    SENSOR_SI7021
} sensor_type_t;

static sensor_type_t g_sensor_type = SENSOR_NONE;
static uint8_t g_sensor_addr = 0;

// ---------- I2C Helper Functions ----------
static esp_err_t i2c_init(void) {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = SDA_PIN,
        .scl_io_num = SCL_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,
    };
    esp_err_t err = i2c_param_config(I2C_PORT, &conf);
    if (err != ESP_OK) return err;
    return i2c_driver_install(I2C_PORT, conf.mode, 0, 0, 0);
}

static esp_err_t i2c_probe(uint8_t addr) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_stop(cmd);
    esp_err_t err = i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(50));
    i2c_cmd_link_delete(cmd);
    return err;
}

static esp_err_t i2c_write_bytes(uint8_t addr, const uint8_t* data, size_t len) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    if (len > 0) i2c_master_write(cmd, (uint8_t*)data, len, true);
    i2c_master_stop(cmd);
    esp_err_t err = i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return err;
}

static esp_err_t i2c_read_bytes(uint8_t addr, uint8_t* data, size_t len) {
    if (len == 0) return ESP_OK;
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_READ, true);
    if (len > 1) i2c_master_read(cmd, data, len - 1, I2C_MASTER_ACK);
    i2c_master_read_byte(cmd, &data[len - 1], I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    esp_err_t err = i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return err;
}

// ---------- Sensirion CRC Check ----------
static bool crc_sensirion_2b(const uint8_t *data, uint8_t crc8) {
    uint8_t crc = 0xFF;
    for (int i = 0; i < 2; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
        }
    }
    return (crc == crc8);
}

// ---------- Temperature Sensor Drivers ----------
static bool shtc3_measure(float* tC, float* RH) {
    uint8_t wake[2] = {0x35, 0x17};
    if (i2c_write_bytes(ADDR_SHTC3, wake, 2) != ESP_OK) return false;
    vTaskDelay(pdMS_TO_TICKS(2));

    uint8_t meas[2] = {0x78, 0x66}; // T-first, no stretch
    if (i2c_write_bytes(ADDR_SHTC3, meas, 2) != ESP_OK) return false;
    vTaskDelay(pdMS_TO_TICKS(25));

    uint8_t rx[6];
    if (i2c_read_bytes(ADDR_SHTC3, rx, 6) != ESP_OK) return false;
    if (!crc_sensirion_2b(&rx[0], rx[2]) || !crc_sensirion_2b(&rx[3], rx[5])) return false;

    uint16_t rT = ((uint16_t)rx[0] << 8) | rx[1];
    uint16_t rH = ((uint16_t)rx[3] << 8) | rx[4];
    *tC = -45.0f + 175.0f * (rT / 65535.0f);
    *RH = 100.0f * (rH / 65535.0f);

    uint8_t sleep[2] = {0xB0, 0x98};
    i2c_write_bytes(ADDR_SHTC3, sleep, 2);
    return true;
}

static bool sht3x_measure(uint8_t addr, float* tC, float* RH) {
    uint8_t cmd[2] = {0x24, 0x00}; // single-shot, high rep
    if (i2c_write_bytes(addr, cmd, 2) != ESP_OK) return false;
    vTaskDelay(pdMS_TO_TICKS(15));

    uint8_t rx[6];
    if (i2c_read_bytes(addr, rx, 6) != ESP_OK) return false;
    if (!crc_sensirion_2b(&rx[0], rx[2]) || !crc_sensirion_2b(&rx[3], rx[5])) return false;

    uint16_t rT = ((uint16_t)rx[0] << 8) | rx[1];
    uint16_t rH = ((uint16_t)rx[3] << 8) | rx[4];
    *tC = -45.0f + 175.0f * (rT / 65535.0f);
    *RH = 100.0f * (rH / 65535.0f);
    return true;
}

static bool aht20_init(void) {
    uint8_t rst = 0xBA;
    i2c_write_bytes(ADDR_AHT20, &rst, 1);
    vTaskDelay(pdMS_TO_TICKS(20));
    uint8_t init_cmd[3] = {0xBE, 0x08, 0x00};
    return (i2c_write_bytes(ADDR_AHT20, init_cmd, 3) == ESP_OK);
}

static bool aht20_measure(float* tC, float* RH) {
    uint8_t trig[3] = {0xAC, 0x33, 0x00};
    if (i2c_write_bytes(ADDR_AHT20, trig, 3) != ESP_OK) return false;
    vTaskDelay(pdMS_TO_TICKS(80));

    uint8_t rx[6];
    if (i2c_read_bytes(ADDR_AHT20, rx, 6) != ESP_OK) return false;

    uint32_t rH = ((uint32_t)rx[1] << 12) | ((uint32_t)rx[2] << 4) | ((rx[3] >> 4) & 0x0F);
    uint32_t rT = ((uint32_t)(rx[3] & 0x0F) << 16) | ((uint32_t)rx[4] << 8) | rx[5];
    *RH = (rH * 100.0f) / 1048576.0f;
    *tC = (rT * 200.0f) / 1048576.0f - 50.0f;
    return true;
}

static bool si7021_measure(float* tC, float* RH) {
    uint8_t cmd_rh = 0xE5; // Measure RH (hold)
    if (i2c_write_bytes(ADDR_SI7021, &cmd_rh, 1) != ESP_OK) return false;

    uint8_t rh2[2];
    if (i2c_read_bytes(ADDR_SI7021, rh2, 2) != ESP_OK) return false;
    uint16_t rawRH = ((uint16_t)rh2[0] << 8) | rh2[1];
    *RH = ((125.0f * rawRH) / 65536.0f) - 6.0f;

    uint8_t cmd_t = 0xE3; // Measure T (hold)
    if (i2c_write_bytes(ADDR_SI7021, &cmd_t, 1) != ESP_OK) return false;

    uint8_t t2[2];
    if (i2c_read_bytes(ADDR_SI7021, t2, 2) != ESP_OK) return false;
    uint16_t rawT = ((uint16_t)t2[0] << 8) | t2[1];
    *tC = ((175.72f * rawT) / 65536.0f) - 46.85f;
    return true;
}

// ---------- Sensor Detection ----------
static bool detect_temperature_sensor(void) {
    // Try SHTC3
    if (i2c_probe(ADDR_SHTC3) == ESP_OK) {
        float t, h;
        if (shtc3_measure(&t, &h)) {
            g_sensor_type = SENSOR_SHTC3;
            g_sensor_addr = ADDR_SHTC3;
            ESP_LOGI(TAG, "Detected SHTC3 temperature sensor @0x%02X", ADDR_SHTC3);
            return true;
        }
    }

    // Try SHT3x
    if (i2c_probe(ADDR_SHT3X_1) == ESP_OK) {
        float t, h;
        if (sht3x_measure(ADDR_SHT3X_1, &t, &h)) {
            g_sensor_type = SENSOR_SHT3X;
            g_sensor_addr = ADDR_SHT3X_1;
            ESP_LOGI(TAG, "Detected SHT3x temperature sensor @0x%02X", ADDR_SHT3X_1);
            return true;
        }
    }

    if (i2c_probe(ADDR_SHT3X_2) == ESP_OK) {
        float t, h;
        if (sht3x_measure(ADDR_SHT3X_2, &t, &h)) {
            g_sensor_type = SENSOR_SHT3X;
            g_sensor_addr = ADDR_SHT3X_2;
            ESP_LOGI(TAG, "Detected SHT3x temperature sensor @0x%02X", ADDR_SHT3X_2);
            return true;
        }
    }

    // Try AHT20
    if (i2c_probe(ADDR_AHT20) == ESP_OK) {
        if (aht20_init()) {
            float t, h;
            if (aht20_measure(&t, &h)) {
                g_sensor_type = SENSOR_AHT20;
                g_sensor_addr = ADDR_AHT20;
                ESP_LOGI(TAG, "Detected AHT20 temperature sensor @0x%02X", ADDR_AHT20);
                return true;
            }
        }
    }

    // Try SI7021
    if (i2c_probe(ADDR_SI7021) == ESP_OK) {
        float t, h;
        if (si7021_measure(&t, &h)) {
            g_sensor_type = SENSOR_SI7021;
            g_sensor_addr = ADDR_SI7021;
            ESP_LOGI(TAG, "Detected SI7021 temperature sensor @0x%02X", ADDR_SI7021);
            return true;
        }
    }

    ESP_LOGW(TAG, "No temperature sensor detected, using default 20C");
    return false;
}

static bool read_temperature(float* tC) {
    float RH = 0; // dummy variable

    switch (g_sensor_type) {
        case SENSOR_SHTC3:
            return shtc3_measure(tC, &RH);
        case SENSOR_SHT3X:
            return sht3x_measure(g_sensor_addr, tC, &RH);
        case SENSOR_AHT20:
            return aht20_measure(tC, &RH);
        case SENSOR_SI7021:
            return si7021_measure(tC, &RH);
        default:
            *tC = 20.0f; // default temperature
            return false;
    }
}

// ---------- Ultrasonic Sensor Functions ----------
static void ultrasonic_init(void) {
    // Configure trigger pin as output
    gpio_config_t trig_conf = {
        .pin_bit_mask = (1ULL << TRIG_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&trig_conf);
    gpio_set_level(TRIG_PIN, 0);

    // Configure echo pin as input
    gpio_config_t echo_conf = {
        .pin_bit_mask = (1ULL << ECHO_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&echo_conf);
}

// Measure echo pulse duration in microseconds using CPU cycle counter
static int64_t measure_echo_pulse(void) {
    // Send 10us trigger pulse
    gpio_set_level(TRIG_PIN, 0);
    esp_rom_delay_us(2);
    gpio_set_level(TRIG_PIN, 1);
    esp_rom_delay_us(10);
    gpio_set_level(TRIG_PIN, 0);

    // Wait for echo to go high (with timeout)
    int timeout = 0;
    while (gpio_get_level(ECHO_PIN) == 0) {
        if (++timeout > 10000) return -1; // timeout
        esp_rom_delay_us(1);
    }

    // Measure high pulse duration using esp_timer
    int64_t start = esp_timer_get_time();

    timeout = 0;
    while (gpio_get_level(ECHO_PIN) == 1) {
        if (++timeout > 30000) return -1; // timeout (~30ms max)
        esp_rom_delay_us(1);
    }

    int64_t end = esp_timer_get_time();

    return (end - start); // duration in microseconds
}

// Calculate distance in cm with temperature compensation
static float calculate_distance(int64_t echo_time_us, float temp_c) {
    if (echo_time_us <= 0) return -1.0f;

    // Speed of sound in m/s: v = 331.4 + 0.606 * T (where T is in Celsius)
    float speed_of_sound_m_s = 331.4f + 0.606f * temp_c;

    // Convert to cm/us: (m/s) * (100 cm/m) * (1 s / 1000000 us) = cm/us
    float speed_of_sound_cm_us = speed_of_sound_m_s * 100.0f / 1000000.0f;

    // Distance = (time * speed) / 2 (divide by 2 for round trip)
    float distance_cm = (echo_time_us * speed_of_sound_cm_us) / 2.0f;

    return distance_cm;
}

// Take multiple measurements and return the median for better accuracy
static float measure_distance_median(float temp_c, int num_samples) {
    float samples[num_samples];
    int valid_count = 0;

    for (int i = 0; i < num_samples; i++) {
        int64_t echo_time = measure_echo_pulse();
        if (echo_time > 0) {
            samples[valid_count++] = calculate_distance(echo_time, temp_c);
        }
        vTaskDelay(pdMS_TO_TICKS(10)); // small delay between measurements
    }

    if (valid_count == 0) return -1.0f;

    // Sort samples for median calculation
    for (int i = 0; i < valid_count - 1; i++) {
        for (int j = i + 1; j < valid_count; j++) {
            if (samples[i] > samples[j]) {
                float temp = samples[i];
                samples[i] = samples[j];
                samples[j] = temp;
            }
        }
    }

    // Return median
    return samples[valid_count / 2];
}

// ---------- Main Task ----------
void app_main(void) {
    ESP_LOGI(TAG, "Lab 6.1 - Ultrasonic Range Finder Starting");

    // Initialize I2C for temperature sensor
    ESP_ERROR_CHECK(i2c_init());
    ESP_LOGI(TAG, "I2C initialized (SDA=%d, SCL=%d)", SDA_PIN, SCL_PIN);

    // Detect temperature sensor
    detect_temperature_sensor();

    // Initialize ultrasonic sensor
    ultrasonic_init();
    ESP_LOGI(TAG, "Ultrasonic sensor initialized (TRIG=%d, ECHO=%d)", TRIG_PIN, ECHO_PIN);

    while (1) {
        // Read temperature
        float temp_c = 20.0f; // default
        read_temperature(&temp_c);

        // Clamp temperature to 0-50C range as specified
        if (temp_c < 0.0f) temp_c = 0.0f;
        if (temp_c > 50.0f) temp_c = 50.0f;

        // Measure distance with multiple samples for accuracy
        float distance_cm = measure_distance_median(temp_c, 5);

        if (distance_cm > 0) {
            printf("Distance: %.1f cm at %.0fC\n", distance_cm, temp_c);
        } else {
            printf("Distance: ERROR at %.0fC\n", temp_c);
        }

        // Update once per second
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
