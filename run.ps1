# AHLC — Launch Web Dashboard PowerShell Script
$ErrorActionPreference = "Stop"

Write-Host "================================================================" -ForegroundColor Cyan
Write-Host "  AHLC — Launching Compression Web Dashboard" -ForegroundColor Cyan
Write-Host "================================================================" -ForegroundColor Cyan

# 1. Ensure Python virtual environment exists
if (-not (Test-Path ".venv\Scripts\python.exe")) {
    Write-Host "[INFO] Creating Python virtual environment..." -ForegroundColor Cyan
    python -m venv .venv
    if (Test-Path ".venv\Scripts\pip.exe") {
        Write-Host "[INFO] Installing dependencies..." -ForegroundColor Cyan
        & .\.venv\Scripts\pip.exe install -r requirements.txt
    }
}

# 2. Check for C++ engine
if (-not (Test-Path "build\Release\ahmc.exe") -and -not (Test-Path "build\ahmc.exe")) {
    Write-Host "[WARNING] ahmc.exe not found in build directory." -ForegroundColor Yellow
    Write-Host "Please build the C++ engine first by running: .\build.ps1" -ForegroundColor Yellow
}

# 3. Launch Flask app
Write-Host "`n[OK] Starting Flask application on http://127.0.0.1:5000" -ForegroundColor Green
Write-Host "Press Ctrl+C to stop the server.`n" -ForegroundColor Gray
& .\.venv\Scripts\python.exe flask_app\app.py

