#!/usr/bin/env bash
# ============================================================
# ESP32-P4C6 Demo Environment — Firmware Build Script (macOS/Linux)
# ============================================================
# Usage:
#   ./build_firmware.sh                              # build only
#   ./build_firmware.sh --flash                      # build + flash (auto port)
#   ./build_firmware.sh --flash --port /dev/ttyACM0  # build + flash to port
#   ./build_firmware.sh --flash --monitor            # flash + serial monitor
# ============================================================

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FW="$SCRIPT_DIR/firmware"
FLASH=0; MONITOR=0; PORT=""

for arg in "$@"; do
    case $arg in
        --flash)   FLASH=1 ;;
        --monitor) MONITOR=1 ;;
        --port)    shift; PORT="$1" ;;
        --port=*)  PORT="${arg#*=}" ;;
    esac
done

# ── Find IDF ─────────────────────────────────────────────────

IDF_PY=""
for candidate in \
    "$HOME/.espressif/v5.4.1/esp-idf/tools/idf.py" \
    "$HOME/esp/esp-idf/tools/idf.py" \
    "/opt/esp-idf/tools/idf.py"; do
    if [ -f "$candidate" ]; then
        IDF_PY="$candidate"
        IDF_PATH="$(dirname "$(dirname "$candidate")")"
        break
    fi
done

if [ -z "$IDF_PY" ]; then
    echo "ERROR: ESP-IDF not found. Run ./setup.sh first."
    exit 1
fi

# Source IDF export
. "$(dirname "$IDF_PY")/../export.sh" > /dev/null 2>&1 || true
echo "Using IDF: $IDF_PATH"

# ── Build ─────────────────────────────────────────────────────

echo "Building firmware..."
cd "$FW"
python3 "$IDF_PY" build
echo "Build successful."

# ── Flash ─────────────────────────────────────────────────────

if [ "$FLASH" -eq 1 ]; then
    FLASH_ARGS="-p $PORT flash"
    [ -z "$PORT" ] && FLASH_ARGS="flash"
    [ "$MONITOR" -eq 1 ] && FLASH_ARGS="$FLASH_ARGS monitor"
    echo "Flashing..."
    python3 "$IDF_PY" $FLASH_ARGS
    echo "Flash successful."
fi
