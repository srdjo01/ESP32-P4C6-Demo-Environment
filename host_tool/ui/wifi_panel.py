"""
wifi_panel.py — Wi-Fi panel (scan, connect, ping)

Wi-Fi is provided by the ESP32-C6 co-processor. The P4 proxies these commands
to the C6 over a direct UART link, so this panel uses the normal P4 connection.
"""

from __future__ import annotations
from PyQt6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
    QLineEdit, QGroupBox, QTableWidget, QTableWidgetItem,
    QHeaderView, QProgressBar,
)
from PyQt6.QtCore import Qt, QThread, pyqtSignal
from protocol.commands import (
    cmd_wifi_scan, cmd_wifi_connect, cmd_wifi_disconnect, cmd_wifi_ping
)


class _Worker(QThread):
    finished = pyqtSignal(dict)
    def __init__(self, conn, command: dict, timeout: float = 15.0):
        super().__init__()
        self._conn    = conn
        self._command = command
        self._timeout = timeout
    def run(self):
        resp = self._conn.send_command(self._command, timeout=self._timeout)
        self.finished.emit(resp or {})


class WifiPanel(QWidget):
    def __init__(self, connection, parent=None):
        super().__init__(parent)
        self._conn   = connection
        self._worker = None
        self._build_ui()

    def _build_ui(self):
        layout = QVBoxLayout(self)
        layout.setAlignment(Qt.AlignmentFlag.AlignTop)

        info = QLabel(
            "Wi-Fi is provided by the ESP32-C6 co-processor. The P4 forwards these\n"
            "commands to the C6 over a direct UART link, so just connect to the P4\n"
            "in the top bar, then scan / connect / ping below."
        )
        info.setWordWrap(True)
        layout.addWidget(info)

        self._progress = QProgressBar()
        self._progress.setRange(0, 0)
        self._progress.setVisible(False)
        layout.addWidget(self._progress)

        # ── Scan ──
        scan_box = QGroupBox("Network scan")
        scan_layout = QVBoxLayout(scan_box)
        scan_ctrl = QHBoxLayout()
        btn_scan = QPushButton("Scan for networks")
        btn_scan.clicked.connect(self._do_scan)
        scan_ctrl.addWidget(btn_scan)
        self._scan_status = QLabel("")
        scan_ctrl.addWidget(self._scan_status)
        scan_ctrl.addStretch()
        scan_layout.addLayout(scan_ctrl)
        self._scan_table = QTableWidget(0, 3)
        self._scan_table.setHorizontalHeaderLabels(["SSID", "RSSI (dBm)", "Auth"])
        self._scan_table.horizontalHeader().setSectionResizeMode(
            QHeaderView.ResizeMode.Stretch
        )
        self._scan_table.setEditTriggers(QTableWidget.EditTrigger.NoEditTriggers)
        self._scan_table.setMaximumHeight(200)
        self._scan_table.cellDoubleClicked.connect(self._pick_ssid)
        scan_layout.addWidget(self._scan_table)
        layout.addWidget(scan_box)

        # ── Connect ──
        conn_box = QGroupBox("Connect to network")
        conn_layout = QVBoxLayout(conn_box)
        ssid_row = QHBoxLayout()
        ssid_row.addWidget(QLabel("SSID:"))
        self._ssid_edit = QLineEdit()
        ssid_row.addWidget(self._ssid_edit)
        conn_layout.addLayout(ssid_row)
        pass_row = QHBoxLayout()
        pass_row.addWidget(QLabel("Password:"))
        self._pass_edit = QLineEdit()
        self._pass_edit.setEchoMode(QLineEdit.EchoMode.Password)
        pass_row.addWidget(self._pass_edit)
        conn_layout.addLayout(pass_row)
        btn_row = QHBoxLayout()
        btn_connect = QPushButton("Connect")
        btn_connect.clicked.connect(self._do_connect)
        btn_disconnect = QPushButton("Disconnect")
        btn_disconnect.clicked.connect(self._do_disconnect)
        btn_row.addWidget(btn_connect)
        btn_row.addWidget(btn_disconnect)
        btn_row.addStretch()
        conn_layout.addLayout(btn_row)
        self._conn_status = QLabel("Not connected")
        conn_layout.addWidget(self._conn_status)
        layout.addWidget(conn_box)

        # ── Ping ──
        ping_box = QGroupBox("Ping")
        ping_layout = QHBoxLayout(ping_box)
        ping_layout.addWidget(QLabel("Host:"))
        self._ping_edit = QLineEdit("8.8.8.8")
        self._ping_edit.setFixedWidth(200)
        ping_layout.addWidget(self._ping_edit)
        btn_ping = QPushButton("Ping")
        btn_ping.clicked.connect(self._do_ping)
        ping_layout.addWidget(btn_ping)
        self._ping_result = QLabel("")
        ping_layout.addWidget(self._ping_result)
        ping_layout.addStretch()
        layout.addWidget(ping_box)

    def _busy(self, state: bool):
        self._progress.setVisible(state)

    def _do_scan(self):
        if not self._conn.is_connected:
            self._scan_status.setText("Not connected")
            self._scan_status.setStyleSheet("color: #e74c3c;")
            return
        self._busy(True)
        self._scan_status.setText("Scanning…")
        self._scan_status.setStyleSheet("")
        self._worker = _Worker(self._conn, cmd_wifi_scan(), timeout=15.0)
        self._worker.finished.connect(self._on_scan_result)
        self._worker.start()

    def _on_scan_result(self, resp: dict):
        self._busy(False)
        if not resp:
            self._scan_status.setText("No response — is the ESP32-C6 powered/flashed?")
            self._scan_status.setStyleSheet("color: #e74c3c;")
            return
        if resp.get("status") != "ok":
            self._scan_status.setText("Error: " + resp.get("message", "scan failed"))
            self._scan_status.setStyleSheet("color: #e74c3c;")
            return
        networks = resp.get("networks", [])
        self._scan_table.setRowCount(0)
        for net in networks:
            row = self._scan_table.rowCount()
            self._scan_table.insertRow(row)
            self._scan_table.setItem(row, 0, QTableWidgetItem(net.get("ssid", "")))
            self._scan_table.setItem(row, 1, QTableWidgetItem(str(net.get("rssi", 0))))
            self._scan_table.setItem(row, 2, QTableWidgetItem(net.get("auth", "")))
        self._scan_status.setText(f"{len(networks)} network(s) found")
        self._scan_status.setStyleSheet("color: #2ecc71;")

    def _pick_ssid(self, row: int, _col: int):
        item = self._scan_table.item(row, 0)
        if item:
            self._ssid_edit.setText(item.text())

    def _do_connect(self):
        if not self._conn.is_connected:
            return
        ssid = self._ssid_edit.text().strip()
        if not ssid:
            return
        self._busy(True)
        self._worker = _Worker(
            self._conn,
            cmd_wifi_connect(ssid, self._pass_edit.text()),
            timeout=15.0,
        )
        self._worker.finished.connect(self._on_connect_result)
        self._worker.start()

    def _on_connect_result(self, resp: dict):
        self._busy(False)
        if resp and resp.get("status") == "ok":
            ip = resp.get("ip", "?")
            self._conn_status.setText(f"Connected — IP: {ip}")
            self._conn_status.setStyleSheet("color: green;")
        else:
            self._conn_status.setText("Connection failed")
            self._conn_status.setStyleSheet("color: red;")

    def _do_disconnect(self):
        if not self._conn.is_connected:
            return
        self._conn.send_raw(cmd_wifi_disconnect())
        self._conn_status.setText("Disconnected")
        self._conn_status.setStyleSheet("")

    def _do_ping(self):
        if not self._conn.is_connected:
            return
        host = self._ping_edit.text().strip()
        if not host:
            return
        self._busy(True)
        self._worker = _Worker(self._conn, cmd_wifi_ping(host), timeout=8.0)
        self._worker.finished.connect(self._on_ping_result)
        self._worker.start()

    def _on_ping_result(self, resp: dict):
        self._busy(False)
        if resp and resp.get("status") == "ok":
            ms = resp.get("latency_ms", "?")
            self._ping_result.setText(f"✓  {ms} ms")
            self._ping_result.setStyleSheet("color: green;")
        else:
            self._ping_result.setText("✗  failed")
            self._ping_result.setStyleSheet("color: red;")
