# Lab 7 - Weather Station

Complete implementation of Lab 7 using ESP32 as a weather station that communicates with a server.

## Overview

This lab is divided into three parts, each building on the previous one:

- **lab7_1**: ESP32 makes HTTP GET requests to wttr.in for weather data
- **lab7_2**: ESP32 POSTs temperature sensor data to a server
- **lab7_3**: Full integration - ESP32 GETs location, queries weather, and POSTs both temperatures

## Prerequisites

- ESP32 development board
- ESP-IDF installed and configured
- Python 3 (for the servers in lab7_2 and lab7_3)
- WiFi network (phone hotspot works great!)

## Quick Start

### Lab 7.1 - Get Weather (5 points)

Get temperature from wttr.in using HTTP requests.

```bash
cd lab7_1
idf.py menuconfig  # Configure WiFi
idf.py build flash monitor
```

See [lab7_1/INSTRUCTIONS.md](lab7_1/INSTRUCTIONS.md) for details.

### Lab 7.2 - Post to Server (5 points)

Read ESP32 temperature sensor and POST to a server.

```bash
# Terminal 1: Start server
cd lab7_2
python3 server.py

# Terminal 2: Build and flash ESP32
cd lab7_2
idf.py menuconfig  # Configure WiFi and server IP
idf.py build flash monitor
```

See [lab7_2/INSTRUCTIONS.md](lab7_2/INSTRUCTIONS.md) for details.

### Lab 7.3 - Full Integration (10 points)

Complete weather station with GET and POST.

```bash
# Terminal 1: Start server
cd lab7_3
python3 server.py

# Terminal 2: Build and flash ESP32
cd lab7_3
idf.py menuconfig  # Configure WiFi and server IP
idf.py build flash monitor
```

See [lab7_3/INSTRUCTIONS.md](lab7_3/INSTRUCTIONS.md) for details.

## Network Setup (Important!)

All labs require network connectivity:

### Using Phone Hotspot (Recommended)

1. Enable hotspot on your phone
2. Note the SSID and password
3. Connect your laptop to the same hotspot
4. Find your laptop's IP: `ifconfig` or `ip addr show`
5. Configure ESP32 WiFi via `idf.py menuconfig`
6. Update `SERVER_IP` in the code (lab7_2, lab7_3)

### Using Lab WiFi

1. Configure ESP32 to use lab WiFi SSID
2. Find your server's IP on the network
3. Update `SERVER_IP` in the code

## Common Issues

### ESP32 can't connect to WiFi
- Double-check SSID and password in menuconfig
- Make sure hotspot is active
- Try rebooting the ESP32

### ESP32 can't reach server
- Verify both devices are on the same network
- Check `SERVER_IP` is correct in the code
- Try pinging the server from ESP32's network
- Make sure server is running (`python3 server.py`)

### Temperature sensor not working
- The ESP32 has an internal temperature sensor
- It reads chip temperature (typically 40-60°C)
- This is normal - it's not measuring room temperature

## Directory Structure

```
CSE121/
├── lab7_1/               # Lab 7.1 - HTTP GET
│   ├── main/
│   │   └── http_request_example_main.c
│   └── INSTRUCTIONS.md
├── lab7_2/               # Lab 7.2 - HTTP POST
│   ├── main/
│   │   └── http_request_example_main.c
│   ├── server.py
│   └── INSTRUCTIONS.md
├── lab7_3/               # Lab 7.3 - Full Integration
│   ├── main/
│   │   └── http_request_example_main.c
│   ├── server.py
│   └── INSTRUCTIONS.md
└── common_components/    # Shared WiFi connection code
    └── protocol_examples_common/
```

## Submission

Create a zip file containing:
- `lab7_1/` directory
- `lab7_2/` directory
- `lab7_3/` directory
- `report.pdf` with:
  - Screenshots of each lab working
  - Brief description of what each lab does
  - Any challenges you faced

Submit to Gradescope.

## Resources

- [wttr.in documentation](https://github.com/chubin/wttr.in)
- [ESP-IDF HTTP examples](https://github.com/espressif/esp-idf/tree/master/examples/protocols)
- Lab 7 introduction video: https://youtu.be/GIbA3fHAHxY

## Tips

- Start with lab7_1 to verify WiFi works
- Test the Python servers independently using curl
- Use `idf.py monitor` to see ESP32 logs
- Both ESP32 and server log all requests - useful for debugging!
- You can change the location in lab7_3's server.py

Good luck! 🌤️
