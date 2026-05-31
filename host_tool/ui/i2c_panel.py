"""
i2c_panel.py — Live I2C sensor panel

Shows accelerometer (X/Y/Z in m/s²) and RTC (current time) with
continuous auto-refresh.
"""

from __future__ import annotations
from PyQt6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
    QGroupBox, QCheckBox, QTimeEdit, QPushButton, QGridLayout,
)
from PyQt6.QtCore import Qt, QTimer, QTime
from protocol.commands import cmd_i2c_read


class I2cPanel(QWidget):
    def __init__(self, connection, parent=None):
        super().__init__(parent)
        self._conn = connection
        self._timer = QTimer(self)
        self._timer.timeout.connect(self._refresh_all)
        self._build_ui()

    def _build_ui(self):
        layout = QVBoxLayout(self)
        layout.setAlignment(Qt.AlignmentFlag.AlignTop)

        info = QLabel(
            "Live sensor readings from the on-board I2C devices.\n"
            "I2C bus 0: SDA = GPIO 31, SCL = GPIO 32."
        )
        info.setWordWrap(True)
        layout.addWidget(info)

        # ── Accelerometer ──
        accel_box = QGroupBox("Accelerometer (addr 0x6A, LSM6DS-family)")
        accel_grid = QGridLayout(accel_box)
        for col, label in enumerate(("Axis", "Value (m/s²)")):
            accel_grid.addWidget(QLabel(f"<b>{label}</b>"), 0, col)

        self._accel_labels: dict[str, QLabel] = {}
        for row, axis in enumerate(("X", "Y", "Z"), start=1):
            accel_grid.addWidget(QLabel(axis), row, 0)
            lbl = QLabel("—")
            self._accel_labels[axis] = lbl
            accel_grid.addWidget(lbl, row, 1)

        btn_read_accel = QPushButton("Read once")
        btn_read_accel.clicked.connect(self._refresh_accel)
        accel_grid.addWidget(btn_read_accel, 4, 0, 1, 2)
        layout.addWidget(accel_box)

        # ── RTC ──
        rtc_box = QGroupBox("RTC — PCF8563 (addr 0x51)")
        rtc_layout = QVBoxLayout(rtc_box)
        self._rtc_label = QLabel("—")
        self._rtc_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        font = self._rtc_label.font()
        font.setPointSize(16)
        self._rtc_label.setFont(font)
        rtc_layout.addWidget(self._rtc_label)

        btn_read_rtc = QPushButton("Read once")
        btn_read_rtc.clicked.connect(self._refresh_rtc)
        rtc_layout.addWidget(btn_read_rtc)
        layout.addWidget(rtc_box)

        # ── Auto-refresh ──
        auto_row = QHBoxLayout()
        self._cb_auto = QCheckBox("Auto-refresh every 500 ms")
        self._cb_auto.toggled.connect(self._toggle_auto)
        auto_row.addWidget(self._cb_auto)
        auto_row.addStretch()
        layout.addLayout(auto_row)

    def _toggle_auto(self, checked: bool):
        if checked:
            self._timer.start(500)
        else:
            self._timer.stop()

    def _refresh_all(self):
        self._refresh_accel()
        self._refresh_rtc()

    def _refresh_accel(self):
        if not self._conn.is_connected:
            return
        resp = self._conn.send_command(cmd_i2c_read("accel"), timeout=2.0)
        if not resp or resp.get("status") != "ok":
            return
        for axis in ("X", "Y", "Z"):
            val = resp.get(axis.lower(), None)
            lbl = self._accel_labels[axis]
            lbl.setText(f"{val:+.3f}" if val is not None else "—")

    def _refresh_rtc(self):
        if not self._conn.is_connected:
            return
        resp = self._conn.send_command(cmd_i2c_read("rtc"), timeout=2.0)
        if not resp or resp.get("status") != "ok":
            return
        y = resp.get("year",   0)
        mo = resp.get("month",  0)
        d = resp.get("day",    0)
        h = resp.get("hour",   0)
        mi = resp.get("minute", 0)
        s = resp.get("second", 0)
        self._rtc_label.setText(
            f"{y:04d}-{mo:02d}-{d:02d}  {h:02d}:{mi:02d}:{s:02d}"
        )
