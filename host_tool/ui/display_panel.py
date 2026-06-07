"""
display_panel.py — Display control panel

Two modes:
  - Colour bars test pattern (eight horizontal bands)
  - User-supplied text centred on the 466×466 round display
"""

from __future__ import annotations
from PyQt6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
    QLineEdit, QGroupBox, QTextEdit,
)
from PyQt6.QtCore import Qt
from protocol.commands import cmd_display_pattern, cmd_display_text, cmd_display_clear


class DisplayPanel(QWidget):
    def __init__(self, connection, parent=None):
        super().__init__(parent)
        self._conn = connection
        self._build_ui()

    def _build_ui(self):
        layout = QVBoxLayout(self)
        layout.setAlignment(Qt.AlignmentFlag.AlignTop)

        info = QLabel(
            "Control the on-board 466×466 MIPI DSI round display (CO5300 controller).\n"
            "⚠  Board #5: display requires a new FPC cable and adapter fix before use."
        )
        info.setWordWrap(True)
        layout.addWidget(info)

        # ── Test pattern ──
        pattern_box = QGroupBox("Colour-bar test pattern")
        pattern_layout = QVBoxLayout(pattern_box)
        pattern_layout.addWidget(QLabel(
            "Fills the screen with eight horizontal colour bars "
            "(white, yellow, cyan, green, magenta, red, blue, black)."
        ))
        btn_pattern = QPushButton("Show test pattern")
        btn_pattern.clicked.connect(self._do_pattern)
        pattern_layout.addWidget(btn_pattern)
        layout.addWidget(pattern_box)

        # ── Text ──
        text_box = QGroupBox("Display text")
        text_layout = QVBoxLayout(text_box)
        text_layout.addWidget(QLabel("Enter text to show on the display (UTF-8, max 256 chars):"))
        self._text_edit = QLineEdit()
        self._text_edit.setPlaceholderText("Hello, ESP32-P4C6!")
        self._text_edit.returnPressed.connect(self._do_text)
        text_layout.addWidget(self._text_edit)
        btn_text = QPushButton("Show text")
        btn_text.clicked.connect(self._do_text)
        text_layout.addWidget(btn_text)
        layout.addWidget(text_box)

        # ── Clear ──
        btn_clear = QPushButton("Clear display (fill black)")
        btn_clear.clicked.connect(self._do_clear)
        layout.addWidget(btn_clear)

        # ── Status ──
        self._status = QLabel("")
        layout.addWidget(self._status)

    def _do_pattern(self):
        if not self._conn.is_connected:
            return
        resp = self._conn.send_command(cmd_display_pattern())
        if resp and resp.get("status") == "ok":
            self._status.setText("✓  Test pattern displayed")
            self._status.setStyleSheet("color: green;")
        else:
            self._status.setText("⚠  Display not connected")
            self._status.setStyleSheet("color: #e67e22;")

    def _do_text(self):
        if not self._conn.is_connected:
            return
        text = self._text_edit.text()
        if not text:
            return
        resp = self._conn.send_command(cmd_display_text(text[:256]))
        if resp and resp.get("status") == "ok":
            self._status.setText(f'✓  Displaying: "{text[:40]}"')
            self._status.setStyleSheet("color: green;")
        else:
            self._status.setText("⚠  Display not connected")
            self._status.setStyleSheet("color: #e67e22;")

    def _do_clear(self):
        if not self._conn.is_connected:
            return
        resp = self._conn.send_command(cmd_display_clear())
        if resp and resp.get("status") == "ok":
            self._status.setText("Display cleared")
            self._status.setStyleSheet("")
        else:
            self._status.setText("⚠  Display not connected")
            self._status.setStyleSheet("color: #e67e22;")
