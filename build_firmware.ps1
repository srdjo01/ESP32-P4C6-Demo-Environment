# ============================================================
# ESP32-P4C6 Demo Environment — Firmware Build Script (Windows)
# ============================================================
# Builds/flashes the ESP32-P4 app (firmware/) and/or the ESP32-C6
# Wi-Fi/BLE co-processor (firmware_c6/).
#
# Usage:
#   .\build_firmware.ps1                              # build P4 only
#   .\build_firmware.ps1 -Target both                # build P4 + C6
#   .\build_firmware.ps1 -Flash -Port COM5           # build + flash P4 to COM5
#   .\build_firmware.ps1 -Target c6 -Flash -C6Port COM4   # build + flash C6
#   .\build_firmware.ps1 -Target both -Flash -Port COM5 -C6Port COM4
#   .\build_firmware.ps1 -Flash -Monitor             # flash P4 + serial monitor
# ============================================================

param(
    [ValidateSet("p4", "c6", "both")]
    [string]$Target = "p4",
    [switch]$Flash,
    [switch]$Monitor,
    [string]$Port   = "COM5",   # P4 flash port (USB-Serial/JTAG)
    [string]$C6Port = "COM4"    # C6 flash port (CH340)
)

$ErrorActionPreference = "Stop"
$ROOT = $PSScriptRoot

# ── IDF environment ──────────────────────────────────────────

$IDF_PYTHON   = "C:\Espressif\tools\python\v5.4.1\venv\Scripts\python.exe"
$IDF_PY       = "C:\Espressif\v5.4.1\esp-idf\tools\idf.py"

if (-not (Test-Path $IDF_PYTHON)) {
    Write-Host "ERROR: ESP-IDF not found at $IDF_PYTHON. Run .\setup.ps1 first." -ForegroundColor Red
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

# ── Build / flash one firmware project ───────────────────────

function Invoke-FwBuild {
    param([string]$Dir, [string]$Name, [string]$FlashPort)

    Write-Host ""
    Write-Host "=== $Name  ($Dir) ===" -ForegroundColor Cyan
    Set-Location $Dir
    & $IDF_PYTHON $IDF_PY build
    if ($LASTEXITCODE -ne 0) { Write-Host "$Name build failed." -ForegroundColor Red; exit 1 }
    Write-Host "$Name build successful." -ForegroundColor Green

    if ($Flash) {
        Write-Host "Flashing $Name to $FlashPort..." -ForegroundColor Cyan
        if ($Monitor) {
            & $IDF_PYTHON $IDF_PY -p $FlashPort flash monitor
        } else {
            & $IDF_PYTHON $IDF_PY -p $FlashPort flash
        }
        if ($LASTEXITCODE -ne 0) { Write-Host "$Name flash failed." -ForegroundColor Red; exit 1 }
        Write-Host "$Name flash successful." -ForegroundColor Green
    }
}

# ── Dispatch ─────────────────────────────────────────────────

if ($Target -eq "p4" -or $Target -eq "both") {
    Invoke-FwBuild -Dir "$ROOT\firmware"    -Name "ESP32-P4" -FlashPort $Port
}
if ($Target -eq "c6" -or $Target -eq "both") {
    Invoke-FwBuild -Dir "$ROOT\firmware_c6" -Name "ESP32-C6" -FlashPort $C6Port
}
