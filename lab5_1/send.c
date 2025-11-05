#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <gpiod.h>

#define LED_PIN 17  // GPIO pin for LED (can be changed)
#define DOT_DURATION 200000  // Duration of a dot in microseconds (200ms)
#define GPIO_CHIP "gpiochip0"  // Raspberry Pi GPIO chip

// GPIO line
static struct gpiod_chip *chip = NULL;
static struct gpiod_line *line = NULL;

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

// Initialize GPIO using libgpiod
int gpio_init(int pin) {
    // Open GPIO chip
    chip = gpiod_chip_open_by_name(GPIO_CHIP);
    if (!chip) {
        perror("Failed to open GPIO chip");
        return -1;
    }

    // Get GPIO line
    line = gpiod_chip_get_line(chip, pin);
    if (!line) {
        perror("Failed to get GPIO line");
        gpiod_chip_close(chip);
        return -1;
    }

    // Request line as output
    if (gpiod_line_request_output(line, "morse_led", 0) < 0) {
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
        gpiod_line_set_value(line, 0);  // Turn off LED
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
    usleep(DOT_DURATION);
    gpio_write(0);
    usleep(DOT_DURATION);  // Gap between symbols
}

// Function to send a dash (3 times dot duration)
void send_dash() {
    gpio_write(1);
    usleep(DOT_DURATION * 3);
    gpio_write(0);
    usleep(DOT_DURATION);  // Gap between symbols
}

// Function to send a character in morse code
void send_morse_char(const char* morse) {
    if (strcmp(morse, "/") == 0) {
        // Word separator (7 units total: 3 after last letter + 4 more = 7)
        // We already have 3 units from letter gap, add 4 more
        usleep(DOT_DURATION * 4);
        return;
    }

    for (int i = 0; morse[i] != '\0'; i++) {
        if (morse[i] == '.') {
            send_dot();
        } else if (morse[i] == '-') {
            send_dash();
        }
    }
    // Gap between letters (3 units total, we already have 1 from symbol gap)
    usleep(DOT_DURATION * 2);
}

// Function to send a message in morse code
void send_message(const char* message) {
    printf("%s\n", message);

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
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <repetitions> <message>\n", argv[0]);
        fprintf(stderr, "Example: %s 4 \"hello ESP32\"\n", argv[0]);
        return 1;
    }

    int repetitions = atoi(argv[1]);
    if (repetitions <= 0) {
        fprintf(stderr, "Error: repetitions must be a positive integer\n");
        return 1;
    }

    // Initialize GPIO
    printf("Initializing GPIO pin %d...\n", LED_PIN);
    if (gpio_init(LED_PIN) < 0) {
        fprintf(stderr, "Error: Failed to initialize GPIO\n");
        fprintf(stderr, "Make sure you run this program with sudo:\n");
        fprintf(stderr, "  sudo %s %d \"%s\"\n", argv[0], repetitions, argv[2]);
        return 1;
    }

    printf("Starting transmission...\n");
    gpio_write(0);  // Start with LED off

    // Send the message multiple times
    for (int i = 0; i < repetitions; i++) {
        send_message(argv[2]);

        // Gap between repetitions (longer pause)
        if (i < repetitions - 1) {
            usleep(DOT_DURATION * 7);  // 7 unit gap between messages
        }
    }

    // Cleanup
    printf("Cleaning up...\n");
    gpio_cleanup();

    return 0;
}
