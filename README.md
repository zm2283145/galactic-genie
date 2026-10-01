# Galactic Genie

A from-scratch, portable reimplementation of the Genie engine, as used by *Star Wars: Galactic Battlegrounds Saga*. The current targets are PS Vita and PC. It loads the original Clone Campaigns data files from your own copy of the game; no game assets are included in this repository.

It is written clean-room style: file formats come from public documentation (openage, genieutils). Game behaviour is to be matched by observation, and by using Ghidra only to answer "how does X work" questions. Decompiled code is never copied into the tree.

## Status: native skirmish and campaign frontend

| Area | State |
|---|---|
| DRS archives (40/60-byte headers, expansion override order) | done |
| JASC palettes (interfac.drs 50500) | done |
| SLP 2.0N decoder (all draw commands, player colour, shadows, outlines) | done |
| `genie_x1.dat` (VER 5.9) full parse, verified byte-exact to EOF | done |
| Sprite atlas builder (per SLP and player colour) | done |
| Isometric terrain renderer with `blendomatic.dat` priority/mode edge blending | done |
| Terrain elevation with original slope geometry and neighbor-sensitive lighting | done |
| CPX/SCX discovery, metadata, structural validation, and initialization for all 43 stock XCAM missions | done |
| Initial trigger runtime: timers, object/area/resource conditions, ordered effects, voiced dialogue, and collision-aware scripted movement | in progress |
| Units: idle/walk animation, 8-way facing with mirroring, graphic deltas for buildings | done |
| Combat: contextual orders, deterministic accuracy/misses, projectiles/blast, DAT-driven attributes, shields, damage, and death | in progress |
| Major SWGB mechanics: conversion, Holocrons, stealth/detection, Standard/Conquest/Time/Score/Command Center victory, and aircraft rules | done |
| Fishing/naval economy: Utility Trawler gathering, Aqua Harvesters, naval construction/repair, island resources, and AI use | done |
| Vita: renderer, camera controls, original cursors, unit selection/status markers, formation movement, and fog-aware navigable minimap | done |
| Vita frontend: startup validation, title/main/campaign menus, shared pause/options/objectives, saves, and outcomes | done |
| Deterministic random maps: grasslands, archipelago, and compact two-island regression layout | done |
| PC `swgbtool`: data inspection, CPX/SCX listing, and procedural/scenario PNG rendering | done |

See [docs/ROADMAP.md](docs/ROADMAP.md) for what comes next.

## Layout

```
src/core/      format loaders (no engine or platform deps)
src/engine/    assets (atlases), game world, simulation
src/render/    Renderer interface; GL (vitaGL) and software backends
src/platform/  Vita entry point
tools/         swgbtool (PC), build and deploy scripts
vita/          LiveArea assets (8-bit indexed PNGs, as the Vita requires)
```

## Building

The Vita build needs VitaSDK with vitaGL, plus CMake and Ninja:

```powershell
tools\build_vita.ps1          # -> build-vita\swgb.vpk
tools\build_vita.ps1 -Pc      # also builds build-pc\swgbtool.exe
```

To build on Linux (the PC tool only):

```sh
cmake -B build-pc && cmake --build build-pc
./build-pc/swgbtool render "<game>/Game/Data" scene.png 1 3.0 0.8
```

## Installing on the Vita

1. Copy these files from the game's `Game/Data` folder to `ux0:data/swgb/Data/`: `genie_x1.dat`, `graphics.drs`, `graphics_x1.drs`, `terrain.drs`, `terrain_x1.drs`, `interfac.drs`, `interfac_x1.drs`, `sounds.drs`, `sounds_x1.drs`, `blendomatic.dat`, `STemplet.dat`, `FilterMaps.dat`, `VIEW_ICM.DAT`, `lightMaps.dat`, and `PatternMasks.dat`. Also copy `Game/language.dll`, `language_x1.dll`, and `language_x2.dll` for localized interface names. `tools\deploy_vita.ps1 -GameData` copies the core data; `tools\deploy_vita.ps1 -UnitSoundData` copies the unit audio archives; `tools\deploy_vita.ps1 -LanguageData` copies the language strings.
2. Copy `Game/Campaign/XCAM1.CPX`, `XCAM2.CPX`, `XCAM3.CPX`, `XCAM4.CPX`, `Xcam5.cpx`, and `XCAM8.CPX` to `ux0:data/swgb/Campaign/`. `tools\deploy_vita.ps1 -CampaignData` copies the six archives over FTP. Startup validates all 43 missions before presenting the campaign browser, with localized campaign/mission names, factions, briefings, objectives, completion state, difficulty, and sequential or development access.
3. Copy the MP3 dialogue referenced by the mission from `Game/Sound/Scenario` to
   `ux0:data/swgb/Sound/Scenario/`. `tools\deploy_vita.ps1 -SoundData` extracts the names from
   the archive/entry selected by `-CampaignArchive` and `-CampaignEntry` and copies only those files.
4. Copy `Game/MUSIC/Track02.ogg` and `Track03.ogg` to `ux0:data/swgb/Music/`, and the WAV files
   from `Game/Sound/Terrain` to `ux0:data/swgb/Sound/Terrain/`. The deployment script options
   `-MusicData` and `-TerrainSoundData` copy these soundtrack and ambience files.
5. Copy the original `Game/AI` directory to `ux0:data/swgb/AI/`.
   `tools\deploy_vita.ps1 -AiData` copies every `.per` file while preserving
   personality subdirectories required by Computer Classic.
6. Install `swgb.vpk` with VitaShell. `tools\deploy_vita.ps1 -Vpk` uploads it to `ux0:data/swgb/`.
7. The app writes a log to `ux0:data/swgb/swgb.log`. `tools\deploy_vita.ps1 -PullLog` fetches it.

Frontend controls: d-pad or left-stick up/down selects an entry, left/right changes difficulty or
a lobby value, X activates, and O returns. Touching an entry selects and activates it. The main
menu exposes Single Player, visibly unavailable Multiplayer, a reserved Scenario Editor route,
Options, Credits/Data Status, and Exit. Campaign browsing uses the original localized language
tables and discovered XCAM contents rather than a hardcoded mission. L toggles sequential versus
development mission access in the mission browser.

START pauses an active match. The shared pause framework can resume, show campaign objectives or
skirmish status, save/load, restart, adjust options, surrender, and return to the campaign browser
or main menu as appropriate. Campaign outcomes unlock the next mission and offer continue, replay,
campaign-browser, and main-menu routes. Generated-skirmish behavior remains compatible.

Gameplay controls: left stick, d-pad, or a touch drag scrolls; L/R zoom. The right stick moves the
command cursor, and holding it against a screen edge scrolls the camera. X selects a visible
unit or building for inspection. With friendly units selected, O over a hostile unit issues an
attack order; O over terrain issues a move order. A touch tap uses the same contextual selection
or command behavior. Double-tap a unit or press X twice to select every matching friendly unit
in the viewport. Hold Square and move the right stick, or hold Square while touch-dragging, to
box-select units. The normal, move, and attack cursors are loaded from the original
`interfac.drs`, with a procedural fallback if they are unavailable. Triangle cycles selected
combat units through defensive, stand-ground, passive, and aggressive stances. The selection
panel's garrison command enters original-cursor building targeting; Triangle on a selected
production building opens its Units, Research, and Commands pages, where queued items can be
cancelled, while a gate opens only its lock/unlock command. Selected buildings show power and
shield-coverage state. The upper-right minimap is always available: tap or drag it directly, or
move the right-stick cursor over it and press X. Select+d-pad recalls groups 1-4; hold Square
during the same chord to assign the current ordered local selection. Recalling a group twice
centers its first unit. The Options screen provides live master/music/dialogue/effects levels
and a Left-Handed preset that swaps the camera/cursor sticks and X/O gameplay roles.

Jedi and Sith with the original action-104 task expose **Convert** with command-sheet frame 14 on
their Commands page. Choose it, then target a currently visible eligible enemy. Conversion uses the
DAT work/recharge times, Force Influence and Concentration restrictions, Faith in the Force
resistance, and Stamina recharge modifier. The localized Force Power gauge starts at 100%, empties
after a successful conversion, and recharges before Convert becomes available again. Converters
drop a carried Holocron before changing ownership. Eligible Force users can context-command a
visible Holocron, then context-command a friendly Temple to secure it. A carried Holocron uses its
original graphic and floats in front of the carrier without replacing the carrier's animation.
Discovered Holocrons remain on the world/minimap under explored fog, while undiscovered or carried
Holocrons are not exposed. Each secured Holocron generates the civilization's DAT resource-191 Nova
rate. Vita fog presentation is a cached screen-space overlay rather than per-row rectangle fan-out,
keeping its render cost to one draw while preserving the existing visibility state.

The skirmish lobby offers the original **Standard**, **Conquest**, **Time Limit**, and **Score**
victory choices plus the existing Command Center compatibility mode. Standard combines military
conquest with Monument and all-Holocron control. Monument/Holocron control displays a persistent
countdown and resets when control is lost. Time Limit and Score show live objective status; team
victory requires mutual alliance and allied-victory participation. Stealth from Mind Trick is
enforced in rendering, selection, commands, minimap, automatic acquisition, conversion, and AI
queries. Perception-enabled Masters and DAT trait-bit-8 detectors share detection with allies;
`FORCESIGHT` remains the explicit human-player detection bypass.

Settings are stored in `ux0:data/swgb/settings.bin`, campaign completion/unlock state in
`ux0:data/swgb/campaign.profile`, and the current continuation in
`ux0:data/swgb/skirmish.save`. All are bounded, versioned, checksummed, and atomically replaced.
Corrupt settings recover to defaults with an explicit message; corrupt, oversized, mismatched, or
unsupported campaign profiles and saves are rejected without replacing the current match. Save
version 3 adds a campaign archive/entry context while retaining version-1 and version-2 generated
skirmish compatibility.

Combat includes pursuit with collision-aware A* pathfinding, attack animations and
acknowledgements, original projectile graphics and weapon sounds, researched accuracy with
deterministic misses, DAT attack/armor classes, frame delays, secondary projectiles, minimum
range, blast impact, shields, reload timing, health-bar depletion, death animations, remains,
rubble, and progressive building damage graphics. X can select a visible hostile unit or
building for inspection without allowing it to receive orders. Idle combat units acquire nearby
hostile targets according to stance, retaliate when attacked, return after defensive pursuits,
and armed buildings fire automatically. Move and attack orders briefly display the original
red target marker. Exact original range/elevation accuracy modifiers and blast-level comparison
remain under executable research.

On island maps, select a Utility Trawler and command visible fish or an Aqua Harvester with the
normal contextual input. Trawlers carry fish as food, deposit at compatible naval drop sites,
retarget nearby fish after exhaustion, construct DAT-authorized naval buildings and defenses,
and repair eligible naval/mechanical targets. Carried food and capacity appear in the selection
panel, and generated-skirmish saves retain active fishing, projectiles, and formation movement.

## License

GPL-3.0-or-later. *Star Wars: Galactic Battlegrounds* and its data files belong to their respective owners. This project requires a legally owned copy of the game.
