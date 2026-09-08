import cv2
import numpy as np
import socket
import argparse
import threading
import http.server
import socketserver
import configparser
import os

IMAGE_WIDTH = 320
IMAGE_HEIGHT = 240

latest_frame = None
frame_count = 0

def load_config():
    config = configparser.ConfigParser()
    config_path = os.path.join(os.path.dirname(__file__), 'config.ini')
    
    if os.path.exists(config_path):
        config.read(config_path)
        return {
            'esp_ip': config.get('network', 'esp_ip', fallback='YOUR_ESP32_IP'),
            'udp_port': config.getint('network', 'udp_port', fallback=9090),
            'http_port': config.getint('network', 'http_port', fallback=8080)
        }
    return {
        'esp_ip': 'YOUR_ESP32_IP',
        'udp_port': 9090,
        'http_port': 8080
    }

def udp_receiver(port, esp_ip):
    global latest_frame, frame_count
    
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(('0.0.0.0', port))
    sock.settimeout(5.0)

    print(f"UDP receiver listening on port {port}")
    print(f"Format: JPEG, Resolution: {IMAGE_WIDTH}x{IMAGE_HEIGHT}")
    
    if esp_ip and esp_ip != 'YOUR_ESP32_IP':
        sock.sendto(b"START", (esp_ip, port))
        print(f"Sent START command to {esp_ip}:{port}")

    bytes_data = bytes()
    first_frame = True
    jpeg_start_found = False

    while True:
        try:
            data, addr = sock.recvfrom(4096)
            
            if not jpeg_start_found:
                soi_pos = data.find(b'\xff\xd8')
                if soi_pos != -1:
                    jpeg_start_found = True
                    bytes_data = data[soi_pos:]
            else:
                bytes_data += data
            
            if jpeg_start_found:
                eoi_pos = bytes_data.rfind(b'\xff\xd9')
                if eoi_pos != -1:
                    frame_data = bytes_data[:eoi_pos + 2]
                    bytes_data = bytes_data[eoi_pos + 2:]
                    
                    latest_frame = frame_data
                    frame_count += 1
                    
                    if first_frame:
                        print(f"First frame received! Size: {len(frame_data)} bytes")
                        first_frame = False

        except socket.timeout:
            print("UDP timeout")
        except Exception as e:
            print(f"UDP error: {e}")
            import traceback
            traceback.print_exc()

class MJPEGHandler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == '/stream':
            self.send_response(200)
            self.send_header('Content-Type', 'multipart/x-mixed-replace; boundary=frame')
            self.end_headers()
            
            while True:
                frame_data = latest_frame
                if frame_data is not None:
                    try:
                        self.wfile.write(b'--frame\r\n')
                        self.wfile.write(b'Content-Type: image/jpeg\r\n')
                        self.wfile.write(b'Content-Length: ' + str(len(frame_data)).encode() + b'\r\n')
                        self.wfile.write(b'\r\n')
                        self.wfile.write(frame_data)
                        self.wfile.write(b'\r\n')
                    except Exception:
                        break
                else:
                    import time
                    time.sleep(0.05)
        elif self.path == '/':
            html = """
            <!DOCTYPE html>
            <html>
            <head>
                <title>ESP32 Camera</title>
                <style>
                    body { margin: 0; background: #000; display: flex; justify-content: center; align-items: center; height: 100vh; }
                    img { max-width: 100%; max-height: 100%; }
                </style>
            </head>
            <body>
                <img src="/stream" />
            </body>
            </html>
            """
            self.send_response(200)
            self.send_header('Content-Type', 'text/html')
            self.end_headers()
            self.wfile.write(html.encode())
        else:
            self.send_response(404)
            self.end_headers()
    
    def log_message(self, format, *args):
        pass

def http_server(port):
    print(f"HTTP server running on http://localhost:{port}")
    print(f"Open http://localhost:{port}/ in your browser")
    
    with socketserver.TCPServer(('0.0.0.0', port), MJPEGHandler) as httpd:
        httpd.serve_forever()

def cv2_viewer(args):
    global frame_count
    
    display_frame_count = 0
    while True:
        frame_data = latest_frame
        if frame_data is not None:
            try:
                img = cv2.imdecode(np.frombuffer(frame_data, dtype=np.uint8), cv2.IMREAD_COLOR)
                
                if img is not None:
                    display_frame_count += 1
                    img = cv2.resize(img, (640, 480), interpolation=cv2.INTER_LINEAR)
                    h, w = img.shape[:2]
                    cv2.putText(img, f"Frame: {display_frame_count}", (w - 280, 40),
                                cv2.FONT_HERSHEY_SIMPLEX, 0.9, (255, 255, 255), 3)
                    cv2.putText(img, "Press ESC to quit", (w - 280, 80),
                                cv2.FONT_HERSHEY_SIMPLEX, 0.9, (255, 255, 255), 3)
                    cv2.imshow('ESP32 Camera', img)
            except Exception as e:
                print(f"Decode error: {e}")
        
        key = cv2.waitKey(1) & 0xFF
        if key == 27:
            break
    
    cv2.destroyAllWindows()

def main():
    default_config = load_config()
    
    parser = argparse.ArgumentParser(description='ESP32 Camera UDP Viewer (JPEG)')
    parser.add_argument('-i', '--ip', type=str, default=default_config['esp_ip'], help='ESP32 IP address')
    parser.add_argument('-p', '--port', type=int, default=default_config['udp_port'], help='UDP port')
    parser.add_argument('-hp', '--http-port', type=int, default=default_config['http_port'], help='HTTP server port')
    args = parser.parse_args()

    print("=====================================")
    print("ESP32 Camera UDP Viewer + HTTP Stream")
    print("Format: JPEG, Resolution: 320x240")
    print("UDP simple chunk (4096 bytes/chunk)")
    print("=====================================")
    print()

    udp_thread = threading.Thread(target=udp_receiver, args=(args.port, args.ip), daemon=True)
    udp_thread.start()

    http_thread = threading.Thread(target=http_server, args=(args.http_port,), daemon=True)
    http_thread.start()

    print(f"UDP port: {args.port}")
    print(f"HTTP port: {args.http_port}")
    print()
    print("Press ESC in the OpenCV window to quit.")
    print("=====================================")
    print()

    cv2_viewer(args)
    
    print("Exiting...")

if __name__ == "__main__":
    main()