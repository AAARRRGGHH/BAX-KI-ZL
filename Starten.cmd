@echo off
setlocal
cd /d "%~dp0"
if not exist "build\Release\SmartMidiSimulator.exe" (
  powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Build.ps1"
  if errorlevel 1 (
    pause
    exit /b 1
  )
)
"build\Release\SmartMidiSimulator.exe" %*
if errorlevel 1 pause
