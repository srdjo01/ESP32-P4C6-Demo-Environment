"""
settings.py — persistent user settings (port aliases, tool paths).

Stored as JSON under the platform's user config dir:
    macOS / Linux : ~/.esp32_p4c6_tool/config.json
    Windows       : %APPDATA%/ESP32-P4C6-Tool/config.json
"""
from __future__ import annotations

import json
import os
import sys
from pathlib import Path
from typing import Optional


def _config_dir() -> Path:
    if sys.platform == "win32":
        base = os.environ.get("APPDATA") or str(Path.home() / "AppData" / "Roaming")
        return Path(base) / "ESP32-P4C6-Tool"
    return Path.home() / ".esp32_p4c6_tool"


class Settings:
    """JSON-backed config. Writes are atomic; reads are tolerant of corruption."""

    def __init__(self) -> None:
        self._dir  = _config_dir()
        self._path = self._dir / "config.json"
        self._data: dict = {
            "port_aliases": {},     # {device_path: "friendly name"}
            "tool_paths":   {},     # idf_path / python / idf_python_env / esp_python
            "last_ports":   {},     # {"p4": "...", "c6": "...", "main": "..."}
            "last_target":  "p4",
        }
        self._load()

    # ── Persistence ───────────────────────────────────────────────────────

    def _load(self) -> None:
        if not self._path.is_file():
            return
        try:
            with self._path.open("r", encoding="utf-8") as fp:
                loaded = json.load(fp)
            if isinstance(loaded, dict):
                self._data.update(loaded)
                self._data.setdefault("port_aliases", {})
                self._data.setdefault("tool_paths",   {})
                self._data.setdefault("last_ports",   {})
        except (OSError, json.JSONDecodeError):
            # Corrupt or unreadable — keep defaults; will rewrite on next save.
            pass

    def save(self) -> None:
        try:
            self._dir.mkdir(parents=True, exist_ok=True)
            tmp = self._path.with_suffix(".tmp")
            with tmp.open("w", encoding="utf-8") as fp:
                json.dump(self._data, fp, indent=2, sort_keys=True)
            tmp.replace(self._path)
        except OSError as e:
            print(f"[settings] save failed: {e}", flush=True)

    @property
    def config_path(self) -> Path:
        return self._path

    # ── Port aliases ──────────────────────────────────────────────────────

    @property
    def port_aliases(self) -> dict[str, str]:
        return dict(self._data["port_aliases"])

    def alias_for(self, device: str) -> Optional[str]:
        return self._data["port_aliases"].get(device)

    def set_alias(self, device: str, alias: str) -> None:
        alias = alias.strip()
        if alias:
            self._data["port_aliases"][device] = alias
        else:
            self._data["port_aliases"].pop(device, None)

    def replace_aliases(self, mapping: dict[str, str]) -> None:
        self._data["port_aliases"] = {
            dev: name.strip() for dev, name in mapping.items() if name.strip()
        }

    @staticmethod
    def label_for(device: str, alias: Optional[str]) -> str:
        return f"{alias} — {device}" if alias else device

    # ── Tool paths ────────────────────────────────────────────────────────

    @property
    def tool_paths(self) -> dict[str, str]:
        return dict(self._data["tool_paths"])

    def get_tool(self, key: str, default: str = "") -> str:
        return self._data["tool_paths"].get(key, default)

    def set_tool(self, key: str, value: str) -> None:
        if value:
            self._data["tool_paths"][key] = value
        else:
            self._data["tool_paths"].pop(key, None)

    # ── Last-used ports / target ──────────────────────────────────────────

    def last_port(self, slot: str) -> str:
        return self._data["last_ports"].get(slot, "")

    def set_last_port(self, slot: str, device: str) -> None:
        self._data["last_ports"][slot] = device

    @property
    def last_target(self) -> str:
        return self._data.get("last_target", "p4")

    @last_target.setter
    def last_target(self, value: str) -> None:
        self._data["last_target"] = value


# Module-level singleton so all panels share state.
settings = Settings()
