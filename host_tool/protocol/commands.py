"""
commands.py — typed helpers for building JSON command dicts.

Every function returns a plain dict that can be passed directly to
BoardConnection.send_command().

Example
───────
    resp = conn.send_command(cmd_gpio_set(14, 1))
    if resp and resp.get("status") == "ok":
        ...
"""

from __future__ import annotations
import base64
from typing import Optional


def cmd_ping() -> dict:
    return {"cmd": "ping"}


# ── GPIO ────────────────────────────────────────────────────────────────────

def cmd_gpio_set(pin: int, level: int) -> dict:
    return {"cmd": "gpio_set", "pin": pin, "level": level}


def cmd_gpio_get(pin: int) -> dict:
    return {"cmd": "gpio_get", "pin": pin}


def cmd_ign_ilum_get() -> dict:
    return {"cmd": "ign_ilum_get"}


# ── UART ────────────────────────────────────────────────────────────────────

def cmd_uart_send(port: int, data: bytes) -> dict:
    return {
        "cmd":      "uart_send",
        "port":     port,
        "data_b64": base64.b64encode(data).decode(),
    }


def cmd_uart_recv(port: int, timeout_ms: int = 200) -> dict:
    return {"cmd": "uart_recv", "port": port, "timeout_ms": timeout_ms}


# ── I2C ─────────────────────────────────────────────────────────────────────

def cmd_i2c_read(sensor: str) -> dict:
    """sensor: "accel" or "rtc" """
    return {"cmd": "i2c_read", "sensor": sensor}


# ── eMMC ────────────────────────────────────────────────────────────────────

def cmd_emmc_test(freq_khz: int, size_kb: int = 64) -> dict:
    return {"cmd": "emmc_test", "freq_khz": freq_khz, "size_kb": size_kb}


# ── Wi-Fi ────────────────────────────────────────────────────────────────────

def cmd_wifi_scan() -> dict:
    return {"cmd": "wifi_scan"}


def cmd_wifi_connect(ssid: str, password: str = "") -> dict:
    return {"cmd": "wifi_connect", "ssid": ssid, "password": password}


def cmd_wifi_disconnect() -> dict:
    return {"cmd": "wifi_disconnect"}


def cmd_wifi_ping(host: str) -> dict:
    return {"cmd": "wifi_ping", "host": host}


# ── Display ──────────────────────────────────────────────────────────────────

def cmd_display_pattern() -> dict:
    return {"cmd": "display_pattern"}


def cmd_display_text(text: str) -> dict:
    return {"cmd": "display_text", "text": text}


def cmd_display_clear() -> dict:
    return {"cmd": "display_clear"}
