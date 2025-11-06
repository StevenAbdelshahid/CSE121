# Lab 5.3 Receiver (ESP32)

High-speed Morse code receiver using GPIO interrupts for fast edge detection.

## Key Improvements over Lab 5.2

- **GPIO digital input** instead of ADC polling → Much faster response
- **Interrupt-driven** edge detection → No polling delay
- **Configurable timing** parameters for different speeds
- **Higher sample rate** capable of detecting rapid pulses

## Building and Flashing

```bash
# Set target (adjust for your board)
idf.py set-target esp32c3

# Build
idf.py build

# Flash and monitor
idf.py flash monitor
```

## Configuration

### Speed Configuration

Edit `main/lab5_3.c` before building:

```c
#define DEFAULT_DOT_US      10000   // Dot duration in microseconds
#define DEFAULT_TOLERANCE   0.5     // Timing tolerance (50%)
```

**Common speeds:**

| Speed (chars/sec) | DOT_US | Comments |
|------------------|---------|----------|
| 1 | 100000 | Lab 5.2 baseline |
| 5 | 20000 | Moderate speed |
| 10 | 10000 | Default, good starting point |
| 15 | 6667 | Fast |
| 20 | 5000 | Very fast |
| 25 | 4000 | Extreme |

Formula: `DOT_US = 1,000,000 / (speed * 10)`

### GPIO Pin

Default: GPIO1 (`PHOTODIODE_GPIO`)

To change, edit in `lab5_3.c`:
```c
#define PHOTODIODE_GPIO     GPIO_NUM_1
```

**Available pins:**
- ESP32-C3: GPIO0-10
- ESP32: GPIO0-39 (avoid 6-11)
- ESP32-S3: GPIO0-48 (avoid flash pins)

## Wiring

### Digital Input (Recommended)

For best high-speed performance, use a comparator:

```
Photodiode → Comparator (LM393) → ESP32 GPIO1
                    |
                   GND
```

The comparator converts analog light levels to clean digital HIGH/LOW.

### Direct Photodiode (Simple)

```
Photodiode (+) → GPIO1
Photodiode (-) → GND
10kΩ resistor: GPIO1 to GND
```

**Note:** Direct connection limits maximum speed due to slow photodiode response.

## Operation

### Starting the Receiver

```bash
idf.py flash monitor
```

You should see:
```
I (123) morse_fast: Lab 5.3 - High-Speed Morse Code Receiver
I (124) morse_fast: ==========================================
I (125) morse_fast: Timing configured for dot=10000 us (tolerance=50%)
I (126) morse_fast:   Dot: 5000-15000 us
I (127) morse_fast:   Dash: 25000-35000 us
I (128) morse_fast:   Letter gap: >20000 us
I (129) morse_fast:   Word gap: >60000 us
I (130) morse_fast: GPIO 1 configured with interrupt on both edges
I (131) morse_fast: Receiver ready! Waiting for transmissions...
```

### Receiving Messages

When the Pi transmits, you'll see:
```
HELLO WORLD
```

Characters appear as they're decoded in real-time.

## Troubleshooting

### "Pulse duration out of range" warnings

The detected pulse doesn't match expected timing.

**Solutions:**
1. Update `DEFAULT_DOT_US` to match sender speed
2. Increase `DEFAULT_TOLERANCE` (try 0.6 or 0.7)
3. Verify sender is transmitting at expected speed

### Wrong characters ("???")

Timing mismatch or noisy signal.

**Solutions:**
1. Double-check timing configuration
2. Improve shielding from ambient light
3. Check wiring connections
4. Reduce transmission speed
5. Use comparator circuit for cleaner signal

### No detection at all

**Checklist:**
1. Is GPIO pin correct in code?
2. Is photodiode connected and working?
3. Test with slow speed first (1-5 chars/sec)
4. Check LED is actually blinking (visible to eye at slow speed)
5. Verify photodiode is pointing at LED
6. Try with phone flashlight to verify photodiode works

### Characters missing or cut off

Speed too fast for hardware.

**Solutions:**
1. Reduce transmission speed
2. Upgrade to comparator circuit
3. Use faster photodiode
4. Reduce LED-photodiode distance (min 1mm)

## Performance Tips

### For Maximum Speed:

1. **Use digital comparator circuit** - biggest improvement
2. **Match timing exactly** - calculate DOT_US precisely
3. **Reduce tolerance** once stable (0.3-0.4)
4. **Minimize distance** to LED (but >1mm)
5. **Shield from ambient light**
6. **Use dedicated GPIO** pin (not shared with peripherals)

### Monitoring Performance:

Watch for these warnings:
- "Pulse duration out of range" → timing mismatch
- Long gaps between characters → signal too weak
- Garbled output → speed too fast

## Advanced Configuration

### Adjusting Timing Tolerance

More tolerance = more robust, but allows more errors
Less tolerance = more accurate, but requires perfect timing

```c
#define DEFAULT_TOLERANCE   0.3     // Tight (30%)
#define DEFAULT_TOLERANCE   0.5     // Normal (50%)
#define DEFAULT_TOLERANCE   0.7     // Loose (70%)
```

### Queue Size

If missing edges at very high speed:

```c
gpio_evt_queue = xQueueCreate(200, sizeof(edge_event_t));  // Increase from 100
```

### Task Priority

Increase decoder priority if needed:

```c
xTaskCreate(morse_decoder_task, "morse_decoder", 4096, NULL, 15, NULL);  // Increase from 10
```

## Expected Performance

| Hardware | Max Speed |
|----------|-----------|
| Direct photodiode | 5-10 chars/sec |
| With comparator | 15-25 chars/sec |
| Optimized setup | 25+ chars/sec |

Your mileage may vary based on:
- Photodiode response time
- LED brightness
- Distance and alignment
- Ambient light conditions
- ESP32 board and clock speed
