# Roadmap

The goal is a playable SWGB: Clone Campaigns skirmish on the PS Vita, running on the original data files.

## Milestone 1: engine foundation (done)
- Format loaders: DRS, palette, SLP, and the full `genie_x1.dat`.
- Terrain and sprite rendering, animated units, a camera, and the Vita build.

## Milestone 2: map and visual fidelity
- Terrain blending with `blendomatic.dat` (including SWGB's 11 modes, terrain `blendType` lookup and `blendPriority`) (done).
- Elevation: all 17 rendered slope shapes, high-quality `FilterMaps` resampling, tile/object height
  placement, neighbor-sensitive `PatternMasks`/`lightMaps` shading, and a memory-bounded
  per-frame slope cache are done. Terrain blend masks are resampled through the same slope filters.
- Cliffs, trees and gaia resources (carbon, ore, nova, fruit, animals).
- Scenario (`.scx`) loader, so real maps can be shown instead of the procedural test map.
- Fog of war and explored/visible tile state.
- Shadow and player-outline passes (PX_OUTLINE pixels drawn only when occluded).

## Milestone 3: simulation core
- Fixed-timestep simulation separate from rendering. Deterministic, to leave room for multiplayer later.
- Tile passability from terrain restrictions, and A* pathing with unit clearance sizes.
- Unit commands from the dat task lists (`UnitHeader.tasks`): move, attack, gather, build, repair, garrison.
- Combat: attack/armor classes, projectiles, reload times and blast damage.
- Economy: the four SWGB resources, drop sites, training queues, population (power cores and prefab shelters).
- Tech effects (`Effect`/`EffectCommand`) and ages, which SWGB calls tech levels.
- SWGB-specific mechanics: shields, air units, power cores, Jedi/Sith conversion, holocrons.

## Milestone 4: interface and input on Vita
- Font and UI rendering from interfac.drs SLPs and language_x1.dll strings.
- Selection: touch tap/drag-box, and a controller cursor with snap-to-unit.
- Command panel, minimap, resource bar and the pause menu.
- Audio: sounds.drs/sounds_x1.drs WAVs through SceAudio, and music.

## Milestone 5: AI and skirmish
- A subset of the AI script interpreter (`.per` rules), checked against the original using Ghidra where needed.
- A skirmish setup screen, and victory/defeat conditions.

## Vita performance notes
- The main memory cost is texture atlases (about 27 MB for the test scene). Plan: evict unused SLP sheets with an LRU cache, and consider 8-bit paletted textures with a palette shader to cut memory by 4x.
- Terrain tiles are batched by terrain type, and sprites are batched by atlas page after depth sort.
- The dat parses in well under a second on PC; measure on hardware (the log reports load time and FPS).

## Useful references
- openage `doc/media/*`: DRS, SLP, blendomatic, terrain and palette format notes.
- genieutils: the dat layout for every Genie game version (this project implements the GV_CC path only).
