# ============================================================
# ESP32-P4C6 Demo Environment — Windows Setup Script
# ============================================================
# Installs: ESP-IDF v5.4.1 via EIM, Python venv for GUI,
#           PyInstaller for .exe build.
#
# Usage:
#   .\setup.ps1              # full setup
#   .\setup.ps1 -SkipIDF     # samo GUI dependencies
#   .\setup.ps1 -BuildAll    # setup + build firmware + .exe
# ============================================================

param(
    [switch]$SkipIDF,
    [switch]$BuildAll
)

$ErrorActionPreference = "Stop"
$ROOT = $PSScriptRoot

function Write-Step([string]$msg) {
    Write-Host "`n==> $msg" -ForegroundColor Cyan
}

function Write-OK([string]$msg) {
    Write-Host "  [OK] $msg" -ForegroundColor Green
}

function Write-Fail([string]$msg) {
    Write-Host "  [FAIL] $msg" -ForegroundColor Red
}

# ── 1. Prerequisites check ──────────────────────────────────

Write-Step "Checking prerequisites..."

# Python
try {
    $pyver = python --version 2>&1
    Write-OK "Python: $pyver"
} catch {
    Write-Fail "Python not found. Install from https://python.org"
    exit 1
}

# Git
try {
    $gitver = git --version 2>&1
    Write-OK "Git: $gitver"
} catch {
    Write-Fail "Git not found. Install from https://git-scm.com"
    exit 1
}

# winget
try {
    $wgver = winget --version 2>&1
    Write-OK "winget: $wgver"
} catch {
    Write-Fail "winget not found. Update Windows or install App Installer from Microsoft Store."
    exit 1
}

# ── 2. GUI Python venv ──────────────────────────────────────

Write-Step "Setting up host_tool Python venv..."

$VENV = "$ROOT\host_tool\.venv"
if (-not (Test-Path "$VENV\Scripts\python.exe")) {
    Write-Host "  Creating venv..."
    python -m venv $VENV
}

Write-Host "  Installing GUI dependencies..."
# Note: `python -m venv` already provides pip, so ensurepip is not needed (and
# `ensurepip --quiet` is invalid and would abort under ErrorActionPreference=Stop).
& "$VENV\Scripts\python.exe" -m pip install --upgrade pip --quiet
& "$VENV\Scripts\python.exe" -m pip install -r "$ROOT\host_tool\requirements.txt" --quiet
& "$VENV\Scripts\python.exe" -m pip install pyinstaller --quiet
Write-OK "GUI venv ready at host_tool\.venv"

# ── 3. ESP-IDF via EIM ─────────────────────────────────────

if (-not $SkipIDF) {
    Write-Step "Installing ESP-IDF v5.4.1 via EIM..."

    $EIM_PATH = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\Espressif.EIM-CLI_Microsoft.Winget.Source_8wekyb3d8bbwe\eim.exe"
    $IDF_PATH = "C:\Espressif\v5.4.1\esp-idf"

    if (Test-Path $IDF_PATH) {
        Write-OK "ESP-IDF v5.4.1 already installed at $IDF_PATH"
    } else {
        # Install EIM if missing
        if (-not (Test-Path $EIM_PATH)) {
            Write-Host "  Installing EIM..."
            winget install --id Espressif.EIM-CLI --silent --accept-package-agreements --accept-source-agreements
            Start-Sleep -Seconds 3
        }

        if (-not (Test-Path $EIM_PATH)) {
            Write-Fail "EIM not found after install. Try manually: winget install Espressif.EIM-CLI"
            exit 1
        }

        Write-Host "  Downloading and installing ESP-IDF v5.4.1 (may take 15-30 min)..."
        & $EIM_PATH install `
            --idf-versions v5.4.1 `
            --target esp32p4 `
            --path "C:\Espressif" `
            --non-interactive true `
            --install-all-prerequisites true `
            --cleanup true `
            --do-not-track true

        if ($LASTEXITCODE -ne 0) {
            Write-Fail "EIM install failed."
            exit 1
        }
    }

    # Fix toolchain if incomplete (extract from cached ZIP)
    $TOOLCHAIN_CC1 = "C:\Espressif\tools\riscv32-esp-elf\esp-14.2.0_20241119\riscv32-esp-elf\libexec\gcc\riscv32-esp-elf\14.2.0\cc1.exe"
    if (-not (Test-Path $TOOLCHAIN_CC1)) {
        Write-Host "  Toolchain incomplete — re-extracting from cached ZIP..."
        $ZIP = "C:\Users\$env:USERNAME\.espressif\dist\riscv32-esp-elf-14.2.0_20241119-x86_64-w64-mingw32.zip"
        if (-not (Test-Path $ZIP)) {
            $ZIP = "C:\Espressif\tools\dist\riscv32-esp-elf-14.2.0_20241119-x86_64-w64-mingw32.zip"
        }
        if (Test-Path $ZIP) {
            $DEST = "C:\Espressif\tools\riscv32-esp-elf\esp-14.2.0_20241119"
            Remove-Item -Recurse -Force $DEST -ErrorAction SilentlyContinue
            & "C:\Espressif\tools\python\v5.4.1\venv\Scripts\python.exe" -c @"
import zipfile, os
with zipfile.ZipFile(r'$ZIP', 'r') as zf:
    zf.extractall(r'$DEST')
print('Extracted', len(zf.namelist()), 'files')
"@
            Write-OK "Toolchain extracted"
        } else {
            Write-Fail "Toolchain ZIP not found. Re-run setup to download."
        }
    } else {
        Write-OK "Toolchain OK"
    }

    Write-OK "ESP-IDF v5.4.1 ready"
}

# ── 4. Done ─────────────────────────────────────────────────

Write-Host ""
Write-Host "============================================" -ForegroundColor Green
Write-Host " Setup complete!" -ForegroundColor Green
Write-Host "============================================" -ForegroundColor Green
Write-Host ""
Write-Host " Run GUI:          .\run_gui.ps1"
Write-Host " Build firmware:   .\build_firmware.ps1 -Flash -Port COM5"
Write-Host " Build .exe:       .\build_gui.ps1"
Write-Host ""

if ($BuildAll) {
    Write-Step "Building firmware..."
    & "$ROOT\build_firmware.ps1"
    Write-Step "Building GUI .exe..."
    & "$ROOT\build_gui.ps1"
}
