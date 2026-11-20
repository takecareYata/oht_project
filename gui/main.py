
from socket import socket
import sys
import struct
from datetime import datetime
from unittest import result
from PyQt6.QtWidgets import QLineEdit

from PyQt6.QtCore import QTimer
from PyQt6.QtNetwork import (
    QTcpServer, QTcpSocket, QHostAddress, QNetworkInterface
)
from PyQt6.QtWidgets import (
    QApplication, QMainWindow, QWidget,
    QVBoxLayout, QHBoxLayout, QLabel,
    QPushButton, QGroupBox, QTextEdit,
    QSpinBox
)


# 임시 TCP 프레임 규격
# C struct: uint16_t, padding 2, uint32_t,
#           uint32_t, uint16_t, uint16_t
# Jetson TStcp.h 기준: 14바이트, Big-Endian
FRAME_FORMAT = ">HIIHH"
FRAME_SIZE = struct.calcsize(FRAME_FORMAT)


def make_command_frame(msgid, payload):
    # CRC 제외 12바이트 생성
    data = struct.pack(
        ">HIIH",
        msgid,
        4,
        payload,
        0
    )

    # Jetson CRC: 앞 12바이트 합계의 하위 16비트
    crc = sum(data) & 0xFFFF

    return data + struct.pack(">H", crc)

def make_start_frame():
    return struct.pack(
        FRAME_FORMAT,
        0x0010,  # msgid: START 명령
        4,       # len: payload 4바이트
        1,       # payload: START
        0,       # reserved
        0        # crc: 임시값
    )

def get_local_ips():
    ips = []

    for interface in QNetworkInterface.allInterfaces():
        for entry in interface.addressEntries():
            ip = entry.ip().toString()

            if ":" not in ip and not ip.startswith("127."):
                if ip not in ips:
                    ips.append(ip)

    return ips


class OHTMonitor(QMainWindow):

    def __init__(self):
        super().__init__()

        self.setWindowTitle("OHT / LOAD PORT - TCP Server")
        self.resize(950, 650)

        self.server = QTcpServer(self)
        self.client = None
        self.rx_buffer = bytearray()

        self.server.newConnection.connect(
            self.on_new_connection
        )

        central = QWidget()
        self.setCentralWidget(central)
        layout = QVBoxLayout(central)

            # TCP 서버 설정
        network = QGroupBox("TCP Server Settings")
        net_layout = QHBoxLayout(network)

        # 내 PC IP
        self.ip_input = QLineEdit()
        self.ip_input.setText("10.10.15.32")
        self.ip_input.setPlaceholderText("Local Server IP")

        # 서버 포트
        self.port_input = QSpinBox()
        self.port_input.setRange(1, 65535)
        self.port_input.setValue(5000)

        # Jetson IP
        self.jetson_ip_input = QLineEdit()
        self.jetson_ip_input.setText("10.10.15.63")
        self.jetson_ip_input.setPlaceholderText("Allowed Jetson IP")

        # 서버 버튼
        self.start_btn = QPushButton("START SERVER")
        self.start_btn.clicked.connect(self.start_server)

        self.stop_btn = QPushButton("STOP SERVER")
        self.stop_btn.clicked.connect(self.stop_server)
        self.stop_btn.setEnabled(False)

        net_layout.addWidget(QLabel("Local IP:"))
        net_layout.addWidget(self.ip_input)

        net_layout.addWidget(QLabel("Local Port:"))
        net_layout.addWidget(self.port_input)

        net_layout.addWidget(QLabel("Jetson IP:"))
        net_layout.addWidget(self.jetson_ip_input)

        net_layout.addWidget(self.start_btn)
        net_layout.addWidget(self.stop_btn)

        layout.addWidget(network)

        # 연결 상태
        self.connection = QLabel("SERVER: STOPPED")
        layout.addWidget(self.connection)

        self.client_label = QLabel("Jetson: NOT CONNECTED")
        layout.addWidget(self.client_label)

        # 장치 상태
        device_layout = QHBoxLayout()
        
        # 연결 상태
        self.connection = QLabel("SERVER: STOPPED")
        layout.addWidget(self.connection)

        self.client_label = QLabel("Jetson: NOT CONNECTED")
        layout.addWidget(self.client_label)

        # 장치 상태
        device_layout = QHBoxLayout()

        for name in [
            "X Axis MCU", "Z Axis MCU", "Load Port MCU"
        ]:
            group = QGroupBox(name)
            group_layout = QVBoxLayout(group)
            group_layout.addWidget(QLabel("Status: OFFLINE"))
            group_layout.addWidget(QLabel("Position: --"))
            device_layout.addWidget(group)

        layout.addLayout(device_layout)

        # OHT 상태
        self.state_label = QLabel("Transfer State: IDLE")
        layout.addWidget(self.state_label)

        e84_group = QGroupBox("E84 PIO Status")
        e84_layout = QHBoxLayout(e84_group)

        for signal in ["VALID", "READY", "BUSY", "COMPT"]:
            e84_layout.addWidget(QLabel(f"{signal}: OFF"))

        layout.addWidget(e84_group)

        # 기존 제어 버튼
        control_group = QGroupBox("OHT Control")
        control_layout = QHBoxLayout(control_group)

        for command in [
            "HOME", "START", "STOP", "RESET FAULT"
        ]:
            button = QPushButton(command)
            button.clicked.connect(
                lambda checked=False, cmd=command:
                self.send_command(cmd)
            )
            control_layout.addWidget(button)

        layout.addWidget(control_group)

        # TCP 테스트 버튼
        test_group = QGroupBox("TCP Frame Test")
        test_layout = QVBoxLayout(test_group)

        test_layout.addWidget(
            QLabel("MSG ID: 0x0000 | LEN: 4 | PAYLOAD: 0")
        )

        self.test_btn = QPushButton("SEND TEST 0x00")
        self.test_btn.setEnabled(False)
        self.test_btn.clicked.connect(self.send_test_frame)

        test_layout.addWidget(self.test_btn)
        layout.addWidget(test_group)

        # 로그
        self.log = QTextEdit()
        self.log.setReadOnly(True)
        layout.addWidget(self.log)

        self.write_log("OHT GUI TCP Server initialized.")

    def write_log(self, message):
        now = datetime.now().strftime("%H:%M:%S")
        self.log.append(f"[{now}] {message}")

    def start_server(self):
        ip = self.ip_input.text().strip()
        port = self.port_input.value()

        address = QHostAddress()

        if not address.setAddress(ip):
            self.write_log("[ERROR] Invalid Local IP")
            return

        success = self.server.listen(address, port)

        if not success:
            self.write_log(
                f"[ERROR] {self.server.errorString()}"
            )
            return

        self.connection.setText(
            f"SERVER: LISTENING {ip}:{port}"
        )

        self.start_btn.setEnabled(False)
        self.stop_btn.setEnabled(True)

        # 서버 실행 중에는 설정 변경 불가
        self.ip_input.setEnabled(False)
        self.port_input.setEnabled(False)
        self.jetson_ip_input.setEnabled(False)

        self.write_log(
            f"[SERVER] Listening on {ip}:{port}"
        )

    def stop_server(self):
        self.server.close()

        if self.client is not None:
            self.client.abort()
            self.client = None

        self.rx_buffer.clear()

        self.connection.setText("SERVER: STOPPED")
        self.client_label.setText("Jetson: NOT CONNECTED")
        self.test_btn.setEnabled(False)

        self.start_btn.setEnabled(True)
        self.stop_btn.setEnabled(False)
        self.port_input.setEnabled(True)

        self.write_log("[SERVER] Stopped")

    def on_new_connection(self):
        while self.server.hasPendingConnections():
            socket = self.server.nextPendingConnection()
            
                        # 허용된 Jetson IP만 연결
            allowed_ip = self.jetson_ip_input.text().strip()
            client_ip = socket.peerAddress().toString()

            if client_ip != allowed_ip:
                self.write_log(
                    f"[TCP] Rejected client: {client_ip}"
                )
                socket.abort()
                socket.deleteLater()
                continue

            if self.client is not None:
                # 테스트에서는 동시 접속 1개만 허용
                socket.abort()
                socket.deleteLater()
                self.write_log(
                    "[SERVER] Extra connection rejected"
                )
                continue

            self.client = socket
            self.rx_buffer.clear()

            socket.readyRead.connect(
                lambda s=socket: self.on_received(s)
            )
            socket.disconnected.connect(
                lambda s=socket: self.on_disconnected(s)
            )

            ip = socket.peerAddress().toString()
            port = socket.peerPort()

            self.client_label.setText(
                f"Jetson: CONNECTED ({ip}:{port})"
            )
            self.test_btn.setEnabled(True)

            self.write_log(
                f"[TCP] Client connected: {ip}:{port}"
            )

    def on_disconnected(self, socket):
        if socket is not self.client:
            return

        self.write_log("[TCP] Client disconnected")
        self.client_label.setText("Jetson: NOT CONNECTED")
        self.test_btn.setEnabled(False)

        self.rx_buffer.clear()
        self.client = None
        socket.deleteLater()

    def send_test_frame(self):
        if self.client is None or (
            self.client.state() !=
            QTcpSocket.SocketState.ConnectedState
        ):
            self.write_log("[ERROR] No TCP client")
            return

        frame = make_test_frame()
        result = self.client.write(frame)

        if result == -1:
            self.write_log(
                f"[ERROR] {self.client.errorString()}"
            )
            return

        self.write_log("[TX] MSG ID: 0x0000")
        self.write_log(
            f"[TX HEX] {frame.hex(' ').upper()}"
        )
        self.write_log(f"[TX] Queued: {result} bytes")

    def on_received(self, socket):

        if socket is not self.client:
            return

        data = bytes(socket.readAll())

        if not data:
            return

        self.rx_buffer.extend(data)

        while len(self.rx_buffer) >= 2:

            msgid = struct.unpack(
                ">H", self.rx_buffer[:2]
            )[0]

            # STATUS는 30바이트, 나머지는 14바이트
            if msgid == 0x0013:
                frame_size = 30
            else:
                frame_size = 14

            if len(self.rx_buffer) < frame_size:
                break

            frame = bytes(
                self.rx_buffer[:frame_size]
            )
            del self.rx_buffer[:frame_size]

            self.write_log(
                f"[RX HEX] {frame.hex(' ').upper()}"
            )

            # CRC 확인
            received_crc = struct.unpack(
                ">H", frame[-2:]
            )[0]

            calculated_crc = (
                sum(frame[:-2]) & 0xFFFF
            )

            if received_crc != calculated_crc:
                self.write_log(
                    "[RX ERROR] CRC mismatch"
                )
                continue

            if msgid == 0x0013:
                values = struct.unpack(
                    ">H I 5I H H", frame
                )

                _, length, state, x_axis, z_axis, e84, fault, reserved, crc = values

                self.write_log(
                    f"[STATE] state={state}, "
                    f"X={x_axis}, Z={z_axis}, "
                    f"E84={e84}, fault={fault}"
                )
                continue

            msgid, length, payload, reserved, crc = (
                struct.unpack(FRAME_FORMAT, frame)
            )

            self.write_log(
                f"[RX] MSGID=0x{msgid:04X}, "
                f"LEN={length}, PAYLOAD={payload}"
            )

            if msgid == 0x0011:
                if payload == 1:
                    self.write_log(
                        "[COMMAND] ACCEPTED"
                    )
                elif payload == 2:
                    self.write_log(
                        "[COMMAND] REJECTED"
                    )
                else:
                    self.write_log(
                        f"[COMMAND] RESULT={payload}"
                    )

            elif msgid == 0x0015:
                self.write_log(
                    f"[COMPLETE] command={payload}"
                )

            elif msgid == 0xFFFF:
                self.write_log(
                    f"[ALARM] fault={payload}"
                )

            elif msgid == 0x0000:
                self.write_log(
                    "[LINK TEST] Response received"
                )
                
    def send_command(self, command):

        if command == "START":
            frame = make_command_frame(0x0010, 1)

        elif command == "STOP":
            frame = make_command_frame(0x0020, 0)

        else:
            self.write_log(f"[INFO] {command} not implemented")
            return

        if self.client is None or (
            self.client.state() !=
            QTcpSocket.SocketState.ConnectedState
        ):
            self.write_log("[ERROR] Jetson not connected")
            return

        result = self.client.write(frame)

        if result == -1:
            self.write_log(
                f"[ERROR] {self.client.errorString()}"
            )
            return

        self.write_log(f"[TX] {command}")
        self.write_log(
            f"[TX HEX] {frame.hex(' ').upper()}"
        )
        self.write_log(
            f"[TX] Queued: {result}/{len(frame)} bytes"
        )
        
    def closeEvent(self, event):
        self.stop_server()
        super().closeEvent(event)


if __name__ == "__main__":
    app = QApplication(sys.argv)

    window = OHTMonitor()
    window.show()

    sys.exit(app.exec())
