# Roadmap

The goal is a playable SWGB: Clone Campaigns reimplementation on the PS Vita, running on the original data files.

## Milestone 1: engine foundation (done)
- Format loaders: DRS, palette, SLP, and the full `genie_x1.dat`.
- Terrain and sprite rendering, animated units, a camera, and the Vita build.

## Milestone 2: map and visual fidelity
- Terrain blending with `blendomatic.dat` (including SWGB's 11 modes, terrain `blendType` lookup and `blendPriority`) (done).
- Elevation: all 17 rendered slope shapes, high-quality `FilterMaps` resampling, tile/object height
  placement, neighbor-sensitive `PatternMasks`/`lightMaps` shading, and a memory-bounded
  per-frame slope cache are done. Terrain blend masks are resampled through the same slope filters.
- Cliffs, trees and gaia resources (carbon, ore, nova, fruit, animals).
- CPX campaign archive and SCX terrain/elevation loading are done. The frontend discovers the six
  stock `XCAM*.CPX` archives, structurally loads all 43 missions, and presents localized campaign
  and mission metadata. Sequential profile progression and an explicit development-access option
  replace the former hardcoded “Breaking Bread” entry. Development builds can also start on a
  96-by-96 two-island test map. A full-height ocean channel blocks
  ground passage while aircraft cross normally; both shorelines accept Shipyards and each island
  retains a base, livestock, power/shield coverage, test targets, a 24-tree carbon forest,
  12 food nodes, and separate 8-node ore and nova deposits on accessible land.
- Initial scenario objects and the saved camera are loaded, including trees, resources, buildings, and
  units. Terrain aliases and civilization-specific farm terrain are resolved from the original data.
- Player names, civilizations, colors, resources, population limits, diplomacy, and allied-victory state
  are parsed. Trigger definitions, objective metadata, conditions, effects, display order, dialogue, and
  selected-object references are parsed and validated across all 43 original `XCAM` scenarios.
- The stock trigger runtime executes ordered, delayed, looping, activation/deactivation chains and
  every condition/effect type used by the 43 original missions. This includes object/area/type/class,
  technology, AI-signal, visibility, garrison, foundation, power, population, resource, diplomacy,
  difficulty, defeat, camera, objectives, dialogue/audio, task/patrol/freeze/stop, damage/HP/attack,
  ownership, foundation, gate, enable/disable, victory, and defeat behavior. Movement uses DAT terrain
  restrictions, flying-unit behavior, static obstacle footprints, destination formations, dynamic
  occupancy, and bounded A* routes. Scenario MP3 dialogue is decoded and queued with its matching
  subtitle on Vita. DAT-driven selection and movement acknowledgements are loaded from the original
  sound archives and mixed over dialogue. Combat-driven object removal now feeds trigger conditions.
  Scenario-authored restrictions, starting ages, resources, population, diplomacy, allied victory,
  embedded personalities, and AI goals are initialized. A deterministic host gate parses all 43
  missions and runs 79 bounded difficulty-relevant simulations with zero unsupported stock-used
  trigger types.
- Per-player fog of war is implemented from researched DAT line of sight.
  Current visibility moves with units while exploration persists; allied
  players share sight. Unexplored terrain is shrouded, explored terrain is
  dimmed through smoothly interpolated isometric masks, and enemy units,
  attacks, selection, and automatic targeting require current sight. Static
  resource nodes and discovered buildings remain known after exploration.
  `FORCEEXPLORE` reveals the full terrain and buildings for the human player
  while keeping units hidden; `FORCESIGHT` grants that player live vision.
- Shadow and player-outline passes (PX_OUTLINE pixels drawn only when occluded).

## Milestone 3: simulation core
- Fixed-timestep simulation separate from rendering. Deterministic, to leave room for multiplayer later.
- Player-issued move and attack commands use cached terrain-restriction A* pathing, dynamic occupancy,
  shared formation routes, route-tangent marching columns, slowest-member group speed with bounded
  catch-up, collision-free movement within established formations, precise destination formations,
  and continuous walking animation for every actively marching member. Idle units do not perform the
  former sample wander; generated units begin in distinct slots. Workers, traders, and distant
  stragglers move independently. Broader task-list actions remain.
- Unit commands from the dat task lists (`UnitHeader.tasks`): move, attack, gather, build, repair,
  and garrison. Workers repair owned or allied buildings and DAT-designated mechanical classes,
  consume half of the target's original resource cost across a full HP restoration, and use the
  original working animation.
- The initial combat slice includes contextual hostile targeting, runtime hit points, DAT-driven
  attack/armor classes, range and minimum range, reload timing, attack animations and sounds,
  original projectile flight and weapon sounds, health-bar depletion, death sounds, and progressive
  building damage/fire graphics. Original death/destruction animations transition into unit remains
  or building rubble and decay over time. Units support aggressive, defensive, stand-ground, and
  passive stances with original-style pursuit and return behavior; eligible units and armed buildings
  acquire targets and retaliate automatically. Building damage overlays sort above their owning
  building while preserving world depth. Group attacks reserve separate melee/ranged approach slots,
  use outer waiting positions when contact space is full, and pass through attackers bound for
  different final slots rather than deadlocking. Units sharing an attack, construction, gathering,
  repair, garrison, or formation destination cooperatively yield and displace one another instead of
  treating every nearby unit as a hard obstacle.
  Projectile accuracy now uses researched DAT accuracy, deterministic
  hit/miss rolls, fixed plausible miss impacts, and the existing projectile,
  shield, diplomacy, Attack Ground, and splash paths. Exact original
  range/elevation accuracy modifiers and blast-level comparison remain open.
  Explicit guard/patrol commands are implemented with saved-state restoration.
- Economy: the four SWGB resources drive shared building queues for units and research. Original
  `tech-level-1`, `MADE-*`, and `AVAIL-*` technologies determine civilization-correct production
  choices, while researched replacement effects upgrade both available choices and existing units
  and buildings. Production locations follow each building's researched upgrade lineage, so Command
  Centers and Troop Centers retain their original production menus after advancing a Tech Level.
  Command Centers also progress through their TL2, TL3, and TL4 appearance replacements.
  Every queued unit/research item is displayed and can be cancelled for a refund.
  Power Cores and mobile Power Droids provide the original nine-tile power coverage; selected power
  sources show the original blue coverage ring. Unpowered buildings train and research at 25% speed.
  DAT power-indicator frames render solid green while powered and blink red while
  unpowered. Selected buildings also show powered/unpowered and shield-coverage icons. Power Core
  placement/construction uses the original blue coverage ring, while Shield Generators retain the
  original yellow shield-boundary ring during placement and draw their original civilization-specific
  animated shield field while completed and powered. Workers expose
  technology-driven economy, military, and defense building pages, validate placement and costs with
  flashing green/red footprints, walk to foundations, switch to their original Builder state, and
  construct buildings with progressive graphics and hit points. Multiple workers can be assigned to
  one foundation using the original diminishing-returns build-rate curve; retasked workers can resume
  a foundation through a contextual command. After completing a resource drop site, assigned workers
  automatically gather a compatible nearby node; otherwise they continue onto the nearest friendly
  foundation within their original line of sight. Workers use original resource cursors, task variants,
  working/carrying animations, capacities, and civilization drop sites while gathering food,
  carbon, ore, and nova, and multiple selected workers can share one gather order. The panel shows
  each worker's carried resource amount and capacity. Workers accept explicit orders to deposit a
  partial load at any compatible building and then return to their previous node. Depleted trees and
  other nodes transition through their original death/remains graphics instead of vanishing. Farms
  are available as a base economy building rather than depending on their
  circular `MADE-*` technology. Neutral Nerfs and Banthas use the
  original Gaia color, convert through proximity capture, and remain excluded from proactive
  military/building targeting; explicit attacks still work. Hostile class-5 Gaia predators attack
  player units and provoke retaliation. Animal Nurseries produce food at their DAT work rate per
  garrisoned animal. Live-animal slaughter, farms,
  automatic adjacent-node retargeting, and DAT-driven population use/capacity are implemented.
  Production waits at 100% when housing is full and resumes after capacity is
  added. Utility Trawlers now gather raw resource 17 from DAT fish and Aqua
  Harvesters into food, carry/deposit/retarget after exhaustion, build and
  repair the DAT-authorized naval set, expose original task presentation, and
  participate in generated island-map AI economies.
- Researched technology effects apply packed DAT attack/armor modifiers plus generic health, speed,
  and reload-time modifiers, along with chained unit/building upgrade and age effects. Civilization
  `techTreeId` effects now apply their disabled-research lists before menus and automatic technologies
  are evaluated. Player resource attributes drive aircraft Shield
  Modifications consistently. Researched garrison capacity, accuracy, work
  rate, carrying capacity, and base armor now join the existing hit-point,
  sight, movement, packed armor/attack, reload, range, projectile, and minimum
  range consumers. Conversion and Holocron consumers are now limited to the
  executable/DAT-backed resources documented in `ORIGINAL_SYSTEMS.md`; other
  unproven player-resource counters remain unsupported rather than being
  blanket-applied.
- SWGB-specific mechanics: power and shield coverage are implemented. A powered Shield Generator
  gives eligible units and buildings a gold shield bar equal to maximum HP, with original
  tiered regeneration, non-stacking coverage, overflow damage, and one-HP per-hit leakage for
  mobile units but not buildings. Shields retained after leaving coverage drain visibly at 40
  points per second, reduced to 20 by Superconducting Shields; Shield Wall enables adjacent wall
  coverage. World and selection bars display the remaining shield amount numerically.
  Civilization-correct Air Transports and Transport Ships are available from completed Airbases and
  Shipyards through their original `AVAIL-*` technologies. Aircraft movement ignores ground
  occupancy according to DAT fly mode, while class-based air/ground weapon restrictions, transports,
  shields, formations, repair, fog, AI production/use, projectiles, death, and save/load share the
  normal authoritative systems. No unsupported fuel mechanic is added.
  Jedi/Sith action-104 conversion is complete through command UI, deterministic DAT work/recharge,
  researched eligibility/resistance/recharge modifiers, ownership cleanup, AI-safe visibility, and
  save/load. Holocrons now have deterministic generated-map placement, discovery, Force-user
  pickup/carry/drop, Temple delivery/ejection/ownership, DAT resource-191 Nova generation,
  minimap/world presentation, AI acquisition/delivery, reset, and save/load. Mind Trick stealth and
  Perception/trait-bit-8 detection are enforced across rendering, minimap, selection, targeting,
  automatic acquisition, conversion, allied sharing, AI knowledge, cheats, and persistence.

## Milestone 4: interface and input on Vita
- Replace the temporary bitmap trigger-dialogue font with UI rendering from `interfac.drs` SLPs.
  Original `language.dll`, `language_x1.dll`, and `language_x2.dll` string tables are parsed and
  already supply localized names to the selection panel.
- Single, box, and visible-type selection, original leader-first group ordering and command audio,
  selection sounds, and contextual original normal/move/attack cursors are implemented. The command
  cursor scrolls the camera at screen edges. Unit panels and control groups remain.
- The command/info panel shows the selected unit or building portrait, live health and shields,
  non-zero combat attributes, stance, multi-selection portraits with individual health, formation controls,
  Vita command hints, and an original-style five-by-three icon command grid with a high-contrast
  information pane. Buildings separate civilization-correct Units and Research tabs, support touch
  or L/R switching, and show every cancellable queue item plus current progress. Research pages use
  the Clone Campaigns technology sheets and show only technologies whose current Tech Level and
  prerequisites are satisfied. A successor sharing a button slot replaces its prerequisite in that
  same slot after research, while visible choices retain exact costs, duration, requirements, and
  DAT-derived effects. Gates expose lock/unlock commands rather than production;
  selected gate footprints follow their oriented annexes. Unit/group panels use the original command sheet for
  garrison targeting, repair targeting, gate locking, destruction, and ejection. The destroy command
  removes one object per press in reverse selection order. Eligible buildings show their garrisoned
  unit portraits; a portrait ejects only that unit while Eject All remains available. Animal
  Nurseries use the same clickable occupant portraits and show their live food-per-second rate. Attack-capable
  selections expose a dedicated original-style stance grid while Triangle still cycles stances
  directly. Mobile units expose the original Stop, Patrol, Guard, Follow, and Attack Ground
  icons in a dedicated command grid; patrol resumes after stance-aware combat, Guard protects
  and returns to a friendly target, Follow maintains spacing, and Stop clears the active order.
  Choosing a formation immediately rearranges idle combat units.
  Multiple selected workers can open the same three categorized building pages and share the resulting
  construction order and placement preview. Empty buildings do not
  expose an action-menu hint. Selection status distinguishes the local player,
  named allies, neutral sides, and enemies using scenario diplomacy, scenario player names, and
  civilization names. Status icons expose powered, unpowered, shielded, and unshielded tooltips.
  Enemy selections hide unit/research queue names and progress without hiding construction progress;
  the custom `MANY BOTHANS` toggle restores that production intelligence without changing fog.
  Player-color occlusion outlines use a thicker, high-alpha masked pass so units remain legible
  behind buildings and resource sprites.
  A persistent resource, population, and Tech Level bar is shown at the top, including age-research
  progress. A scrollable L + R + Select test menu exposes
  the original base-game and Clone Campaigns cheats, including resource grants, unit spawns, instant
  production/construction, victory/player defeat, and hidden DAT cheat technologies. It also includes
  the custom `OIIA OIIA` secret-unit entry: the OIIA Cat uses the contributed 94-frame cat animation,
  spins while attacking, and plays an original synthesized oiia-style vocal/chiptune cue.
  The app now starts at a controller/touch title and main menu rather than a fixed development
  match. Skirmish opens a complete setup lobby; the existing Breaking Bread vertical slice remains
  available from the main menu, while unavailable Multiplayer is visibly disabled.
  START pauses an active match with resume, same-seed restart, and main-menu controls;
  victory and defeat provide restart and main-menu controls without restarting the process.
  A 128-by-128 cached original-style diamond minimap now renders terrain/elevation,
  explored/current fog, DAT-filtered objects, player colors, the camera viewport, and
  hostile-damage pings. Touch, cursor presses, and touch drags navigate it without per-tile
  draw calls. Four Vita control groups support ordered assignment, recall, repeated-recall
  centering, dead-object cleanup, and visible status/count feedback.
  The pause menu now freezes simulation and provides save, load, confirmed same-seed restart,
  options, and confirmed return-to-menu actions. Generated skirmishes use a bounded,
  checksummed, versioned authoritative save containing map, objects/orders/queues/garrisons,
  fog, projectiles/remains, AI state, outcome state, camera, selection, formations, and groups.
  The main menu can continue a validated save. Master, music, dialogue, and effects volume
  apply live and persist with a constrained Standard/Left-Handed Vita control preset;
  corrupt settings and incompatible saves are explicitly rejected.
  The startup/title/main/campaign/pause/objectives/options/data-status/confirmation/outcome surfaces
  now share a Vita-bounded modern Star Wars visual and navigation framework with localized original
  labels where available. Multiplayer remains visibly unavailable and the Scenario Editor route is
  reserved without entering a false flow. Campaign saves carry their source archive/entry and the
  campaign profile persists completion/unlocks independently. Further in-game HUD art fidelity and
  keyboard mappings for control groups 5-10 remain.
- Audio: selection, move, and attack acknowledgements plus camera-relative weapon-fire, impact, and
  death effects are implemented. The original Ogg soundtrack streams from storage, ducks beneath
  scenario dialogue, and mixes with terrain-specific world ambience. Scenario speaker text uses
  the speaking player's color, while hostile damage uses the original cooldown-controlled
  `atakwarn.wav` with a red warning naming the attacking player. Other players' ordinary research
  is silent, while their Tech Level advancement is announced by name using `archupg.wav`.
  Garrison/ejection, gate
  transformation, unit-training, construction-start, and building-completion cues use their DAT
  sound groups. Livestock capture uses `capsheep.wav`, and queued Tech Level completion uses the
  executable's `archupg.wav` resource. User-facing master, music, dialogue, and effects volume
  controls are implemented.

## Milestone 5: AI and skirmish
- The original AI economy and first military progression slice are implemented. The `.per` parser
  loads the complete 40-file Computer Expanded personality (1,423 rules), including
  recursive/conditional loads, constants, Boolean expressions, and three-valued unsupported facts.
  Goals, timers, strategic numbers, and escrow drive real gathering, Tech Level advancement,
  construction, research, Shipyards, and civilization-correct naval production through normal
  placement, costs, population, and queues. Worker allocation spreads tree jobs and recovers
  depleted resources without exposing deposits through fog.
- Original `attack-now` actions select currently visible enemies. Idle military units physically
  scout unexplored passable tiles, while non-air attackers require a valid route to weapon range;
  the AI receives no hidden target positions. Persistent land, naval, and air groups use the
  ordinary formation marcher and do not replace equivalent active orders on later rule pulses.
  Land groups separated from a discovered enemy building reserve DAT-compatible transports,
  assemble at a reachable shore, board with capacity reservations, cross valid air/water routes,
  unload onto route-valid enemy terrain, reform, and continue the attack. A strategic manager now
  maintains Tech-Level-scaled land, naval, air, and transport targets; balances DAT production,
  escorts invasions with ships and aircraft, prioritizes critical targets, regroups survivors, and
  retreats badly outmatched armies for a timed rebuild. Scenario-selected personalities and
  Scenario-selected embedded personalities and objective-aware, fog-constrained build-forward
  placement are implemented; generated matches retain selectable Expanded and Classic personalities.
- AI workers near a threatened Command Center enter it through ordinary garrison orders, remain
  excluded from economy retasking while moving or sheltered, and eject after five safe seconds.
  All completed garrison-capable non-transport buildings evacuate and reject new occupants at or
  below 20 percent health, then unlock after repair above that threshold.
- Generated matches now expose original Computer Expanded and Computer Classic personalities,
  five difficulty defines, both civilizations, diplomacy/team state, deterministic seeds,
  population and resource presets, and Standard, Conquest, Time Limit, Score, or Command Center
  victory. Standard combines conquest with controlled Monument and all-Holocron countdowns;
  control loss resets the countdown, mutual allies share team evaluation, and ties resolve by the
  lowest player number. Grasslands,
  Archipelago, and the compact two-island regression map are selectable. Starts have symmetric
  DAT resource patches and island maps retain DAT-valid Shipyard shorelines. Repeated matches
  reset simulation, AI, trigger, fog, queue, outcome, input, and audio state.
- Generated matches now enforce conquest elimination, AI recognition of defeated players, and
  collapse/outmatched surrender. Local victory and defeat display full-screen outcome panels and
  play the original `WON1.MP3` or `lost.mp3` stream.
- Original AI `hold-holocrons` and `enemy-captured-holocrons` facts now read authoritative
  carried/secured Holocron state. Native AI carriers only acquire currently visible Holocrons,
  return them to friendly Temples, and never query hidden positions.

## Vita performance notes
- The main memory cost is texture atlases. Terrain and sprite working sets are capped at 64 MB,
  stale sheets are evicted before cache misses, and atlas widths adapt to their actual contents.
  An 8-bit paletted texture path could reduce this further.
- Terrain tiles are batched by terrain type, and sprites are batched by atlas page after depth sort.
- The dat parses in well under a second on PC; measure on hardware (the log reports load time and FPS).

## Useful references
- openage `doc/media/*`: DRS, SLP, blendomatic, terrain and palette format notes.
- genieutils: the dat layout for every Genie game version (this project implements the GV_CC path only).
