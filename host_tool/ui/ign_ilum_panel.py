"""
ign_ilum_panel.py — Ignition and Illumination input panel

Reads the 12/24 V digital inputs (level-shifted to 3.3 V on the board).
The panel polls automatically while the "Auto-refresh" checkbox is checked.
"""

from __future__ import annotations
from PyQt6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
    QGroupBox, QCheckBox,
)
from PyQt6.QtCore import Qt, QTimer
from protocol.commands import cmd_ign_ilum_get


class IgnIlumPanel(QWidget):
    def __init__(self, connection, parent=None):
        super().__init__(parent)
        self._conn = connection
        self._timer = QTimer(self)
        self._timer.timeout.connect(self._refresh)
        self._build_ui()

    def _build_ui(self):
        layout = QVBoxLayout(self)
        layout.setAlignment(Qt.AlignmentFlag.AlignTop)

        info = QLabel(
            "Read the 12/24 V digital input lines.\n"
            "GPIO 26 = Illumination,  GPIO 27 = Ignition.\n"
            "The board's level-shifter brings these to 3.3 V logic."
        )
        info.setWordWrap(True)
        layout.addWidget(info)

        group = QGroupBox("Input states")
        grp_layout = QVBoxLayout(group)

        self._ign_label  = self._make_indicator("Ignition  (GPIO 27)", grp_layout)
        self._ilum_label = self._make_indicator("Illumination (GPIO 26)", grp_layout)

        layout.addWidget(group)

        ctrl = QHBoxLayout()
        btn_read = QPushButton("Read once")
        btn_read.clicked.connect(self._refresh)
        self._cb_auto = QCheckBox("Auto-refresh (500 ms)")
        self._cb_auto.toggled.connect(self._toggle_auto)
        ctrl.addWidget(btn_read)
        ctrl.addWidget(self._cb_auto)
        ctrl.addStretch()
        layout.addLayout(ctrl)

    @staticmethod
    def _make_indicator(text: str, parent_layout) -> QLabel:
        row = QWidget()
        row_layout = QHBoxLayout(row)
        row_layout.setContentsMargins(0, 0, 0, 0)
        row_layout.addWidget(QLabel(text))
        lbl = QLabel("—")
        lbl.setFixedWidth(70)
        lbl.setAlignment(Qt.AlignmentFlag.AlignCenter)
        row_layout.addWidget(lbl)
        row_layout.addStretch()
        parent_layout.addWidget(row)
        return lbl

    def _toggle_auto(self, checked: bool):
        if checked:
            self._timer.start(500)
        else:
            self._timer.stop()

    def _refresh(self):
        if not self._conn.is_connected:
            return
        resp = self._conn.send_command(cmd_ign_ilum_get(), timeout=1.0)
        if not resp or resp.get("status") != "ok":
            return
        self._set_indicator(self._ign_label,  resp.get("ignition",     -1))
        self._set_indicator(self._ilum_label, resp.get("illumination", -1))

    @staticmethod
    def _set_indicator(lbl: QLabel, level: int):
        if level == 1:
            lbl.setText("ACTIVE")
            lbl.setStyleSheet("background:#2ecc71;color:white;border-radius:4px;")
        elif level == 0:
            lbl.setText("INACTIVE")
            lbl.setStyleSheet("background:#95a5a6;color:white;border-radius:4px;")
        else:
            lbl.setText("—")
            lbl.setStyleSheet("")
