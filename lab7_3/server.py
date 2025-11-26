#!/usr/bin/env python3
"""
Lab 7.3 Server - Handles both GET and POST requests from ESP32
- GET /location - Returns the configured location
- POST / - Receives weather station data
"""

from http.server import BaseHTTPRequestHandler, HTTPServer
import datetime

# Configure your location here
LOCATION = "Santa-Cruz"

class Lab73Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        """Handle GET requests for location"""
        if self.path == '/location':
            timestamp = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
            print(f"\n[{timestamp}] GET /location")
            print(f"Client: {self.client_address[0]}:{self.client_address[1]}")
            print(f"Sending location: {LOCATION}")
            print("-" * 60)

            self.send_response(200)
            self.send_header('Content-type', 'text/plain')
            self.end_headers()
            self.wfile.write(LOCATION.encode())
        else:
            self.send_response(404)
            self.send_header('Content-type', 'text/plain')
            self.end_headers()
            self.wfile.write(b"Not Found")

    def do_POST(self):
        """Handle POST requests with weather data"""
        # Get the content length and read the POST data
        content_length = int(self.headers.get('Content-Length', 0))
        post_data = self.rfile.read(content_length).decode('utf-8')

        # Log the received data with timestamp
        timestamp = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        print(f"\n[{timestamp}] POST /")
        print(f"Client: {self.client_address[0]}:{self.client_address[1]}")
        print("=" * 60)
        print("Weather Station Data:")
        print(post_data)
        print("=" * 60)

        # Send response back to ESP32
        self.send_response(200)
        self.send_header('Content-type', 'text/plain')
        self.end_headers()
        response = "OK - Data received"
        self.wfile.write(response.encode())

    def log_message(self, format, *args):
        # Suppress default logging to keep output clean
        pass

def run_server(port=1234):
    server_address = ('0.0.0.0', port)
    httpd = HTTPServer(server_address, Lab73Handler)
    print(f"Lab 7.3 Weather Station Server")
    print(f"Starting on port {port}")
    print(f"Configured location: {LOCATION}")
    print(f"\nWaiting for requests from ESP32...")
    print(f"- GET /location will return: {LOCATION}")
    print(f"- POST / will log weather data")
    print(f"\nPress Ctrl+C to stop\n")
    print("=" * 60)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\n\nServer stopped")
        httpd.server_close()

if __name__ == '__main__':
    run_server()
