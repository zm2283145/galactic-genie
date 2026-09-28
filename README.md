# swgb-vita

A from-scratch reimplementation of the Genie engine, as used by *Star Wars: Galactic Battlegrounds Saga*, for the PS Vita. It loads the original Clone Campaigns data files from your own copy of the game. No game assets are included in this repository.

It is written clean-room style: file formats come from public documentation (openage, genieutils). Game behaviour is to be matched by observation, and by using Ghidra only to answer "how does X work" questions. Decompiled code is never copied into the tree.

## Status: milestone 2 (map and visual fidelity, in progress)

| Area | State |
|---|---|
| DRS archives (40/60-byte headers, expansion override order) | done |
| JASC palettes (interfac.drs 50500) | done |
| SLP 2.0N decoder (all draw commands, player colour, shadows, outlines) | done |
| `genie_x1.dat` (VER 5.9) full parse, verified byte-exact to EOF | done |
| Sprite atlas builder (per SLP and player colour) | done |
| Isometric terrain renderer with `blendomatic.dat` priority/mode edge blending | done |
| Terrain elevation with original slope geometry and neighbor-sensitive lighting | done |
| CPX/SCX terrain, elevation, saved camera, and initial object loading (“Breaking Bread”) | done |
| Units: idle/walk animation, 8-way facing with mirroring, graphic deltas for buildings | done |
| Vita: vitaGL batched fixed-function renderer, stick/d-pad/touch scroll, L/R zoom | done |
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

1. Copy these files from the game's `Game/Data` folder to `ux0:data/swgb/Data/`: `genie_x1.dat`, `graphics.drs`, `graphics_x1.drs`, `terrain.drs`, `terrain_x1.drs`, `interfac.drs`, `interfac_x1.drs`, `blendomatic.dat`, `STemplet.dat`, `FilterMaps.dat`, `VIEW_ICM.DAT`, `lightMaps.dat`, and `PatternMasks.dat` (about 313 MB). `tools\deploy_vita.ps1 -GameData` does this over FTP.
2. Copy `Game/Campaign/XCAM3.CPX` to `ux0:data/swgb/Campaign/xcam3.cpx`. `tools\deploy_vita.ps1 -CampaignData` does this over FTP. The current vertical slice loads its second mission, “Breaking Bread,” including its initial trees, resources, buildings, and units.
3. Install `swgb.vpk` with VitaShell. `tools\deploy_vita.ps1 -Vpk` uploads it to `ux0:data/swgb/`.
4. The app writes a log to `ux0:data/swgb/swgb.log`. `tools\deploy_vita.ps1 -PullLog` fetches it.

Controls: left stick, d-pad or a touch drag scrolls. L and R zoom. SELECT toggles the minimap overlay. START quits.

## License

GPL-3.0-or-later. *Star Wars: Galactic Battlegrounds* and its data files belong to their respective owners. This project requires a legally owned copy of the game.
