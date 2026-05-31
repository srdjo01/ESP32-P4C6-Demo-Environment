"""
usb_serial_panel.py — Raw USB serial log panel

Displays every raw line received from the board (including JSON responses
and unsolicited events).  Useful for debugging and monitoring firmware logs.
"""

from __future__ import annotations
from PyQt6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QPushButton,
    QTextEdit, QCheckBox, QLabel,
)
from PyQt6.QtCore import Qt
from PyQt6.QtGui import QTextCharFormat, QColor, QFont


class UsbSerialPanel(QWidget):
    def __init__(self, connection, parent=None):
        super().__init__(parent)
        self._conn = connection
        self._conn.raw_line_received.connect(self._on_line)
        self._build_ui()

    def _build_ui(self):
        layout = QVBoxLayout(self)

        info = QLabel(
            "Raw stream from the board over USB CDC.\n"
            "Shows all JSON responses, events, and any unstructured log output."
        )
        info.setWordWrap(True)
        layout.addWidget(info)

        self._log = QTextEdit()
        self._log.setReadOnly(True)
        self._log.setFont(QFont("Courier New", 9))
        self._log.setLineWrapMode(QTextEdit.LineWrapMode.NoWrap)
        layout.addWidget(self._log)

        ctrl = QHBoxLayout()
        btn_clear = QPushButton("Clear")
        btn_clear.clicked.connect(self._log.clear)
        self._cb_scroll = QCheckBox("Auto-scroll")
        self._cb_scroll.setChecked(True)
        ctrl.addWidget(btn_clear)
        ctrl.addWidget(self._cb_scroll)
        ctrl.addStretch()
        layout.addLayout(ctrl)

    def _on_line(self, text: str):
        # Colour-code JSON lines vs plain log output
        if text.startswith("{"):
            colour = "#aaffaa" if '"status":"ok"' in text or '"event"' in text else "#ffaaaa"
        else:
            colour = "#cccccc"

        self._log.setTextColor(QColor(colour))
        self._log.append(text)

        if self._cb_scroll.isChecked():
            sb = self._log.verticalScrollBar()
            sb.setValue(sb.maximum())
