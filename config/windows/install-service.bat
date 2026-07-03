@echo off
setlocal EnableExtensions

rem ============================================================
rem  Install NMOS Sync Daemon as a Windows service via bundled NSSM.
rem
rem  Usage:
rem    1. Extract the portable package.
rem    2. Open Command Prompt as Administrator.
rem    3. Run: install-service.bat
rem
rem ============================================================

set "BASE_DIR=%~dp0"
if "%BASE_DIR:~-1%"=="\" set "BASE_DIR=%BASE_DIR:~0,-1%"

set "SERVICE_NAME=nmos-sync-daemon"
set "DISPLAY_NAME=NMOS Sync Daemon"
set "DESCRIPTION=NMOS Sync Daemon portable Windows service"
set "EXE_PATH=%BASE_DIR%\nmos-sync-daemon.exe"
set "CONFIG_PATH=%BASE_DIR%\daemon_config.json"
set "NSSM_PATH=%BASE_DIR%\nssm-2.24\win64\nssm.exe"
set "LOG_DIR=%BASE_DIR%\logs"

echo [CHECK] Verifying administrator permission...
net session >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Please run this script from an elevated Command Prompt.
    exit /b 1
)

echo [CHECK] Verifying packaged files...
if not exist "%NSSM_PATH%" (
    echo [ERROR] NSSM not found: "%NSSM_PATH%"
    echo [ERROR] Expected bundled path: nssm-2.24\win64\nssm.exe
    exit /b 1
)

if not exist "%EXE_PATH%" (
    echo [ERROR] nmos-sync-daemon.exe not found: "%EXE_PATH%"
    exit /b 1
)

if not exist "%CONFIG_PATH%" (
    echo [ERROR] daemon_config.json not found: "%CONFIG_PATH%"
    exit /b 1
)

if not exist "%LOG_DIR%" mkdir "%LOG_DIR%"
if errorlevel 1 (
    echo [ERROR] Failed to create log directory: "%LOG_DIR%"
    exit /b 1
)

echo [CHECK] Checking existing service "%SERVICE_NAME%"...
sc query "%SERVICE_NAME%" >nul 2>&1
if not errorlevel 1 (
    echo [ERROR] Service already exists: "%SERVICE_NAME%"
    echo [ERROR] Run uninstall-service.bat first if you want a clean reinstall.
    exit /b 1
)

echo [INSTALL] Installing service "%SERVICE_NAME%"...
"%NSSM_PATH%" install "%SERVICE_NAME%" "%EXE_PATH%" "%CONFIG_PATH%"
if errorlevel 1 (
    echo [ERROR] NSSM install failed.
    exit /b 1
)

echo [CONFIG] Applying service configuration...
"%NSSM_PATH%" set "%SERVICE_NAME%" DisplayName "%DISPLAY_NAME%" >nul
if errorlevel 1 goto :config_failed

"%NSSM_PATH%" set "%SERVICE_NAME%" Description "%DESCRIPTION%" >nul
if errorlevel 1 goto :config_failed

"%NSSM_PATH%" set "%SERVICE_NAME%" AppDirectory "%BASE_DIR%" >nul
if errorlevel 1 goto :config_failed

"%NSSM_PATH%" set "%SERVICE_NAME%" AppStdout "%LOG_DIR%\stdout.log" >nul
if errorlevel 1 goto :config_failed

"%NSSM_PATH%" set "%SERVICE_NAME%" AppStderr "%LOG_DIR%\stderr.log" >nul
if errorlevel 1 goto :config_failed

"%NSSM_PATH%" set "%SERVICE_NAME%" AppRotateFiles 1 >nul
if errorlevel 1 goto :config_failed

"%NSSM_PATH%" set "%SERVICE_NAME%" AppRotateOnline 1 >nul
if errorlevel 1 goto :config_failed

"%NSSM_PATH%" set "%SERVICE_NAME%" AppRotateBytes 10485760 >nul
if errorlevel 1 goto :config_failed

"%NSSM_PATH%" set "%SERVICE_NAME%" Start SERVICE_AUTO_START >nul
if errorlevel 1 goto :config_failed

"%NSSM_PATH%" set "%SERVICE_NAME%" AppStopMethodConsole 15000 >nul
if errorlevel 1 goto :config_failed

"%NSSM_PATH%" set "%SERVICE_NAME%" ObjectName LocalSystem >nul
if errorlevel 1 goto :config_failed

echo [START] Starting service "%SERVICE_NAME%"...
"%NSSM_PATH%" start "%SERVICE_NAME%"
if errorlevel 1 (
    echo [ERROR] NSSM start failed. Check logs under "%LOG_DIR%".
    exit /b 1
)

echo [SUCCESS] Service installed and started: "%SERVICE_NAME%"
echo [INFO] NSSM: "%NSSM_PATH%"
echo [INFO] Logs: "%LOG_DIR%"
exit /b 0

:config_failed
echo [ERROR] NSSM service configuration failed.
echo [INFO] Cleaning up partially installed service...
"%NSSM_PATH%" remove "%SERVICE_NAME%" confirm >nul 2>&1
exit /b 1
