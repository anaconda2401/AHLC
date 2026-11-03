@echo off
setlocal enabledelayedexpansion

echo ================================================================
echo   AHLC — Running Complete Test Suite
echo ================================================================

if exist ".venv\Scripts\python.exe" (
    set "PY_BIN=.venv\Scripts\python.exe"
) else (
    set "PY_BIN=python"
)

"%PY_BIN%" scratch\run_all_tests.py
if %errorlevel% neq 0 (
    echo.
    echo [ERROR] One or more test suites failed!
    pause
    exit /b 1
)

echo.
echo [OK] All AHLC test suites passed successfully!
pause

