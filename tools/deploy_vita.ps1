# Uploads to the Vita over VitaShell / vitacompanion FTP.
#
#   tools\deploy_vita.ps1 -Vpk          # copies build-vita\swgb.vpk to ux0:data/swgb/swgb.vpk (install with VitaShell)
#   tools\deploy_vita.ps1 -GameData     # one-time: copies the SWGB:CC files to ux0:data/swgb/Data
#   tools\deploy_vita.ps1 -PullLog      # downloads ux0:data/swgb/swgb.log to build-vita\swgb.log
param(
    [string]$Vita = "10.1.1.93",
    [int]$Port = 1337,
    [string]$GameDir = "D:\GOG\Star Wars - Galactic Battlegrounds\Game\Data",
    [switch]$Vpk,
    [switch]$GameData,
    [switch]$PullLog
)
$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$base = "ftp://$Vita`:$Port"

function Ftp-MkDir($path) {
    try {
        $r = [Net.FtpWebRequest]::Create("$base/$path")
        $r.Method = [Net.WebRequestMethods+Ftp]::MakeDirectory
        $r.UsePassive = $true
        $r.GetResponse().Close()
    } catch { } # already exists
}

function Ftp-Put($local, $remote) {
    $len = (Get-Item $local).Length
    Write-Host ("  {0} -> {1} ({2:N1} MB)" -f (Split-Path $local -Leaf), $remote, ($len / 1MB))
    $r = [Net.FtpWebRequest]::Create("$base/$remote")
    $r.Method = [Net.WebRequestMethods+Ftp]::UploadFile
    $r.UseBinary = $true
    $r.UsePassive = $true
    $s = $r.GetRequestStream()
    $f = [IO.File]::OpenRead($local)
    try { $f.CopyTo($s, 1MB) } finally { $f.Close(); $s.Close() }
    $r.GetResponse().Close()
}

function Ftp-Get($remote, $local) {
    $r = [Net.FtpWebRequest]::Create("$base/$remote")
    $r.Method = [Net.WebRequestMethods+Ftp]::DownloadFile
    $r.UseBinary = $true
    $r.UsePassive = $true
    $resp = $r.GetResponse()
    $f = [IO.File]::Create($local)
    try { $resp.GetResponseStream().CopyTo($f) } finally { $f.Close(); $resp.Close() }
}

Ftp-MkDir "ux0:/data/swgb"
if ($GameData) {
    Ftp-MkDir "ux0:/data/swgb/Data"
    $files = "genie_x1.dat", "GRAPHICS.DRS", "graphics_x1.drs", "TERRAIN.DRS", "terrain_x1.drs",
             "INTERFAC.DRS", "interfac_x1.drs", "blendomatic.dat"
    foreach ($f in $files) { Ftp-Put (Join-Path $GameDir $f) "ux0:/data/swgb/Data/$($f.ToLower())" }
}
if ($Vpk) { Ftp-Put (Join-Path $repo "build-vita\swgb.vpk") "ux0:/data/swgb/swgb.vpk" }
if ($PullLog) {
    $out = Join-Path $repo "build-vita\swgb.log"
    Ftp-Get "ux0:/data/swgb/swgb.log" $out
    Get-Content $out
}
