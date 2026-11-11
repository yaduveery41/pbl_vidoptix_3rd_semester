@echo off
setlocal
cd /d "%~dp0"

if not exist vidoptix_backend.exe (
  echo Backend executable not found. Run build.bat first.
  pause
  exit /b 1
)

where python >nul 2>nul
if errorlevel 1 (
  echo ERROR: Python was not found on PATH.
  echo Install Python from python.org and enable "Add Python to PATH".
  pause
  exit /b 1
)

echo Checking Python package installer...
python -m pip --version >nul 2>nul
if errorlevel 1 (
  echo pip is missing. Trying to install it using Python's ensurepip...
  python -m ensurepip --upgrade
  if errorlevel 1 (
    echo.
    echo Could not bootstrap pip for this Python installation.
    echo This often happens with the MSYS2 Python build.
    echo Install standard Windows Python from https://www.python.org/downloads/windows/
    echo During setup, check "Add python.exe to PATH" and "pip".
    echo Then close this window, open a NEW terminal, and run start.bat again.
    pause
    exit /b 1
  )
)

echo Installing/checking VidOptix dependencies...
python -m pip install --user -r requirements.txt
if errorlevel 1 (
  echo.
  echo Could not install Flask.
  echo If this Python came from MSYS2, install standard Windows Python from python.org,
  echo then reopen the terminal and run start.bat again.
  pause
  exit /b 1
)

echo.
echo Starting VidOptix...
echo Open http://127.0.0.1:8765 in Chrome or Edge.
echo Keep this window open while using VidOptix.
python app.py
pause
