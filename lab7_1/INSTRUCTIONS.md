# Lab 7.1 - Get Weather from wttr.in

This lab gets the temperature from wttr.in using HTTP GET requests.

## Configuration

Before building, you need to configure your WiFi settings:

```bash
cd lab7_1
idf.py menuconfig
```

Navigate to:
1. **Example Connection Configuration**
2. Set **WiFi SSID** to your phone's hotspot name
3. Set **WiFi Password** to your phone's hotspot password
4. Save and exit (press S, then Q)

## Build and Flash

```bash
idf.py build
idf.py flash monitor
```

## What it does

- Connects to your phone's WiFi hotspot
- Makes an HTTP GET request to wttr.in for Santa Cruz weather
- Displays the temperature in Celsius
- Repeats every 10 seconds

## Expected Output

You should see:
- WiFi connection success
- Temperature reading from wttr.in (e.g., "+15°C")
