@echo off
setlocal
cd /d "%~dp0"
where g++ >nul 2>nul
if errorlevel 1 (
  echo ERROR: g++ was not found. Install MinGW-w64/MSYS2 and add g++ to PATH.
  pause
  exit /b 1
)
where ffmpeg >nul 2>nul
if errorlevel 1 (
  echo ERROR: ffmpeg was not found. Install FFmpeg and add it to PATH.
  pause
  exit /b 1
)
where ffprobe >nul 2>nul
if errorlevel 1 (
  echo ERROR: ffprobe was not found. It is normally included with FFmpeg.
  pause
  exit /b 1
)
echo Building VidOptix C++ backend with custom Huffman coding...
g++ -std=c++17 -O2 vidoptix_backend.cpp -o vidoptix_backend.exe
if errorlevel 1 (
  echo Build failed.
  pause
  exit /b 1
)
echo Build successful: vidoptix_backend.exe
pause
