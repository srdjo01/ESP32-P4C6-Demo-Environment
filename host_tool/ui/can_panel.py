"""
can_panel.py — CAN bus testing via TJA1051 transceiver.

Wiring (ESP32-P4 ↔ TJA1051):
    GPIO1  → TXD
    GPIO2  ← RXD
    GPIO3  → S/STB  (low = normal, high = silent)
    GND    ↔ GND

The board talks to the TWAI controller on the P4; this panel exposes start /
stop, silent-mode toggle, sending an arbitrary frame, polling for received
frames, querying status, and an internal-loopback self-test that needs no
transceiver to verify the controller path.
"""
from __future__ import annotations

import base64
from typing import Optional

from PyQt6.QtCore import QTimer, Qt
from PyQt6.QtWidgets import (
    QCheckBox, QComboBox, QFormLayout, QGroupBox, QHBoxLayout, QLabel,
    QLineEdit, QPlainTextEdit, QPushButton, QSpinBox, QVBoxLayout, QWidget,
)

from protocol.commands import (
    cmd_can_start, cmd_can_stop, cmd_can_silent,
    cmd_can_send, cmd_can_recv, cmd_can_status, cmd_can_self_test,
)


_BITRATES = [
    ("1 Mbit/s",    1_000_000),
    ("800 kbit/s",    800_000),
    ("500 kbit/s",    500_000),
    ("250 kbit/s",    250_000),
    ("125 kbit/s",    125_000),
    ("100 kbit/s",    100_000),
    ("50 kbit/s",      50_000),
    ("25 kbit/s",      25_000),
]


def _parse_hex_bytes(text: str) -> Optional[bytes]:
    """Accept '11 22 AA' or '11,22,aa' or '0x11 0x22'. Empty string → b''."""
    s = text.strip()
    if not s:
        return b""
    s = s.replace(",", " ").replace("0x", "").replace("0X", "")
    parts = s.split()
    out = bytearray()
    for p in parts:
        if len(p) == 0:
            continue
        if not all(c in "0123456789abcdefABCDEF" for c in p):
            return None
        if len(p) == 1:
            p = "0" + p
        if len(p) > 2:
            # split into byte chunks
            if len(p) % 2 != 0:
                return None
            for i in range(0, len(p), 2):
                out.append(int(p[i:i+2], 16))
        else:
            out.append(int(p, 16))
    if len(out) > 8:
        return None
    return bytes(out)


def _format_frame(resp: dict) -> str:
    can_id = resp.get("id", 0)
    dlc    = resp.get("dlc", 0)
    ext    = resp.get("extended", False)
    rtr    = resp.get("rtr",      False)
    raw    = base64.b64decode(resp.get("data_b64", ""))
    id_fmt = f"0x{can_id:08X}" if ext else f"0x{can_id:03X}"
    data_fmt = " ".join(f"{b:02X}" for b in raw[:dlc])
    flags = []
    if ext: flags.append("EXT")
    if rtr: flags.append("RTR")
    flag_str = (" [" + ",".join(flags) + "]") if flags else ""
    return f"{id_fmt}{flag_str}  dlc={dlc}  {data_fmt}"


class CanPanel(QWidget):
    def __init__(self, connection, parent=None):
        super().__init__(parent)
        self._conn = connection
        self._poll = QTimer(self)
        self._poll.setInterval(100)
        self._poll.timeout.connect(self._poll_rx)
        self._build_ui()

    # ── UI ────────────────────────────────────────────────────────────────

    def _build_ui(self):
        layout = QVBoxLayout(self)

        layout.addWidget(QLabel(
            "CAN bus via TJA1051  (P4 GPIO1=TX, GPIO2=RX, GPIO3=STB).\n"
            "Wire GND of the board to TJA1051 GND. CAN_H / CAN_L go to the bus."
        ))

        layout.addWidget(self._build_bus_box())
        layout.addWidget(self._build_send_box())
        layout.addWidget(self._build_recv_box())
        layout.addWidget(self._build_status_box())

    def _build_bus_box(self) -> QWidget:
        box = QGroupBox("Bus control")
        row = QHBoxLayout(box)

        row.addWidget(QLabel("Bitrate:"))
        self._bitrate = QComboBox()
        for label, value in _BITRATES:
            self._bitrate.addItem(label, value)
        self._bitrate.setCurrentIndex(2)  # default 500 kbit/s
        row.addWidget(self._bitrate)

        self._btn_start = QPushButton("Start")
        self._btn_start.clicked.connect(self._do_start)
        self._btn_stop  = QPushButton("Stop")
        self._btn_stop.clicked.connect(self._do_stop)
        row.addWidget(self._btn_start)
        row.addWidget(self._btn_stop)

        self._silent = QCheckBox("Silent mode (STB high)")
        self._silent.toggled.connect(self._do_silent)
        row.addWidget(self._silent)

        self._btn_self_test = QPushButton("Self-test (loopback)")
        self._btn_self_test.clicked.connect(self._do_self_test)
        row.addWidget(self._btn_self_test)

        row.addStretch()
        return box

    def _build_send_box(self) -> QWidget:
        box = QGroupBox("Send frame")
        form = QFormLayout(box)

        self._send_id = QLineEdit("0x123")
        self._send_id.setFixedWidth(120)
        form.addRow("ID (hex):", self._send_id)

        self._send_data = QLineEdit("11 22 33 44")
        self._send_data.setPlaceholderText("hex bytes, max 8 — e.g. 11 22 AA BB")
        form.addRow("Data:", self._send_data)

        flags_row = QHBoxLayout()
        self._ext_box = QCheckBox("Extended (29-bit)")
        self._rtr_box = QCheckBox("RTR (remote)")
        flags_row.addWidget(self._ext_box)
        flags_row.addWidget(self._rtr_box)
        flags_row.addStretch()
        btn_send = QPushButton("Send")
        btn_send.clicked.connect(self._do_send)
        flags_row.addWidget(btn_send)
        form.addRow("", self._wrap(flags_row))
        return box

    def _build_recv_box(self) -> QWidget:
        box = QGroupBox("Receive")
        v = QVBoxLayout(box)

        ctl = QHBoxLayout()
        self._auto_poll = QCheckBox("Auto-poll")
        self._auto_poll.toggled.connect(self._on_auto_poll)
        ctl.addWidget(self._auto_poll)

        ctl.addWidget(QLabel("Interval (ms):"))
        self._poll_ms = QSpinBox()
        self._poll_ms.setRange(20, 2000)
        self._poll_ms.setValue(100)
        self._poll_ms.valueChanged.connect(lambda v: self._poll.setInterval(v))
        ctl.addWidget(self._poll_ms)

        btn_once  = QPushButton("Receive once")
        btn_once.clicked.connect(self._poll_rx)
        btn_clear = QPushButton("Clear")
        btn_clear.clicked.connect(lambda: self._rx_log.clear())
        ctl.addWidget(btn_once)
        ctl.addWidget(btn_clear)
        ctl.addStretch()
        v.addLayout(ctl)

        self._rx_log = QPlainTextEdit()
        self._rx_log.setReadOnly(True)
        self._rx_log.setMaximumBlockCount(2000)
        v.addWidget(self._rx_log)
        return box

    def _build_status_box(self) -> QWidget:
        box = QGroupBox("Status")
        v = QVBoxLayout(box)
        row = QHBoxLayout()
        btn = QPushButton("Refresh status")
        btn.clicked.connect(self._do_status)
        row.addWidget(btn)
        row.addStretch()
        v.addLayout(row)
        self._status_lbl = QLabel("Bus stopped.")
        self._status_lbl.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse)
        v.addWidget(self._status_lbl)
        return box

    def _wrap(self, layout) -> QWidget:
        w = QWidget()
        w.setLayout(layout)
        return w

    # ── Actions ───────────────────────────────────────────────────────────

    def _send(self, payload: dict, timeout: float = 3.0):
        if not self._conn.is_connected:
            self._rx_log.appendPlainText("[!] Not connected.")
            return None
        return self._conn.send_command(payload, timeout=timeout)

    def _do_start(self):
        bitrate = self._bitrate.currentData()
        resp = self._send(cmd_can_start(bitrate))
        if resp and resp.get("status") == "ok":
            self._rx_log.appendPlainText(f"[bus] started @ {bitrate} bit/s")
            self._do_status()
        else:
            msg = (resp or {}).get("message", "no response")
            self._rx_log.appendPlainText(f"[bus] start failed: {msg}")

    def _do_stop(self):
        if self._auto_poll.isChecked():
            self._auto_poll.setChecked(False)
        resp = self._send(cmd_can_stop())
        if resp and resp.get("status") == "ok":
            self._rx_log.appendPlainText("[bus] stopped")
            self._do_status()

    def _do_silent(self, silent: bool):
        resp = self._send(cmd_can_silent(silent))
        if resp and resp.get("status") == "ok":
            self._rx_log.appendPlainText(f"[stb] {'silent' if silent else 'normal'}")

    def _do_self_test(self):
        bitrate = self._bitrate.currentData()
        resp = self._send(cmd_can_self_test(bitrate), timeout=4.0)
        if not resp:
            self._rx_log.appendPlainText("[self-test] no response")
            return
        ok = resp.get("loopback_ok")
        if ok:
            self._rx_log.appendPlainText(f"[self-test] PASS (controller loopback @ {bitrate})")
        else:
            self._rx_log.appendPlainText("[self-test] FAIL — controller did not loop frame back")

    def _do_send(self):
        try:
            id_text = self._send_id.text().strip()
            can_id  = int(id_text, 16) if id_text.lower().startswith("0x") else int(id_text, 16)
        except ValueError:
            self._rx_log.appendPlainText("[!] Invalid ID — use hex, e.g. 0x123")
            return

        ext = self._ext_box.isChecked()
        rtr = self._rtr_box.isChecked()
        if ext and can_id > 0x1FFFFFFF:
            self._rx_log.appendPlainText("[!] Extended ID out of range (>29 bits)")
            return
        if not ext and can_id > 0x7FF:
            self._rx_log.appendPlainText("[!] Standard ID out of range (>11 bits)")
            return

        data = b"" if rtr else _parse_hex_bytes(self._send_data.text())
        if data is None:
            self._rx_log.appendPlainText("[!] Invalid data — hex bytes only, max 8")
            return

        resp = self._send(cmd_can_send(can_id, data, extended=ext, rtr=rtr))
        if not resp:
            self._rx_log.appendPlainText("[tx] no response")
            return
        if resp.get("status") != "ok":
            self._rx_log.appendPlainText(f"[tx] error: {resp.get('message')}")
            return
        id_fmt = f"0x{can_id:08X}" if ext else f"0x{can_id:03X}"
        flags = []
        if ext: flags.append("EXT")
        if rtr: flags.append("RTR")
        flag_str = (" [" + ",".join(flags) + "]") if flags else ""
        data_fmt = " ".join(f"{b:02X}" for b in data)
        self._rx_log.appendPlainText(
            f"[TX]  {id_fmt}{flag_str}  dlc={len(data)}  {data_fmt}"
        )

    def _on_auto_poll(self, on: bool):
        if on:
            self._poll.start()
        else:
            self._poll.stop()

    def _poll_rx(self):
        if not self._conn.is_connected:
            return
        resp = self._send(cmd_can_recv(timeout_ms=20), timeout=1.0)
        if not resp or resp.get("status") != "ok":
            return
        if not resp.get("received"):
            return
        self._rx_log.appendPlainText(f"[RX]  {_format_frame(resp)}")

    def _do_status(self):
        resp = self._send(cmd_can_status())
        if not resp or resp.get("status") != "ok":
            self._status_lbl.setText("Status: (no response)")
            return
        self._status_lbl.setText(
            f"started={resp.get('started')}  silent={resp.get('silent')}  "
            f"bitrate={resp.get('bitrate')}  bus_off={resp.get('bus_off')}\n"
            f"tx_queued={resp.get('tx_queued')}  rx_queued={resp.get('rx_queued')}  "
            f"tx_err={resp.get('tx_errors')}  rx_err={resp.get('rx_errors')}  "
            f"bus_err={resp.get('bus_errors')}  arb_lost={resp.get('arb_lost')}"
        )
