#!/usr/bin/env bash
# ============================================================
# ESP32-P4C6 Demo Environment — macOS / Linux Setup Script
# ============================================================
# Installs: ESP-IDF v5.4.1, Python venv for GUI, PyInstaller.
#
# Usage:
#   ./setup.sh              # full setup
#   ./setup.sh --skip-idf   # GUI dependencies only
#   ./setup.sh --build-all  # setup + build firmware + .app
# ============================================================

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SKIP_IDF=0
BUILD_ALL=0

for arg in "$@"; do
    case $arg in
        --skip-idf) SKIP_IDF=1 ;;
        --build-all) BUILD_ALL=1 ;;
    esac
done

step()  { echo; echo "==> $1"; }
ok()    { echo "  [OK] $1"; }
fail()  { echo "  [FAIL] $1"; exit 1; }

# ── 1. Prerequisites ─────────────────────────────────────────

step "Checking prerequisites..."

command -v python3 >/dev/null 2>&1 || fail "python3 not found. Install via brew: brew install python"
ok "python3: $(python3 --version)"

command -v git >/dev/null 2>&1 || fail "git not found. Install via brew: brew install git"
ok "git: $(git --version)"

if [[ "$OSTYPE" == "darwin"* ]]; then
    command -v brew >/dev/null 2>&1 || fail "Homebrew not found. Install: /bin/bash -c \"\$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)\""
    ok "Homebrew available"

    # cmake and ninja needed for ESP-IDF
    brew list cmake &>/dev/null || brew install cmake
    brew list ninja &>/dev/null || brew install ninja
    ok "cmake + ninja"
fi

# ── 2. GUI Python venv ───────────────────────────────────────

step "Setting up host_tool Python venv..."

VENV="$SCRIPT_DIR/host_tool/.venv"
if [ ! -f "$VENV/bin/python" ]; then
    echo "  Creating venv..."
    python3 -m venv "$VENV"
fi

echo "  Installing GUI dependencies..."
"$VENV/bin/pip" install -r "$SCRIPT_DIR/host_tool/requirements.txt" --quiet
"$VENV/bin/pip" install pyinstaller --quiet
ok "GUI venv ready at host_tool/.venv"

# ── 3. ESP-IDF via EIM ──────────────────────────────────────

if [ "$SKIP_IDF" -eq 0 ]; then
    step "Installing ESP-IDF v5.4.1..."

    IDF_DIR="$HOME/.espressif/esp-idf/v5.4.1"
    EIM_BIN="$HOME/.espressif/eim/eim"

    if [ -f "$IDF_DIR/tools/idf.py" ]; then
        ok "ESP-IDF v5.4.1 already installed at $IDF_DIR"
    else
        # Download and install EIM
        if [ ! -f "$EIM_BIN" ]; then
            echo "  Downloading EIM..."
            mkdir -p "$HOME/.espressif/eim"
            if [[ "$OSTYPE" == "darwin"* ]]; then
                EIM_URL="https://github.com/espressif/idf-im-ui/releases/download/v0.12.6/eim-cli-macos-x64"
                if [[ "$(uname -m)" == "arm64" ]]; then
                    EIM_URL="https://github.com/espressif/idf-im-ui/releases/download/v0.12.6/eim-cli-macos-arm64"
                fi
            else
                EIM_URL="https://github.com/espressif/idf-im-ui/releases/download/v0.12.6/eim-cli-linux-x64"
            fi
            curl -L "$EIM_URL" -o "$EIM_BIN"
            chmod +x "$EIM_BIN"
        fi

        echo "  Installing ESP-IDF v5.4.1 (may take 15-30 min)..."
        "$EIM_BIN" install \
            --idf-versions v5.4.1 \
            --target esp32p4 \
            --path "$HOME/.espressif" \
            --non-interactive true \
            --cleanup true \
            --do-not-track true

        # Run IDF install script to set up Python venv
        if [ -f "$HOME/.espressif/v5.4.1/esp-idf/install.sh" ]; then
            "$HOME/.espressif/v5.4.1/esp-idf/install.sh" esp32p4
        fi
    fi

    ok "ESP-IDF v5.4.1 ready"
fi

# ── 4. Done ──────────────────────────────────────────────────

echo ""
echo "============================================"
echo " Setup complete!"
echo "============================================"
echo ""
echo " Run GUI:        ./run_gui.sh"
echo " Build firmware: ./build_firmware.sh --flash --port /dev/tty.usbmodem*"
echo " Build .app:     ./build_gui.sh"
echo ""

if [ "$BUILD_ALL" -eq 1 ]; then
    step "Building firmware..."
    "$SCRIPT_DIR/build_firmware.sh"
    step "Building GUI .app..."
    "$SCRIPT_DIR/build_gui.sh"
fi
