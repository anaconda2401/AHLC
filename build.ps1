# AHLC — Build and Setup PowerShell Script
$ErrorActionPreference = "Stop"

Write-Host "================================================================" -ForegroundColor Cyan
Write-Host "  AHLC — Build and Setup Script" -ForegroundColor Cyan
Write-Host "================================================================" -ForegroundColor Cyan

# 1. Locate CMake
$cmakeBin = (Get-Command cmake -ErrorAction SilentlyContinue)?.Source
if (-not $cmakeBin) {
    $candidates = @(
        "C:\Program Files\CMake\bin\cmake.exe",
        "E:\Programs\VisualStudio\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    )
    foreach ($cand in $candidates) {
        if (Test-Path $cand) {
            $cmakeBin = $cand
            break
        }
    }
}

if (-not $cmakeBin) {
    Write-Host "[ERROR] CMake not found. Please install CMake 3.20+ or ensure it is in PATH." -ForegroundColor Red
    exit 1
}
Write-Host "[OK] Using CMake: $cmakeBin" -ForegroundColor Green

# 2. Locate vcpkg toolchain
$vcpkgToolchain = $null
if ($env:VCPKG_ROOT -and (Test-Path "$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake")) {
    $vcpkgToolchain = "$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake"
} else {
    $searchPaths = @(
        "$HOME\vcpkg\scripts\buildsystems\vcpkg.cmake",
        "C:\vcpkg\scripts\buildsystems\vcpkg.cmake",
        "E:\Programs\vcpkg-ahcl\scripts\buildsystems\vcpkg.cmake"
    )
    foreach ($path in $searchPaths) {
        if (Test-Path $path) {
            $vcpkgToolchain = $path
            break
        }
    }
}

if ($vcpkgToolchain) {
    Write-Host "[OK] Using vcpkg toolchain: $vcpkgToolchain" -ForegroundColor Green
    & $cmakeBin -S . -B build -DCMAKE_TOOLCHAIN_FILE="$vcpkgToolchain" -DVCPKG_TARGET_TRIPLET=x64-windows
} else {
    Write-Host "[WARNING] vcpkg.cmake not found. Attempting configure without explicit toolchain..." -ForegroundColor Yellow
    & $cmakeBin -S . -B build
}

if ($LASTEXITCODE -ne 0) {
    Write-Host "[ERROR] CMake configuration failed." -ForegroundColor Red
    exit 1
}

# 3. Build Release binary
Write-Host "`n[INFO] Building AHLC engine (Release)..." -ForegroundColor Cyan
& $cmakeBin --build build --config Release
if ($LASTEXITCODE -ne 0) {
    Write-Host "[ERROR] Build failed." -ForegroundColor Red
    exit 1
}
Write-Host "[OK] C++ engine built successfully." -ForegroundColor Green

# 4. Setup Python environment
Write-Host "`n[INFO] Setting up Python virtual environment..." -ForegroundColor Cyan
if (-not (Test-Path ".venv")) {
    python -m venv .venv
}

if (Test-Path ".\.venv\Scripts\pip.exe") {
    Write-Host "[INFO] Installing Python dependencies..." -ForegroundColor Cyan
    & .\.venv\Scripts\pip.exe install -r requirements.txt
    Write-Host "[OK] Python dependencies installed." -ForegroundColor Green
} else {
    Write-Host "[WARNING] .venv not found. Install requirements manually: pip install -r requirements.txt" -ForegroundColor Yellow
}

Write-Host "`n================================================================" -ForegroundColor Cyan
Write-Host "  AHLC Build Complete!" -ForegroundColor Cyan
Write-Host "  To run web dashboard:  .\run.ps1  or  .\run.bat" -ForegroundColor White
Write-Host "  To run test suite:     .\test.ps1 or  .\test.bat" -ForegroundColor White
Write-Host "================================================================" -ForegroundColor Cyan

