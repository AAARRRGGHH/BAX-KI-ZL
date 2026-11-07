@echo off
setlocal
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0PowerDiagnose.ps1" -ReadTrace -Port "%~1"
pause
