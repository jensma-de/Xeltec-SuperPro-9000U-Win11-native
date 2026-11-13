@echo off
setlocal
title SUPERPRO 9000U - Prepare software
where py >nul 2>nul
if not errorlevel 1 (
    py -3 "%~dp0prepare.py"
) else (
    where python >nul 2>nul
    if errorlevel 1 (
        echo Python 3.10 or newer is required.
        echo Download it from https://www.python.org/downloads/windows/
        pause
        exit /b 1
    )
    python "%~dp0prepare.py"
)
if errorlevel 1 (
    echo Preparation failed. Check the message above and README.md.
    pause
    exit /b 1
)
pause
