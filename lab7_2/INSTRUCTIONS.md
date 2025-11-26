# Lab 7.2 - POST Temperature to Server

This lab reads temperature data (simulated for ESP-IDF v5.1.6 compatibility) and POSTs it to a server.

## Step 1: Set up the Server

1. Connect your laptop/phone to the same WiFi network as your ESP32 (your phone hotspot)
2. Find your device's IP address:
   - On Linux/Mac: `ifconfig` or `ip addr show`
   - On Windows: `ipconfig`
   - Look for your WiFi interface IP (e.g., 192.168.43.1)

3. Run the server:
```bash
cd lab7_2
python3 server.py
```

The server will start listening on port 1234.

## Step 2: Configure ESP32

1. Edit `main/http_request_example_main.c`
2. Update line 26 with your server's IP:
```c
#define SERVER_IP "192.168.43.1"  // Replace with YOUR IP
```

3. Configure WiFi (same as lab7_1):
```bash
idf.py menuconfig
```
- Go to **Example Connection Configuration**
- Set WiFi SSID and Password
- Save and exit

## Step 3: Build and Flash

```bash
idf.py build
idf.py flash monitor
```

## Expected Output

**ESP32 side:**
- Reads simulated temperature sensor
- Logs temperature (e.g., "ESP32 Temperature: 26.34°C")
- Sends POST request to server
- Receives response

**Server side:**
- Displays received POST data
- Shows ESP32 IP and temperature reading
- Example: `ESP32_Temperature: 26.34 C`

## Notes

- Uses simulated temperature sensor (returns values between 20-30°C)
- Requests are sent every 5 seconds
- Make sure both devices are on the same network!
- For ESP-IDF v5.1.6 compatibility, temperature is simulated rather than reading actual hardware sensor
