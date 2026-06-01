"""
board_connection.py — serial port management and JSON framing.
"""

from __future__ import annotations

import json
import threading
import time
from typing import Optional

import serial
import serial.tools.list_ports

from PyQt6.QtCore import QObject, QThread, pyqtSignal
from PyQt6.QtWidgets import QApplication


class _ReaderThread(QThread):
    """Background thread that reads lines from the serial port."""

    line_received = pyqtSignal(str)

    def __init__(self, port: serial.Serial, parent: QObject | None = None):
        super().__init__(parent)
        self._port  = port
        self._stop  = threading.Event()

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
    """Manages the serial connection to the ESP32-P4C6 board."""

    message_received  = pyqtSignal(dict)
    raw_line_received = pyqtSignal(str)
    connected_changed = pyqtSignal(bool)

    def __init__(self, parent: QObject | None = None):
        super().__init__(parent)
        self._port:   Optional[serial.Serial] = None
        self._reader: Optional[_ReaderThread] = None
        self._lock    = threading.Lock()
        self._pending_cmd:      Optional[str]  = None
        self._pending_resp:     Optional[dict] = None
        self._pending_evt       = threading.Event()
        self._in_send_command   = False

    # ── Connection management ───────────────────────────────────────────

    def open(self, port_name: str, baud: int = 115200) -> bool:
        try:
            p = serial.Serial(port_name, baud, timeout=0)
        except serial.SerialException:
            return False
        with self._lock:
            self._port = p
        self._reader = _ReaderThread(p, self)
        self._reader.line_received.connect(self._on_line)
        self._reader.start()
        self.connected_changed.emit(True)
        return True

    def close(self):
        # Unblock any waiting send_command call immediately.
        self._pending_evt.set()
        if self._reader:
            self._reader.stop()
            self._reader.wait(1000)
            self._reader = None
        with self._lock:
            if self._port and self._port.is_open:
                try:
                    self._port.close()
                except Exception:
                    pass
            self._port = None
        self.connected_changed.emit(False)

    @property
    def is_connected(self) -> bool:
        with self._lock:
            return self._port is not None and self._port.is_open

    # ── Sending ─────────────────────────────────────────────────────────

    def send_raw(self, obj: dict):
        line = json.dumps(obj, separators=(",", ":")) + "\n"
        print(f"[TX] {line.strip()}", flush=True)
        with self._lock:
            if self._port and self._port.is_open:
                try:
                    self._port.write(line.encode())
                except serial.SerialException as e:
                    print(f"[WARN] send_raw failed: {e}", flush=True)
                    self._port = None

    def send_command(self, obj: dict, timeout: float = 5.0) -> Optional[dict]:
        """
        Send a command and wait for the response.
        Pumps Qt events every 50 ms so the UI stays responsive.
        Re-entrant calls (from timers/slots inside processEvents) return None immediately.
        """
        if self._in_send_command:
            print(f"[WARN] re-entrant send_command({obj.get('cmd')}) blocked", flush=True)
            return None
        self._in_send_command = True
        cmd = obj.get("cmd")
        try:
            with self._lock:
                self._pending_cmd  = cmd
                self._pending_resp = None
                self._pending_evt.clear()
            self.send_raw(obj)
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                if self._pending_evt.wait(0.05):
                    break
                QApplication.processEvents()
            with self._lock:
                resp = self._pending_resp
                self._pending_cmd  = None
                self._pending_resp = None
            print(f"[RX resp] cmd={cmd} resp={resp}", flush=True)
            return resp
        finally:
            self._in_send_command = False

    # ── Receiving ───────────────────────────────────────────────────────

    def _on_line(self, text: str):
        print(f"[RX] {text[:120]}", flush=True)
        self.raw_line_received.emit(text)
        try:
            obj = json.loads(text)
        except json.JSONDecodeError:
            return
        self.message_received.emit(obj)
        with self._lock:
            if self._pending_cmd and obj.get("cmd") == self._pending_cmd:
                print(f"[RX] matched cmd={self._pending_cmd}, setting event", flush=True)
                self._pending_resp = obj
                self._pending_evt.set()

    # ── Port enumeration ─────────────────────────────────────────────────

    @staticmethod
    def list_ports() -> list[str]:
        return sorted(p.device for p in serial.tools.list_ports.comports())
