# Lab 5.2 - Morse Code Receiver (ESP32 with Photodiode)

This program receives Morse code light signals from the Raspberry Pi LED using a photodiode connected to the ESP32 ADC.

## Hardware Setup

### Components Required:
- ESP32 board (ESP32-C3 or similar)
- Photodiode (light sensor)
- 10kΩ resistor (pull-down for photodiode)
- Breadboard and jumper wires
- Raspberry Pi 4 with LED (from Lab 5.1)

### Wiring:

**Photodiode Connection:**
1. Photodiode anode (+) → GPIO1 (ADC1_CH0)
2. Photodiode cathode (-) → GND
3. 10kΩ resistor between GPIO1 and GND (pull-down)

**Alternative Pins:**
- ESP32-C3: GPIO0-4 (ADC1_CH0-4)
- ESP32: GPIO32-39 (ADC1_CH4-7)
- ESP32-S3: GPIO1-10 (ADC1_CH0-9)

If using a different pin, update `ADC_CHANNEL` in lab5_2.c

### Positioning:
- Place photodiode **at least 1mm away** from the Raspberry Pi LED
- Point photodiode directly at the LED
- Reduce ambient light for better detection

## Software Setup

### Building and Flashing:

```bash
cd lab5_2

# Configure for your ESP32 board (if needed)
idf.py set-target esp32c3

# Build
idf.py build

# Flash and monitor
idf.py flash monitor
```

### Running the System:

1. **Start ESP32 receiver** (this program):
   ```bash
   cd lab5_2
   idf.py flash monitor
   ```

2. **Start Raspberry Pi transmitter** (Lab 5.1):
   ```bash
   cd lab5_1
   sudo ./send 4 "hello ESP32"
   ```

3. **Watch the ESP32 console** - decoded messages will appear!

## Configuration

### Adjust Light Threshold:
If the photodiode isn't detecting the LED, adjust the threshold in `lab5_2.c`:

```c
#define LIGHT_THRESHOLD     1000    // Increase if too sensitive, decrease if not detecting
```

Monitor raw ADC values in the console to determine the right threshold.

### Timing Parameters:
Default timing matches Lab 5.1 (200ms dot duration):
- DOT: 200ms
- DASH: 500ms (2.5x dot)
- Symbol gap: <400ms
- Letter gap: 500-1200ms
- Word gap: >1200ms

## Expected Output

When receiving "hello ESP32":

```
I (1234) morse_receiver: Detected: DOT (210 ms)
I (1445) morse_receiver: Detected: DOT (205 ms)
I (1656) morse_receiver: Detected: DOT (198 ms)
I (1867) morse_receiver: Detected: DOT (203 ms)
H
I (2100) morse_receiver: Detected: DOT (195 ms)
E
...
HELLO ESP32
```

## Troubleshooting

**No detection:**
- Check wiring (photodiode polarity!)
- Verify photodiode is working: shine phone flashlight on it and check ADC values
- Adjust LIGHT_THRESHOLD in code
- Ensure LED from Lab 5.1 is working

**Wrong characters:**
- Adjust timing parameters
- Ensure photodiode is close enough to LED (but >1mm away)
- Reduce ambient light interference

**Intermittent detection:**
- Use shielding around LED/photodiode
- Check for loose connections
- Ensure stable power supply

## Performance Requirements

Lab 5.2 requires:
- Message transmission time: <10 seconds for "hello ESP32"
- Speed: ~1 character/second
- Minimum distance: 1mm between LED and photodiode

## Next Steps

After verifying Lab 5.2 works, proceed to Lab 5.3 to optimize for faster transmission speeds!
