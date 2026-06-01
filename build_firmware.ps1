# ============================================================
# ESP32-P4C6 Demo Environment — Firmware Build Script (Windows)
# ============================================================
# Usage:
#   .\build_firmware.ps1                    # build only
#   .\build_firmware.ps1 -Flash             # build + flash (auto-detect port)
#   .\build_firmware.ps1 -Flash -Port COM5  # build + flash to COM5
#   .\build_firmware.ps1 -Flash -Monitor    # flash + open serial monitor
# ============================================================

param(
    [switch]$Flash,
    [switch]$Monitor,
    [string]$Port = "COM5"
)

$ErrorActionPreference = "Stop"
$ROOT = $PSScriptRoot
$FW   = "$ROOT\firmware"

# ── IDF environment ──────────────────────────────────────────

$IDF_PYTHON   = "C:\Espressif\tools\python\v5.4.1\venv\Scripts\python.exe"
$IDF_PY       = "C:\Espressif\v5.4.1\esp-idf\tools\idf.py"

if (-not (Test-Path $IDF_PYTHON)) {
    Write-Host "ERROR: ESP-IDF not found. Run .\setup.ps1 first." -ForegroundColor Red
    exit 1
}

$env:IDF_PATH                   = "C:\Espressif\v5.4.1\esp-idf"
$env:IDF_TOOLS_PATH             = "C:\Espressif\tools"
$env:IDF_PYTHON_ENV_PATH        = "C:\Espressif\tools\python\v5.4.1\venv"
$env:IDF_COMPONENT_LOCAL_STORAGE_URL = "file://C:\Espressif\tools"
$env:ESP_ROM_ELF_DIR            = "C:\Espressif\tools\esp-rom-elfs\20241011"
$env:OPENOCD_SCRIPTS            = "C:\Espressif\tools\openocd-esp32\v0.12.0-esp32-20241016\openocd-esp32\share\openocd\scripts"
$env:PATH = "C:\Espressif\tools\ccache\4.10.2\ccache-4.10.2-windows-x86_64;" +
            "C:\Espressif\tools\cmake\3.30.2\bin;" +
            "C:\Espressif\tools\ninja\1.12.1;" +
            "C:\Espressif\tools\riscv32-esp-elf\esp-14.2.0_20241119\riscv32-esp-elf\bin;" +
            "C:\Espressif\tools\python\v5.4.1\venv\Scripts;" +
            $env:PATH

# ── Build ────────────────────────────────────────────────────

Write-Host "Building firmware..." -ForegroundColor Cyan
Set-Location $FW
& $IDF_PYTHON $IDF_PY build
if ($LASTEXITCODE -ne 0) { Write-Host "Build failed." -ForegroundColor Red; exit 1 }
Write-Host "Build successful." -ForegroundColor Green

# ── Flash ────────────────────────────────────────────────────

if ($Flash) {
    Write-Host "Flashing to $Port..." -ForegroundColor Cyan
    if ($Monitor) {
        & $IDF_PYTHON $IDF_PY -p $Port flash monitor
    } else {
        & $IDF_PYTHON $IDF_PY -p $Port flash
    }
    if ($LASTEXITCODE -ne 0) { Write-Host "Flash failed." -ForegroundColor Red; exit 1 }
    Write-Host "Flash successful." -ForegroundColor Green
}
