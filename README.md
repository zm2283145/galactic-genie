# Galactic Genie

A from-scratch, portable reimplementation of the Genie engine, as used by *Star Wars: Galactic Battlegrounds Saga*. The current targets are PS Vita and PC. It loads the original Clone Campaigns data files from your own copy of the game; no game assets are included in this repository.

It is written clean-room style: file formats come from public documentation (openage, genieutils). Game behaviour is to be matched by observation, and by using Ghidra only to answer "how does X work" questions. Decompiled code is never copied into the tree.

## Status: milestone 3 (simulation core, in progress)

| Area | State |
|---|---|
| DRS archives (40/60-byte headers, expansion override order) | done |
| JASC palettes (interfac.drs 50500) | done |
| SLP 2.0N decoder (all draw commands, player colour, shadows, outlines) | done |
| `genie_x1.dat` (VER 5.9) full parse, verified byte-exact to EOF | done |
| Sprite atlas builder (per SLP and player colour) | done |
| Isometric terrain renderer with `blendomatic.dat` priority/mode edge blending | done |
| Terrain elevation with original slope geometry and neighbor-sensitive lighting | done |
| CPX/SCX terrain, elevation, player state, triggers, saved camera, and initial object loading (“Breaking Bread”) | done |
| Initial trigger runtime: timers, object/area/resource conditions, ordered effects, voiced dialogue, and collision-aware scripted movement | in progress |
| Units: idle/walk animation, 8-way facing with mirroring, graphic deltas for buildings | done |
| Combat: contextual attack orders, pursuit, projectiles, DAT-driven range/reload/damage, health, building damage, and death | in progress |
| Vita: renderer, camera controls, original cursors, unit selection/status markers, formation movement, and debug minimap | done |
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
2. Copy `Game/Campaign/XCAM3.CPX` to `ux0:data/swgb/Campaign/xcam3.cpx`. `tools\deploy_vita.ps1 -CampaignData` does this over FTP. The current vertical slice loads its second mission, “Breaking Bread,” including its initial trees, resources, buildings, and units.
3. Copy the MP3 dialogue referenced by the mission from `Game/Sound/Scenario` to
   `ux0:data/swgb/Sound/Scenario/`. `tools\deploy_vita.ps1 -SoundData` extracts the names from
   campaign entry 2 and copies only those files.
4. Install `swgb.vpk` with VitaShell. `tools\deploy_vita.ps1 -Vpk` uploads it to `ux0:data/swgb/`.
5. The app writes a log to `ux0:data/swgb/swgb.log`. `tools\deploy_vita.ps1 -PullLog` fetches it.

Controls: left stick, d-pad, or a touch drag scrolls; L/R zoom. The right stick moves the
command cursor, and holding it against a screen edge scrolls the camera. X selects a visible
unit or building for inspection. With friendly units selected, O over a hostile unit issues an
attack order; O over terrain issues a move order. A touch tap uses the same contextual selection
or command behavior. Double-tap a unit or press X twice to select every matching friendly unit
in the viewport. Hold Square and move the right stick, or hold Square while touch-dragging, to
box-select units. The normal, move, and attack cursors are loaded from the original
`interfac.drs`, with a procedural fallback if they are unavailable. Triangle cycles selected
combat units through defensive, stand-ground, passive, and aggressive stances. SELECT toggles
the minimap overlay and START quits.

Combat currently includes pursuit with collision-aware A* pathfinding, attack animations and
acknowledgements, original projectile graphics and weapon sounds, DAT attack/armor classes,
reload timing, health-bar depletion, death sounds and animations, decaying unit remains and
building rubble, and progressive building fire/damage graphics. X can also select a visible
hostile unit or building for inspection without allowing it to receive orders. Accuracy, blast
damage, and explicit guard/patrol commands remain to be implemented. Idle combat units acquire
nearby hostile targets according to their stance, retaliate when attacked, return to their
assigned post after defensive pursuits, and armed buildings fire automatically. Move and attack
orders briefly display the original game's red target marker at the commanded location.

## License

GPL-3.0-or-later. *Star Wars: Galactic Battlegrounds* and its data files belong to their respective owners. This project requires a legally owned copy of the game.
