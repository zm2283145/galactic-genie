# Uploads to the Vita over VitaShell / vitacompanion FTP.
#
#   tools\deploy_vita.ps1 -Vpk          # copies build-vita\swgb.vpk to ux0:data/swgb/swgb.vpk (install with VitaShell)
#   tools\deploy_vita.ps1 -GameData     # one-time: copies the SWGB:CC files to ux0:data/swgb/Data
#   tools\deploy_vita.ps1 -CampaignData # copies XCAM3.CPX for the Breaking Bread scenario
#   tools\deploy_vita.ps1 -SoundData    # copies voices referenced by the selected campaign mission
#   tools\deploy_vita.ps1 -PullLog      # downloads ux0:data/swgb/swgb.log to build-vita\swgb.log
param(
    [string]$Vita = "10.1.1.93",
    [int]$Port = 1337,
    [string]$GameDir = "D:\GOG\Star Wars - Galactic Battlegrounds\Game\Data",
    [string]$CampaignDir = "D:\GOG\Star Wars - Galactic Battlegrounds\Game\Campaign",
    [string]$SoundDir = "D:\GOG\Star Wars - Galactic Battlegrounds\Game\Sound\Scenario",
    [int]$CampaignEntry = 2,
    [switch]$Vpk,
    [switch]$GameData,
    [switch]$CampaignData,
    [switch]$SoundData,
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
             "INTERFAC.DRS", "interfac_x1.drs", "blendomatic.dat", "STemplet.dat", "FilterMaps.dat",
             "VIEW_ICM.DAT", "lightMaps.dat", "PatternMasks.dat"
    foreach ($f in $files) { Ftp-Put (Join-Path $GameDir $f) "ux0:/data/swgb/Data/$($f.ToLower())" }
}
if ($CampaignData) {
    Ftp-MkDir "ux0:/data/swgb/Campaign"
    Ftp-Put (Join-Path $CampaignDir "XCAM3.CPX") "ux0:/data/swgb/Campaign/xcam3.cpx"
}
if ($SoundData) {
    $tool = Join-Path $repo "build-pc\swgbtool.exe"
    if (-not (Test-Path $tool)) {
        throw "build-pc\swgbtool.exe is required; run tools\build_vita.ps1 -Pc first"
    }
    $env:Path = "C:\msys64\mingw64\bin;C:\msys64\usr\bin;" + $env:Path
    $campaign = Join-Path $CampaignDir "XCAM3.CPX"
    $scenario = (& $tool scenario $campaign $CampaignEntry 2>&1) -join "`n"
    if ($LASTEXITCODE -ne 0) { throw "could not inspect campaign sounds: $scenario" }
    $names = [regex]::Matches($scenario, "sound '([^']+)'") |
        ForEach-Object { $_.Groups[1].Value } |
        Where-Object { $_ } |
        Sort-Object -Unique
    Ftp-MkDir "ux0:/data/swgb/Sound"
    Ftp-MkDir "ux0:/data/swgb/Sound/Scenario"
    foreach ($name in $names) {
        $local = Join-Path $SoundDir "$name.mp3"
        if (-not (Test-Path $local)) { throw "missing scenario sound $local" }
        Ftp-Put $local "ux0:/data/swgb/Sound/Scenario/$($name.ToLower()).mp3"
    }
}
if ($Vpk) { Ftp-Put (Join-Path $repo "build-vita\swgb.vpk") "ux0:/data/swgb/swgb.vpk" }
if ($PullLog) {
    $out = Join-Path $repo "build-vita\swgb.log"
    Ftp-Get "ux0:/data/swgb/swgb.log" $out
    Get-Content $out
}
