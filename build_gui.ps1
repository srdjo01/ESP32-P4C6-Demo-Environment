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

# Ensure pip and PyInstaller are available
& $PY -m ensurepip --upgrade --quiet 2>$null
if (-not (& $PY -c "import PyInstaller" 2>$null)) {
    Write-Host "Installing PyInstaller..." -ForegroundColor Cyan
    & $PY -m pip install pyinstaller --quiet
}

Write-Host "Building GUI executable..." -ForegroundColor Cyan
Set-Location $TOOL

$mode = if ($OneDir) { "--onedir" } else { "--onefile" }

& $PY -m PyInstaller main.py `
    $mode `
    --windowed `
    --name "ESP32-P4C6-Tool" `
    --distpath "$DIST" `
    --workpath "$ROOT\build_gui_tmp" `
    --specpath "$ROOT\build_gui_tmp" `
    --clean `
    --noconfirm

if ($LASTEXITCODE -ne 0) {
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
