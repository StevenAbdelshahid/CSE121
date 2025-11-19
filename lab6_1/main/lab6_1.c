// Lab 6.1 - Ultrasonic Range Finder with Temperature Compensation
// ESP32-C3 with RCWL-1601 (SR04-compatible) ultrasonic sensor
// Uses fixed temperature of 23C for speed of sound calculation

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char* TAG = "LAB6_1";

// ===== Ultrasonic Sensor Pins =====
#define TRIG_PIN  GPIO_NUM_4
#define ECHO_PIN  GPIO_NUM_5

// ===== Fixed Temperature (no sensor needed) =====
#define TEMP_C 23.0f

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
    ESP_LOGI(TAG, "Using fixed temperature: %.0fC", TEMP_C);

    // Initialize ultrasonic sensor
    ultrasonic_init();
    ESP_LOGI(TAG, "Ultrasonic sensor initialized (TRIG=%d, ECHO=%d)", TRIG_PIN, ECHO_PIN);

    while (1) {
        // Use fixed temperature
        float temp_c = TEMP_C;

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
