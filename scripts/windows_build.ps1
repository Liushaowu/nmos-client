# NMOS Sync Daemon - Windows Static Build & Package Script (VS 2022)
#
# Usage:
#   .\scripts\windows_build.ps1
#   .\scripts\windows_build.ps1 -Debug
#   .\scripts\windows_build.ps1 -Configuration Debug -RunTests
#   .\scripts\windows_build.ps1 -Configuration Release -SkipPackage
#
# Prerequisites:
#   - Visual Studio 2022 (Desktop development with C++)
#   - MSVC v143, recommended extra: MSVC v142 for some Conan dependencies
#   - CMake >= 3.23
#   - Python 3 + Conan 2.x
#   - Npcap Runtime/SDK when LLDP is enabled

param(
    [ValidateSet("Release", "Debug")]
    [string]$Configuration = "Release",

    # Backward-compatible shortcut used by earlier versions of this script.
    [switch]$Debug,

    # Skip the Conan install step when dependencies/toolchain are already generated.
    [switch]$SkipConanInstall,

    # Build only; do not run install/cpack.
    [switch]$SkipPackage,

    # Run CTest after build.
    [switch]$RunTests
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = "Stop"

if ($Debug) {
    $Configuration = "Debug"
}

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$binaryDirName = if ($Configuration -eq "Debug") { "win-debug" } else { "win-release" }
$binaryDir = Join-Path $projectRoot (Join-Path "build" $binaryDirName)
$portableDir = Join-Path $binaryDir "portable"
$preset = if ($Configuration -eq "Debug") { "win-debug" } else { "win-release" }
$profilePath = Join-Path $PSScriptRoot "conan_profile_vs2022"
$userPresetsPath = Join-Path $projectRoot "CMakeUserPresets.json"
$userPresetsBackupPath = Join-Path $projectRoot "CMakeUserPresets.json.conan-generated.bak"
$originalCl = $env:_CL_
$userPresetsMoved = $false

function Write-Banner {
    param([string]$Message)
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host " $Message" -ForegroundColor Cyan
    Write-Host "========================================" -ForegroundColor Cyan
}

function Write-Step {
    param([string]$Message)
    Write-Host "[$Message]" -ForegroundColor Yellow
}

function Fail {
    param([string]$Message)
    Write-Host "ERROR: $Message" -ForegroundColor Red
    exit 1
}

function Invoke-Checked {
    param(
        [string]$Description,
        [scriptblock]$Command
    )

    Write-Step $Description
    & $Command
    if ($LASTEXITCODE -ne 0) {
        throw "$Description failed with exit code $LASTEXITCODE"
    }
}

function Find-ConanExe {
    $cmd = Get-Command conan -ErrorAction SilentlyContinue
    if ($cmd) {
        return $cmd.Source
    }

    $candidates = @(
        "C:\Python314\Scripts\conan.exe",
        "$env:APPDATA\Python\Python314\Scripts\conan.exe",
        "$env:APPDATA\Python\Scripts\conan.exe",
        "$env:LOCALAPPDATA\Python\Python314\Scripts\conan.exe"
    )

    $pyPrefix = (python -c "import sys; print(sys.prefix)" 2>$null) -replace '\s+$', ''
    $userBase = (python -c "import site; print(site.USER_BASE)" 2>$null) -replace '\s+$', ''
    if ($pyPrefix) { $candidates += (Join-Path $pyPrefix "Scripts\conan.exe") }
    if ($userBase) { $candidates += (Join-Path $userBase "Scripts\conan.exe") }

    foreach ($candidate in $candidates | Select-Object -Unique) {
        if (Test-Path -LiteralPath $candidate) {
            return $candidate
        }
    }
    return $null
}

function Test-ConanGeneratedUserPresets {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) {
        return $false
    }
    $content = Get-Content -LiteralPath $Path -Raw
    return ($content -match '"vendor"\s*:\s*\{\s*"conan"\s*:' -or
            $content -match 'build/win-release/CMakePresets.json' -or
            $content -match 'build/win-debug/CMakePresets.json')
}

function Move-ConanUserPresetsAside {
    if (-not (Test-ConanGeneratedUserPresets -Path $userPresetsPath)) {
        return $false
    }

    if (Test-Path -LiteralPath $userPresetsBackupPath) {
        Remove-Item -LiteralPath $userPresetsBackupPath -Force
    }

    Move-Item -LiteralPath $userPresetsPath -Destination $userPresetsBackupPath -Force
    Write-Host "Temporarily moved Conan-generated CMakeUserPresets.json to avoid duplicate conan-default presets." -ForegroundColor Gray
    return $true
}

function Restore-ConanUserPresets {
    if ($userPresetsMoved -and (Test-Path -LiteralPath $userPresetsBackupPath)) {
        if (Test-Path -LiteralPath $userPresetsPath) {
            Remove-Item -LiteralPath $userPresetsPath -Force
        }
        Move-Item -LiteralPath $userPresetsBackupPath -Destination $userPresetsPath -Force
        Write-Host "Restored Conan-generated CMakeUserPresets.json." -ForegroundColor Gray
    }
}

function Show-ToolVersion {
    param(
        [string]$Name,
        [scriptblock]$Command
    )
    try {
        $output = & $Command 2>$null | Select-Object -First 1
        if ($output) {
            Write-Host "${Name}: $output" -ForegroundColor Gray
        }
    } catch {
        Write-Host "${Name}: unable to query version" -ForegroundColor DarkYellow
    }
}

try {
    Write-Banner "NMOS Sync Daemon - Windows $Configuration Build (VS 2022)"
    Write-Host "Project root: $projectRoot" -ForegroundColor Gray
    Write-Host "Build dir   : $binaryDir" -ForegroundColor Gray
    Write-Host "Preset      : $preset" -ForegroundColor Gray
    Write-Host ""

    Write-Step "0/5 Checking prerequisites"
    if (-not (Get-Command python -ErrorAction SilentlyContinue)) { Fail "Python 3 not found in PATH" }
    if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) { Fail "CMake not found in PATH" }
    if (-not (Get-Command cpack -ErrorAction SilentlyContinue)) { Fail "CPack not found in PATH" }
    if (-not (Test-Path -LiteralPath $profilePath)) { Fail "Conan profile not found: $profilePath" }
    Show-ToolVersion "Python" { python --version }
    Show-ToolVersion "CMake" { cmake --version }
    Show-ToolVersion "CPack" { cpack --version }

    Write-Step "1/5 Locating Conan"
    $conanExe = Find-ConanExe
    if (-not $conanExe) {
        Write-Host "Conan not found. Installing with pip..." -ForegroundColor Yellow
        python -m pip install conan
        if ($LASTEXITCODE -ne 0) { Fail "Failed to install Conan with pip" }
        $conanExe = Find-ConanExe
    }
    if (-not $conanExe) { Fail "conan.exe not found after installation" }
    Write-Host "Conan: $conanExe" -ForegroundColor Green
    & $conanExe --version

    Push-Location $projectRoot
    try {
        if (-not $SkipConanInstall) {
            Invoke-Checked "2/5 Detecting Conan profile" {
                & $conanExe profile detect --force
            }

            # mDNSResponder C sources can contain non-UTF8 characters on Chinese Windows.
            # _CL_ applies to MSVC invocations launched by Conan recipes.
            $env:_CL_ = "/utf-8"

            Invoke-Checked "3/5 Installing Conan dependencies ($Configuration, static runtime, LLDP ON)" {
                & $conanExe install . `
                    --output-folder="$binaryDir" `
                    --build=missing `
                    --profile:all="$profilePath" `
                    -s "build_type=$Configuration" `
                    -s "compiler.runtime_type=$Configuration" `
                    -o "nmos-cpp/*:shared=False" `
                    -c "tools.cmake.cmaketoolchain:extra_variables={'NMOS_CPP_BUILD_LLDP':'ON'}"
            }
        } else {
            Write-Host "Skipping Conan install by request." -ForegroundColor Yellow
        }

        # Conan writes CMakeUserPresets.json with a repeated conan-default preset for each output folder.
        # Project presets do not require it, so move it aside while running cmake/cpack presets.
        $userPresetsMoved = Move-ConanUserPresetsAside

        Invoke-Checked "4/5 Configuring CMake preset $preset" {
            cmake --preset $preset
        }

        Invoke-Checked "5/5 Building $Configuration" {
            cmake --build --preset $preset --config $Configuration
        }

        if ($RunTests) {
            Invoke-Checked "Running tests" {
                ctest --test-dir $binaryDir -C $Configuration --output-on-failure
            }
        }

        if (-not $SkipPackage) {
            if (Test-Path -LiteralPath $portableDir) {
                Remove-Item -LiteralPath $portableDir -Recurse -Force
            }

            Invoke-Checked "Installing portable files to build directory" {
                cmake --install $binaryDir --config $Configuration --prefix $portableDir
            }

            Invoke-Checked "Packaging portable ZIP" {
                cpack --preset $preset -B $binaryDir
            }
        } else {
            Write-Host "Skipping package by request." -ForegroundColor Yellow
        }

        $exePath = Join-Path $binaryDir (Join-Path "src" (Join-Path $Configuration "nmos-sync-daemon.exe"))
        $zip = Get-ChildItem -LiteralPath $binaryDir -Filter "nmos-sync-daemon-*-win64.zip" -ErrorAction SilentlyContinue |
            Sort-Object LastWriteTime -Descending |
            Select-Object -First 1

        Write-Host ""
        Write-Banner "BUILD SUCCESS"
        Write-Host "Executable: $exePath" -ForegroundColor Green
        if (Test-Path -LiteralPath $portableDir) {
            Write-Host "Portable  : $portableDir" -ForegroundColor Green
        }
        if ($zip) {
            Write-Host "ZIP       : $($zip.FullName)" -ForegroundColor Green
        } elseif (-not $SkipPackage) {
            Write-Host "ZIP       : not found in $binaryDir" -ForegroundColor DarkYellow
        }
        Write-Host "Solution  : $(Join-Path $binaryDir 'nmos-client.sln')" -ForegroundColor Green
    }
    finally {
        Pop-Location
    }
}
catch {
    Write-Host ""
    Write-Host "BUILD FAILED" -ForegroundColor Red
    Write-Host $_.Exception.Message -ForegroundColor Red
    exit 1
}
finally {
    $env:_CL_ = $originalCl
    Restore-ConanUserPresets
}
