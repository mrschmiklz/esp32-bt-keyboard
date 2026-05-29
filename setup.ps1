# setup.ps1 — install the hardloop host tooling on Windows
#
# Run once (from this folder):
#   powershell -ExecutionPolicy Bypass -File .\setup.ps1            # core CLI tooling
#   powershell -ExecutionPolicy Bypass -File .\setup.ps1 -Bridge    # also install the network bridge
# Then test:
#   python .\hardloop.py --status

param([switch]$Bridge)

$ErrorActionPreference = "Stop"

Write-Host "=== Hardloop Setup (Windows) ===" -ForegroundColor Cyan

# 1. Python 3
$python = Get-Command python -ErrorAction SilentlyContinue
if (-not $python) {
    Write-Host "Python 3 was not found on PATH." -ForegroundColor Yellow
    Write-Host "Install it from https://www.python.org/downloads/ (check 'Add to PATH'),"
    Write-Host "or run:  winget install Python.Python.3"
    exit 1
}
Write-Host ("Using {0}" -f $python.Source)

# 2. Python dependencies
Write-Host "Installing Python dependencies..."
if ($Bridge) {
    $reqs = Join-Path $PSScriptRoot "requirements-bridge.txt"
} else {
    $reqs = Join-Path $PSScriptRoot "requirements.txt"
}
python -m pip install --quiet -r $reqs

# 3. Detect ESP32 serial port
Write-Host ""
Write-Host "Detected serial ports:"
Push-Location $PSScriptRoot
python -c "from port_detect import list_ports_verbose, find_esp32_port; list_ports_verbose(); print(); print('Auto-detected port:', find_esp32_port())"
Pop-Location

Write-Host ""
Write-Host "=== Setup complete ===" -ForegroundColor Green
Write-Host ""
Write-Host "Usage:"
Write-Host "  python .\hardloop.py --status"
Write-Host '  python .\hardloop.py "your message here"'
if ($Bridge) {
    Write-Host "  python .\bridge.py                  # network bridge (localhost)"
} else {
    Write-Host ""
    Write-Host "Add the network bridge later with:  .\setup.ps1 -Bridge"
}
Write-Host ""
Write-Host 'Override port:  $env:HARDLOOP_PORT = "COM6"'
Write-Host "Explicit port:  python .\hardloop.py --port COM5 --status"
