#!/usr/bin/env python3
"""
Lab 7.2 Server - Receives POST requests from ESP32
Run this on your laptop or phone that's connected to the same WiFi network
"""

from http.server import BaseHTTPRequestHandler, HTTPServer
import datetime

class Lab72Handler(BaseHTTPRequestHandler):
    def do_POST(self):
        # Get the content length and read the POST data
        content_length = int(self.headers.get('Content-Length', 0))
        post_data = self.rfile.read(content_length).decode('utf-8')

        # Log the received data with timestamp
        timestamp = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        print(f"\n[{timestamp}] Received POST request:")
        print(f"Client: {self.client_address[0]}:{self.client_address[1]}")
        print(f"Data: {post_data}")
        print("-" * 60)

        # Send response back to ESP32
        self.send_response(200)
        self.send_header('Content-type', 'text/plain')
        self.end_headers()
        response = f"OK - Received: {post_data}"
        self.wfile.write(response.encode())

    def log_message(self, format, *args):
        # Suppress default logging to keep output clean
        pass

def run_server(port=1234):
    server_address = ('0.0.0.0', port)
    httpd = HTTPServer(server_address, Lab72Handler)
    print(f"Lab 7.2 Server starting on port {port}")
    print(f"Waiting for POST requests from ESP32...")
    print(f"Press Ctrl+C to stop\n")
    print("=" * 60)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\n\nServer stopped")
        httpd.server_close()

if __name__ == '__main__':
    run_server()
