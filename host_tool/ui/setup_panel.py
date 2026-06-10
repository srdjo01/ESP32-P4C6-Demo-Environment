"""
setup_panel.py — configure ESP-IDF and Python tool paths.

Auto-detects ESP-IDF on macOS / Linux / Windows in the usual install
locations, and lets the user override any path manually with a file
picker. A "Test" button verifies that idf.py can actually run.
"""
from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Optional

from PyQt6.QtCore import Qt, pyqtSignal
from PyQt6.QtWidgets import (
    QFileDialog, QFormLayout, QGroupBox, QHBoxLayout, QLabel, QLineEdit,
    QPlainTextEdit, QPushButton, QVBoxLayout, QWidget,
)

from config import settings


# ── ESP-IDF discovery ────────────────────────────────────────────────────

def _candidate_idf_dirs() -> list[Path]:
    home = Path.home()
    out: list[Path] = []
    env = os.environ.get("IDF_PATH")
    if env:
        out.append(Path(env))
    # macOS / Linux
    out += [
        home / ".espressif" / "v5.4.1" / "esp-idf",
        home / "esp" / "v5.4" / "esp-idf",
        home / "esp" / "v5.4.1" / "esp-idf",
        home / "esp" / "esp-idf",
        Path("/opt/esp-idf"),
    ]
    # Windows
    if sys.platform == "win32":
        out += [
            Path("C:/Espressif/frameworks/esp-idf"),
            Path("C:/esp/esp-idf"),
            home / "esp" / "esp-idf",
        ]
    return out


def autodetect_idf() -> Optional[Path]:
    for d in _candidate_idf_dirs():
        if (d / "tools" / "idf.py").is_file():
            return d
    return None


def autodetect_python() -> Optional[Path]:
    """Pick a Python that ESP-IDF v5.4 actually has a venv for (3.9–3.13)."""
    candidates = []
    for ver in ("3.13", "3.11", "3.9"):
        exe = "python" + ver
        if sys.platform == "win32":
            exe += ".exe"
        path = shutil.which(exe)
        if path:
            candidates.append(Path(path))
    if candidates:
        return candidates[0]
    fallback = shutil.which("python3") or shutil.which("python")
    return Path(fallback) if fallback else None


def autodetect_idf_python_env(python: Optional[Path]) -> Optional[Path]:
    """Match the IDF venv directory to the chosen interpreter version."""
    if not python:
        return None
    home = Path.home()
    base = home / ".espressif" / "python_env"
    if not base.is_dir():
        return None
    try:
        # Probe the interpreter for its actual version.
        out = subprocess.check_output(
            [str(python), "-c", "import sys;print(f'{sys.version_info.major}.{sys.version_info.minor}')"],
            text=True, stderr=subprocess.DEVNULL, timeout=5,
        ).strip()
    except Exception:
        return None
    needle = f"_py{out}_env"
    for child in sorted(base.iterdir()):
        if child.is_dir() and needle in child.name:
            return child
    return None


def venv_python(venv_dir: str | Path) -> Optional[Path]:
    """Return the python executable inside an IDF venv directory."""
    if not venv_dir:
        return None
    base = Path(venv_dir)
    if sys.platform == "win32":
        candidates = [base / "Scripts" / "python.exe"]
    else:
        candidates = [base / "bin" / "python", base / "bin" / "python3"]
    for c in candidates:
        if c.is_file():
            return c
    return None


def resolved_idf_python(esp_python: str, idf_python_env: str) -> tuple[Optional[Path], Optional[str]]:
    """
    Pick the python that should actually run idf.py.

    idf.py and its CLI deps (click, etc.) live inside the IDF venv at
    IDF_PYTHON_ENV_PATH. The "Python interpreter" field is just the bootstrap
    python used to *create* that venv — it does NOT have click installed.
    Returns (python_path, reason_or_none). If both inputs are bad, returns
    (None, error_message).
    """
    venv_py = venv_python(idf_python_env) if idf_python_env else None
    if venv_py:
        return venv_py, None
    if esp_python and Path(esp_python).is_file():
        return Path(esp_python), (
            "warning: IDF venv not set; falling back to bootstrap python "
            "(idf.py may fail with 'No module named click')"
        )
    return None, "no usable python — set IDF Python venv or Python interpreter"


# ── Panel ────────────────────────────────────────────────────────────────

class _PathRow(QWidget):
    """One row: label + line edit + Browse button (file or directory)."""

    changed = pyqtSignal()

    def __init__(self, *, key: str, kind: str, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self._key  = key
        self._kind = kind   # "dir" or "file"
        layout = QHBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        self._edit = QLineEdit(settings.get_tool(key))
        self._edit.setPlaceholderText("(auto-detect)")
        self._edit.textChanged.connect(self.changed.emit)
        layout.addWidget(self._edit, 1)
        btn = QPushButton("Browse…")
        btn.setFixedWidth(90)
        btn.clicked.connect(self._browse)
        layout.addWidget(btn)

    def _browse(self) -> None:
        if self._kind == "dir":
            path = QFileDialog.getExistingDirectory(self, "Choose directory", self._edit.text())
        else:
            path, _ = QFileDialog.getOpenFileName(self, "Choose file", self._edit.text())
        if path:
            self._edit.setText(path)

    def value(self) -> str:
        return self._edit.text().strip()

    def set_value(self, v: str) -> None:
        self._edit.setText(v)


class SetupPanel(QWidget):
    def __init__(self, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        layout = QVBoxLayout(self)

        # ── Paths group ──────────────────────────────────────────────────
        paths_box = QGroupBox("Tool Paths  (changes are saved automatically)")
        form = QFormLayout(paths_box)
        self._idf_row    = _PathRow(key="idf_path",       kind="dir")
        self._py_row     = _PathRow(key="esp_python",     kind="file")
        self._venv_row   = _PathRow(key="idf_python_env", kind="dir")
        for row in (self._idf_row, self._py_row, self._venv_row):
            row.changed.connect(self._save)
        form.addRow("ESP-IDF directory:",       self._idf_row)
        form.addRow("Python interpreter:",      self._py_row)
        form.addRow("IDF Python venv:",         self._venv_row)
        layout.addWidget(paths_box)

        # ── Action buttons ───────────────────────────────────────────────
        btn_row = QHBoxLayout()
        btn_detect = QPushButton("Auto-detect")
        btn_detect.clicked.connect(self._auto_detect)
        btn_save = QPushButton("Save")
        btn_save.clicked.connect(self._save_explicit)
        btn_test = QPushButton("Test")
        btn_test.clicked.connect(self._test)
        btn_row.addWidget(btn_detect)
        btn_row.addWidget(btn_save)
        btn_row.addWidget(btn_test)
        btn_row.addStretch()
        layout.addLayout(btn_row)

        # ── Output / status box ──────────────────────────────────────────
        layout.addWidget(QLabel("Output:"))
        self._log = QPlainTextEdit()
        self._log.setReadOnly(True)
        self._log.setMaximumBlockCount(2000)
        layout.addWidget(self._log, 1)

        # Footer with config file path
        cfg_label = QLabel(f"Settings file: {settings.config_path}")
        cfg_label.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse)
        cfg_label.setStyleSheet("color: gray;")
        layout.addWidget(cfg_label)

        if not (self._idf_row.value() or self._py_row.value() or self._venv_row.value()):
            self._auto_detect()

    # ── Slots ────────────────────────────────────────────────────────────

    def _log_line(self, msg: str) -> None:
        self._log.appendPlainText(msg)

    def _auto_detect(self) -> None:
        self._log.clear()
        self._log_line(f"Platform: {sys.platform}")

        idf = autodetect_idf()
        if idf:
            self._idf_row.set_value(str(idf))
            self._log_line(f"ESP-IDF       : {idf}")
        else:
            self._log_line("ESP-IDF       : NOT FOUND (set manually)")

        py = autodetect_python()
        if py:
            self._py_row.set_value(str(py))
            self._log_line(f"Python        : {py}")
        else:
            self._log_line("Python        : NOT FOUND")

        venv = autodetect_idf_python_env(py)
        if venv:
            self._venv_row.set_value(str(venv))
            self._log_line(f"IDF venv      : {venv}")
        else:
            self._log_line("IDF venv      : not detected (will be created on first build)")

    def _save(self) -> None:
        settings.set_tool("idf_path",       self._idf_row.value())
        settings.set_tool("esp_python",     self._py_row.value())
        settings.set_tool("idf_python_env", self._venv_row.value())
        settings.save()

    def _save_explicit(self) -> None:
        self._save()
        self._log_line(f"Saved settings to {settings.config_path}")

    def _test(self) -> None:
        self._log.clear()
        idf  = self._idf_row.value()
        py   = self._py_row.value()
        venv = self._venv_row.value()

        if not idf or not Path(idf, "tools", "idf.py").is_file():
            self._log_line(f"FAIL: idf.py not found under {idf!r}")
            return

        run_py, note = resolved_idf_python(py, venv)
        if note:
            self._log_line(note)
        if not run_py:
            self._log_line(f"FAIL: {note}")
            return
        self._log_line(f"Using python  : {run_py}")

        env = os.environ.copy()
        env["IDF_PATH"] = idf
        if venv:
            env["IDF_PYTHON_ENV_PATH"] = venv
        if py:
            env["ESP_PYTHON"] = py
        # Put the venv's bin dir on PATH so any helper scripts it ships are reachable.
        env["PATH"] = str(Path(run_py).parent) + os.pathsep + env.get("PATH", "")

        idf_py = str(Path(idf) / "tools" / "idf.py")
        try:
            res = subprocess.run(
                [str(run_py), idf_py, "--version"],
                env=env, capture_output=True, text=True, timeout=20,
            )
        except Exception as e:
            self._log_line(f"FAIL: could not invoke idf.py — {e}")
            return

        if res.stdout:
            self._log_line(res.stdout.strip())
        if res.stderr:
            self._log_line(res.stderr.strip())
        if res.returncode == 0:
            self._log_line("OK: idf.py is runnable.")
        else:
            self._log_line(f"FAIL: idf.py exited with code {res.returncode}")
