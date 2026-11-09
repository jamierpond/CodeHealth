# Installs the codehealth release binary for this Windows machine (x64 or ARM64).
#   irm https://raw.githubusercontent.com/jamierpond/CodeHealth/main/install.ps1 | iex
# $env:CODEHEALTH_VERSION picks a release (e.g. v0.1.0; default: the latest).
# $env:CODEHEALTH_INSTALL_DIR picks where it goes (default: ~\.local\bin).
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$repository = 'https://github.com/jamierpond/CodeHealth'
$installDir = if ($env:CODEHEALTH_INSTALL_DIR) { $env:CODEHEALTH_INSTALL_DIR } else { Join-Path $HOME '.local\bin' }

function Get-Target {
    $machine = if ($env:PROCESSOR_ARCHITEW6432) { $env:PROCESSOR_ARCHITEW6432 } else { $env:PROCESSOR_ARCHITECTURE }
    if ($machine -eq 'ARM64') { return 'windows-arm64' }
    if ($machine -eq 'AMD64') { return 'windows-x64' }
    throw "codehealth install: no Windows build for $machine"
}

function Get-ReleaseUrl {
    if ($env:CODEHEALTH_VERSION) { return "$repository/releases/download/$($env:CODEHEALTH_VERSION)" }
    return "$repository/releases/latest/download"
}

function Add-ToUserPath([string] $directory) {
    $userPath = [Environment]::GetEnvironmentVariable('Path', 'User')
    if (($userPath -split ';') -notcontains $directory) {
        [Environment]::SetEnvironmentVariable('Path', "$userPath;$directory", 'User')
        Write-Host "added $directory to your user PATH; open a new terminal to use it"
    }
}

$target = Get-Target
$archive = "codehealth-$target.zip"
$workDir = Join-Path ([IO.Path]::GetTempPath()) ([Guid]::NewGuid())
New-Item -ItemType Directory -Path $workDir | Out-Null
try {
    Invoke-WebRequest -UseBasicParsing -Uri "$(Get-ReleaseUrl)/$archive" -OutFile (Join-Path $workDir $archive)
    Invoke-WebRequest -UseBasicParsing -Uri "$(Get-ReleaseUrl)/codehealth-$target.sha256" -OutFile (Join-Path $workDir 'expected.sha256')
    $expected = ((Get-Content (Join-Path $workDir 'expected.sha256') -Raw).Trim() -split '\s+')[0]
    $actual = (Get-FileHash -Algorithm SHA256 (Join-Path $workDir $archive)).Hash
    if ($actual -ne $expected) { throw "codehealth install: checksum mismatch for $archive" }

    Expand-Archive -Path (Join-Path $workDir $archive) -DestinationPath (Join-Path $workDir 'unpacked')
    New-Item -ItemType Directory -Force -Path $installDir | Out-Null
    Copy-Item -Force (Join-Path $workDir 'unpacked\codehealth.exe') (Join-Path $installDir 'codehealth.exe')
    Write-Host "installed $(& (Join-Path $installDir 'codehealth.exe') --version) to $installDir\codehealth.exe"
    Add-ToUserPath $installDir
} finally {
    Remove-Item -Recurse -Force $workDir
}
