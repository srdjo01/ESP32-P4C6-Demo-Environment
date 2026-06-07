"""
gpio_panel.py — Free GPIO control panel (GPIO 16–19)

Each pin has a "Set HIGH" / "Set LOW" button pair and a live state indicator
that is updated whenever the user reads the pin.

GPIO 14 and 15 are intentionally excluded: they carry the direct UART link to
the ESP32-C6 (Wi-Fi/BLE), so the firmware reserves them and rejects gpio_*
commands for those pins.
"""

from __future__ import annotations
from PyQt6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
    QGroupBox, QGridLayout,
)
from PyQt6.QtCore import Qt
from protocol.commands import cmd_gpio_set, cmd_gpio_get


# GPIO 14/15 carry the ESP32-C6 UART link, so only 16–19 are free for testing
# (must match FREE_GPIO[] in firmware/components/gpio_module/gpio_module.c).
FREE_GPIO_PINS = [16, 17, 18, 19]


class GpioPanel(QWidget):
    def __init__(self, connection, parent=None):
        super().__init__(parent)
        self._conn = connection
        self._state_labels: dict[int, QLabel] = {}
        self._build_ui()

    def _build_ui(self):
        layout = QVBoxLayout(self)
        layout.setAlignment(Qt.AlignmentFlag.AlignTop)

        info = QLabel(
            "Drive and read the free GPIO pins (16–19).\n"
            "These pins are configured as push-pull outputs; "
            "use 'Read' to sample their current level.\n"
            "(GPIO 14/15 are reserved for the ESP32-C6 Wi-Fi/BLE UART link.)"
        )
        info.setWordWrap(True)
        layout.addWidget(info)

        grid_box = QGroupBox("Free GPIO pins")
        grid = QGridLayout(grid_box)
        grid.addWidget(QLabel("<b>Pin</b>"),    0, 0)
        grid.addWidget(QLabel("<b>State</b>"),  0, 1)
        grid.addWidget(QLabel("<b>Control</b>"), 0, 2)

        for row, pin in enumerate(FREE_GPIO_PINS, start=1):
            pin_label  = QLabel(f"GPIO {pin}")
            state_label = QLabel("—")
            state_label.setFixedWidth(60)
            state_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
            self._state_labels[pin] = state_label

            btn_high = QPushButton("HIGH")
            btn_low  = QPushButton("LOW")
            btn_read = QPushButton("Read")
            btn_high.setFixedWidth(60)
            btn_low .setFixedWidth(60)
            btn_read.setFixedWidth(60)

            btn_high.clicked.connect(lambda _, p=pin: self._set(p, 1))
            btn_low .clicked.connect(lambda _, p=pin: self._set(p, 0))
            btn_read.clicked.connect(lambda _, p=pin: self._read(p))

            ctrl_row = QWidget()
            ctrl_layout = QHBoxLayout(ctrl_row)
            ctrl_layout.setContentsMargins(0, 0, 0, 0)
            ctrl_layout.addWidget(btn_high)
            ctrl_layout.addWidget(btn_low)
            ctrl_layout.addWidget(btn_read)

            grid.addWidget(pin_label,  row, 0)
            grid.addWidget(state_label, row, 1)
            grid.addWidget(ctrl_row,   row, 2)

        layout.addWidget(grid_box)

    def _set(self, pin: int, level: int):
        if not self._conn.is_connected:
            return
        resp = self._conn.send_command(cmd_gpio_set(pin, level))
        if resp and resp.get("status") == "ok":
            self._update_label(pin, level)

    def _read(self, pin: int):
        if not self._conn.is_connected:
            return
        resp = self._conn.send_command(cmd_gpio_get(pin))
        if resp and resp.get("status") == "ok":
            self._update_label(pin, resp.get("level", -1))

    def _update_label(self, pin: int, level: int):
        lbl = self._state_labels.get(pin)
        if lbl is None:
            return
        if level == 1:
            lbl.setText("HIGH")
            lbl.setStyleSheet("background:#2ecc71;color:white;border-radius:4px;")
        elif level == 0:
            lbl.setText("LOW")
            lbl.setStyleSheet("background:#e74c3c;color:white;border-radius:4px;")
        else:
            lbl.setText("—")
            lbl.setStyleSheet("")
