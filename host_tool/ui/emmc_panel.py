"""
emmc_panel.py — eMMC write/read/verify test panel

The user selects a bus frequency, optionally adjusts the data size,
then runs the test.  Results show pass/fail per phase and duration.
"""

from __future__ import annotations
from PyQt6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
    QComboBox, QSpinBox, QGroupBox, QTableWidget, QTableWidgetItem,
    QHeaderView, QProgressBar,
)
from PyQt6.QtCore import Qt, QThread, pyqtSignal
from protocol.commands import cmd_emmc_test


FREQ_OPTIONS = [
    ("400 kHz  (init speed)",   400),
    ("4 MHz",                   4000),
    ("10 MHz",                  10000),
    ("20 MHz  (default)",       20000),
    ("40 MHz  (high speed)",    40000),
    ("52 MHz  (max HS)",        52000),
]


class _TestWorker(QThread):
    finished = pyqtSignal(dict)

    def __init__(self, conn, freq_khz: int, size_kb: int):
        super().__init__()
        self._conn     = conn
        self._freq_khz = freq_khz
        self._size_kb  = size_kb

    def run(self):
        resp = self._conn.send_command(
            cmd_emmc_test(self._freq_khz, self._size_kb),
            timeout=300.0
        )
        self.finished.emit(resp or {})


class EmmcPanel(QWidget):
    def __init__(self, connection, parent=None):
        super().__init__(parent)
        self._conn   = connection
        self._worker = None
        self._build_ui()

    def _build_ui(self):
        layout = QVBoxLayout(self)
        layout.setAlignment(Qt.AlignmentFlag.AlignTop)

        info = QLabel(
            "Write a known test pattern to the eMMC, read it back, and verify "
            "data integrity.\nTest multiple bus frequencies to confirm reliable "
            "operation at each clock speed."
        )
        info.setWordWrap(True)
        layout.addWidget(info)

        # Controls
        ctrl_box = QGroupBox("Test parameters")
        ctrl_layout = QHBoxLayout(ctrl_box)
        ctrl_layout.addWidget(QLabel("Bus frequency:"))
        self._freq_combo = QComboBox()
        for label, value in FREQ_OPTIONS:
            self._freq_combo.addItem(label, value)
        self._freq_combo.setCurrentIndex(4)  # default 40 MHz
        ctrl_layout.addWidget(self._freq_combo)
        ctrl_layout.addWidget(QLabel("  Data size (KB):"))
        self._size_spin = QSpinBox()
        self._size_spin.setRange(1, 4096)
        self._size_spin.setValue(64)
        ctrl_layout.addWidget(self._size_spin)
        self._btn_run = QPushButton("Run test")
        self._btn_run.clicked.connect(self._run_test)
        ctrl_layout.addWidget(self._btn_run)
        ctrl_layout.addStretch()
        layout.addWidget(ctrl_box)

        # Progress
        self._progress = QProgressBar()
        self._progress.setRange(0, 0)
        self._progress.setVisible(False)
        layout.addWidget(self._progress)

        # Results table
        results_box = QGroupBox("Results")
        results_layout = QVBoxLayout(results_box)
        self._table = QTableWidget(0, 5)
        self._table.setHorizontalHeaderLabels(
            ["Frequency", "Size (KB)", "Write", "Read+Verify", "Duration (ms)"]
        )
        self._table.horizontalHeader().setSectionResizeMode(
            QHeaderView.ResizeMode.Stretch
        )
        self._table.setEditTriggers(QTableWidget.EditTrigger.NoEditTriggers)
        results_layout.addWidget(self._table)
        btn_clear = QPushButton("Clear results")
        btn_clear.clicked.connect(lambda: self._table.setRowCount(0))
        results_layout.addWidget(btn_clear)
        layout.addWidget(results_box)

    def _run_test(self):
        if not self._conn.is_connected:
            return
        freq_khz = self._freq_combo.currentData()
        size_kb  = self._size_spin.value()
        self._btn_run.setEnabled(False)
        self._progress.setVisible(True)
        self._worker = _TestWorker(self._conn, freq_khz, size_kb)
        self._worker.finished.connect(self._on_result)
        self._worker.start()

    def _on_result(self, resp: dict):
        self._btn_run.setEnabled(True)
        self._progress.setVisible(False)
        if not resp:
            return

        freq_khz    = resp.get("freq_khz", 0)
        size_kb     = resp.get("size_kb",  0)
        write_ok    = resp.get("write_ok",  False)
        verify_ok   = resp.get("verify_ok", False)
        duration_ms = resp.get("duration_ms", 0)

        row = self._table.rowCount()
        self._table.insertRow(row)

        # Format frequency label
        if freq_khz >= 1000:
            freq_str = f"{freq_khz // 1000} MHz"
        else:
            freq_str = f"{freq_khz} kHz"

        def cell(text, ok=None):
            item = QTableWidgetItem(text)
            item.setTextAlignment(Qt.AlignmentFlag.AlignCenter)
            if ok is True:
                item.setBackground(__import__("PyQt6.QtGui", fromlist=["QColor"]).QColor("#2ecc71"))
            elif ok is False:
                item.setBackground(__import__("PyQt6.QtGui", fromlist=["QColor"]).QColor("#e74c3c"))
            return item

        self._table.setItem(row, 0, cell(freq_str))
        self._table.setItem(row, 1, cell(str(size_kb)))
        self._table.setItem(row, 2, cell("PASS" if write_ok  else "FAIL", write_ok))
        self._table.setItem(row, 3, cell("PASS" if verify_ok else "FAIL", verify_ok))
        self._table.setItem(row, 4, cell(str(duration_ms)))
