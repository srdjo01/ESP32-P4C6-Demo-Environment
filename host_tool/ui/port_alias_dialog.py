"""
port_alias_dialog.py — edit friendly names for detected COM ports.

Shows every port pyserial currently sees plus any alias the user previously
saved (even if that port isn't connected right now), so removing an alias
for a missing device is still possible.
"""
from __future__ import annotations

from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import (
    QDialog, QDialogButtonBox, QHeaderView, QLabel, QPushButton,
    QTableWidget, QTableWidgetItem, QVBoxLayout, QHBoxLayout, QWidget,
)

from config import settings, list_ports


class PortAliasDialog(QDialog):
    COL_DEVICE = 0
    COL_DESC   = 1
    COL_ALIAS  = 2

    def __init__(self, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.setWindowTitle("Edit COM Port Aliases")
        self.resize(620, 360)

        layout = QVBoxLayout(self)
        layout.addWidget(QLabel(
            "Give each port a friendly name so it's easy to pick the right "
            "one. Aliases are saved per device and shown across the app."
        ))

        self._table = QTableWidget(0, 3, self)
        self._table.setHorizontalHeaderLabels(["Device", "Description", "Alias"])
        self._table.verticalHeader().setVisible(False)
        hdr = self._table.horizontalHeader()
        hdr.setSectionResizeMode(self.COL_DEVICE, QHeaderView.ResizeMode.ResizeToContents)
        hdr.setSectionResizeMode(self.COL_DESC,   QHeaderView.ResizeMode.Stretch)
        hdr.setSectionResizeMode(self.COL_ALIAS,  QHeaderView.ResizeMode.Stretch)
        layout.addWidget(self._table)

        # Toolbar row
        bar = QHBoxLayout()
        btn_refresh = QPushButton("Refresh")
        btn_refresh.clicked.connect(self._populate)
        btn_clear = QPushButton("Clear All Aliases")
        btn_clear.clicked.connect(self._clear_all)
        bar.addWidget(btn_refresh)
        bar.addWidget(btn_clear)
        bar.addStretch()
        layout.addLayout(bar)

        buttons = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Ok |
            QDialogButtonBox.StandardButton.Cancel
        )
        buttons.accepted.connect(self.accept)
        buttons.rejected.connect(self.reject)
        layout.addWidget(buttons)

        self._populate()

    # ── Table population ──────────────────────────────────────────────────

    def _populate(self) -> None:
        # Merge live ports with any previously-saved aliases for missing devices.
        live = {p.device: p for p in list_ports()}
        rows: list[tuple[str, str, str]] = []
        for dev, info in live.items():
            rows.append((dev, info.description, info.alias or ""))
        for dev, alias in settings.port_aliases.items():
            if dev not in live:
                rows.append((dev, "(not connected)", alias))
        rows.sort(key=lambda r: r[0].lower())

        self._table.setRowCount(len(rows))
        for row, (dev, desc, alias) in enumerate(rows):
            dev_item  = QTableWidgetItem(dev)
            desc_item = QTableWidgetItem(desc)
            for it in (dev_item, desc_item):
                it.setFlags(it.flags() & ~Qt.ItemFlag.ItemIsEditable)
            self._table.setItem(row, self.COL_DEVICE, dev_item)
            self._table.setItem(row, self.COL_DESC,   desc_item)
            self._table.setItem(row, self.COL_ALIAS,  QTableWidgetItem(alias))

    def _clear_all(self) -> None:
        for row in range(self._table.rowCount()):
            self._table.item(row, self.COL_ALIAS).setText("")

    # ── Result ────────────────────────────────────────────────────────────

    def accept(self) -> None:
        mapping: dict[str, str] = {}
        for row in range(self._table.rowCount()):
            dev   = self._table.item(row, self.COL_DEVICE).text()
            alias = self._table.item(row, self.COL_ALIAS).text()
            if alias.strip():
                mapping[dev] = alias.strip()
        settings.replace_aliases(mapping)
        settings.save()
        super().accept()
