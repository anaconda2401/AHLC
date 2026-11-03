@echo off
setlocal enabledelayedexpansion

echo ================================================================
echo   AHLC — Launching Compression Web Dashboard
echo ================================================================

:: 1. Ensure Python virtual environment exists
if not exist ".venv\Scripts\python.exe" (
    echo [INFO] Python virtual environment not found. Setting up...
    python -m venv .venv
    if %errorlevel% neq 0 (
        py -m venv .venv
    )
    if exist ".venv\Scripts\pip.exe" (
        echo [INFO] Installing dependencies...
        ".venv\Scripts\pip.exe" install -r requirements.txt
    ) else (
        echo [ERROR] Failed to set up virtual environment. Please run:
        echo   python -m venv .venv
        echo   .venv\Scripts\pip install -r requirements.txt
        pause
        exit /b 1
    )
)

:: 2. Check for ahmc executable
if not exist "build\Release\ahmc.exe" (
    if not exist "build\ahmc.exe" (
        echo [WARNING] ahmc.exe not found in build directory.
        echo Please build the C++ engine first by running: build.bat
        echo.
    )
)

:: 3. Start Flask app
echo [OK] Starting Flask application on http://127.0.0.1:5000
echo Press Ctrl+C to stop the server.
echo.
".venv\Scripts\python.exe" flask_app\app.py
pause

