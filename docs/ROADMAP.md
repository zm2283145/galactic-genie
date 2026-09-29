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
- Player-issued move and attack commands use cached terrain-restriction A* pathing, dynamic occupancy,
  shared formation routes, route-tangent marching columns, slowest-member group speed with bounded
  catch-up, collision-free movement within established formations, and precise destination formations.
  Workers, traders, and distant stragglers move independently. Broader task-list actions remain.
- Unit commands from the dat task lists (`UnitHeader.tasks`): move, attack, gather, build, repair, garrison.
- The initial combat slice includes contextual hostile targeting, runtime hit points, DAT-driven
  attack/armor classes, range and minimum range, reload timing, attack animations and sounds,
  original projectile flight and weapon sounds, health-bar depletion, death sounds, and progressive
  building damage/fire graphics. Original death/destruction animations transition into unit remains
  or building rubble and decay over time. Units support aggressive, defensive, stand-ground, and
  passive stances with original-style pursuit and return behavior; eligible units and armed buildings
  acquire targets and retaliate automatically. Building damage overlays sort above their owning
  building while preserving world depth. Group attacks reserve separate melee/ranged approach slots,
  use outer waiting positions when contact space is full, and pass through attackers bound for
  different final slots rather than deadlocking.
  Accuracy, blast damage, and explicit guard/patrol commands remain.
- Economy: the four SWGB resources drive shared building queues for units and research. Original
  `tech-level-1`, `MADE-*`, and `AVAIL-*` technologies determine civilization-correct production
  choices, while researched replacement effects upgrade both available choices and existing units
  and buildings. Every queued unit/research item is displayed and can be cancelled for a refund.
  Power Cores provide the original nine-tile power coverage; unpowered buildings train and research
  at 25% speed. DAT power-indicator frames render solid green while powered and blink red while
  unpowered. Selected buildings also show powered/unpowered and shield-coverage icons. Power Core
  placement/construction uses the original blue coverage ring, while Shield Generators retain the
  original yellow shield-boundary ring through placement, construction, and completion. Workers expose
  technology-driven economy, military, and defense building pages, validate placement and costs with
  flashing green/red footprints, walk to foundations, switch to their original Builder state, and
  construct buildings with progressive graphics and hit points. Retasked workers can resume a
  foundation through a contextual command. Gathering/drop sites, multiple builders,
  repair/construction cancellation, shield damage, and population-limit enforcement remain.
- Researched technology effects apply packed DAT attack/armor modifiers plus generic health, speed,
  and reload-time modifiers, along with chained unit/building upgrade and age effects. Remaining
  attributes and resource effects still need
  to be applied.
- SWGB-specific mechanics: power coverage and shield-coverage detection are implemented. Shield
  absorption/regeneration, air units, Jedi/Sith conversion, and holocrons remain.

## Milestone 4: interface and input on Vita
- Replace the temporary bitmap trigger-dialogue font with UI rendering from `interfac.drs` SLPs.
  Original `language.dll`, `language_x1.dll`, and `language_x2.dll` string tables are parsed and
  already supply localized names to the selection panel.
- Single, box, and visible-type selection, original leader-first group ordering and command audio,
  selection sounds, and contextual original normal/move/attack cursors are implemented. The command
  cursor scrolls the camera at screen edges. Unit panels and control groups remain.
- The command/info panel shows the selected unit or building portrait, live health, attack,
  armor, range, stance, multi-selection portraits with individual health, formation controls,
  Vita command hints, and contextual action menus. Buildings separate civilization-correct Units
  and Research tabs, support touch or L/R switching, and show every cancellable queue item plus
  current progress. Gates expose lock/unlock commands rather than production; selected gate
  footprints follow their oriented annexes. Unit/group panels provide original-icon garrison
  targeting, while eligible buildings provide ejection and capacity feedback. Workers open a
  Buildings page with a placement preview. Selection status distinguishes the local player,
  named allies, neutral sides, and enemies using scenario diplomacy. A persistent resource and population bar is shown at
  the top. A scrollable L + R + Select test menu exposes
  the original base-game and Clone Campaigns cheats, including resource grants, unit spawns, instant
  production/construction, victory/player defeat, and hidden DAT cheat technologies. General worker
  task buttons, minimap, and pause menu remain.
- Audio: selection, move, and attack acknowledgements plus camera-relative weapon-fire, impact, and
  death effects are implemented. The original Ogg soundtrack streams from storage, ducks beneath
  scenario dialogue, and mixes with terrain-specific world ambience. Garrison/ejection, gate
  transformation, unit-training, and construction cues use their DAT sound groups. User-facing
  volume controls remain.

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
