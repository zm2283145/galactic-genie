# Builds the Vita VPK (and optionally the PC swgbtool) with VitaSDK + Ninja.
#
#   powershell -ExecutionPolicy Bypass -File tools\build_vita.ps1 [-Clean] [-Pc]
#
# Requires $env:VITASDK (e.g. C:\vitasdk) and CMake/Ninja from C:\msys64\mingw64\bin.
param(
    [switch]$Clean,
    [switch]$Pc
)
$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
if (-not $env:VITASDK) { $env:VITASDK = "C:\vitasdk" }
$env:Path = "$env:VITASDK\bin;C:\msys64\mingw64\bin;C:\msys64\usr\bin;" + $env:Path

function Build($dir, $extra) {
    $b = Join-Path $repo $dir
    if ($Clean -and (Test-Path $b)) { Remove-Item -Recurse -Force $b }
    if (-not (Test-Path (Join-Path $b "build.ninja"))) {
        & cmake -S $repo -B $b -G Ninja @extra
        if ($LASTEXITCODE -ne 0) { throw "cmake configure failed ($dir)" }
    }
    & cmake --build $b
    if ($LASTEXITCODE -ne 0) { throw "build failed ($dir)" }
}

$tc = (Join-Path $env:VITASDK "share\vita.toolchain.cmake") -replace '\\', '/'
Build "build-vita" @("-DCMAKE_TOOLCHAIN_FILE=$tc", "-DCMAKE_BUILD_TYPE=Release")
if ($Pc) { Build "build-pc" @("-DCMAKE_BUILD_TYPE=Release") }

Get-Item (Join-Path $repo "build-vita\swgb.vpk") | Select-Object FullName, Length, LastWriteTime
