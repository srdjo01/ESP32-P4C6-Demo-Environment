#!/usr/bin/env bash
# ============================================================
# ESP32-P4C6 Demo Environment — Firmware Build Script (macOS/Linux)
# ============================================================
# Builds/flashes the ESP32-P4 app (firmware/) and/or the ESP32-C6
# Wi-Fi/BLE co-processor (firmware_c6/).
#
# Usage:
#   ./build_firmware.sh                                   # build P4 only
#   ./build_firmware.sh --target both                     # build P4 + C6
#   ./build_firmware.sh --flash --port /dev/tty.usbmodem* # build + flash P4
#   ./build_firmware.sh --target c6 --flash --c6-port /dev/tty.usbserial*
#   ./build_firmware.sh --target both --flash --port <p4> --c6-port <c6>
#   ./build_firmware.sh --flash --monitor                # flash P4 + monitor
# ============================================================

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TARGET="p4"; FLASH=0; MONITOR=0; PORT=""; C6PORT=""

while [ $# -gt 0 ]; do
    case "$1" in
        --target)    TARGET="$2"; shift ;;
        --target=*)  TARGET="${1#*=}" ;;
        --flash)     FLASH=1 ;;
        --monitor)   MONITOR=1 ;;
        --port)      PORT="$2"; shift ;;
        --port=*)    PORT="${1#*=}" ;;
        --c6-port)   C6PORT="$2"; shift ;;
        --c6-port=*) C6PORT="${1#*=}" ;;
    esac
    shift
done

# ── Find IDF ─────────────────────────────────────────────────

IDF_PY=""
# Allow caller to override via env var; otherwise probe known locations.
CANDIDATES=()
if [ -n "$IDF_PATH" ]; then
    CANDIDATES+=("$IDF_PATH/tools/idf.py")
fi
CANDIDATES+=(
    "$HOME/.espressif/v5.4.1/esp-idf/tools/idf.py"
    "$HOME/esp/v5.4/esp-idf/tools/idf.py"
    "$HOME/esp/v5.4.1/esp-idf/tools/idf.py"
    "$HOME/esp/esp-idf/tools/idf.py"
    "/opt/esp-idf/tools/idf.py"
)
for candidate in "${CANDIDATES[@]}"; do
    if [ -f "$candidate" ]; then
        IDF_PY="$candidate"
        break
    fi
done

if [ -z "$IDF_PY" ]; then
    echo "ERROR: ESP-IDF not found. Run ./setup.sh first."
    exit 1
fi

# Source IDF export so the toolchain + python env are on PATH.
# IDF v5.4 doesn't ship a venv for python 3.14 — pin to 3.13 if available.
if command -v python3.13 >/dev/null 2>&1; then
    export IDF_PYTHON_ENV_PATH="$HOME/.espressif/python_env/idf5.4_py3.13_env"
    export ESP_PYTHON="$(command -v python3.13)"
fi
# export.sh can return non-zero on benign warnings, so disable -e around it.
set +e
. "$(dirname "$IDF_PY")/../export.sh" > /dev/null 2>&1
set -e
echo "Using IDF: $(dirname "$(dirname "$IDF_PY")")"

# ── Build / flash one firmware project ───────────────────────

build_fw() {
    local dir="$1" name="$2" flashport="$3"
    echo ""
    echo "=== $name  ($dir) ==="
    cd "$dir"
    python3 "$IDF_PY" build
    echo "$name build successful."

    if [ "$FLASH" -eq 1 ]; then
        local args="flash"
        [ -n "$flashport" ] && args="-p $flashport flash"
        [ "$MONITOR" -eq 1 ] && args="$args monitor"
        echo "Flashing $name..."
        python3 "$IDF_PY" $args
        echo "$name flash successful."
    fi
}

# ── Dispatch ─────────────────────────────────────────────────

case "$TARGET" in
    p4)   build_fw "$SCRIPT_DIR/firmware"    "ESP32-P4" "$PORT" ;;
    c6)   build_fw "$SCRIPT_DIR/firmware_c6" "ESP32-C6" "$C6PORT" ;;
    both) build_fw "$SCRIPT_DIR/firmware"    "ESP32-P4" "$PORT"
          build_fw "$SCRIPT_DIR/firmware_c6" "ESP32-C6" "$C6PORT" ;;
    *)    echo "ERROR: --target must be p4, c6, or both (got '$TARGET')"; exit 1 ;;
esac
