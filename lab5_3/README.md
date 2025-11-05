# Lab 5.3 - High-Speed Morse Code Communication

Optimized version of Lab 5.2 with significantly increased transmission speeds.

## Goal

Find the maximum reliable transmission speed and document both:
- **Highest successful speed** (where messages are received correctly)
- **Lowest failing speed** (where messages start to fail)

The difference between pass and fail speeds must be ≤ 25%.

**Formula:** `(fail_speed - pass_speed) / pass_speed ≤ 0.25`

**Example:**
- Pass: 10 chars/sec, Fail: 12 chars/sec → (12-10)/10 = 0.20 = 20% ✅
- Pass: 10 chars/sec, Fail: 15 chars/sec → (15-10)/10 = 0.50 = 50% ❌

## Extra Credit

Top 5 fastest submissions get bonus points:
1. 🥇 1st place: +10 points
2. 🥈 2nd place: +8 points
3. 🥉 3rd place: +6 points
4. 4th place: +4 points
5. 5th place: +2 points

---

## Hardware Setup

### Components Required:
- Raspberry Pi 4 with LED (from Lab 5.1)
- ESP32 board
- Photodiode or LDR (light dependent resistor)
- For digital GPIO mode: Comparator circuit (optional but recommended)
- 10kΩ resistor
- Breadboard and jumper wires

### Wiring Options:

#### Option 1: Digital GPIO (Recommended for speed)
Use a comparator (LM393 or similar) to convert analog photodiode signal to digital:

```
Photodiode → Comparator → ESP32 GPIO1 (digital 0/1)
```

Benefits: Faster response, cleaner signal, higher speed capability

#### Option 2: Simple Photodiode
Direct connection (same as Lab 5.2):

```
Photodiode (+) → GPIO1
Photodiode (-) → GND + 10kΩ pull-down
```

**Note:** Digital GPIO method will achieve much higher speeds!

### Physical Setup:
- Distance: 1-5mm between LED and photodiode (closer = faster possible)
- Alignment: Point photodiode directly at LED
- Shield from ambient light for best results
- Stable mounting (no vibration)

---

## Software Setup

### Raspberry Pi Sender

```bash
cd lab5_3/sender
make
sudo ./send <reps> "<message>" <speed>
```

**Parameters:**
- `reps`: Number of repetitions
- `message`: Text to transmit
- `speed`: Characters per second (decimal allowed)

**Examples:**
```bash
# Baseline speed (Lab 5.2 equivalent)
sudo ./send 1 "hello world" 1

# Optimized speed
sudo ./send 1 "hello world" 10

# High speed
sudo ./send 1 "hello world" 15

# Challenge speed
sudo ./send 1 "test" 20
```

The program automatically calculates Morse timing and reports actual transmission speed.

### ESP32 Receiver

```bash
cd lab5_3/receiver

# Configure for your board
idf.py set-target esp32c3

# Build and flash
idf.py build flash monitor
```

**Speed Configuration:**

Edit `lab5_3/receiver/main/lab5_3.c` to match your transmission speed:

```c
#define DEFAULT_DOT_US      10000   // For 10 chars/sec
#define DEFAULT_TOLERANCE   0.5     // 50% tolerance
```

**Speed-to-timing conversion:**
- 1 char/sec: dot = 100,000 us (100ms)
- 5 chars/sec: dot = 20,000 us (20ms)
- 10 chars/sec: dot = 10,000 us (10ms)
- 15 chars/sec: dot = 6,667 us (6.7ms)
- 20 chars/sec: dot = 5,000 us (5ms)

Formula: `dot_us = 1,000,000 / (speed * 10)`

---

## Testing Methodology

### Step 1: Baseline Test (Lab 5.2 speed)

```bash
# Sender (Pi)
sudo ./send 1 "hello ESP32" 1

# Receiver (ESP32) - should display:
# HELLO ESP32
```

If this doesn't work, fix your setup before proceeding!

### Step 2: Find Maximum Speed

Use the automated test script:

```bash
cd lab5_3/sender
chmod +x test_speeds.sh
./test_speeds.sh
```

This will test speeds: 1, 5, 10, 12, 15, 18, 20, 22, 25 chars/sec

For each test:
1. Transmits message at that speed
2. You verify if ESP32 received correctly
3. Records pass/fail

**OR** test manually:

```bash
# Test increasing speeds until failure
sudo ./send 1 "test" 10   # Does it work?
sudo ./send 1 "test" 12   # Does it work?
sudo ./send 1 "test" 15   # Does it work?
sudo ./send 1 "test" 18   # Does it work?
sudo ./send 1 "test" 20   # Does it fail?
```

### Step 3: Fine-Tune Around Failure Point

If you fail at 20 chars/sec and succeed at 18:
- Difference: (20-18)/18 = 11% ✅ Good!

If gap is too large, test intermediate values:
```bash
sudo ./send 1 "test" 18    # Pass
sudo ./send 1 "test" 18.5  # Pass?
sudo ./send 1 "test" 19    # Pass?
sudo ./send 1 "test" 19.5  # Fail?
```

### Step 4: Document Results

Record your findings:
```
Test Message: "test" (4 characters)
Hardware: [describe your setup]

Successful speeds:
- 10 chars/sec: ✅ Pass
- 12 chars/sec: ✅ Pass
- 15 chars/sec: ✅ Pass
- 18 chars/sec: ✅ Pass

Failed speeds:
- 20 chars/sec: ❌ Fail (wrong characters)
- 22 chars/sec: ❌ Fail (no detection)

Maximum reliable: 18 chars/sec
First failure: 20 chars/sec
Difference: (20-18)/18 = 11.1% ✅
```

---

## Optimization Tips

### To Increase Speed:

**Hardware:**
1. Use digital comparator circuit (biggest improvement!)
2. Reduce LED-to-photodiode distance (careful: min 1mm)
3. Use brighter LED or higher current (with proper resistor!)
4. Shield from ambient light
5. Use faster photodiode (check response time spec)

**Software (Sender):**
1. Already optimized with nanosleep and -O3 compilation
2. Reduce system load (close other programs)
3. Use real-time priority: `sudo nice -n -20 ./send ...`

**Software (Receiver):**
1. Adjust tolerance if getting errors: `DEFAULT_TOLERANCE`
2. Reduce queue size if overwhelmed
3. Update timing parameters to match sender
4. Use dedicated core for ISR processing

### Common Issues:

**"Pulse duration out of range" warnings:**
- Receiver timing doesn't match sender
- Update `DEFAULT_DOT_US` in receiver code
- Increase `DEFAULT_TOLERANCE`

**Wrong characters decoded:**
- Speed too fast for hardware
- Photodiode too slow
- Ambient light interference
- Try reducing speed slightly

**No detection:**
- Check wiring
- Verify GPIO pin number
- Test with slow speed first (1 char/sec)

---

## Performance Analysis

### Expected Results:

| Hardware Setup | Expected Max Speed |
|----------------|-------------------|
| Photodiode + ADC (Lab 5.2) | 1-5 chars/sec |
| Photodiode + GPIO | 10-15 chars/sec |
| Photodiode + Comparator + GPIO | 15-25 chars/sec |
| Optimized circuit | 25+ chars/sec |

### Transmission Time Requirements:

Lab requirement: "hello ESP32" < 10 seconds

| Speed | Time for "hello ESP32" (10 chars) |
|-------|----------------------------------|
| 1 c/s | 10.0 seconds |
| 5 c/s | 2.0 seconds ✅ |
| 10 c/s | 1.0 second ✅ |
| 15 c/s | 0.67 seconds ✅ |
| 20 c/s | 0.50 seconds ✅ |

---

## Submission Requirements

Document in your report:

1. **Hardware setup description:**
   - Photodiode type/model
   - Circuit diagram
   - LED-photodiode distance
   - Any optimizations

2. **Test results table:**
   - Message tested
   - Speeds tested (pass/fail)
   - Maximum reliable speed
   - First failing speed
   - Speed difference percentage

3. **Evidence:**
   - Console output from sender showing actual speed
   - Console output from receiver showing decoded message
   - Photo of your setup

4. **Analysis:**
   - What limited your maximum speed?
   - What optimizations did you try?
   - How could speed be improved further?

---

## Files to Submit

```
lab5_3/
├── sender/
│   ├── send.c
│   ├── send (compiled binary)
│   └── Makefile
└── receiver/
    ├── CMakeLists.txt
    ├── main/
    │   ├── CMakeLists.txt
    │   └── lab5_3.c
    └── sdkconfig.defaults
```

Plus your `report.pdf` with methodology and results!

---

## Good Luck! 🚀

Aim high - the top 5 get extra credit!
