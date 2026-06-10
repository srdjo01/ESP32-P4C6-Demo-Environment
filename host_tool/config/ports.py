"""
ports.py — alias-aware enumeration of serial ports, plus a reusable
QComboBox subclass that other panels can drop in.

Behavior is identical on macOS and Windows: pyserial's `comports()` returns
device paths (`/dev/cu.*` or `COM*`) which we use as the canonical key for
aliases.
"""
from __future__ import annotations

from dataclasses import dataclass
from typing import Optional

import serial.tools.list_ports
from PyQt6.QtCore import pyqtSignal
from PyQt6.QtWidgets import QComboBox

from config import settings


@dataclass
class PortInfo:
    device: str               # canonical: "/dev/cu.usbmodem..." or "COM5"
    description: str          # vendor-supplied description
    alias: Optional[str]      # user-supplied friendly name, if any

    @property
    def label(self) -> str:
        return settings.label_for(self.device, self.alias)


def list_ports() -> list[PortInfo]:
    """Return all serial ports, sorted by device path, with aliases attached."""
    out: list[PortInfo] = []
    for p in serial.tools.list_ports.comports():
        out.append(PortInfo(
            device=p.device,
            description=p.description or "",
            alias=settings.alias_for(p.device),
        ))
    out.sort(key=lambda x: x.device.lower())
    return out


class PortCombo(QComboBox):
    """
    Combo box pre-populated with `list_ports()`. The visible text is the
    alias-aware label; the userData is the raw device path.
    """

    selection_changed = pyqtSignal(str)   # emits device path (or "")

    def __init__(self, *, slot: str = "", parent=None) -> None:
        super().__init__(parent)
        self._slot = slot
        self.setMinimumWidth(220)
        self.currentIndexChanged.connect(self._emit_selection)
        self.refresh()

    def refresh(self) -> None:
        prev = self.current_device() or settings.last_port(self._slot)
        self.blockSignals(True)
        self.clear()
        for info in list_ports():
            self.addItem(info.label, info.device)
        if prev:
            idx = self.findData(prev)
            if idx >= 0:
                self.setCurrentIndex(idx)
        self.blockSignals(False)
        self._emit_selection()

    def current_device(self) -> str:
        data = self.currentData()
        return data or ""

    def set_current_device(self, device: str) -> None:
        idx = self.findData(device)
        if idx >= 0:
            self.setCurrentIndex(idx)

    def _emit_selection(self, *_args) -> None:
        dev = self.current_device()
        if self._slot and dev:
            settings.set_last_port(self._slot, dev)
        self.selection_changed.emit(dev)
