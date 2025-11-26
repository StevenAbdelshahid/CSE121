# Lab 7.3 - Complete Weather Station

This lab integrates GET and POST requests to create a complete weather station:
1. ESP32 GETs location from server
2. ESP32 GETs outdoor weather from wttr.in for that location
3. ESP32 reads temperature data (simulated for ESP-IDF v5.1.6 compatibility)
4. ESP32 POSTs both temperatures to server

## Step 1: Configure Server Location

Edit `server.py` line 12 to set your desired location:
```python
LOCATION = "Santa-Cruz"  # Change to your city
```

You can use any location that wttr.in supports (e.g., "San-Francisco", "New-York", "London")

## Step 2: Set up the Server

1. Connect your laptop to the same WiFi network as your ESP32 (your phone hotspot)
2. Find your device's IP address:
   - Linux/Mac: `ifconfig` or `ip addr show`
   - Windows: `ipconfig`
   - Look for WiFi interface IP (e.g., 192.168.43.1)

3. Run the server:
```bash
cd lab7_3
python3 server.py
```

The server will:
- Listen on port 1234
- Respond to GET /location with your configured location
- Log POST requests with weather data

## Step 3: Configure ESP32

1. Edit `main/http_request_example_main.c`
2. Update line 29 with your server's IP:
```c
#define SERVER_IP "192.168.43.1"  // Replace with YOUR IP
```

3. Configure WiFi:
```bash
cd lab7_3
idf.py menuconfig
```
- Go to **Example Connection Configuration**
- Set WiFi SSID (your phone's hotspot name)
- Set WiFi Password
- Save and exit

## Step 4: Build and Flash

```bash
idf.py build
idf.py flash monitor
```

## Expected Output

**ESP32 Console:**
```
Step 1: Getting location from server...
Server location: Santa-Cruz
Step 2: Getting weather for Santa-Cruz from wttr.in...
Outdoor temperature: +15°C
Step 3: Reading ESP32 temperature sensor...
ESP32 sensor temperature: 52.34°C
Step 4: Posting data to server...

=== Weather Station Summary ===
Location: Santa-Cruz
Outdoor: +15°C
ESP32: 52.34°C
```

**Server Console:**
```
GET /location
Sending location: Santa-Cruz

POST /
Weather Station Data:
Location: Santa-Cruz
Outdoor Temperature: +15°C
ESP32 Sensor Temperature: 52.34 C
```

## Testing

You can test the server manually:
```bash
# Test GET
curl http://YOUR_SERVER_IP:1234/location

# Test POST
curl -X POST http://YOUR_SERVER_IP:1234/ -d "Test data"
```

## Notes

- The cycle repeats every 10 seconds
- Both ESP32 and server log all information
- Make sure all devices are on the same network
- Uses simulated temperature sensor (20-30°C) for ESP-IDF v5.1.6 compatibility
