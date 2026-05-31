"""
main_window.py — Main application window

Layout
──────
  Top bar:  COM port selector  [Refresh]  [Connect / Disconnect]  status indicator
  Tab bar:  GPIO | Ign/Ilum | UART | I2C Sensors | eMMC | Wi-Fi | Display | USB Serial Log
"""

from __future__ import annotations
from PyQt6.QtWidgets import (
    QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QLabel, QPushButton, QComboBox, QTabWidget, QStatusBar,
)
from PyQt6.QtCore import Qt
from PyQt6.QtGui import QFont

from protocol.board_connection import BoardConnection
from ui.gpio_panel       import GpioPanel
from ui.ign_ilum_panel   import IgnIlumPanel
from ui.uart_panel       import UartPanel
from ui.i2c_panel        import I2cPanel
from ui.emmc_panel       import EmmcPanel
from ui.wifi_panel       import WifiPanel
from ui.display_panel    import DisplayPanel
from ui.usb_serial_panel import UsbSerialPanel


class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("ESP32-P4C6 Demo — Board Verification Tool")
        self.resize(920, 700)

        self._conn = BoardConnection(self)
        self._conn.connected_changed.connect(self._on_connection_changed)
        self._conn.message_received.connect(self._on_message)

        self._build_ui()
        self._refresh_ports()

    # ── UI construction ─────────────────────────────────────────────────

    def _build_ui(self):
        root = QWidget()
        self.setCentralWidget(root)
        root_layout = QVBoxLayout(root)
        root_layout.setContentsMargins(8, 8, 8, 8)
        root_layout.setSpacing(6)

        root_layout.addWidget(self._build_connection_bar())
        root_layout.addWidget(self._build_tabs())

        self.setStatusBar(QStatusBar())
        self._set_status("Disconnected")

    def _build_connection_bar(self) -> QWidget:
        bar = QWidget()
        layout = QHBoxLayout(bar)
        layout.setContentsMargins(0, 0, 0, 0)

        layout.addWidget(QLabel("COM port:"))
        self._port_combo = QComboBox()
        self._port_combo.setMinimumWidth(160)
        layout.addWidget(self._port_combo)

        btn_refresh = QPushButton("Refresh")
        btn_refresh.setFixedWidth(70)
        btn_refresh.clicked.connect(self._refresh_ports)
        layout.addWidget(btn_refresh)

        layout.addWidget(QLabel("Baud:"))
        self._baud_combo = QComboBox()
        for b in ["115200", "230400", "460800", "921600"]:
            self._baud_combo.addItem(b, int(b))
        self._baud_combo.setFixedWidth(90)
        layout.addWidget(self._baud_combo)

        self._btn_connect = QPushButton("Connect")
        self._btn_connect.setFixedWidth(100)
        self._btn_connect.clicked.connect(self._toggle_connection)
        layout.addWidget(self._btn_connect)

        self._status_dot = QLabel("●")
        self._status_dot.setFont(QFont("Arial", 14))
        self._status_dot.setStyleSheet("color: #e74c3c;")
        layout.addWidget(self._status_dot)

        self._conn_label = QLabel("Disconnected")
        layout.addWidget(self._conn_label)

        layout.addStretch()

        btn_ping = QPushButton("Ping board")
        btn_ping.setFixedWidth(90)
        btn_ping.clicked.connect(self._do_ping)
        layout.addWidget(btn_ping)

        return bar

    def _build_tabs(self) -> QTabWidget:
        tabs = QTabWidget()
        tabs.addTab(GpioPanel(self._conn),        "GPIO")
        tabs.addTab(IgnIlumPanel(self._conn),      "Ign / Ilum")
        tabs.addTab(UartPanel(self._conn),         "UART")
        tabs.addTab(I2cPanel(self._conn),          "I2C Sensors")
        tabs.addTab(EmmcPanel(self._conn),         "eMMC")
        tabs.addTab(WifiPanel(self._conn),         "Wi-Fi")
        tabs.addTab(DisplayPanel(self._conn),      "Display")
        tabs.addTab(UsbSerialPanel(self._conn),    "USB Serial Log")
        return tabs

    # ── Connection management ───────────────────────────────────────────

    def _refresh_ports(self):
        current = self._port_combo.currentText()
        self._port_combo.clear()
        for p in BoardConnection.list_ports():
            self._port_combo.addItem(p)
        # Restore previous selection if still present
        idx = self._port_combo.findText(current)
        if idx >= 0:
            self._port_combo.setCurrentIndex(idx)

    def _toggle_connection(self):
        if self._conn.is_connected:
            self._conn.close()
        else:
            port = self._port_combo.currentText()
            baud = self._baud_combo.currentData()
            if not port:
                self._set_status("No port selected")
                return
            if not self._conn.open(port, baud):
                self._set_status(f"Failed to open {port}")

    def _on_connection_changed(self, connected: bool):
        if connected:
            self._btn_connect.setText("Disconnect")
            self._status_dot.setStyleSheet("color: #2ecc71;")
            self._conn_label.setText(f"Connected — {self._port_combo.currentText()}")
            self._set_status(f"Connected to {self._port_combo.currentText()}")
        else:
            self._btn_connect.setText("Connect")
            self._status_dot.setStyleSheet("color: #e74c3c;")
            self._conn_label.setText("Disconnected")
            self._set_status("Disconnected")

    def _on_message(self, msg: dict):
        if msg.get("event") == "ready":
            fw  = msg.get("firmware", "?")
            ver = msg.get("version",  "?")
            self._set_status(f"Board ready — {fw} v{ver}")

    def _do_ping(self):
        if not self._conn.is_connected:
            self._set_status("Not connected")
            return
        resp = self._conn.send_command({"cmd": "ping"}, timeout=3.0)
        if resp and resp.get("status") == "ok":
            uptime = resp.get("uptime_ms", 0)
            self._set_status(
                f"Ping OK — firmware {resp.get('firmware','?')} "
                f"v{resp.get('version','?')}  uptime {uptime // 1000} s"
            )
        else:
            self._set_status("Ping failed — no response")

    def _set_status(self, text: str):
        sb = self.statusBar()
        if sb:
            sb.showMessage(text)

    def closeEvent(self, event):
        self._conn.close()
        super().closeEvent(event)
