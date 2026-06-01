#!/usr/bin/env bash
# Launch the ESP32-P4C6 GUI tool (macOS / Linux)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VENV="$SCRIPT_DIR/host_tool/.venv/bin"
if [ ! -f "$VENV/python" ]; then
    echo "ERROR: venv not found. Run ./setup.sh first."
    exit 1
fi
cd "$SCRIPT_DIR/host_tool"
"$VENV/python" main.py
