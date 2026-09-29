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
- CPX campaign archive and SCX terrain/elevation loading are done. The Vita vertical slice now opens
  Galactic Empire mission 2, “Breaking Bread,” directly from the original `XCAM3.CPX`.
- Initial scenario objects and the saved camera are loaded, including trees, resources, buildings, and
  units. Terrain aliases and civilization-specific farm terrain are resolved from the original data.
- Player names, civilizations, colors, resources, population limits, diplomacy, and allied-victory state
  are parsed. Trigger definitions, objective metadata, conditions, effects, display order, dialogue, and
  selected-object references are parsed and validated across all 43 original `XCAM` scenarios.
- The first trigger runtime executes ordered trigger chains, timers, object/area/resource/difficulty
  conditions, player/resource/diplomacy changes, object creation/removal/ownership, gate state,
  collision-aware scripted movement, and visible queued dialogue. Movement uses DAT terrain
  restrictions, flying-unit behavior, static obstacle footprints, destination formations, dynamic
  occupancy, and bounded A* routes. Scenario MP3 dialogue is decoded and queued with its matching
  subtitle on Vita. DAT-driven selection and movement acknowledgements are loaded from the original
  sound archives and mixed over dialogue. Combat-driven object removal now feeds trigger conditions.
  AI goals, full technology effects, general world effects, music, and campaign progression remain.
- Fog of war and explored/visible tile state.
- Shadow and player-outline passes (PX_OUTLINE pixels drawn only when occluded).

## Milestone 3: simulation core
- Fixed-timestep simulation separate from rendering. Deterministic, to leave room for multiplayer later.
- Player-issued move and attack commands use terrain-restriction A* pathing, dynamic occupancy,
  destination formations, and pursuit to weapon range. Broader task-list actions remain.
- Unit commands from the dat task lists (`UnitHeader.tasks`): move, attack, gather, build, repair, garrison.
- The initial combat slice includes contextual hostile targeting, runtime hit points, DAT-driven
  attack/armor classes, range and minimum range, reload timing, attack animations and sounds,
  original projectile flight and weapon sounds, health-bar depletion, death sounds, and progressive
  building damage/fire graphics. Original death/destruction animations transition into unit remains
  or building rubble and decay over time. Units support aggressive, defensive, stand-ground, and
  passive stances; eligible units and armed buildings acquire targets and retaliate automatically.
  Accuracy, blast damage, and explicit guard/patrol commands remain.
- Economy: the four SWGB resources, drop sites, training queues, population (power cores and prefab shelters).
- Tech effects (`Effect`/`EffectCommand`) and ages, which SWGB calls tech levels.
- SWGB-specific mechanics: shields, air units, power cores, Jedi/Sith conversion, holocrons.

## Milestone 4: interface and input on Vita
- Replace the temporary bitmap trigger-dialogue font with UI rendering from `interfac.drs` SLPs.
  Original `language.dll`, `language_x1.dll`, and `language_x2.dll` string tables are parsed and
  already supply localized names to the selection panel.
- Single, box, and visible-type selection, selection sounds, and contextual original normal/move/attack
  cursors are implemented. The command cursor scrolls the camera at screen edges. Unit panels and
  control groups remain.
- The first command/info panel shows the selected unit or building portrait, live health,
  attack, armor, range, stance, and Vita command hints. Full task buttons, minimap,
  resource bar, and pause menu remain.
- Audio: selection, move, and attack acknowledgements plus weapon-fire and death effects are
  implemented. World ambience, volume controls, and music remain.

## Milestone 5: AI and skirmish
- A subset of the AI script interpreter (`.per` rules), checked against the original using Ghidra where needed.
- A skirmish setup screen, and victory/defeat conditions.

## Vita performance notes
- The main memory cost is texture atlases. Terrain and sprite working sets are capped at 64 MB,
  stale sheets are evicted before cache misses, and atlas widths adapt to their actual contents.
  An 8-bit paletted texture path could reduce this further.
- Terrain tiles are batched by terrain type, and sprites are batched by atlas page after depth sort.
- The dat parses in well under a second on PC; measure on hardware (the log reports load time and FPS).

## Useful references
- openage `doc/media/*`: DRS, SLP, blendomatic, terrain and palette format notes.
- genieutils: the dat layout for every Genie game version (this project implements the GV_CC path only).
