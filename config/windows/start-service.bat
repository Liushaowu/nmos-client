@echo off
setlocal EnableExtensions

set "SERVICE_NAME=nmos-sync-daemon"

echo [CHECK] Verifying administrator permission...
net session >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Please run this script from an elevated Command Prompt.
    exit /b 1
)

echo [CHECK] Checking service "%SERVICE_NAME%"...
sc query "%SERVICE_NAME%" >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Service does not exist: "%SERVICE_NAME%"
    echo [INFO] Run install-service.bat first.
    exit /b 1
)

echo [START] Starting service "%SERVICE_NAME%"...
sc start "%SERVICE_NAME%"
if errorlevel 1 (
    echo [ERROR] Failed to start service "%SERVICE_NAME%".
    exit /b 1
)

echo [SUCCESS] Service start requested: "%SERVICE_NAME%"
exit /b 0
