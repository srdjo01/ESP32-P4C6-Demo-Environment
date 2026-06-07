"""
bluetooth_panel.py — BLE scan panel (ESP32-C6)

Bluetooth is provided by the ESP32-C6 co-processor. This panel runs a passive
BLE scan and lists nearby devices with their signal strength (RSSI).

The P4 proxies the scan command to the C6 over a direct UART link, so this panel
uses the normal P4 connection.
"""

from __future__ import annotations
from PyQt6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
    QGroupBox, QTableWidget, QTableWidgetItem, QHeaderView, QProgressBar,
)
from PyQt6.QtCore import Qt, QThread, pyqtSignal
from PyQt6.QtGui import QColor
from protocol.commands import cmd_ble_scan


def _rssi_color(rssi: int) -> QColor:
    """Green (strong) → yellow → red (weak), matching typical RSSI ranges."""
    if rssi >= -60:
        return QColor("#2ecc71")   # strong
    if rssi >= -75:
        return QColor("#f1c40f")   # medium
    return QColor("#e74c3c")       # weak


class _Worker(QThread):
    finished = pyqtSignal(dict)

    def __init__(self, conn, command: dict, timeout: float = 10.0):
        super().__init__()
        self._conn    = conn
        self._command = command
        self._timeout = timeout

    def run(self):
        resp = self._conn.send_command(self._command, timeout=self._timeout)
        self.finished.emit(resp or {})


class BluetoothPanel(QWidget):
    def __init__(self, connection, parent=None):
        super().__init__(parent)
        self._conn   = connection
        self._worker = None
        self._build_ui()

    def _build_ui(self):
        layout = QVBoxLayout(self)
        layout.setAlignment(Qt.AlignmentFlag.AlignTop)

        info = QLabel(
            "Bluetooth (BLE) is provided by the on-board ESP32-C6 co-processor.\n"
            "The host talks only to the ESP32-P4 (COM port in the top bar); the P4 "
            "forwards the scan to the C6 over the direct inter-chip UART link "
            "(P4 GPIO15/14 ↔ C6 GPIO21/20).\n"
            "Connect to the P4, then run a 3-second passive scan for nearby BLE "
            "devices. Signal strength (RSSI) is colour-coded: green = strong, "
            "yellow = medium, red = weak."
        )
        info.setWordWrap(True)
        layout.addWidget(info)

        self._progress = QProgressBar()
        self._progress.setRange(0, 0)
        self._progress.setVisible(False)
        layout.addWidget(self._progress)

        scan_box = QGroupBox("BLE device scan")
        scan_layout = QVBoxLayout(scan_box)

        ctrl_row = QHBoxLayout()
        btn_scan = QPushButton("Scan for BLE devices")
        btn_scan.clicked.connect(self._do_scan)
        ctrl_row.addWidget(btn_scan)
        self._count_label = QLabel("")
        ctrl_row.addWidget(self._count_label)
        ctrl_row.addStretch()
        scan_layout.addLayout(ctrl_row)

        self._table = QTableWidget(0, 3)
        self._table.setHorizontalHeaderLabels(["Name", "Address", "RSSI (dBm)"])
        self._table.horizontalHeader().setSectionResizeMode(
            QHeaderView.ResizeMode.Stretch
        )
        self._table.setEditTriggers(QTableWidget.EditTrigger.NoEditTriggers)
        scan_layout.addWidget(self._table)
        layout.addWidget(scan_box)

    def _do_scan(self):
        if not self._conn.is_connected:
            self._count_label.setText("Not connected")
            self._count_label.setStyleSheet("color: #e74c3c;")
            return
        self._progress.setVisible(True)
        self._count_label.setText("Scanning… (3 s)")
        self._count_label.setStyleSheet("")
        self._worker = _Worker(self._conn, cmd_ble_scan(), timeout=10.0)
        self._worker.finished.connect(self._on_result)
        self._worker.start()

    def _on_result(self, resp: dict):
        self._progress.setVisible(False)
        if not resp:
            self._count_label.setText("No response — is the ESP32-C6 powered/flashed?")
            self._count_label.setStyleSheet("color: #e74c3c;")
            return
        if resp.get("status") != "ok":
            self._count_label.setText("Error: " + resp.get("message", "scan failed"))
            self._count_label.setStyleSheet("color: #e74c3c;")
            return

        devices = resp.get("devices", [])
        self._table.setRowCount(0)
        # Sort strongest signal first.
        devices.sort(key=lambda d: d.get("rssi", -999), reverse=True)
        for dev in devices:
            row = self._table.rowCount()
            self._table.insertRow(row)
            rssi = dev.get("rssi", 0)
            name_item = QTableWidgetItem(dev.get("name", "(unknown)"))
            addr_item = QTableWidgetItem(dev.get("addr", ""))
            rssi_item = QTableWidgetItem(str(rssi))
            rssi_item.setTextAlignment(Qt.AlignmentFlag.AlignCenter)
            rssi_item.setBackground(_rssi_color(rssi))
            self._table.setItem(row, 0, name_item)
            self._table.setItem(row, 1, addr_item)
            self._table.setItem(row, 2, rssi_item)

        self._count_label.setText(f"{len(devices)} device(s) found")
        self._count_label.setStyleSheet("color: #2ecc71;")
