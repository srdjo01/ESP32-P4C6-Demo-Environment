"""
uart_panel.py — UART send/receive panel for CN3 and CN4

The user types text, clicks "Send", and the response (echo or reply) is shown.
Data is base64-encoded/decoded transparently.
"""

from __future__ import annotations
import base64
from PyQt6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
    QTextEdit, QLineEdit, QComboBox, QGroupBox,
)
from PyQt6.QtCore import Qt
from protocol.commands import cmd_uart_send, cmd_uart_recv


class UartPanel(QWidget):
    def __init__(self, connection, parent=None):
        super().__init__(parent)
        self._conn = connection
        self._build_ui()

    def _build_ui(self):
        layout = QVBoxLayout(self)
        layout.setAlignment(Qt.AlignmentFlag.AlignTop)

        info = QLabel(
            "Send and receive data on CN3 (UART1, GPIO4/5) and CN4 (UART2).\n"
            "Use a loopback wire or external device on the connector.\n"
            "Data is text-encoded; non-printable bytes shown as hex escapes."
        )
        info.setWordWrap(True)
        layout.addWidget(info)

        # Port selector
        port_row = QHBoxLayout()
        port_row.addWidget(QLabel("Port:"))
        self._port_combo = QComboBox()
        self._port_combo.addItem("CN3 (port 3)", 3)
        self._port_combo.addItem("CN4 (port 4)", 4)
        self._port_combo.setFixedWidth(150)
        port_row.addWidget(self._port_combo)
        port_row.addStretch()
        layout.addLayout(port_row)

        # Send group
        send_box = QGroupBox("Send")
        send_layout = QVBoxLayout(send_box)
        self._send_edit = QLineEdit()
        self._send_edit.setPlaceholderText("Type text to send…")
        self._send_edit.returnPressed.connect(self._do_send)
        btn_send = QPushButton("Send")
        btn_send.clicked.connect(self._do_send)
        send_row = QHBoxLayout()
        send_row.addWidget(self._send_edit)
        send_row.addWidget(btn_send)
        send_layout.addLayout(send_row)
        layout.addWidget(send_box)

        # Receive group
        recv_box = QGroupBox("Receive")
        recv_layout = QVBoxLayout(recv_box)
        recv_ctrl = QHBoxLayout()
        btn_recv = QPushButton("Receive (200 ms)")
        btn_recv.clicked.connect(self._do_recv)
        btn_clear = QPushButton("Clear")
        btn_clear.clicked.connect(lambda: self._recv_log.clear())
        recv_ctrl.addWidget(btn_recv)
        recv_ctrl.addWidget(btn_clear)
        recv_ctrl.addStretch()
        recv_layout.addLayout(recv_ctrl)
        self._recv_log = QTextEdit()
        self._recv_log.setReadOnly(True)
        self._recv_log.setMaximumHeight(200)
        recv_layout.addWidget(self._recv_log)
        layout.addWidget(recv_box)

    def _current_port(self) -> int:
        return self._port_combo.currentData()

    def _do_send(self):
        if not self._conn.is_connected:
            return
        text = self._send_edit.text()
        if not text:
            return
        data = (text + "\r\n").encode("utf-8")
        resp = self._conn.send_command(cmd_uart_send(self._current_port(), data))
        if resp and resp.get("status") == "ok":
            self._recv_log.append(f"[TX → port {self._current_port()}] {text}")

    def _do_recv(self):
        if not self._conn.is_connected:
            return
        resp = self._conn.send_command(
            cmd_uart_recv(self._current_port(), timeout_ms=200), timeout=2.0
        )
        if not resp or resp.get("status") != "ok":
            return
        raw = base64.b64decode(resp.get("data_b64", ""))
        n   = resp.get("bytes", 0)
        if n == 0:
            self._recv_log.append(f"[RX ← port {self._current_port()}] (no data)")
        else:
            text = "".join(
                chr(b) if 32 <= b < 127 else f"\\x{b:02X}" for b in raw.rstrip(b"\r\n")
            )
            self._recv_log.append(f"[RX ← port {self._current_port()}] {text}")
