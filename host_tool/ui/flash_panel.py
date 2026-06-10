"""
flash_panel.py — build & flash ESP32-P4 / ESP32-C6 firmware from the UI.

Uses the tool paths set on the Setup tab and the alias-aware port pickers
from `config.ports`. Firmware project directories are auto-detected from
the repo layout but can be overridden per-target. Build output streams
live into a console widget.
"""
from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path
from typing import Optional

from PyQt6.QtCore import QProcess, pyqtSignal
from PyQt6.QtWidgets import (
    QCheckBox, QComboBox, QFileDialog, QFormLayout, QGroupBox, QHBoxLayout,
    QLabel, QLineEdit, QPlainTextEdit, QPushButton, QVBoxLayout, QWidget,
)

from config import PortCombo, settings
from ui.port_alias_dialog import PortAliasDialog
from ui.setup_panel import resolved_idf_python


# ── Firmware project autodetect ──────────────────────────────────────────

def _looks_like_idf_project(p: Path) -> bool:
    """An IDF project root holds a CMakeLists.txt and a `main/` directory."""
    return (p / "CMakeLists.txt").is_file() and (p / "main").is_dir()


def autodetect_firmware_dirs() -> tuple[Optional[Path], Optional[Path]]:
    """
    Walk up from this file looking for a parent that contains both
    `firmware/` and `firmware_c6/` IDF projects. Falls back to whichever
    of the two is found independently.
    """
    here = Path(__file__).resolve()
    p4: Optional[Path] = None
    c6: Optional[Path] = None
    for parent in here.parents:
        cand_p4 = parent / "firmware"
        cand_c6 = parent / "firmware_c6"
        if p4 is None and _looks_like_idf_project(cand_p4):
            p4 = cand_p4
        if c6 is None and _looks_like_idf_project(cand_c6):
            c6 = cand_c6
        if p4 and c6:
            break
    return p4, c6


# ── IDF environment loader ───────────────────────────────────────────────

def load_idf_env(idf_path: str, esp_python: str, idf_python_env: str) -> tuple[dict[str, str], str]:
    """
    Source ESP-IDF's export.sh / export.bat in a child shell and capture the
    resulting environment. This is what `build_firmware.sh` does — it ensures
    the riscv toolchain, esptool, and the IDF venv's site-packages are all on
    PATH for sub-processes.

    Returns (env_dict, info_message). On failure, env_dict is the parent
    process env with the IDF-related vars filled in by hand (still good
    enough on systems where everything is already on PATH).
    """
    env = os.environ.copy()
    env["IDF_PATH"] = idf_path
    if esp_python:
        env["ESP_PYTHON"] = esp_python
    if idf_python_env:
        env["IDF_PYTHON_ENV_PATH"] = idf_python_env

    info = ""
    if sys.platform == "win32":
        export_bat = Path(idf_path) / "export.bat"
        if not export_bat.is_file():
            return env, f"warning: {export_bat} not found, build may fail"
        cmd = ["cmd", "/c", str(export_bat), "&&", "set"]
    else:
        export_sh = Path(idf_path) / "export.sh"
        if not export_sh.is_file():
            return env, f"warning: {export_sh} not found, build may fail"
        # `env -0` outputs NUL-separated KEY=VALUE pairs so values containing
        # newlines (rare, but possible in PATH on macOS) survive intact.
        cmd = ["bash", "-c", f". '{export_sh}' >/dev/null 2>&1 && env -0"]

    try:
        res = subprocess.run(cmd, env=env, capture_output=True, timeout=60)
    except Exception as e:
        return env, f"warning: could not source export script — {e}"
    if res.returncode != 0:
        return env, f"warning: export script returned {res.returncode}"

    parsed: dict[str, str] = {}
    if sys.platform == "win32":
        for line in res.stdout.decode("utf-8", errors="replace").splitlines():
            if "=" in line:
                k, v = line.split("=", 1)
                parsed[k] = v
    else:
        for chunk in res.stdout.split(b"\0"):
            if not chunk:
                continue
            try:
                line = chunk.decode("utf-8")
            except UnicodeDecodeError:
                continue
            if "=" in line:
                k, v = line.split("=", 1)
                parsed[k] = v

    if parsed:
        env.update(parsed)
        info = "loaded IDF environment from export script"
    else:
        info = "warning: export script produced no output"
    return env, info


# ── Reusable path-picker row ─────────────────────────────────────────────

class _DirRow(QWidget):
    """Line edit + Browse button for an optional directory path."""

    changed = pyqtSignal()

    def __init__(self, placeholder: str, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        layout = QHBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        self._edit = QLineEdit()
        self._edit.setPlaceholderText(placeholder)
        self._edit.textChanged.connect(self.changed.emit)
        layout.addWidget(self._edit, 1)
        btn = QPushButton("Browse…")
        btn.setFixedWidth(90)
        btn.clicked.connect(self._browse)
        layout.addWidget(btn)

    def _browse(self) -> None:
        start = self._edit.text().strip() or str(Path.home())
        path = QFileDialog.getExistingDirectory(self, "Choose firmware directory", start)
        if path:
            self._edit.setText(path)

    def value(self) -> str:
        return self._edit.text().strip()

    def set_value(self, v: str) -> None:
        self._edit.setText(v)

    def set_enabled(self, enabled: bool) -> None:
        self._edit.setEnabled(enabled)
        # Browse button is the second child widget.
        for i in range(self.layout().count()):
            w = self.layout().itemAt(i).widget()
            if w:
                w.setEnabled(enabled)


# ── Panel ────────────────────────────────────────────────────────────────

class FlashPanel(QWidget):
    log_line = pyqtSignal(str)

    def __init__(self, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self._proc: Optional[QProcess] = None
        self._queue: list[tuple[str, list[str], dict[str, str], Path]] = []

        layout = QVBoxLayout(self)
        layout.addWidget(self._build_target_box())
        layout.addWidget(self._build_firmware_box())
        layout.addWidget(self._build_ports_box())
        layout.addWidget(self._build_action_row())

        layout.addWidget(QLabel("Output:"))
        self._log = QPlainTextEdit()
        self._log.setReadOnly(True)
        self._log.setMaximumBlockCount(5000)
        font = self._log.font()
        font.setFamily("Menlo" if sys.platform == "darwin" else "Consolas")
        self._log.setFont(font)
        layout.addWidget(self._log, 1)

        self.log_line.connect(self._append_log)
        self._restore_last_target()
        self._restore_firmware_paths()

    # ── UI sections ──────────────────────────────────────────────────────

    def _build_target_box(self) -> QWidget:
        box = QGroupBox("Target")
        row = QHBoxLayout(box)
        self._target_combo = QComboBox()
        self._target_combo.addItem("ESP32-P4 only",  "p4")
        self._target_combo.addItem("ESP32-C6 only",  "c6")
        self._target_combo.addItem("Both (P4 + C6)", "both")
        self._target_combo.currentIndexChanged.connect(self._on_target_changed)
        row.addWidget(self._target_combo)
        row.addStretch()
        return box

    def _build_firmware_box(self) -> QWidget:
        box = QGroupBox("Firmware (optional — leave blank to use repo defaults)")
        form = QFormLayout(box)

        self._p4_dir_row = _DirRow("(auto-detect: <repo>/firmware)")
        self._c6_dir_row = _DirRow("(auto-detect: <repo>/firmware_c6)")
        self._p4_dir_row.changed.connect(self._save_firmware_paths)
        self._c6_dir_row.changed.connect(self._save_firmware_paths)

        form.addRow("ESP32-P4 directory:", self._p4_dir_row)
        form.addRow("ESP32-C6 directory:", self._c6_dir_row)

        controls = QHBoxLayout()
        btn_detect = QPushButton("Auto-detect")
        btn_detect.clicked.connect(self._autodetect_firmware)
        btn_clear = QPushButton("Clear")
        btn_clear.clicked.connect(self._clear_firmware)
        controls.addWidget(btn_detect)
        controls.addWidget(btn_clear)
        controls.addStretch()
        form.addRow("", self._wrap(controls))
        return box

    def _build_ports_box(self) -> QWidget:
        box = QGroupBox("Ports")
        form = QFormLayout(box)

        self._p4_combo = PortCombo(slot="p4")
        self._c6_combo = PortCombo(slot="c6")

        controls = QHBoxLayout()
        btn_refresh = QPushButton("Refresh")
        btn_refresh.clicked.connect(self._refresh_ports)
        btn_alias = QPushButton("Aliases…")
        btn_alias.clicked.connect(self._edit_aliases)
        controls.addWidget(btn_refresh)
        controls.addWidget(btn_alias)
        controls.addStretch()

        form.addRow("ESP32-P4 port:", self._p4_combo)
        form.addRow("ESP32-C6 port:", self._c6_combo)
        form.addRow("",               self._wrap(controls))
        return box

    def _wrap(self, layout) -> QWidget:
        w = QWidget()
        w.setLayout(layout)
        return w

    def _build_action_row(self) -> QWidget:
        w = QWidget()
        row = QHBoxLayout(w)
        row.setContentsMargins(0, 0, 0, 0)

        self._flash_box   = QCheckBox("Flash after build")
        self._flash_box.setChecked(True)
        self._monitor_box = QCheckBox("Monitor after flash")
        row.addWidget(self._flash_box)
        row.addWidget(self._monitor_box)

        row.addStretch()

        self._btn_build = QPushButton("Build")
        self._btn_build.clicked.connect(lambda: self._start(flash=False))
        self._btn_flash = QPushButton("Build && Flash")
        self._btn_flash.clicked.connect(lambda: self._start(flash=True))
        self._btn_stop  = QPushButton("Stop")
        self._btn_stop.clicked.connect(self._stop)
        self._btn_stop.setEnabled(False)
        row.addWidget(self._btn_build)
        row.addWidget(self._btn_flash)
        row.addWidget(self._btn_stop)
        return w

    # ── Helpers ──────────────────────────────────────────────────────────

    def _restore_last_target(self) -> None:
        idx = self._target_combo.findData(settings.last_target)
        if idx >= 0:
            self._target_combo.setCurrentIndex(idx)
        self._on_target_changed()

    def _restore_firmware_paths(self) -> None:
        p4 = settings.get_tool("firmware_p4_dir")
        c6 = settings.get_tool("firmware_c6_dir")
        if p4:
            self._p4_dir_row.set_value(p4)
        if c6:
            self._c6_dir_row.set_value(c6)

    def _save_firmware_paths(self) -> None:
        settings.set_tool("firmware_p4_dir", self._p4_dir_row.value())
        settings.set_tool("firmware_c6_dir", self._c6_dir_row.value())
        settings.save()

    def _autodetect_firmware(self) -> None:
        p4, c6 = autodetect_firmware_dirs()
        if p4:
            self._p4_dir_row.set_value(str(p4))
            self._append_log(f"P4 firmware: {p4}")
        else:
            self._append_log("P4 firmware: not found")
        if c6:
            self._c6_dir_row.set_value(str(c6))
            self._append_log(f"C6 firmware: {c6}")
        else:
            self._append_log("C6 firmware: not found")

    def _clear_firmware(self) -> None:
        self._p4_dir_row.set_value("")
        self._c6_dir_row.set_value("")

    def _on_target_changed(self) -> None:
        target = self._target_combo.currentData()
        settings.last_target = target
        settings.save()
        self._p4_combo.setEnabled(target in ("p4", "both"))
        self._c6_combo.setEnabled(target in ("c6", "both"))

    def _refresh_ports(self) -> None:
        self._p4_combo.refresh()
        self._c6_combo.refresh()

    def _edit_aliases(self) -> None:
        dlg = PortAliasDialog(self)
        if dlg.exec():
            self._refresh_ports()

    def _append_log(self, text: str) -> None:
        self._log.appendPlainText(text.rstrip())

    def _set_running(self, running: bool) -> None:
        for b in (self._btn_build, self._btn_flash):
            b.setEnabled(not running)
        self._btn_stop.setEnabled(running)

    # ── Resolved firmware directories ────────────────────────────────────

    def _resolved_firmware_dir(self, which: str) -> Optional[Path]:
        """Override → autodetect. Returns None if neither exists."""
        override = self._p4_dir_row.value() if which == "p4" else self._c6_dir_row.value()
        if override:
            p = Path(override).expanduser()
            return p if _looks_like_idf_project(p) else None
        p4, c6 = autodetect_firmware_dirs()
        return p4 if which == "p4" else c6

    # ── Build orchestration ──────────────────────────────────────────────

    def _start(self, *, flash: bool) -> None:
        if self._proc is not None:
            return
        self._log.clear()

        idf  = settings.get_tool("idf_path")
        py   = settings.get_tool("esp_python")
        venv = settings.get_tool("idf_python_env")

        if not idf or not Path(idf, "tools", "idf.py").is_file():
            self._append_log(
                "ERROR: ESP-IDF path not configured.\n"
                "  Open the Setup tab, click Auto-detect (or Browse to your "
                "ESP-IDF directory), then come back here."
            )
            self._append_log(f"  (current value: {idf!r})")
            return

        run_py, note = resolved_idf_python(py, venv)
        if note:
            self._append_log(note)
        if not run_py:
            self._append_log(f"ERROR: {note}")
            return

        target = self._target_combo.currentData()
        do_flash   = flash and self._flash_box.isChecked()
        do_monitor = do_flash and self._monitor_box.isChecked()

        # Source IDF's export script so the toolchain (riscv-elf-gcc) and
        # all IDF python helpers are reachable. This mirrors what
        # build_firmware.sh does. Falls back to a hand-built env if sourcing
        # fails — that still works when the user already has the toolchain
        # on PATH from their shell profile.
        env, info = load_idf_env(idf, py, venv)
        if info:
            self._append_log(info)
        # Make sure the venv's bin dir is the FIRST thing on PATH so that
        # invocations like `python` inside helper scripts find the venv.
        env["PATH"] = str(Path(run_py).parent) + os.pathsep + env.get("PATH", "")

        idf_py = str(Path(idf) / "tools" / "idf.py")

        jobs: list[tuple[str, list[str], dict[str, str], Path]] = []
        if target in ("p4", "both"):
            cwd = self._resolved_firmware_dir("p4")
            if not cwd:
                self._append_log("ERROR: P4 firmware directory not found. Use Auto-detect or set it manually.")
                return
            jobs.append(self._build_job("ESP32-P4", cwd,
                                        self._p4_combo.current_device(),
                                        do_flash, do_monitor, str(run_py), idf_py, env))
        if target in ("c6", "both"):
            cwd = self._resolved_firmware_dir("c6")
            if not cwd:
                self._append_log("ERROR: C6 firmware directory not found. Use Auto-detect or set it manually.")
                return
            jobs.append(self._build_job("ESP32-C6", cwd,
                                        self._c6_combo.current_device(),
                                        do_flash, do_monitor, str(run_py), idf_py, env))

        if not jobs:
            self._append_log("Nothing to do.")
            return

        if do_flash:
            for label, _args, _env, _cwd in jobs:
                if not self._port_for(label):
                    self._append_log(f"ERROR: no port selected for {label}.")
                    return

        self._queue = jobs
        self._set_running(True)
        self._run_next()

    def _build_job(self, label: str, project_dir: Path, port: str,
                   flash: bool, monitor: bool,
                   py: str, idf_py: str, env: dict[str, str]):
        args = [idf_py]
        if port and flash:
            args += ["-p", port]
        args += ["build"]
        if flash:
            args += ["flash"]
        if monitor:
            args += ["monitor"]
        return (label, [py, *args], env, project_dir)

    def _port_for(self, label: str) -> str:
        return self._p4_combo.current_device() if "P4" in label else self._c6_combo.current_device()

    def _run_next(self) -> None:
        if not self._queue:
            self._append_log("\n=== All jobs complete. ===")
            self._set_running(False)
            return

        label, argv, env, cwd = self._queue.pop(0)
        self._append_log(f"\n=== {label}  ({cwd}) ===")
        self._append_log("$ " + " ".join(argv))

        proc = QProcess(self)
        proc.setProcessChannelMode(QProcess.ProcessChannelMode.MergedChannels)
        proc.setWorkingDirectory(str(cwd))
        qenv = proc.processEnvironment()
        for k, v in env.items():
            qenv.insert(k, v)
        proc.setProcessEnvironment(qenv)
        proc.readyReadStandardOutput.connect(lambda: self._on_proc_output(proc))
        proc.finished.connect(lambda code, _status: self._on_proc_finished(label, code))
        proc.errorOccurred.connect(lambda err: self._append_log(f"[process error] {err}"))

        self._proc = proc
        proc.start(argv[0], argv[1:])

    def _on_proc_output(self, proc: QProcess) -> None:
        data = bytes(proc.readAllStandardOutput()).decode("utf-8", errors="replace")
        for line in data.splitlines():
            self._append_log(line)

    def _on_proc_finished(self, label: str, code: int) -> None:
        self._proc = None
        self._append_log(f"--- {label} exited with code {code} ---")
        if code != 0:
            self._queue.clear()
            self._set_running(False)
            return
        self._run_next()

    def _stop(self) -> None:
        if self._proc:
            self._append_log("Stopping…")
            self._proc.kill()
        self._queue.clear()
        self._set_running(False)
