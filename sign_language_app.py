import sys
import socket
import cv2
import numpy as np
import configparser
import os
from PyQt5.QtWidgets import (
    QApplication, QMainWindow, QWidget, QLabel, QPushButton,
    QVBoxLayout, QHBoxLayout, QStatusBar, QFrame, QProgressBar
)
from PyQt5.QtCore import QTimer, Qt, QThread, pyqtSignal
from PyQt5.QtGui import QImage, QPixmap, QColor, QPalette


class UdpReceiverThread(QThread):
    frame_received = pyqtSignal(np.ndarray)
    connection_status = pyqtSignal(bool)

    def __init__(self, host, port):
        super().__init__()
        self.host = host
        self.port = port
        self.running = True
        self.buffer_size = 4096

    def run(self):
        try:
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            self.sock.bind((self.host, self.port))
            self.connection_status.emit(True)
        except Exception as e:
            self.connection_status.emit(False)
            return

        bytes_data = bytes()
        jpeg_start_found = False

        while self.running:
            try:
                data, _ = self.sock.recvfrom(self.buffer_size)

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

                        img_array = np.frombuffer(frame_data, dtype=np.uint8)
                        frame = cv2.imdecode(img_array, cv2.IMREAD_COLOR)
                        if frame is not None and frame.size > 0:
                            self.frame_received.emit(frame)
            except Exception:
                pass

    def stop(self):
        self.running = False
        try:
            self.sock.close()
        except:
            pass
        self.wait()


class CameraStreamApp(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("ESP32 Camera Stream")
        self.resize(1000, 650)

        self.load_config()

        self.frame_count = 0
        self.fps = 0.0
        self.last_fps_time = cv2.getTickCount()
        self.current_resolution = "0x0"
        self.is_connected = False
        self.last_frame_time = 0
        self.is_detecting = False

        self.udp_thread = None
        self.timer = QTimer()
        self.timer.timeout.connect(self.update_stats)
        self.detect_timer = QTimer()
        self.detect_timer.timeout.connect(self.check_detect_status)

        self.init_ui()

    def load_config(self):
        config = configparser.ConfigParser()
        config_path = os.path.join(os.path.dirname(__file__), 'config.ini')
        
        if os.path.exists(config_path):
            config.read(config_path)
            self.host = config.get('network', 'host', fallback='0.0.0.0')
            self.port = config.getint('network', 'udp_port', fallback=9090)
            self.detect_timeout = config.getint('network', 'detect_timeout', fallback=60)
        else:
            self.host = '0.0.0.0'
            self.port = 9090
            self.detect_timeout = 60

    def init_ui(self):
        central_widget = QWidget()
        self.setCentralWidget(central_widget)

        self.image_label = QLabel()
        self.image_label.setAlignment(Qt.AlignCenter)
        self.image_label.setMinimumSize(320, 240)

        self.image_frame = QFrame()
        self.image_frame.setStyleSheet("""
            QFrame {
                background: qlineargradient(x1:0, y1:0, x2:0, y2:1, 
                    stop:0 #2a2a2a, stop:1 #1a1a1a);
                border-radius: 12px;
                padding: 10px;
                box-shadow: 0 8px 32px rgba(0, 0, 0, 0.4);
            }
        """)

        self.spinner_label = QLabel()
        self.spinner_label.setAlignment(Qt.AlignCenter)
        self.spinner_angle = 0

        self.detect_text_label = QLabel("AI检测中...")
        self.detect_text_label.setStyleSheet("""
            QLabel {
                color: #44aaff;
                font-weight: bold;
                font-size: 18px;
                margin-top: 15px;
            }
        """)
        self.detect_text_label.setAlignment(Qt.AlignCenter)

        self.detect_progress = QProgressBar()
        self.detect_progress.setStyleSheet("""
            QProgressBar {
                border: none;
                border-radius: 10px;
                height: 10px;
                background-color: rgba(255, 255, 255, 0.2);
                margin: 15px 30px 0 30px;
            }
            QProgressBar::chunk {
                border-radius: 10px;
                background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                    stop:0 #44aaff, stop:1 #ff44aa);
            }
        """)
        self.detect_progress.setRange(0, 100)
        self.detect_progress.setValue(0)
        self.detect_progress.setTextVisible(False)

        loading_layout = QVBoxLayout()
        loading_layout.addWidget(self.spinner_label)
        loading_layout.addWidget(self.detect_text_label)
        loading_layout.addWidget(self.detect_progress)
        loading_layout.setAlignment(Qt.AlignCenter)

        self.loading_widget = QWidget()
        self.loading_widget.setLayout(loading_layout)
        self.loading_widget.setStyleSheet("""
            QWidget {
                background-color: rgba(0, 0, 0, 0.8);
                border-radius: 8px;
            }
        """)
        self.loading_widget.hide()

        image_layout = QVBoxLayout(self.image_frame)
        image_layout.addWidget(self.image_label)
        image_layout.addWidget(self.loading_widget)

        self.status_label = QLabel("等待连接...")
        self.status_label.setStyleSheet("""
            QLabel {
                color: #ff4444;
                font-weight: bold;
                font-size: 14px;
                padding: 5px 10px;
                background-color: rgba(255, 68, 68, 0.1);
                border-radius: 20px;
            }
        """)

        self.fps_label = QLabel("FPS: --")
        self.fps_label.setStyleSheet("""
            QLabel {
                color: #44ff44;
                font-weight: bold;
                font-size: 14px;
                padding: 5px 10px;
                background-color: rgba(68, 255, 68, 0.1);
                border-radius: 20px;
            }
        """)

        self.res_label = QLabel("分辨率: --")
        self.res_label.setStyleSheet("""
            QLabel {
                color: #44aaff;
                font-weight: bold;
                font-size: 14px;
                padding: 5px 10px;
                background-color: rgba(68, 170, 255, 0.1);
                border-radius: 20px;
            }
        """)

        self.frame_label = QLabel("帧数: 0")
        self.frame_label.setStyleSheet("""
            QLabel {
                color: #ffaa44;
                font-weight: bold;
                font-size: 14px;
                padding: 5px 10px;
                background-color: rgba(255, 170, 68, 0.1);
                border-radius: 20px;
            }
        """)

        self.start_btn = QPushButton("开始接收")
        self.start_btn.setStyleSheet("""
            QPushButton {
                background-color: #4CAF50;
                color: white;
                font-weight: bold;
                font-size: 14px;
                padding: 10px 25px;
                border-radius: 8px;
                border: none;
            }
            QPushButton:hover {
                background-color: #45a049;
            }
            QPushButton:pressed {
                background-color: #3d8b40;
            }
        """)
        self.start_btn.clicked.connect(self.start_stream)

        self.stop_btn = QPushButton("停止接收")
        self.stop_btn.setStyleSheet("""
            QPushButton {
                background-color: #f44336;
                color: white;
                font-weight: bold;
                font-size: 14px;
                padding: 10px 25px;
                border-radius: 8px;
                border: none;
            }
            QPushButton:hover {
                background-color: #da190b;
            }
            QPushButton:pressed {
                background-color: #b91509;
            }
        """)
        self.stop_btn.clicked.connect(self.stop_stream)
        self.stop_btn.setEnabled(False)

        top_bar_layout = QHBoxLayout()
        top_bar_layout.addWidget(self.status_label)
        top_bar_layout.addWidget(self.fps_label)
        top_bar_layout.addWidget(self.res_label)
        top_bar_layout.addWidget(self.frame_label)
        top_bar_layout.addStretch()
        top_bar_layout.addWidget(self.start_btn)
        top_bar_layout.addWidget(self.stop_btn)

        main_layout = QVBoxLayout(central_widget)
        main_layout.addLayout(top_bar_layout)
        main_layout.addWidget(self.image_frame)
        main_layout.setSpacing(15)
        main_layout.setContentsMargins(20, 20, 20, 20)

        central_widget.setStyleSheet("""
            QWidget {
                background-color: #1a1a2e;
            }
        """)

        self.set_label_placeholder()

    def set_label_placeholder(self):
        self.image_label.setText("等待ESP32连接...")
        self.image_label.setStyleSheet("""
            QLabel {
                color: #666;
                font-size: 18px;
                background-color: #2a2a2a;
                border-radius: 8px;
            }
        """)

    def start_stream(self):
        if self.udp_thread and self.udp_thread.isRunning():
            return

        self.udp_thread = UdpReceiverThread(self.host, self.port)
        self.udp_thread.frame_received.connect(self.display_frame)
        self.udp_thread.connection_status.connect(self.on_connection_status)
        self.udp_thread.start()

        self.start_btn.setEnabled(False)
        self.stop_btn.setEnabled(True)
        self.timer.start(1000)
        self.detect_timer.start(500)

    def stop_stream(self):
        if self.udp_thread:
            self.udp_thread.stop()
            self.udp_thread = None

        self.start_btn.setEnabled(True)
        self.stop_btn.setEnabled(False)
        self.timer.stop()
        self.detect_timer.stop()
        self.is_connected = False
        self.is_detecting = False
        self.loading_widget.hide()
        self.update_status()
        self.set_label_placeholder()

    def on_connection_status(self, connected):
        self.is_connected = connected
        self.update_status()

    def update_status(self):
        if self.is_connected:
            self.status_label.setText("已连接")
            self.status_label.setStyleSheet("""
                QLabel {
                    color: #44ff44;
                    font-weight: bold;
                    font-size: 14px;
                    padding: 5px 10px;
                    background-color: rgba(68, 255, 68, 0.1);
                    border-radius: 20px;
                }
            """)
        else:
            self.status_label.setText("等待连接...")
            self.status_label.setStyleSheet("""
                QLabel {
                    color: #ff4444;
                    font-weight: bold;
                    font-size: 14px;
                    padding: 5px 10px;
                    background-color: rgba(255, 68, 68, 0.1);
                    border-radius: 20px;
                }
            """)

    def display_frame(self, frame):
        self.last_frame_time = cv2.getTickCount()

        if self.is_detecting:
            self.is_detecting = False
            self.loading_widget.hide()
            self.detect_progress.setValue(0)

        self.frame_count += 1

        h, w = frame.shape[:2]
        self.current_resolution = f"{w}x{h}"

        rgb_frame = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
        bytes_per_line = 3 * w
        qt_image = QImage(rgb_frame.data, w, h, bytes_per_line, QImage.Format_RGB888)

        pixmap = QPixmap.fromImage(qt_image)
        self.image_label.setPixmap(pixmap.scaled(
            self.image_label.size(),
            Qt.KeepAspectRatio,
            Qt.SmoothTransformation
        ))

        self.image_label.setStyleSheet("""
            QLabel {
                background-color: #2a2a2a;
                border-radius: 8px;
            }
        """)

        current_time = cv2.getTickCount()
        elapsed = (current_time - self.last_fps_time) / cv2.getTickFrequency()
        if elapsed >= 1.0:
            self.fps = self.frame_count / elapsed
            self.frame_count = 0
            self.last_fps_time = current_time

    def update_stats(self):
        self.fps_label.setText(f"FPS: {self.fps:.1f}")
        self.res_label.setText(f"分辨率: {self.current_resolution}")
        self.frame_label.setText(f"帧数: {self.frame_count}")

    def check_detect_status(self):
        if not self.is_connected or self.last_frame_time == 0:
            return

        current_time = cv2.getTickCount()
        elapsed = (current_time - self.last_frame_time) / cv2.getTickFrequency()

        if elapsed >= self.detect_timeout:
            self.is_detecting = False
            self.loading_widget.hide()
            self.detect_progress.setValue(0)
            return

        if elapsed >= 2.0 and not self.is_detecting:
            self.is_detecting = True
            self.detect_start_time = current_time
            self.loading_widget.show()

        if self.is_detecting:
            detect_elapsed = (current_time - self.detect_start_time) / cv2.getTickFrequency()

            self.spinner_angle = (self.spinner_angle + 20) % 360
            self.update_spinner()

            progress = min(int((detect_elapsed / self.detect_timeout) * 100), 100)
            self.detect_progress.setValue(progress)

    def update_spinner(self):
        size = 60
        img = np.zeros((size, size, 3), dtype=np.uint8)

        center_x, center_y = size // 2, size // 2
        radius = size // 2 - 4

        for i in range(12):
            angle = (i * 30 + self.spinner_angle) * np.pi / 180
            end_x = int(center_x + radius * np.cos(angle))
            end_y = int(center_y + radius * np.sin(angle))

            brightness = 255 - (i * 20)
            cv2.line(img, (center_x, center_y), (end_x, end_y),
                     (brightness, brightness, brightness), 3)

        rgb_img = cv2.cvtColor(img, cv2.COLOR_BGR2RGB)
        h, w = rgb_img.shape[:2]
        bytes_per_line = 3 * w
        qt_img = QImage(rgb_img.data, w, h, bytes_per_line, QImage.Format_RGB888)
        pixmap = QPixmap.fromImage(qt_img)
        self.spinner_label.setPixmap(pixmap)

    def resizeEvent(self, event):
        if self.image_label.pixmap():
            self.image_label.setPixmap(self.image_label.pixmap().scaled(
                self.image_label.size(),
                Qt.KeepAspectRatio,
                Qt.SmoothTransformation
            ))

    def closeEvent(self, event):
        self.stop_stream()
        event.accept()


if __name__ == "__main__":
    app = QApplication(sys.argv)
    app.setStyle("Fusion")

    palette = QPalette()
    palette.setColor(QPalette.Window, QColor(26, 26, 46))
    palette.setColor(QPalette.WindowText, QColor(255, 255, 255))
    app.setPalette(palette)

    window = CameraStreamApp()
    window.show()
    sys.exit(app.exec_())