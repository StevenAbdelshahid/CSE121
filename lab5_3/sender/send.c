#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <time.h>
#include <gpiod.h>

#define LED_PIN 17  // GPIO pin for LED
#define GPIO_CHIP "gpiochip0"  // Raspberry Pi GPIO chip

// Default speed: 10 characters per second (optimized for Lab 5.3)
#define DEFAULT_SPEED 10.0

// GPIO line
static struct gpiod_chip *chip = NULL;
static struct gpiod_line *line = NULL;

// Timing parameters (calculated based on speed)
static int dot_duration_us;
static int dash_duration_us;
static int symbol_gap_us;
static int letter_gap_us;
static int word_gap_us;

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

// Calculate timing parameters based on desired speed (chars/sec)
void calculate_timing(double speed) {
    // Average Morse code character has about 10 time units
    // (including intra-character gaps)
    // 1 time unit = dot duration

    // For speed chars/sec, we need to fit one character in (1/speed) seconds
    // Average character takes about 10 units, so:
    // dot_duration = (1/speed) / 10 seconds

    double dot_duration_sec = 1.0 / (speed * 10.0);
    dot_duration_us = (int)(dot_duration_sec * 1000000);

    // Standard Morse timing ratios
    dash_duration_us = dot_duration_us * 3;
    symbol_gap_us = dot_duration_us;      // Gap between dots/dashes
    letter_gap_us = dot_duration_us * 3;  // Gap between letters
    word_gap_us = dot_duration_us * 7;    // Gap between words

    printf("Speed: %.2f chars/sec\n", speed);
    printf("Timing (us): dot=%d, dash=%d, symbol_gap=%d, letter_gap=%d, word_gap=%d\n",
           dot_duration_us, dash_duration_us, symbol_gap_us, letter_gap_us, word_gap_us);
}

// High-precision sleep using nanosleep
void precise_sleep_us(int microseconds) {
    struct timespec ts;
    ts.tv_sec = microseconds / 1000000;
    ts.tv_nsec = (microseconds % 1000000) * 1000;
    nanosleep(&ts, NULL);
}

// Initialize GPIO using libgpiod
int gpio_init(int pin) {
    chip = gpiod_chip_open_by_name(GPIO_CHIP);
    if (!chip) {
        perror("Failed to open GPIO chip");
        return -1;
    }

    line = gpiod_chip_get_line(chip, pin);
    if (!line) {
        perror("Failed to get GPIO line");
        gpiod_chip_close(chip);
        return -1;
    }

    if (gpiod_line_request_output(line, "morse_led_fast", 0) < 0) {
        perror("Failed to request GPIO line as output");
        gpiod_chip_close(chip);
        return -1;
    }

    return 0;
}

// Write to GPIO pin
void gpio_write(int value) {
    if (line) {
        gpiod_line_set_value(line, value);
    }
}

// Cleanup GPIO
void gpio_cleanup(void) {
    if (line) {
        gpiod_line_set_value(line, 0);
        gpiod_line_release(line);
        line = NULL;
    }
    if (chip) {
        gpiod_chip_close(chip);
        chip = NULL;
    }
}

// Function to get morse code for a character
const char* get_morse_code(char c) {
    c = toupper(c);
    if (c >= 'A' && c <= 'Z') {
        return morse_table[c - 'A'];
    } else if (c >= '0' && c <= '9') {
        return morse_table[26 + (c - '0')];
    } else if (c == ' ') {
        return "/";  // Word separator
    }
    return NULL;
}

// Function to send a dot
void send_dot() {
    gpio_write(1);
    precise_sleep_us(dot_duration_us);
    gpio_write(0);
    precise_sleep_us(symbol_gap_us);
}

// Function to send a dash
void send_dash() {
    gpio_write(1);
    precise_sleep_us(dash_duration_us);
    gpio_write(0);
    precise_sleep_us(symbol_gap_us);
}

// Function to send a character in morse code
void send_morse_char(const char* morse) {
    if (strcmp(morse, "/") == 0) {
        // Word separator (additional gap)
        precise_sleep_us(word_gap_us - letter_gap_us);
        return;
    }

    for (int i = 0; morse[i] != '\0'; i++) {
        if (morse[i] == '.') {
            send_dot();
        } else if (morse[i] == '-') {
            send_dash();
        }
    }
    // Gap between letters
    precise_sleep_us(letter_gap_us - symbol_gap_us);
}

// Function to send a message in morse code
void send_message(const char* message) {
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    for (int i = 0; message[i] != '\0'; i++) {
        const char* morse = get_morse_code(message[i]);
        if (morse) {
            printf("%s ", morse);
            fflush(stdout);
            send_morse_char(morse);
        }
    }
    printf("\n");
    fflush(stdout);

    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed = (end.tv_sec - start.tv_sec) +
                     (end.tv_nsec - start.tv_nsec) / 1e9;

    // Count non-space characters
    int char_count = 0;
    for (int i = 0; message[i] != '\0'; i++) {
        if (message[i] != ' ') char_count++;
    }

    double actual_speed = char_count / elapsed;
    printf("Transmission time: %.3f seconds\n", elapsed);
    printf("Characters sent: %d\n", char_count);
    printf("Actual speed: %.2f chars/sec\n", actual_speed);
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <repetitions> <message> [speed_chars_per_sec]\n", argv[0]);
        fprintf(stderr, "Example: %s 1 \"hello ESP32\" 10\n", argv[0]);
        fprintf(stderr, "         %s 1 \"test\" 15\n", argv[0]);
        fprintf(stderr, "\nSpeed recommendations:\n");
        fprintf(stderr, "  Lab 5.2 baseline: 1 chars/sec\n");
        fprintf(stderr, "  Lab 5.3 optimized: 10-20 chars/sec\n");
        fprintf(stderr, "  Challenge mode: 20+ chars/sec\n");
        return 1;
    }

    int repetitions = atoi(argv[1]);
    if (repetitions <= 0) {
        fprintf(stderr, "Error: repetitions must be a positive integer\n");
        return 1;
    }

    double speed = DEFAULT_SPEED;
    if (argc >= 4) {
        speed = atof(argv[3]);
        if (speed <= 0) {
            fprintf(stderr, "Error: speed must be positive\n");
            return 1;
        }
    }

    // Calculate timing based on speed
    calculate_timing(speed);

    // Initialize GPIO
    printf("Initializing GPIO pin %d...\n", LED_PIN);
    if (gpio_init(LED_PIN) < 0) {
        fprintf(stderr, "Error: Failed to initialize GPIO\n");
        fprintf(stderr, "Make sure you run this program with sudo:\n");
        fprintf(stderr, "  sudo %s %d \"%s\" %.1f\n", argv[0], repetitions, argv[2], speed);
        return 1;
    }

    printf("Starting high-speed transmission...\n");
    gpio_write(0);

    // Send the message multiple times
    for (int i = 0; i < repetitions; i++) {
        printf("\n=== Transmission %d/%d ===\n", i + 1, repetitions);
        send_message(argv[2]);

        if (i < repetitions - 1) {
            printf("Waiting between repetitions...\n");
            precise_sleep_us(word_gap_us * 2);
        }
    }

    // Cleanup
    printf("\nCleaning up...\n");
    gpio_cleanup();

    return 0;
}
