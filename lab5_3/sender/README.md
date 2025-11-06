# Lab 5.3 Sender (Raspberry Pi)

High-speed Morse code transmitter with configurable transmission speed.

## Building

```bash
make
```

Requires: `libgpiod` (install: `sudo apt-get install gpiod libgpiod-dev`)

## Usage

```bash
sudo ./send <repetitions> "<message>" [speed_chars_per_sec]
```

**Arguments:**
- `repetitions`: Number of times to send the message
- `message`: Text to transmit (A-Z, 0-9, space)
- `speed_chars_per_sec`: Transmission speed (default: 10)

**Examples:**

```bash
# Baseline test (1 char/sec)
sudo ./send 1 "hello" 1

# Normal speed (10 chars/sec)
sudo ./send 1 "hello world" 10

# Fast speed (15 chars/sec)
sudo ./send 1 "test" 15

# Very fast (20 chars/sec)
sudo ./send 1 "test" 20
```

## Output

The program displays:
- Calculated timing parameters
- Morse code pattern being sent
- Transmission time
- Actual speed achieved

Example:
```
Speed: 10.00 chars/sec
Timing (us): dot=10000, dash=30000, symbol_gap=10000, letter_gap=30000, word_gap=70000
Initializing GPIO pin 17...
Starting high-speed transmission...

=== Transmission 1/1 ===
.... . .-.. .-.. ---
Transmission time: 1.234 seconds
Characters sent: 5
Actual speed: 4.05 chars/sec
```

## Speed Testing

Use the provided script:

```bash
chmod +x test_speeds.sh
./test_speeds.sh
```

This will automatically test multiple speeds and prompt you to verify ESP32 reception.

## Optimizations

This sender is optimized for high-speed transmission:

- **Precise timing**: Uses `nanosleep()` for microsecond accuracy
- **Compiler optimization**: Built with `-O3 -march=native`
- **Timing calculation**: Automatically adjusts all timing based on target speed
- **Speed measurement**: Reports actual achieved speed

## Wiring

Same as Lab 5.1:
- GPIO 17 (pin 11) → 330Ω resistor → LED (+)
- LED (-) → GND (pin 39)

For best high-speed performance:
- Use low-inductance wiring
- Keep wires short
- Ensure good power supply

## Troubleshooting

**LED doesn't blink:**
- Run with sudo: `sudo ./send ...`
- Check libgpiod is installed
- Verify wiring

**Speed seems wrong:**
- Check the "Actual speed" output
- System load can affect timing
- Try reducing background processes
- Consider using real-time priority: `sudo nice -n -20 ./send ...`
