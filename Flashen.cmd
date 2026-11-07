@echo off
setlocal
cd /d "%~dp0"
set "SMARTMIDI_PORT=%~1"
if not defined SMARTMIDI_PORT set /p "SMARTMIDI_PORT=COM-Port des Waveshare (z.B. COM7): "
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Firmware.ps1" -Action Flash -Port "%SMARTMIDI_PORT%"
pause
