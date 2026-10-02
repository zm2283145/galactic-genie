# Uploads to the Vita over VitaShell / vitacompanion FTP.
#
#   tools\deploy_vita.ps1 -Vpk          # copies build-vita\swgb.vpk to ux0:data/swgb/swgb.vpk (install with VitaShell)
#   tools\deploy_vita.ps1 -GameData     # one-time: copies the SWGB:CC files to ux0:data/swgb/Data
#   tools\deploy_vita.ps1 -CampaignData # copies all six original XCAM archives (43 missions)
#   tools\deploy_vita.ps1 -IntroMedia   # optional original xlogo1/xintro AVI files
#   tools\deploy_vita.ps1 -SoundData    # copies voices referenced by the selected campaign mission
#   tools\deploy_vita.ps1 -OutcomeSoundData # copies original conquest victory/defeat streams
#   tools\deploy_vita.ps1 -UnitSoundData # copies the DAT-referenced unit sound archives
#   tools\deploy_vita.ps1 -MusicData     # copies the original streamed soundtrack
#   tools\deploy_vita.ps1 -TerrainSoundData # copies camera-relative terrain ambience
#   tools\deploy_vita.ps1 -LanguageData  # copies localized interface strings
#   tools\deploy_vita.ps1 -AiData        # copies the original .per AI personalities
#   tools\deploy_vita.ps1 -AllCampaignSoundData # copies dialogue used by all 43 stock missions
#   tools\deploy_vita.ps1 -CampaignStoryData # copies all original campaign briefing narration
#   tools\deploy_vita.ps1 -AllRequiredData # uploads and size-verifies the complete required installation
#   tools\deploy_vita.ps1 -ScenarioImport -ScenarioFile <file.scx> # stages an editor import
#   tools\deploy_vita.ps1 -PullLog      # downloads ux0:data/swgb/swgb.log to build-vita\swgb.log
param(
    [string]$Vita = "10.1.1.93",
    [int]$Port = 1337,
    [string]$GameDir = "D:\GOG\Star Wars - Galactic Battlegrounds\Game\Data",
    [string]$CampaignDir = "D:\GOG\Star Wars - Galactic Battlegrounds\Game\Campaign",
    [string]$CampaignArchive = "XCAM3.CPX",
    [string]$SoundDir = "D:\GOG\Star Wars - Galactic Battlegrounds\Game\Sound\Scenario",
    [string]$CampaignStoryDir = "D:\GOG\Star Wars - Galactic Battlegrounds\Game\Sound\Campaign",
    [string]$MusicDir = "D:\GOG\Star Wars - Galactic Battlegrounds\Game\MUSIC",
    [string]$TerrainSoundDir = "D:\GOG\Star Wars - Galactic Battlegrounds\Game\Sound\Terrain",
    [string]$AiDir = "D:\GOG\Star Wars - Galactic Battlegrounds\Game\AI",
    [string]$ScenarioFile = "",
    [int]$CampaignEntry = 2,
    [switch]$Vpk,
    [switch]$GameData,
    [switch]$CampaignData,
    [switch]$IntroMedia,
    [switch]$SoundData,
    [switch]$OutcomeSoundData,
    [switch]$UnitSoundData,
    [switch]$MusicData,
    [switch]$TerrainSoundData,
    [switch]$LanguageData,
    [switch]$AiData,
    [switch]$AllCampaignSoundData,
    [switch]$CampaignStoryData,
    [switch]$AllRequiredData,
    [switch]$Verify,
    [switch]$ScenarioImport,
    [switch]$PullLog
)
$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$base = "ftp://$Vita`:$Port"
$campaignArchives = "XCAM1.CPX", "XCAM2.CPX", "XCAM3.CPX", "XCAM4.CPX", "Xcam5.cpx", "XCAM8.CPX"

if ($AllRequiredData) {
    $Vpk = $true
    $GameData = $true
    $CampaignData = $true
    $OutcomeSoundData = $true
    $UnitSoundData = $true
    $MusicData = $true
    $TerrainSoundData = $true
    $LanguageData = $true
    $AiData = $true
    $AllCampaignSoundData = $true
    $CampaignStoryData = $true
    $Verify = $true
}

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
    $remoteUrl = ([Uri]"$base/$remote").AbsoluteUri
    Write-Host ("  {0} -> {1} ({2:N1} MB)" -f (Split-Path $local -Leaf), $remote, ($len / 1MB))
    if ($AllRequiredData) {
        try {
            $existingLen = Ftp-Size $remote
            if ($existingLen -eq $len) {
                Write-Host ("    already verified {0:N1} MB" -f ($existingLen / 1MB))
                return
            }
        } catch {
            # Missing and partially uploaded files continue through the normal upload.
        }
    }
    $uploaded = $false
    for ($attempt = 1; $attempt -le 5; $attempt++) {
        $result = (& curl.exe --silent --show-error --fail --ftp-create-dirs `
            --connect-timeout 20 --max-time 0 --upload-file $local $remoteUrl 2>&1) -join "`n"
        if ($LASTEXITCODE -eq 0) {
            $uploaded = $true
            break
        }
        Write-Warning "upload attempt $attempt failed for $remote`: $result"
        Start-Sleep -Seconds 2
    }
    if (-not $uploaded) {
        throw "could not upload $local to $remote after 5 attempts"
    }
    if ($Verify) {
        $remoteLen = Ftp-Size $remote
        if ($remoteLen -ne $len) {
            throw "remote size mismatch for $remote (local $len, remote $remoteLen)"
        }
        Write-Host ("    verified {0:N1} MB" -f ($remoteLen / 1MB))
    }
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

function Ftp-Size($remote) {
    # VitaShell's response to the FTP SIZE command is unreliable through
    # FtpWebRequest, while its RETR metadata reports the exact byte count.
    $remoteUrl = ([Uri]"$base/$remote").AbsoluteUri
    $headers = (& curl.exe --silent --show-error --head $remoteUrl 2>&1) -join "`n"
    if ($LASTEXITCODE -ne 0) {
        throw "could not inspect remote file $remote`: $headers"
    }
    $match = [regex]::Match($headers, "(?im)^Content-Length:\s*(\d+)")
    if (-not $match.Success) {
        throw "remote file size missing for $remote`: $headers"
    }
    return [int64]$match.Groups[1].Value
}

function Campaign-SoundNames($archive, $entry) {
    $scenario = (& $tool scenario $archive $entry 2>&1) -join "`n"
    if ($LASTEXITCODE -ne 0) {
        throw "could not inspect campaign sounds: $scenario"
    }
    [regex]::Matches($scenario, "sound '([^']+)'") |
        ForEach-Object { $_.Groups[1].Value } |
        Where-Object { $_ }
}

Ftp-MkDir "ux0:/data/swgb"
Ftp-MkDir "ux0:/data/swgb/Scenarios"
Ftp-MkDir "ux0:/data/swgb/Scenarios/Import"
Ftp-MkDir "ux0:/data/swgb/Scenarios/scenarios"
Ftp-MkDir "ux0:/data/swgb/Scenarios/autosave"
Ftp-MkDir "ux0:/data/swgb/Scenarios/recovery"
if ($GameData) {
    Ftp-MkDir "ux0:/data/swgb/Data"
    $files = "genie_x1.dat", "GRAPHICS.DRS", "graphics_x1.drs", "TERRAIN.DRS", "terrain_x1.drs",
             "INTERFAC.DRS", "interfac_x1.drs", "blendomatic.dat", "STemplet.dat", "FilterMaps.dat",
             "VIEW_ICM.DAT", "lightMaps.dat", "PatternMasks.dat"
    foreach ($f in $files) { Ftp-Put (Join-Path $GameDir $f) "ux0:/data/swgb/Data/$($f.ToLower())" }
}
if ($CampaignData) {
    Ftp-MkDir "ux0:/data/swgb/Campaign"
    foreach ($f in $campaignArchives) {
        Ftp-Put (Join-Path $CampaignDir $f) "ux0:/data/swgb/Campaign/$($f.ToLower())"
    }
}
if ($IntroMedia) {
    $gameRoot = Split-Path -Parent $GameDir
    foreach ($f in "xlogo1.avi", "xintro.avi") {
        Ftp-Put (Join-Path $gameRoot $f) "ux0:/data/swgb/$f"
    }
}
if ($SoundData -or $AllCampaignSoundData) {
    $tool = Join-Path $repo "build-pc\swgbtool.exe"
    if (-not (Test-Path $tool)) {
        throw "build-pc\swgbtool.exe is required; run tools\build_vita.ps1 -Pc first"
    }
    $env:Path = "C:\msys64\mingw64\bin;C:\msys64\usr\bin;" + $env:Path
    if ($AllCampaignSoundData) {
        $names = foreach ($archiveName in $campaignArchives) {
            $campaign = Join-Path $CampaignDir $archiveName
            $listing = (& $tool campaign $campaign 2>&1) -join "`n"
            if ($LASTEXITCODE -ne 0) {
                throw "could not inspect campaign archive: $listing"
            }
            $entryMatch = [regex]::Match($listing, "(\d+) entries")
            if (-not $entryMatch.Success) {
                throw "could not determine entry count for $campaign"
            }
            $entryCount = [int]$entryMatch.Groups[1].Value
            for ($entry = 1; $entry -le $entryCount; $entry++) {
                Campaign-SoundNames $campaign $entry
            }
        }
    } else {
        $campaign = Join-Path $CampaignDir $CampaignArchive
        $names = Campaign-SoundNames $campaign $CampaignEntry
    }
    $names = $names | Sort-Object -Unique
    Ftp-MkDir "ux0:/data/swgb/Sound"
    Ftp-MkDir "ux0:/data/swgb/Sound/Scenario"
    $missingSounds = @()
    foreach ($name in $names) {
        $local = Join-Path $SoundDir "$name.mp3"
        if (-not (Test-Path $local)) {
            $missingSounds += $local
            continue
        }
        Ftp-Put $local "ux0:/data/swgb/Sound/Scenario/$($name.ToLower()).mp3"
    }
    if ($missingSounds.Count -gt 0) {
        Write-Warning ("The original installation does not contain {0} referenced scenario sound(s):`n  {1}" -f
            $missingSounds.Count, ($missingSounds -join "`n  "))
    }
}
if ($CampaignStoryData) {
    Ftp-MkDir "ux0:/data/swgb/Sound"
    Ftp-MkDir "ux0:/data/swgb/Sound/Campaign"
    $campaignStory = Get-ChildItem -LiteralPath $CampaignStoryDir -File -Filter "*.mp3"
    if (-not $campaignStory.Count) {
        throw "no campaign story MP3 files found in $CampaignStoryDir"
    }
    foreach ($file in $campaignStory) {
        Ftp-Put $file.FullName "ux0:/data/swgb/Sound/Campaign/$($file.Name.ToLower())"
    }
}
if ($OutcomeSoundData) {
    $streamDir = Join-Path (Split-Path -Parent $SoundDir) "Stream"
    Ftp-MkDir "ux0:/data/swgb/Sound"
    Ftp-MkDir "ux0:/data/swgb/Sound/Scenario"
    foreach ($f in "lost.mp3", "WON1.MP3", "won2.mp3") {
        $local = Join-Path $streamDir $f
        if (-not (Test-Path $local)) {
            throw "missing match outcome sound $local"
        }
        Ftp-Put $local "ux0:/data/swgb/Sound/Scenario/$($f.ToLower())"
    }
}
if ($UnitSoundData) {
    Ftp-MkDir "ux0:/data/swgb/Data"
    foreach ($f in "SOUNDS.DRS", "sounds_x1.drs") {
        Ftp-Put (Join-Path $GameDir $f) "ux0:/data/swgb/Data/$($f.ToLower())"
    }
}
if ($MusicData) {
    Ftp-MkDir "ux0:/data/swgb/Music"
    foreach ($f in "Track02.ogg", "Track03.ogg") {
        Ftp-Put (Join-Path $MusicDir $f) "ux0:/data/swgb/Music/$($f.ToLower())"
    }
}
if ($TerrainSoundData) {
    Ftp-MkDir "ux0:/data/swgb/Sound"
    Ftp-MkDir "ux0:/data/swgb/Sound/Terrain"
    foreach ($file in Get-ChildItem -LiteralPath $TerrainSoundDir -File -Filter *.wav) {
        Ftp-Put $file.FullName "ux0:/data/swgb/Sound/Terrain/$($file.Name.ToLower())"
    }
}
if ($LanguageData) {
    Ftp-MkDir "ux0:/data/swgb/Data"
    $gameRoot = Split-Path -Parent $GameDir
    foreach ($f in "language.dll", "language_x1.dll", "language_x2.dll") {
        Ftp-Put (Join-Path $gameRoot $f) "ux0:/data/swgb/Data/$f"
    }
}
if ($AiData) {
    Ftp-MkDir "ux0:/data/swgb/AI"
    foreach ($file in Get-ChildItem -LiteralPath $AiDir -File -Filter *.per -Recurse) {
        $relative = $file.FullName.Substring($AiDir.TrimEnd('\').Length + 1) -replace '\\', '/'
        $segments = $relative.Split('/')
        $remoteDir = "ux0:/data/swgb/AI"
        for ($index = 0; $index -lt $segments.Length - 1; $index++) {
            $remoteDir += "/$($segments[$index])"
            Ftp-MkDir $remoteDir
        }
        Ftp-Put $file.FullName "ux0:/data/swgb/AI/$relative"
    }
}
if ($ScenarioImport) {
    if (-not $ScenarioFile) {
        throw "-ScenarioImport requires -ScenarioFile"
    }
    if (-not (Test-Path -LiteralPath $ScenarioFile -PathType Leaf)) {
        throw "missing scenario file $ScenarioFile"
    }
    $extension = [IO.Path]::GetExtension($ScenarioFile).ToLowerInvariant()
    if ($extension -ne ".scx") {
        throw "Scenario Editor import staging accepts an original .scx file"
    }
    Ftp-MkDir "ux0:/data/swgb/Scenarios"
    Ftp-MkDir "ux0:/data/swgb/Scenarios/Import"
    Ftp-Put $ScenarioFile "ux0:/data/swgb/Scenarios/Import/import.scx"
}
if ($Vpk) { Ftp-Put (Join-Path $repo "build-vita\swgb.vpk") "ux0:/data/swgb/swgb.vpk" }
if ($PullLog) {
    $out = Join-Path $repo "build-vita\swgb.log"
    Ftp-Get "ux0:/data/swgb/swgb.log" $out
    Get-Content $out
}
