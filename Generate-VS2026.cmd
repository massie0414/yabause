@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\Generate-VS2026.ps1" %*
if errorlevel 1 exit /b 1
