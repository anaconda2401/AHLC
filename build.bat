@echo off
setlocal enabledelayedexpansion

echo ================================================================
echo   AHLC — Build and Setup Script
echo ================================================================

:: 1. Locate CMake
where cmake >nul 2>&1
if %errorlevel% equ 0 (
    set "CMAKE_BIN=cmake"
) else if exist "C:\Program Files\CMake\bin\cmake.exe" (
    set "CMAKE_BIN=C:\Program Files\CMake\bin\cmake.exe"
) else if exist "E:\Programs\VisualStudio\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" (
    set "CMAKE_BIN=E:\Programs\VisualStudio\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
) else (
    echo [ERROR] CMake not found. Please install CMake 3.20+ or ensure it is in your PATH.
    pause
    exit /b 1
)
echo [OK] Using CMake: !CMAKE_BIN!

:: 2. Locate vcpkg toolchain
set "VCPKG_CMAKE="
if defined VCPKG_ROOT (
    if exist "%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" (
        set "VCPKG_CMAKE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake"
    )
)
if not defined VCPKG_CMAKE (
    if exist "%USERPROFILE%\vcpkg\scripts\buildsystems\vcpkg.cmake" (
        set "VCPKG_CMAKE=%USERPROFILE%\vcpkg\scripts\buildsystems\vcpkg.cmake"
    ) else if exist "C:\vcpkg\scripts\buildsystems\vcpkg.cmake" (
        set "VCPKG_CMAKE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake"
    ) else if exist "E:\Programs\vcpkg-ahcl\scripts\buildsystems\vcpkg.cmake" (
        set "VCPKG_CMAKE=E:\Programs\vcpkg-ahcl\scripts\buildsystems\vcpkg.cmake"
    )
)

if not defined VCPKG_CMAKE (
    echo [WARNING] vcpkg.cmake toolchain not found in standard paths.
    echo Please set VCPKG_ROOT environment variable (e.g. set VCPKG_ROOT=C:\path\to\vcpkg)
    echo Attempting CMake configure without explicit toolchain...
    "!CMAKE_BIN!" -S . -B build
) else (
    echo [OK] Using vcpkg toolchain: !VCPKG_CMAKE!
    "!CMAKE_BIN!" -S . -B build -DCMAKE_TOOLCHAIN_FILE="!VCPKG_CMAKE!" -DVCPKG_TARGET_TRIPLET=x64-windows
)

if %errorlevel% neq 0 (
    echo [ERROR] CMake configuration failed.
    pause
    exit /b 1
)

:: 3. Build C++ Release binary
echo.
echo [INFO] Building AHLC engine (Release)...
"!CMAKE_BIN!" --build build --config Release
if %errorlevel% neq 0 (
    echo [ERROR] Build failed.
    pause
    exit /b 1
)
echo [OK] C++ engine built successfully.

:: 4. Python Environment Setup
echo.
echo [INFO] Setting up Python virtual environment...
if not exist ".venv" (
    python -m venv .venv
    if %errorlevel% neq 0 (
        echo [WARNING] Failed to create virtual environment with 'python'. Trying 'py'...
        py -m venv .venv
    )
)

if exist ".venv\Scripts\pip.exe" (
    echo [INFO] Installing Python dependencies from requirements.txt...
    ".venv\Scripts\pip.exe" install -r requirements.txt
    echo [OK] Python dependencies installed.
) else (
    echo [WARNING] .venv\Scripts\pip.exe not found. Please set up a Python virtualenv manually:
    echo   python -m venv .venv
    echo   .venv\Scripts\pip install -r requirements.txt
)

echo.
echo ================================================================
echo   AHLC Build Complete!
echo   To start the web dashboard, run: run.bat
echo   To run the test suite, run: test.bat
echo ================================================================

