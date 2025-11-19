# Lab 6.1 - Ultrasonic Range Finder with Temperature Compensation

## Overview
This lab implements an ultrasonic distance measurement system using the RCWL-1601 (SR04-compatible) ultrasonic sensor with temperature compensation for accurate readings.

## Hardware
- **ESP32-C3 Development Board**
- **RCWL-1601 Ultrasonic Sensor** (or HC-SR04 compatible)

## Pin Configuration
- **Ultrasonic Sensor:**
  - Trigger Pin: GPIO4
  - Echo Pin: GPIO5
  - VCC: 3.3V
  - GND: GND

## Features
1. **Temperature-Compensated Distance Measurement**
   - Uses fixed 23°C temperature for speed of sound calculation
   - Speed of sound formula: v = 331.4 + 0.606 * T (m/s)
   - Simple implementation - no external temp sensor needed

2. **Multi-Sample Median Filtering**
   - Takes 5 measurements per reading
   - Uses median value to reduce noise and improve accuracy

3. **Accurate Timing**
   - Uses `esp_timer_get_time()` for microsecond-precision timing
   - Generates precise 10µs trigger pulse

## Output Format
```
Distance: 12.5 cm at 23C
Distance: 11.8 cm at 23C
Distance: 12.1 cm at 23C
```

## Accuracy
- Measurement range: 10-20 cm (as specified in lab requirements)
- Expected error: < 2 cm
- Update rate: 1 Hz (once per second)

## Building and Flashing
```bash
cd lab6_1
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

## References
- [HC-SR04 Tutorial](https://lastminuteengineers.com/arduino-sr04-ultrasonic-sensor-tutorial)
- [Adafruit Ultrasonic Sensor Datasheet](https://media.digikey.com/pdf/Data%20Sheets/Adafruit%20PDFs/4007_Web.pdf)
