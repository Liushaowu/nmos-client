<#
  Uninstall NMOS Sync Daemon Windows service installed by install-service.ps1.

  Usage from the portable package directory:
    powershell -ExecutionPolicy Bypass -File .\uninstall-service.ps1
#>

param(
    [string]$ServiceName = "nmos-sync-daemon",
    [string]$NssmPath = ""
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
$nssm = Resolve-Nssm -ExplicitPath $NssmPath -BaseDir $baseDir
if (-not $nssm) {
    Write-Host "ERROR: nssm.exe not found." -ForegroundColor Red
    Write-Host "Put nssm.exe next to uninstall-service.ps1, make it available in PATH, or pass -NssmPath." -ForegroundColor Yellow
    exit 1
}

$existing = Get-Service -Name $ServiceName -ErrorAction SilentlyContinue
if (-not $existing) {
    Write-Host "Service does not exist: $ServiceName" -ForegroundColor Yellow
    exit 0
}

Write-Host "Stopping service: $ServiceName" -ForegroundColor Cyan
& $nssm stop $ServiceName

Write-Host "Removing service: $ServiceName" -ForegroundColor Cyan
& $nssm remove $ServiceName confirm
if ($LASTEXITCODE -ne 0) { throw "nssm remove failed with exit code $LASTEXITCODE" }

Write-Host "Uninstall complete." -ForegroundColor Green
