/*
 * Lab 5.2 - Morse Code Receiver using ADC and Photodiode
 *
 * This program reads light signals from a photodiode connected to an ADC pin
 * and decodes Morse code messages transmitted by the Raspberry Pi LED.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

static const char *TAG = "morse_receiver";

// ADC Configuration
#define ADC_UNIT            ADC_UNIT_1
#define ADC_CHANNEL         ADC_CHANNEL_1  // GPIO1 on ESP32-C3
#define ADC_ATTEN           ADC_ATTEN_DB_6  // 6dB attenuation for 0-2450mV range (better sensitivity)
#define ADC_BITWIDTH        ADC_BITWIDTH_12

// Morse code timing parameters (in milliseconds)
#define DOT_DURATION        400     // Base duration for a dot (matches transmitter)
#define DASH_MIN_DURATION   1000    // Minimum duration for a dash (2.5x dot)
#define SYMBOL_GAP_MAX      800     // Max gap between symbols in same letter
#define LETTER_GAP_MIN      1000    // Min gap between letters
#define WORD_GAP_MIN        2400    // Min gap between words
#define TIMEOUT_MS          3000    // Timeout to print buffered letter if no new signals

// Light detection threshold
#define LIGHT_THRESHOLD     22      // 22mV threshold
#define SAMPLE_RATE_MS      10      // Sample every 10ms (prevents watchdog timeout)

// Morse code table
const char* morse_table[] = {
    ".-",    // A
    "-...",  // B
    "-.-.",  // C
    "-..",   // D
    ".",     // E
    "..-.",  // F
    "--.",   // G
    "....",  // H
    "..",    // I
    ".---",  // J
    "-.-",   // K
    ".-..",  // L
    "--",    // M
    "-.",    // N
    "---",   // O
    ".--.",  // P
    "--.-",  // Q
    ".-.",   // R
    "...",   // S
    "-",     // T
    "..-",   // U
    "...-",  // V
    ".--",   // W
    "-..-",  // X
    "-.--",  // Y
    "--..",  // Z
    "-----", // 0
    ".----", // 1
    "..---", // 2
    "...--", // 3
    "....-", // 4
    ".....", // 5
    "-....", // 6
    "--...", // 7
    "---..", // 8
    "----."  // 9
};

// ADC handles
static adc_oneshot_unit_handle_t adc_handle;
static adc_cali_handle_t adc_cali_handle = NULL;

// Function to decode morse code pattern to character
char decode_morse(const char* pattern) {
    if (pattern == NULL || strlen(pattern) == 0) {
        return '\0';
    }

    // Check letters A-Z
    for (int i = 0; i < 26; i++) {
        if (strcmp(pattern, morse_table[i]) == 0) {
            return 'A' + i;
        }
    }

    // Check digits 0-9
    for (int i = 0; i < 10; i++) {
        if (strcmp(pattern, morse_table[26 + i]) == 0) {
            return '0' + i;
        }
    }

    return '?';  // Unknown pattern
}

// Initialize ADC
static void init_adc(void) {
    ESP_LOGI(TAG, "Initializing ADC...");

    // Configure ADC
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc_handle));

    // Configure ADC channel
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH,
        .atten = ADC_ATTEN,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, ADC_CHANNEL, &config));

    // Configure ADC calibration
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT,
        .atten = ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH,
    };

    esp_err_t ret = adc_cali_create_scheme_curve_fitting(&cali_config, &adc_cali_handle);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "ADC calibration enabled");
    } else {
        ESP_LOGW(TAG, "ADC calibration not supported, using raw values");
        adc_cali_handle = NULL;
    }

    ESP_LOGI(TAG, "ADC initialized on channel %d", ADC_CHANNEL);
}

// Read ADC value
static int read_adc(void) {
    int adc_raw = 0;
    int voltage = 0;

    ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, ADC_CHANNEL, &adc_raw));

    if (adc_cali_handle != NULL) {
        ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc_cali_handle, adc_raw, &voltage));
        return voltage;
    }

    return adc_raw;
}

// Main Morse code receiver task
static void morse_receiver_task(void *arg) {
    char morse_buffer[10];  // Buffer to store current morse pattern
    int morse_idx = 0;

    bool light_on = false;
    int64_t light_start_time = 0;
    int64_t gap_start_time = 0;
    int64_t last_activity_time = 0;

    ESP_LOGI(TAG, "Starting Morse code receiver...");
    ESP_LOGI(TAG, "Place photodiode at least 1mm away from LED");
    ESP_LOGI(TAG, "Threshold: %d mV", LIGHT_THRESHOLD);

    while (1) {
        int adc_value = read_adc();
        int64_t current_time = esp_timer_get_time() / 1000;  // Convert to milliseconds

        // Check for timeout - if no activity and we have buffered morse code
        if (morse_idx > 0 && last_activity_time > 0) {
            int64_t idle_time = current_time - last_activity_time;
            if (idle_time > TIMEOUT_MS) {
                // Timeout - print buffered letter
                morse_buffer[morse_idx] = '\0';
                char decoded = decode_morse(morse_buffer);
                printf("%c\n", decoded);
                fflush(stdout);
                ESP_LOGI(TAG, "Timeout - printed buffered letter");
                morse_idx = 0;
                last_activity_time = 0;
            }
        }

        // Detect light state change
        if (adc_value > LIGHT_THRESHOLD && !light_on) {
            // Light turned ON
            light_on = true;
            light_start_time = current_time;
            last_activity_time = current_time;

            // Check if gap before this was long enough for letter/word boundary
            if (gap_start_time > 0) {
                int gap_duration = current_time - gap_start_time;

                if (gap_duration > WORD_GAP_MIN && morse_idx > 0) {
                    // Word boundary - decode and print character, then add space
                    morse_buffer[morse_idx] = '\0';
                    char decoded = decode_morse(morse_buffer);
                    printf("%c ", decoded);
                    fflush(stdout);
                    morse_idx = 0;
                } else if (gap_duration > LETTER_GAP_MIN && morse_idx > 0) {
                    // Letter boundary - decode and print character
                    morse_buffer[morse_idx] = '\0';
                    char decoded = decode_morse(morse_buffer);
                    printf("%c", decoded);
                    fflush(stdout);
                    morse_idx = 0;
                }
            }

        } else if (adc_value <= LIGHT_THRESHOLD && light_on) {
            // Light turned OFF
            light_on = false;
            gap_start_time = current_time;
            last_activity_time = current_time;

            // Determine if it was a dot or dash
            int pulse_duration = current_time - light_start_time;

            if (pulse_duration >= DASH_MIN_DURATION) {
                // It was a dash
                if (morse_idx < sizeof(morse_buffer) - 1) {
                    morse_buffer[morse_idx++] = '-';
                    ESP_LOGI(TAG, "Detected: DASH (%d ms)", pulse_duration);
                }
            } else if (pulse_duration >= DOT_DURATION / 2) {
                // It was a dot
                if (morse_idx < sizeof(morse_buffer) - 1) {
                    morse_buffer[morse_idx++] = '.';
                    ESP_LOGI(TAG, "Detected: DOT (%d ms)", pulse_duration);
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(SAMPLE_RATE_MS));
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "Lab 5.2 - Morse Code Receiver");
    ESP_LOGI(TAG, "============================");

    // Initialize ADC
    init_adc();

    // Start receiver task
    xTaskCreate(morse_receiver_task, "morse_receiver", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "Morse code receiver started. Waiting for signals...");
}
