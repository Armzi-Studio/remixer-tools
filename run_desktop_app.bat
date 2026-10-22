@echo off
title Remixer Tools Launcher
cd /d "%~dp0"

set "EXE_PATH=%~dp0bin\remixer_desktop_app.exe"
if not exist "%EXE_PATH%" (
    set "EXE_PATH=%~dp0build\remixer_desktop_app.exe"
)

if not exist "%EXE_PATH%" (
    echo [ERROR] Remixer Tools Desktop executable not found!
    echo Looked at:
    echo   - %~dp0bin\remixer_desktop_app.exe
    echo   - %~dp0build\remixer_desktop_app.exe
    pause
    exit /b 1
)

echo [OK] Launching Remixer Tools Desktop ...
start "" "%EXE_PATH%"
exit /b 0
