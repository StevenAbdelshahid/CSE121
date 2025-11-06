/*
 * Lab 5.3 - High-Speed Morse Code Receiver
 *
 * Optimized for fast transmission speeds (10-20+ chars/sec)
 * Uses GPIO digital input with interrupts for faster response than ADC
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "morse_fast";

// GPIO Configuration for digital photodiode/comparator input
#define PHOTODIODE_GPIO     GPIO_NUM_1  // GPIO1 - adjust for your board
#define GPIO_INPUT_PIN_SEL  (1ULL << PHOTODIODE_GPIO)

// Configurable speed parameters (adjust based on transmission speed)
// Default: 10 chars/sec
#define DEFAULT_DOT_US      10000   // 10ms dot for 10 chars/sec
#define DEFAULT_TOLERANCE   0.5     // 50% tolerance for timing detection

// Timing parameters (microseconds) - will be adjusted based on config
static int dot_min_us;
static int dot_max_us;
static int dash_min_us;
static int dash_max_us;
static int letter_gap_min_us;
static int word_gap_min_us;

// Event queue for GPIO interrupts
static QueueHandle_t gpio_evt_queue = NULL;

// Edge timing structure
typedef struct {
    int64_t timestamp_us;
    int level;  // 0 = falling edge (light off), 1 = rising edge (light on)
} edge_event_t;

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

// Configure timing based on desired speed
void configure_timing(int dot_duration_us, double tolerance) {
    int tol_us = (int)(dot_duration_us * tolerance);

    dot_min_us = dot_duration_us - tol_us;
    dot_max_us = dot_duration_us + tol_us;

    int dash_duration_us = dot_duration_us * 3;
    dash_min_us = dash_duration_us - tol_us;
    dash_max_us = dash_duration_us + tol_us;

    letter_gap_min_us = dot_duration_us * 2;  // Between 2-7 units
    word_gap_min_us = dot_duration_us * 6;    // 6+ units

    ESP_LOGI(TAG, "Timing configured for dot=%d us (tolerance=%.0f%%)",
             dot_duration_us, tolerance * 100);
    ESP_LOGI(TAG, "  Dot: %d-%d us", dot_min_us, dot_max_us);
    ESP_LOGI(TAG, "  Dash: %d-%d us", dash_min_us, dash_max_us);
    ESP_LOGI(TAG, "  Letter gap: >%d us", letter_gap_min_us);
    ESP_LOGI(TAG, "  Word gap: >%d us", word_gap_min_us);
}

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

// GPIO interrupt handler
static void IRAM_ATTR gpio_isr_handler(void* arg) {
    edge_event_t event;
    event.timestamp_us = esp_timer_get_time();
    event.level = gpio_get_level(PHOTODIODE_GPIO);
    xQueueSendFromISR(gpio_evt_queue, &event, NULL);
}

// Morse decoder task
static void morse_decoder_task(void *arg) {
    edge_event_t event;
    char morse_buffer[10];
    int morse_idx = 0;

    int64_t last_rising_edge = 0;
    int64_t last_falling_edge = 0;

    int total_chars = 0;
    int64_t first_char_time = 0;
    int correct_chars = 0;
    int error_chars = 0;

    ESP_LOGI(TAG, "Morse decoder started. Waiting for signals...");

    while (1) {
        if (xQueueReceive(gpio_evt_queue, &event, portMAX_DELAY)) {

            if (event.level == 1) {
                // Rising edge - light turned ON
                last_rising_edge = event.timestamp_us;

                // Check gap duration before this pulse
                if (last_falling_edge > 0) {
                    int gap_us = event.timestamp_us - last_falling_edge;

                    if (gap_us > word_gap_min_us && morse_idx > 0) {
                        // Word boundary
                        morse_buffer[morse_idx] = '\0';
                        char decoded = decode_morse(morse_buffer);
                        printf("%c ", decoded);
                        fflush(stdout);

                        if (decoded != '?') correct_chars++;
                        else error_chars++;
                        total_chars++;

                        morse_idx = 0;

                    } else if (gap_us > letter_gap_min_us && morse_idx > 0) {
                        // Letter boundary
                        morse_buffer[morse_idx] = '\0';
                        char decoded = decode_morse(morse_buffer);
                        printf("%c", decoded);
                        fflush(stdout);

                        if (first_char_time == 0) {
                            first_char_time = event.timestamp_us;
                        }

                        if (decoded != '?') correct_chars++;
                        else error_chars++;
                        total_chars++;

                        morse_idx = 0;
                    }
                }

            } else {
                // Falling edge - light turned OFF
                last_falling_edge = event.timestamp_us;

                // Measure pulse duration
                if (last_rising_edge > 0) {
                    int pulse_us = event.timestamp_us - last_rising_edge;

                    // Classify as dot or dash
                    if (pulse_us >= dash_min_us && pulse_us <= dash_max_us) {
                        // Dash
                        if (morse_idx < sizeof(morse_buffer) - 1) {
                            morse_buffer[morse_idx++] = '-';
                        }
                    } else if (pulse_us >= dot_min_us && pulse_us <= dot_max_us) {
                        // Dot
                        if (morse_idx < sizeof(morse_buffer) - 1) {
                            morse_buffer[morse_idx++] = '.';
                        }
                    } else {
                        // Pulse duration out of range - might be noise or wrong speed
                        ESP_LOGW(TAG, "Pulse duration %d us out of range", pulse_us);
                    }
                }
            }
        }
    }
}

// Statistics task
static void stats_task(void *arg) {
    int64_t start_time = esp_timer_get_time();
    int last_total = 0;

    vTaskDelay(pdMS_TO_TICKS(5000));  // Wait 5 seconds before first report

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));  // Report every 10 seconds

        int64_t now = esp_timer_get_time();
        double elapsed_sec = (now - start_time) / 1e6;

        ESP_LOGI(TAG, "=== Statistics ===");
        ESP_LOGI(TAG, "Runtime: %.1f seconds", elapsed_sec);

        // Could add more stats here if we track them
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "Lab 5.3 - High-Speed Morse Code Receiver");
    ESP_LOGI(TAG, "==========================================");

    // Configure timing (adjust for your transmission speed)
    // For 10 chars/sec: dot = 10ms
    // For 15 chars/sec: dot = 6.7ms
    // For 20 chars/sec: dot = 5ms
    configure_timing(DEFAULT_DOT_US, DEFAULT_TOLERANCE);

    // Configure GPIO
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_ANYEDGE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = GPIO_INPUT_PIN_SEL,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
    };
    gpio_config(&io_conf);

    // Create event queue
    gpio_evt_queue = xQueueCreate(100, sizeof(edge_event_t));

    // Install GPIO ISR service
    gpio_install_isr_service(0);
    gpio_isr_handler_add(PHOTODIODE_GPIO, gpio_isr_handler, NULL);

    ESP_LOGI(TAG, "GPIO %d configured with interrupt on both edges", PHOTODIODE_GPIO);

    // Start decoder task
    xTaskCreate(morse_decoder_task, "morse_decoder", 4096, NULL, 10, NULL);

    // Start statistics task
    xTaskCreate(stats_task, "stats", 2048, NULL, 5, NULL);

    ESP_LOGI(TAG, "Receiver ready! Waiting for transmissions...");
}
