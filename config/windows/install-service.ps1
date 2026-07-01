<#
  Install NMOS Sync Daemon as a silent Windows background service.

  This executable is a console app, not a native Windows Service binary.
  The script uses NSSM (Non-Sucking Service Manager) as a service wrapper.

  Usage from the portable package directory:
    powershell -ExecutionPolicy Bypass -File .\install-service.ps1

  Optional:
    powershell -ExecutionPolicy Bypass -File .\install-service.ps1 `
      -ServiceName "nmos-sync-daemon" `
      -NssmPath "C:\tools\nssm\win64\nssm.exe"

  Prerequisite:
    Put nssm.exe next to this script, or install NSSM and make nssm.exe available in PATH.
#>

param(
    [string]$ServiceName = "nmos-sync-daemon",
    [string]$DisplayName = "NMOS Sync Daemon",
    [string]$Description = "NMOS Sync Daemon portable Windows service",
    [string]$NssmPath = "",
    [switch]$DelayedAutoStart
)

$ErrorActionPreference = "Stop"

function Test-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Resolve-Nssm {
    param([string]$ExplicitPath, [string]$BaseDir)

    $candidates = @()
    if ($ExplicitPath) { $candidates += $ExplicitPath }
    $candidates += (Join-Path $BaseDir "nssm.exe")
    $candidates += (Join-Path $BaseDir "tools\nssm.exe")
    $candidates += (Join-Path $BaseDir "tools\nssm\nssm.exe")
    $candidates += (Join-Path $BaseDir "tools\nssm\win64\nssm.exe")

    foreach ($candidate in $candidates) {
        if ($candidate -and (Test-Path -LiteralPath $candidate)) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    $command = Get-Command nssm.exe -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    return $null
}

if (-not (Test-Administrator)) {
    Write-Host "ERROR: Please run this script from an elevated PowerShell window." -ForegroundColor Red
    exit 1
}

$baseDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$exePath = Join-Path $baseDir "nmos-sync-daemon.exe"
$configPath = Join-Path $baseDir "daemon_config.json"
$logsDir = Join-Path $baseDir "logs"

if (-not (Test-Path -LiteralPath $exePath)) {
    Write-Host "ERROR: nmos-sync-daemon.exe not found: $exePath" -ForegroundColor Red
    exit 1
}
if (-not (Test-Path -LiteralPath $configPath)) {
    Write-Host "ERROR: daemon_config.json not found: $configPath" -ForegroundColor Red
    exit 1
}

$nssm = Resolve-Nssm -ExplicitPath $NssmPath -BaseDir $baseDir
if (-not $nssm) {
    Write-Host "ERROR: nssm.exe not found." -ForegroundColor Red
    Write-Host "Download NSSM and put nssm.exe next to install-service.ps1, or pass -NssmPath." -ForegroundColor Yellow
    Write-Host "NSSM: https://nssm.cc/download" -ForegroundColor Yellow
    exit 1
}

if (-not (Test-Path -LiteralPath $logsDir)) {
    New-Item -ItemType Directory -Path $logsDir | Out-Null
}

$existing = Get-Service -Name $ServiceName -ErrorAction SilentlyContinue
if ($existing) {
    Write-Host "Service already exists: $ServiceName" -ForegroundColor Yellow
    Write-Host "Use uninstall-service.ps1 first if you want a clean reinstall." -ForegroundColor Yellow
    exit 1
}

Write-Host "Installing service: $ServiceName" -ForegroundColor Cyan
Write-Host "NSSM       : $nssm" -ForegroundColor Gray
Write-Host "Executable : $exePath" -ForegroundColor Gray
Write-Host "Config     : $configPath" -ForegroundColor Gray
Write-Host "Work dir   : $baseDir" -ForegroundColor Gray
Write-Host "Logs       : $logsDir" -ForegroundColor Gray

& $nssm install $ServiceName $exePath $configPath
if ($LASTEXITCODE -ne 0) { throw "nssm install failed with exit code $LASTEXITCODE" }

& $nssm set $ServiceName DisplayName $DisplayName | Out-Null
& $nssm set $ServiceName Description $Description | Out-Null
& $nssm set $ServiceName AppDirectory $baseDir | Out-Null
& $nssm set $ServiceName AppStdout (Join-Path $logsDir "stdout.log") | Out-Null
& $nssm set $ServiceName AppStderr (Join-Path $logsDir "stderr.log") | Out-Null
& $nssm set $ServiceName AppRotateFiles 1 | Out-Null
& $nssm set $ServiceName AppRotateOnline 1 | Out-Null
& $nssm set $ServiceName AppRotateBytes 10485760 | Out-Null
& $nssm set $ServiceName Start SERVICE_AUTO_START | Out-Null
& $nssm set $ServiceName AppStopMethodConsole 15000 | Out-Null

if ($DelayedAutoStart) {
    sc.exe config $ServiceName start= delayed-auto | Out-Null
}

Write-Host "Starting service..." -ForegroundColor Cyan
& $nssm start $ServiceName
if ($LASTEXITCODE -ne 0) { throw "nssm start failed with exit code $LASTEXITCODE" }

Start-Sleep -Seconds 2
$service = Get-Service -Name $ServiceName
Write-Host "Service status: $($service.Status)" -ForegroundColor Green
Write-Host "Install complete." -ForegroundColor Green
