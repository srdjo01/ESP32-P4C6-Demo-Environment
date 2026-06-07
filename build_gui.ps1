# ============================================================
# ESP32-P4C6 Demo Environment — GUI Build Script (Windows)
# Produces: dist\ESP32-P4C6-Tool\ESP32-P4C6-Tool.exe  (folder)
#       or: dist\ESP32-P4C6-Tool.exe                  (single file, slower start)
# ============================================================

param(
    [switch]$OneDir     # use --onedir (faster start, but copies whole folder)
                        # default is --onefile (single portable .exe)
)

$ErrorActionPreference = "Stop"
$ROOT  = $PSScriptRoot
$TOOL  = "$ROOT\host_tool"
$VENV  = "$TOOL\.venv\Scripts"
$DIST  = "$ROOT\dist"
$PY    = "$VENV\python.exe"

# Ensure PyInstaller is available. Probe with ErrorActionPreference relaxed so a
# missing-import (non-zero exit) does not abort the script under "Stop".
$ErrorActionPreference = "Continue"
& $PY -c "import PyInstaller" 2>$null
$havePyInstaller = ($LASTEXITCODE -eq 0)
$ErrorActionPreference = "Stop"
if (-not $havePyInstaller) {
    Write-Host "Installing PyInstaller..." -ForegroundColor Cyan
    & $PY -m pip install pyinstaller
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Failed to install PyInstaller." -ForegroundColor Red
        exit 1
    }
}

Write-Host "Building GUI executable..." -ForegroundColor Cyan
Set-Location $TOOL

$mode = if ($OneDir) { "--onedir" } else { "--onefile" }

# PyInstaller writes its normal progress to stderr; relax ErrorActionPreference
# so that does not abort the script under "Stop". Check the exit code instead.
$ErrorActionPreference = "Continue"
& $PY -m PyInstaller main.py `
    $mode `
    --windowed `
    --name "ESP32-P4C6-Tool" `
    --distpath "$DIST" `
    --workpath "$ROOT\build_gui_tmp" `
    --specpath "$ROOT\build_gui_tmp" `
    --clean `
    --noconfirm
$pyiExit = $LASTEXITCODE
$ErrorActionPreference = "Stop"

if ($pyiExit -ne 0) {
    Write-Host "PyInstaller failed." -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "============================================" -ForegroundColor Green
if ($OneDir) {
    Write-Host " Folder:     dist\ESP32-P4C6-Tool\" -ForegroundColor Green
    Write-Host " Run:        dist\ESP32-P4C6-Tool\ESP32-P4C6-Tool.exe" -ForegroundColor Green
} else {
    Write-Host " Executable: dist\ESP32-P4C6-Tool.exe (single file, portable)" -ForegroundColor Green
}
Write-Host "============================================" -ForegroundColor Green
