@echo off
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Firmware.ps1" -Action Build
if errorlevel 1 pause
