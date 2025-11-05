#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <pigpio.h>

#define LED_PIN 17  // GPIO pin for LED (can be changed)
#define DOT_DURATION 200000  // Duration of a dot in microseconds (200ms)

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
    gpioWrite(LED_PIN, 1);
    usleep(DOT_DURATION);
    gpioWrite(LED_PIN, 0);
    usleep(DOT_DURATION);  // Gap between symbols
}

// Function to send a dash (3 times dot duration)
void send_dash() {
    gpioWrite(LED_PIN, 1);
    usleep(DOT_DURATION * 3);
    gpioWrite(LED_PIN, 0);
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

    // Initialize pigpio
    if (gpioInitialise() < 0) {
        fprintf(stderr, "Error: Failed to initialize pigpio\n");
        fprintf(stderr, "Make sure pigpiod daemon is running: sudo pigpiod\n");
        return 1;
    }

    // Set LED pin as output
    gpioSetMode(LED_PIN, PI_OUTPUT);
    gpioWrite(LED_PIN, 0);  // Start with LED off

    // Send the message multiple times
    for (int i = 0; i < repetitions; i++) {
        send_message(argv[2]);

        // Gap between repetitions (longer pause)
        if (i < repetitions - 1) {
            usleep(DOT_DURATION * 7);  // 7 unit gap between messages
        }
    }

    // Cleanup
    gpioWrite(LED_PIN, 0);  // Ensure LED is off
    gpioTerminate();

    return 0;
}
