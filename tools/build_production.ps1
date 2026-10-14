#Requires -Version 5.1

<#
.SYNOPSIS
Configure, build and install the Windows x64 production launcher.
.DESCRIPTION
Finds CMake on PATH or in Visual Studio (including Build Tools). Uses MinSizeRel,
disables fixture mode and tests, and stops on the first failed step. Requires the
Visual Studio C++ x64 tools and Windows SDK. No global PATH changes are made.
.PARAMETER ManifestUrl
Stable HTTPS URL of manifest.envelope.json.
.PARAMETER KeyId
Signing key identifier used by the Python publisher.
.PARAMETER PublicKey
The 64-character hexadecimal Ed25519 public key, never the private PEM file.
.PARAMETER CMakePath
Optional path to cmake.exe, for example the executable used by VS Code CMake Tools.
.PARAMETER Generator
Optional Visual Studio generator. If omitted, CMake uses the existing build
directory's generator or its default for a new build directory.
.PARAMETER BuildDirectory
Build directory; defaults to build/production. Relative paths use the repository
root, regardless of the current working directory.
.PARAMETER InstallPrefix
Destination for the executable; defaults to dist/production. Relative paths use
the repository root. This is a build output folder, not the player's game folder.
.PARAMETER Parallel
Maximum concurrent build jobs; defaults to 1 to limit memory use.
.EXAMPLE
./tools/build_production.ps1 -ManifestUrl $ManifestUrl -KeyId $KeyId -PublicKey $PublicKey
.EXAMPLE
./tools/build_production.ps1 -ManifestUrl $ManifestUrl -KeyId $KeyId -PublicKey $PublicKey -Generator 'Visual Studio 17 2022' -Parallel 2
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('\Ahttps://[^/\s"\\]+/[^\s"\\]*\z')]
    [string] $ManifestUrl,

    [Parameter(Mandatory = $true)]
    [ValidatePattern('\A[A-Za-z0-9_-]+\z')]
    [string] $KeyId,

    [Parameter(Mandatory = $true)]
    [Alias('PublicKeyHex')]
    [ValidatePattern('\A[0-9a-fA-F]{64}\z')]
    [string] $PublicKey,

    [string] $CMakePath,
    [ValidatePattern('\AVisual Studio [0-9]+ [0-9]{4}\z')]
    [string] $Generator,
    [ValidateNotNullOrEmpty()]
    [string] $BuildDirectory = 'build/production',
    [ValidateNotNullOrEmpty()]
    [string] $InstallPrefix = 'dist/production',
    [ValidateRange(1, 256)]
    [int] $Parallel = 1
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Find-CMake {
    if ($CMakePath) {
        $explicitPath = Get-Item -LiteralPath $CMakePath
        if ($explicitPath.PSIsContainer) {
            throw '-CMakePath must point to cmake.exe, not a folder.'
        }
        return $explicitPath.FullName
    }

    $onPath = Get-Command cmake.exe -CommandType Application -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($onPath) {
        return $onPath.Source
    }

    $installerRoot = [Environment]::GetFolderPath('ProgramFilesX86')
    $vswhere = Join-Path $installerRoot 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (Test-Path -LiteralPath $vswhere -PathType Leaf) {
        $installations = @(& $vswhere -products '*' -requires Microsoft.VisualStudio.Component.VC.CMake.Project -property installationPath)
        if ($LASTEXITCODE -ne 0) {
            throw "Visual Studio discovery failed (vswhere exit code $LASTEXITCODE). Use -CMakePath to select cmake.exe."
        }
        foreach ($installation in $installations) {
            $bundled = Join-Path $installation 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
            if (Test-Path -LiteralPath $bundled -PathType Leaf) {
                return $bundled
            }
        }
    }
    throw 'CMake was not found. Install the Visual Studio C++ CMake tools component, or pass -CMakePath with the full path to the cmake.exe used by VS Code.'
}

function Resolve-OutputPath([string] $Path) {
    if (-not [IO.Path]::IsPathRooted($Path)) {
        $Path = Join-Path $repositoryRoot $Path
    }
    return [IO.Path]::GetFullPath($Path).Replace('\', '/')
}

function Invoke-CMake([string] $Stage, [string[]] $Arguments) {
    Write-Host "`n$Stage"
    # Pass arguments as data, including URLs containing '&' and paths with spaces.
    & $cmake @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Stage failed (CMake exit code $LASTEXITCODE). No later steps were run."
    }
}

if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
    throw 'This helper builds the Windows launcher. See docs/PUBLISHING.md for native Linux build commands.'
}

$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')).Replace('\', '/')
$cmake = Find-CMake
$buildPath = Resolve-OutputPath $BuildDirectory
$installPath = Resolve-OutputPath $InstallPrefix

Write-Host "CMake: $($cmake.Replace('\', '/'))"
Write-Host "Build directory: $buildPath"
Write-Host "Install prefix: $installPath"

$configureArguments = @('-S', $repositoryRoot, '-B', $buildPath, '-A', 'x64')
if ($Generator) {
    $configureArguments += @('-G', $Generator)
}
$configureArguments += @(
    '-DOBK_FIXTURE_BUILD=OFF', '-DBUILD_TESTING=OFF', '-DOBK_BUILD_GUI=ON',
    "-DOBK_MANIFEST_URL=$ManifestUrl", "-DOBK_KEY_ID=$KeyId", "-DOBK_PUBLIC_KEY_HEX=$PublicKey"
)
Invoke-CMake 'Configure production launcher' $configureArguments
Invoke-CMake 'Build production launcher (MinSizeRel)' @('--build', $buildPath, '--config', 'MinSizeRel', '--parallel', "$Parallel")
Invoke-CMake 'Install production launcher' @('--install', $buildPath, '--config', 'MinSizeRel', '--prefix', $installPath)

Write-Host "`nProduction launcher: $installPath/OpenBK2Launcher-Windows-x64.exe"
