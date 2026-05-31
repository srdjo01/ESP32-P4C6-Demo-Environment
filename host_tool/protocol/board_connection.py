"""
board_connection.py — serial port management and JSON framing.

BoardConnection runs in a QThread.  It owns the serial port, reads lines,
parses them as JSON, and emits signals that the UI panels connect to.

Usage
─────
    conn = BoardConnection()
    conn.message_received.connect(my_handler)   # dict per JSON line
    conn.connected_changed.connect(on_connect)
    conn.open("COM3", 115200)
    ...
    response = conn.send_command({"cmd": "ping"}, timeout=2.0)
    conn.close()
"""

from __future__ import annotations

import json
import threading
import time
from typing import Optional

import serial
import serial.tools.list_ports

from PyQt6.QtCore import QObject, QThread, pyqtSignal


class _ReaderThread(QThread):
    """Background thread that reads lines from the serial port."""

    line_received = pyqtSignal(str)

    def __init__(self, port: serial.Serial, parent: QObject | None = None):
        super().__init__(parent)
        self._port   = port
        self._stop   = threading.Event()

    def stop(self):
        self._stop.set()

    def run(self):
        buf = b""
        while not self._stop.is_set():
            try:
                if self._port.in_waiting:
                    buf += self._port.read(self._port.in_waiting)
                else:
                    time.sleep(0.005)
                    continue
                while b"\n" in buf:
                    line, buf = buf.split(b"\n", 1)
                    text = line.strip().decode("utf-8", errors="replace")
                    if text:
                        self.line_received.emit(text)
            except serial.SerialException:
                break
            except Exception:
                pass


class BoardConnection(QObject):
    """
    Manages the serial connection to the ESP32-P4C6 board.

    Signals
    ───────
    message_received(dict)   — emitted for every valid JSON line from the board
    raw_line_received(str)   — emitted for every raw text line (for the log panel)
    connected_changed(bool)  — emitted when connection state changes
    """

    message_received  = pyqtSignal(dict)
    raw_line_received = pyqtSignal(str)
    connected_changed = pyqtSignal(bool)

    def __init__(self, parent: QObject | None = None):
        super().__init__(parent)
        self._port:   Optional[serial.Serial] = None
        self._reader: Optional[_ReaderThread] = None
        self._lock    = threading.Lock()
        # Pending synchronous request waiting for a matching response.
        self._pending_cmd:  Optional[str]   = None
        self._pending_resp: Optional[dict]  = None
        self._pending_evt   = threading.Event()

    # ── Connection management ───────────────────────────────────────────

    def open(self, port_name: str, baud: int = 115200) -> bool:
        try:
            p = serial.Serial(port_name, baud, timeout=0)
        except serial.SerialException as e:
            return False
        with self._lock:
            self._port = p
        self._reader = _ReaderThread(p, self)
        self._reader.line_received.connect(self._on_line)
        self._reader.start()
        self.connected_changed.emit(True)
        return True

    def close(self):
        if self._reader:
            self._reader.stop()
            self._reader.wait(2000)
            self._reader = None
        with self._lock:
            if self._port and self._port.is_open:
                self._port.close()
            self._port = None
        self.connected_changed.emit(False)

    @property
    def is_connected(self) -> bool:
        with self._lock:
            return self._port is not None and self._port.is_open

    # ── Sending ─────────────────────────────────────────────────────────

    def send_raw(self, obj: dict):
        """Send a JSON command without waiting for a response."""
        line = json.dumps(obj, separators=(",", ":")) + "\n"
        with self._lock:
            if self._port and self._port.is_open:
                self._port.write(line.encode())

    def send_command(self, obj: dict, timeout: float = 5.0) -> Optional[dict]:
        """
        Send a command and block until the matching response arrives or timeout.
        Returns the response dict, or None on timeout / disconnection.
        """
        cmd = obj.get("cmd")
        with self._lock:
            self._pending_cmd  = cmd
            self._pending_resp = None
            self._pending_evt.clear()
        self.send_raw(obj)
        self._pending_evt.wait(timeout)
        with self._lock:
            resp = self._pending_resp
            self._pending_cmd  = None
            self._pending_resp = None
        return resp

    # ── Receiving ───────────────────────────────────────────────────────

    def _on_line(self, text: str):
        self.raw_line_received.emit(text)
        try:
            obj = json.loads(text)
        except json.JSONDecodeError:
            return
        self.message_received.emit(obj)

        # Wake up a blocking send_command() if this is its response.
        with self._lock:
            if self._pending_cmd and obj.get("cmd") == self._pending_cmd:
                self._pending_resp = obj
                self._pending_evt.set()

    # ── Port enumeration ─────────────────────────────────────────────────

    @staticmethod
    def list_ports() -> list[str]:
        """Return sorted list of available COM/serial port names."""
        return sorted(p.device for p in serial.tools.list_ports.comports())
