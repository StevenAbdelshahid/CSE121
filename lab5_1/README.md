# Lab 5.1 - Morse Code LED Transmitter for Raspberry Pi 4

This program transmits messages in Morse code using an LED connected to the Raspberry Pi 4.

## Hardware Setup

### Components Required:
- Raspberry Pi 4
- LED
- 330Ω resistor (recommended)
- Breadboard and jumper wires

### Wiring:
1. Connect the LED anode (longer leg) to GPIO 17 (pin 11) through a 330Ω resistor
2. Connect the LED cathode (shorter leg) to GND (any ground pin)

**WARNING**: Always use a resistor with the LED to prevent burning it out!

## Software Setup

### Prerequisites:
The program uses **libgpiod** - the modern Linux GPIO library. Install it on your Raspberry Pi:

```bash
sudo apt-get update
sudo apt-get install gpiod libgpiod-dev
```

### Building:
```bash
make
```

This will create the `send` executable.

### Running:

Run the program with sudo (required for GPIO access):
```bash
sudo ./send <repetitions> "<message>"
```

**Example:**
```bash
sudo ./send 4 "hello ESP32"
```

This will transmit "hello ESP32" in Morse code 4 times.

## Morse Code Timing

The program follows standard Morse code timing:
- Dot: 1 unit (200ms)
- Dash: 3 units (600ms)
- Gap between dots/dashes: 1 unit (200ms)
- Gap between letters: 3 units (600ms)
- Gap between words: 7 units (1400ms)

## Supported Characters

- Letters: A-Z (case insensitive)
- Digits: 0-9
- Space: word separator

## Cleaning Up

To remove the compiled program:
```bash
make clean
```

## Testing GPIO

You can test if your GPIO is working with the command-line tool:
```bash
# Turn LED on
gpioset gpiochip0 17=1

# Turn LED off
gpioset gpiochip0 17=0
```
