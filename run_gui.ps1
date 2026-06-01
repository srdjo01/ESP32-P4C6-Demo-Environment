# Launch the ESP32-P4C6 GUI tool (Windows)
$VENV = "$PSScriptRoot\host_tool\.venv\Scripts"
if (-not (Test-Path "$VENV\python.exe")) {
    Write-Host "ERROR: venv not found. Run .\setup.ps1 first." -ForegroundColor Red
    exit 1
}
Set-Location "$PSScriptRoot\host_tool"
& "$VENV\python.exe" main.py
