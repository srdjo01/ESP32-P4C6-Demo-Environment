#!/usr/bin/env bash
# ============================================================
# ESP32-P4C6 Demo Environment — GUI Build Script (macOS/Linux)
# Produces: dist/ESP32-P4C6-Tool.app  (macOS)
#       or: dist/ESP32-P4C6-Tool/     (Linux folder)
# ============================================================
# Usage:
#   ./build_gui.sh             # onedir bundle (default, faster start)
#   ./build_gui.sh --onefile   # single file (portable, slower start)
# ============================================================

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOL="$SCRIPT_DIR/host_tool"
VENV="$TOOL/.venv/bin"
DIST="$SCRIPT_DIR/dist"
ONEFILE=0

for arg in "$@"; do
    [ "$arg" = "--onefile" ] && ONEFILE=1
done

# Ensure PyInstaller is installed (use python -m pip so it works even if the
# pip wrapper script is missing)
if ! "$VENV/python" -c "import PyInstaller" 2>/dev/null; then
    echo "Installing PyInstaller..."
    "$VENV/python" -m pip install pyinstaller
fi

echo "Building GUI application..."
cd "$TOOL"

MODE="--onedir"
[ "$ONEFILE" -eq 1 ] && MODE="--onefile"

"$VENV/python" -m PyInstaller main.py \
    $MODE \
    --windowed \
    --name "ESP32-P4C6-Tool" \
    --distpath "$DIST" \
    --workpath "$SCRIPT_DIR/build_gui_tmp" \
    --specpath "$SCRIPT_DIR/build_gui_tmp" \
    --clean \
    --noconfirm

echo ""
echo "============================================"
if [[ "$OSTYPE" == "darwin"* ]]; then
    echo " Application: dist/ESP32-P4C6-Tool.app"
    echo " Run:         open dist/ESP32-P4C6-Tool.app"
else
    echo " Folder:      dist/ESP32-P4C6-Tool/"
    echo " Run:         dist/ESP32-P4C6-Tool/ESP32-P4C6-Tool"
fi
echo "============================================"
