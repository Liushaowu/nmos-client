@echo off
setlocal EnableExtensions

rem ============================================================
rem  Uninstall NMOS Sync Daemon Windows service via bundled NSSM.
rem
rem  Usage:
rem    1. Open Command Prompt as Administrator.
rem    2. Run: uninstall-service.bat
rem ============================================================

set "BASE_DIR=%~dp0"
if "%BASE_DIR:~-1%"=="\" set "BASE_DIR=%BASE_DIR:~0,-1%"

set "SERVICE_NAME=nmos-sync-daemon"
set "NSSM_PATH=%BASE_DIR%\nssm-2.24\win64\nssm.exe"

echo [CHECK] Verifying administrator permission...
net session >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Please run this script from an elevated Command Prompt.
    exit /b 1
)

if not exist "%NSSM_PATH%" (
    echo [ERROR] NSSM not found: "%NSSM_PATH%"
    exit /b 1
)

echo [CHECK] Checking service "%SERVICE_NAME%"...
sc query "%SERVICE_NAME%" >nul 2>&1
if errorlevel 1 (
    echo [INFO] Service does not exist: "%SERVICE_NAME%"
    exit /b 0
)

echo [STOP] Stopping service "%SERVICE_NAME%"...
"%NSSM_PATH%" stop "%SERVICE_NAME%" >nul 2>&1

echo [REMOVE] Removing service "%SERVICE_NAME%"...
"%NSSM_PATH%" remove "%SERVICE_NAME%" confirm
if errorlevel 1 (
    echo [ERROR] NSSM remove failed.
    exit /b 1
)

echo [SUCCESS] Service removed: "%SERVICE_NAME%"
exit /b 0
