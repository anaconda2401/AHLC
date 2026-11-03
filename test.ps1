# AHLC - Run Complete Test Suite
$ErrorActionPreference = "Stop"

Write-Host "================================================================" -ForegroundColor Cyan
Write-Host "  AHLC - Running Complete Test Suite" -ForegroundColor Cyan
Write-Host "================================================================" -ForegroundColor Cyan

$pyBin = "python"
if (Test-Path ".\.venv\Scripts\python.exe") {
    $pyBin = ".\.venv\Scripts\python.exe"
}

& $pyBin scratch\run_all_tests.py
if ($LASTEXITCODE -ne 0) {
    Write-Host "`n[ERROR] One or more test suites failed!" -ForegroundColor Red
    exit 1
} else {
    Write-Host "`n[OK] All AHLC test suites passed successfully!" -ForegroundColor Green
}
