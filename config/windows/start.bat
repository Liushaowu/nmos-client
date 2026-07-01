@echo off
chcp 65001 >nul
title NMOS Sync Daemon
cd /d "%~dp0"
echo ========================================
echo   NMOS Sync Daemon - Portable Edition
echo ========================================
echo.
echo Config: %~dp0daemon_config.json
echo.
"%~dp0nmos-sync-daemon.exe" "%~dp0daemon_config.json"
pause