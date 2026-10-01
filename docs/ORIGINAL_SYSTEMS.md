# Original-system verification

Behavior changes should be traced to the Clone Campaigns DAT, tasks, graphics,
language resources, or `battlegrounds_x1.exe` before implementation. Each
subsystem is completed as a vertical slice: reverse engineer, record a behavior
contract, add deterministic host coverage, play-test a rendered scenario, then
build and test on Vita.

## Verification ledger

| System | Original evidence | Current status |
|---|---|---|
| Production exits | `0x56e390` completes production; `0x558810` searches footprint-relative exit positions | Verified and covered |
| Building placement | Preview at `0x5fc7b0`; execution at `0x618e70`; DAT clearance/terrain/hill fields | Verified and covered |
| Command cursors | `mcursors.shp` (`51000`), 19 frames | Verified and covered |
| Gather points | Commands at `0x502580`/`0x5bcb30`; spawned-unit dispatch at `0x56e390` | Verified and covered |
| Gates | State update at `0x558390`; placement at `0x60c100` | Verified and covered |
| Shields and power | Coverage/status logic at `0x54bc40`/`0x55ec20` | Partially verified; mobile damage bleed-through remains open |
| Garrison fire | Volley logic at `0x55be20`/`0x55c0c0` | Verified and covered |
| Walls | Command executor at `0x5ba900`; preview at `0x5fc180` | Verified and covered |
| Formations | Layout/update routines at `0x478e30`, `0x479760`, `0x47c280`, `0x47e780`, `0x480060` | Layout verified; automatic line/column switching remains open |
| Pathfinding | Long-range routines at `0x4982f0`, `0x498820`, `0x4989f0`, `0x499010` | Partially verified and covered |
| Unit information UI | Panel rows at `0x5d98a0`/`0x5db840`; resource rows at `0x5daf72` | Partially verified |
| Workers and Utility Trawlers | DAT class 58 variants; unit 13 action-101/action-106 tasks and naval train locations | Partially verified and covered |
| Combat target eligibility | Attack-task validation at `0x5b9930`; class filter at `0x41c530`; DAT effect 129 | Verified and covered |
| Building attack approach | DAT rectangular collision footprints; attack action and path goal state | Covered; exact original slot ordering remains open |
| Attack Ground | Command-panel construction at `0x503aca`; opcode `0x6b`; DAT blast/projectile fields | Verified and covered |
| Firing presentation | DAT attack graphic, frame delay, projectile totals, spawning area, secondary projectile, and impact data | Partially verified and covered |
| AI scripts | Original Computer Expanded `.per` files and DAT `name2` aliases | Economy, advancement, balanced force replenishment, scouting, formations, escorted transport invasions, retreat, and worker shelter covered |
| Civilization technology trees | Each DAT civilization's `techTreeId` effect; type-102 disabled-technology commands | Verified and covered |
| Air/naval transports | DAT `AVAIL-*` technologies and Airbase/Shipyard train locations | Verified and covered |
| Compact island map | Shipyard terrain `1/4`, side terrain `2/35`, and DAT movement restrictions | Covered |
| Skirmish setup and random-map launch | Preset fields at `0x443440`; seeded generation at `0x4940f0`; AI defines at `0x57eff0` | Verified and covered |
| Match-to-menu transition | Cleanup and `"Main Menu"` transition at `0x45f916` | Verified and covered |
| Minimap and fog | `diam_map` draw dispatcher at `0x4560a0`; DAT `minimapMode`/`minimapColor` | Verified and covered |
| Control groups | `groupnum.shp` string at `0x689920`, referenced at `0x42745d` | Verified and covered |
| Pause/options/audio | Pause request path at `0x4359a9`; sound/music controls at `0x42701f`/`0x427247` | Verified and covered |
| Save/load | `"Save Game Screen"` constructor at `0x5286f0` | Native generated-skirmish format covered |

## Minimap, controls, pause, audio, and saves

Research used the same GOG Clone Campaigns executable and DAT hashes recorded
below. Addresses are image virtual addresses for the preferred `0x400000`
base. The evidence was collected from executable string cross-references and
focused disassembly; behavior not established by those sources is identified
as an implementation contract rather than attributed to the original.

The original diamond-map dispatcher begins at `0x4560a0`. It validates the map,
surface, and view members before drawing, derives a player mask at
`0x456113`-`0x456122`, and branches through separate subordinate paths according
to map state. This is the executable evidence behind the already-researched
`draw_explored_tiles`/`draw_all_tiles` behavior. The Clone Campaigns DAT adds
`Unit::minimapMode` and `Unit::minimapColor`; representative records verify that
Worker 83 and Command Center 109 use mode 1, while tree 348 uses mode 0. Player
color records separately provide `playerColorBase` and `minimapColor`.

Native minimap contract:

- Terrain is projected into a diamond rather than drawn as an unprojected
  debug grid. Terrain palette color and elevation distinguish land, water, and
  raised ground.
- Unexplored pixels are black. Explored pixels remain but are dimmed; currently
  visible pixels use their normal minimap brightness.
- A DAT mode of zero suppresses an object. Mobile objects require current
  visibility. Discovered buildings and static gatherables may remain according
  to the same remembered-object fog rule used in the world renderer.
- Player objects use player colors. Gaia objects use the DAT minimap color.
  The camera viewport and short-lived hostile-damage pings are independent
  overlays.
- The CPU image is bounded at 128 by 128 pixels and rebuilt at 4 Hz. Rendering
  uses one dynamic texture plus a bounded number of viewport/ping lines; it
  never emits one draw call per map tile.

The executable contains the interface asset name `groupnum.shp` at `0x689920`;
its only direct pointer reference is `0x42745d`. Original play observation
establishes assign, recall, and repeated-recall camera centering. The native
implementation retains existing leader-first selection order, stores object
IDs rather than pointers, removes dead/non-local objects before every recall,
and plays the ordinary first-unit selection acknowledgement. Vita exposes four
immediate groups through Select plus the d-pad; holding Square during that chord
assigns instead of recalling. Groups 5-10 remain represented in the save
format and engine API for future keyboard input.

The pause request diagnostic string at `0x68ac84` is referenced by
`0x4359a9`. That path sets the game state at `+0x1e3c` to 4 at `0x4359c3` and
sends message `0x17a2`, confirming pause is simulation state rather than a
visual overlay. The native frontend stops calling `Game::update` while paused,
but continues polling controls and rendering the menu. Resume, save, load,
same-seed restart, options, and return-to-menu are available. Load, restart,
and abandon actions require confirmation; unsupported campaign saving and I/O
failures remain on the pause screen with an explicit error.

The Options transition pushes the `"Options"` screen name at `0x45b050`.
`"Sound Volume"` at `0x6898f8` is referenced by the control creation path at
`0x42701f` and teardown path at `0x427b7d`. `"Music Volume"` at `0x689910` is
referenced by corresponding paths at `0x427247` and `0x427d7b`. The original
also exposes `"Scroll Speed"` at `0x6896d8`, referenced at `0x424d93` and
`0x4253ed`. The reimplementation exposes master, music, dialogue, and effects
from 0-100 in five-point increments. Gains apply live in the audio thread and
persist in a checksummed, versioned settings file. Two constrained,
conflict-free presets are offered: Standard and Left-Handed (camera/cursor
sticks and X/O roles swapped). Corrupt and unsupported settings are rejected,
reported, and recovered to documented defaults.

The original hostile-warning resource name `atakwarn.wav` is stored at
`0x69a1d8` and referenced at `0x5e60b7`; the native attack notification uses
the matching DRS resource 50315 and now also creates a coalesced minimap ping.

The single-player save-screen constructor starts at `0x5286f0`, pushes the
`"Save Game Screen"` identifier at `0x528711`, initializes its owned fields,
and records its mode arguments at `0x528760`-`0x528789`. The new generated
skirmish save format is intentionally native rather than an SCX writer. It has
an eight-byte magic, version, bounded payload size, and payload checksum, and
is replaced through a temporary file with rollback backup. Reads are capped at
32 MiB before allocation.

Save contract:

- The file stores setup/seed, RNG state, simulation time, map/elevation, player
  resources/research/diplomacy/active state, fog, objects and stable IDs, HP and
  shields, production/construction, paths and orders, garrisons, projectiles,
  remains, AI goals/timers/escrow/groups/rule cursor, conquest state, camera,
  selection, formations, and control groups.
- The original AI personality is loaded from disk before mutable AI state is
  applied. A different personality/rule count is rejected rather than partly
  loaded.
- Loading parses and validates into temporary state first; the running match is
  replaced only after the complete payload validates.
- Cursor modes, open action/cheat menus, queued spoken instructions, ambient
  cadence, attack-ping cooldowns, and pathfinder caches are deliberately
  normalized. These presentation/transient caches are reconstructed after
  load; active simulation orders, projectiles, and AI timers are not discarded.
- Campaign scenarios are not presented as saveable until trigger and campaign
  archive continuity have a separately verified contract.

## Skirmish setup, random maps, and match lifecycle

Research for the native setup flow used the GOG Clone Campaigns executable
`battlegrounds_x1.exe` (2,813,952 bytes, SHA-256
`30fac6f443391e1e3a8f29887f85f4c5061ca9e12397a4b50db1ffda0d633761`)
and `genie_x1.dat` (SHA-256
`9d2917c6c67e9df7af656459486f8e102406ad4a7299e7d0a85daac75c9c4b7d`).
Addresses below are image virtual addresses for the executable's preferred
`0x400000` base.

The preset serializer at `0x443440` reads separate fields from the setup
object and emits `m_bDifficulty`, `m_bResources`, and `m_dPopulation` at
`0x443487`, `0x4434bf`, and `0x4434f2`. Their lock flags are adjacent but
independent. This is evidence that these are match configuration, not UI-only
state. The native frontend therefore keeps civilization, opponent
civilization, difficulty, personality, diplomacy/team, map, resources,
population, victory, and seed in one immutable `SkirmishSettings` value and
applies the value when the simulation is rebuilt.

The random-map entry at `0x4940f0` reads the configured seed at global setup
offset `+0x48`. A value of `-1` obtains a random value at `0x632bdd`; the
chosen value is stored at `+0x50`, logged through `"Random Map Seed = %d"` at
`0x494137`, and passed to the random source at `0x632bd3` before generation.
The post-generation random value is logged at `0x49420a`. The sibling entry at
`0x494250` repeats the same seed contract. The reimplementation consequently
accepts an explicit 32-bit seed, produces byte-identical terrain/elevation for
the same settings, and uses a changed seed or map style to produce a different
map.

The setup-to-AI bridge at `0x57eff0` translates match fields into script
defines. Its victory switch begins at `0x57f069`; conquest pushes
`VICTORY-CONQUEST` at `0x57f087`. The difficulty switch begins at `0x57f0ed`
and pushes `DIFFICULTY-EASIEST` through `DIFFICULTY-HARDEST` at
`0x57f0fe`-`0x57f11a`. Population-cap defines follow at `0x57f12a`. The
original data installation supplies `Computer Expanded.per` and
`Computer Classic.per`; both remain selectable and are loaded, rather than
being approximated by a native personality. Classic recursively loads its
`Computer Classic` subdirectory, so Vita deployment preserves the original AI
directory hierarchy.

The in-match transition at `0x45f8e6` releases match-facing state, calls the
world cleanup paths through `0x5e5720` and `0x478770`, and then passes the
literal `"Main Menu"` to the screen transition at `0x45f924`. A nearby branch
at `0x45f987` performs the same menu transition. The native lifecycle follows
that ownership boundary: returning to the menu or restarting clears objects,
AI programs/groups, path caches, fog, triggers, instructions, projectiles,
remains, queues, selection/cursor modes, outcome state, and queued/active
session audio before creating the next match.

Generated-map contract:

- `Grasslands` is connected land with deterministic DAT terrain variation.
- `Archipelago` creates separated land masses, shallow/deep ocean, and shore
  rings. Both starts have a DAT-valid Shipyard site: the complete footprint is
  water terrain `1/4` with adjacent shore terrain `2/35`.
- `Compact Two Islands` retains the existing full-height channel, deep center,
  and shore columns as a selectable regression map.
- Both players receive equal food, carbon, ore, and nova patches. Host
  validation checks that ordinary workers have a route to every resource
  class and that both island starts have a valid Shipyard footprint.
- Command Center victory uses the selected condition directly; Conquest keeps
  the existing last-hostile-assets contract. Unimplemented original victory
  modes are not presented as working choices.

## Compact island match

The Vita development match uses a deterministic 96-by-96 map with the Empire
and Rebel Alliance on separate land masses. A full-height ocean channel prevents
ground units from walking around either map edge, while aircraft retain their
DAT flying movement and cross normally. Each coast has shore terrain beside
shallow water wide enough for the Shipyard's complete clearance footprint; deep
water fills the channel center. Both economies retain guaranteed nearby resources arranged like a random-map
start rather than equal test grids. Each island receives a 24-tree carbon
forest, 12 food nodes, and separate 8-node ore and nova deposits on cleared,
accessible land. The AI's starting processing centers are placed beside their
matching food and carbon patches.

Airbases and Shipyards expose transports through the original data rather than
hardcoded menu entries. At their normal prerequisite levels the Empire receives
Air Transport 1036 and Transport Ship 838, while the Rebel Alliance receives
Air Transport 1046 and Transport Ship 841. Deterministic coverage queues and
completes all four through normal production-exit handling.

## Custom OIIA Cat

The optional `OIIA OIIA` cheat-menu entry is intentionally outside the
original DAT contract. Its 94-frame cat animation comes from the
[OIIA Scratch project](https://scratch.mit.edu/projects/1363024859) by
`bust0588`, with redistribution permission confirmed by the contributor. The
frames are downsampled into one compressed runtime atlas. The Scratch project's
credited commercial music is not bundled; attacks use a newly synthesized
oiia-style vocal/chiptune cue generated in the Vita audio mixer.

## Civilization technology restrictions

Every civilization identifies a technology-tree effect through its DAT
`techTreeId`. Type-102 commands in that effect disable research for the player
before automatic technologies and menus are evaluated. This is why the Empire
does not receive technology 73, Shield Modifications; this is a source-data
restriction, not a UI exception.

Player-attribute technology commands are evaluated by `playerAttribute`.
Shield Modifications sets attribute 38, and the original eligible aircraft
classes use that value for self-shielding. Fighter and bomber coverage verifies
that the effect applies consistently for a civilization that can research it.

## AI scripts

The engine loads the original `.per` files from the user's installation rather
than embedding or translating a personality. The parser handles comments,
quoted strings, nested expressions, recursive `load` forms, conditional loads,
constants, and rules. Loading `Computer Expanded.per` currently resolves 40
source files into 311 constants and 1,423 rules.

Runtime condition evaluation is three-valued: true, false, or unsupported.
Unsupported facts remain unknown, including through `not`, so an unimplemented
negative condition cannot accidentally enable a rule. Rules run in bounded
slices instead of scanning the complete personality every frame.

The runtime supports goals, strategic numbers, Tech Level and age time,
difficulty, player number, named timers, population and housing, resource
amounts, unit/building counts, player age and military comparisons,
technology completion/availability, affordability with escrow, town attack
state, and Boolean/numeric comparisons. Actions set goals and strategic
numbers, control timers and escrow, disable rules, build, train, research, and
execute `attack-now` through the same simulation systems used by the player.
Gather percentages retask real idle workers to compatible original resource
nodes. Original script symbols resolve against DAT names, secondary `name2`
aliases, abstract `*-LINE` names, and the player's enabled
civilization-specific unit entry.

Engine contract:

- AI construction uses normal placement validation, creates a real foundation,
  charges normal DAT costs, and selects a worker/site pair with a valid route.
  At most three construction plans remain active, and one pending foundation
  of a particular building type suppresses duplicates.
- Training and research use normal building queues and population/resource
  checks.
- Gather allocations preserve builders, repairers, and garrisoned workers and
  are rebalanced at a bounded cadence. Percentage rounding uses largest
  remainders, low resource banks receive a real minimum 25% worker share, and
  tree workers spread across separate trees.
- Escrow percentages divert deposited income into per-resource reserves;
  `release-escrow` returns that reserve to the spendable bank. SEA strategies
  also retain enough resources for their first Shipyard and primary combat
  ship so lower-priority plans cannot consume the plan between rule slices.
- Idle military units scout unexplored, passable tiles physically. They reveal
  fog through ordinary line of sight and never receive hidden enemy
  locations. `attack-now` selects only currently visible hostile players,
  separates land, naval, and air forces, and gives each persistent group an
  original formation march before its members enter individual combat.
  Equivalent later `attack-now` pulses do not replace active movement,
  boarding, or attack orders.
- A land group with no route to a discovered enemy building reserves available
  DAT-compatible Air Transports or Transport Ships. Transports move to a
  reachable embarkation shore, passengers reserve capacity and board through
  their action-3 tasks, and the group crosses only through terrain valid for
  the transport. Landing selection requires both a reachable transport route
  and passable ejection positions from which the passengers can route to
  weapon range. After unloading, the passengers reform and continue the same
  attack. Multiple transport groups may move in formation.
- A bounded strategic pass derives land, naval, and air force targets from
  Tech Level and population capacity. It ensures Troop Center, Shipyard, and
  Airbase infrastructure where terrain and prerequisites permit, replenishes
  the domain with the largest proportional deficit, and distributes training
  across the available DAT unit types instead of repeating one unit. Available
  Air Transports and Transport Ships are maintained as land armies grow.
- Target selection ranks Command Centers, military production, armed defenses,
  workers, combat units, and ordinary buildings in that order, then compares
  compatible force count and travel distance. Naval and air units join
  transport groups as escorts, assemble at embarkation, cover the crossing,
  and continue toward the landing target.
- Scattered groups receive another formation order. A group that loses its
  target regroups at its base; a group that has suffered losses and is
  outmatched by at least 1.8-to-1 also retreats. An immediately overwhelming
  force triggers retreat at 4-to-1. Survivors become available to the next
  muster, attacks pause for a 20-second rebuilding window, and normal
  replenishment continues during that pause.
- When an owned Command Center is threatened by an active hostile attack
  within 14 tiles, the defense manager sends nearby class-58 workers into it
  through normal garrison orders and capacity reservations. Economy balancing
  leaves both incoming and sheltered workers alone. Five seconds after the
  nearby attacks stop, only workers sheltered by this manager are ejected;
  the ordinary economy pass then returns them to work.
- The compact opponent starts in the personality's original opening state
  without a prebuilt Troop Center. Five nodes of each resource are guaranteed
  beside its base, so its opening 100%-carbon rule can assign every worker
  without depending on random map generation. Both compact-map players begin
  with 3,000 food, carbon, ore, and nova; scenario resources remain unchanged.
- Unsupported facts block a rule and unsupported actions are reported once;
  neither is silently treated as success.
- Vita loads player 2's `Computer Expanded.per` from
  `ux0:data/swgb/AI`; deployment copies the original `.per` directory only
  when the user explicitly selects `-AiData`.
- Scenario personality references, strategic build-forward placement, and the
  remaining facts/actions are still open.

## Conquest, defeat, and surrender

Generated skirmishes use conquest state independently of campaign trigger
victory. An active player is defeated after losing every live unit and
building; garrisoned units still count, while annex graphics and carcasses do
not. Trigger condition 13 (`Player Defeated`) reads the same player state.
When no hostile active player remains, the local player wins. Losing the
local player's final asset produces defeat.

An AI with no Command Center, fewer than three workers, no military production
building, fewer than four military units, and at least a four-to-one enemy
strength disadvantage begins a 15-second surrender countdown. Recovery resets
the countdown. Surrender removes that player from conquest, clears its AI
orders, and names the surrendering player in the status message.

Victory and defeat stop at a full-screen outcome panel. The original
`Sound/Stream/WON1.MP3` and `lost.mp3` cues play once through the existing
dialogue stream path. Campaign scenarios remain trigger-controlled and do not
enable automatic conquest.

## Fog of war and exploration

The original executable's `diam_map` renderer has separate
`draw_explored_tiles` and `draw_all_tiles` paths (`0x4560a0`). The DAT supplies
each unit and building's `lineOfSight`, and technology attribute 1 modifies
that value through the normal researched player-unit tables.

Engine contract:

- Every player has a transient currently-visible tile grid and a persistent
  explored tile grid. Active, ungarrisoned units and buildings reveal a
  circular area using their researched DAT line of sight; allied players share
  those revealers.
- Visibility is simulation state, not merely a dark screen overlay. Enemy
  objects cannot be selected or automatically acquired outside current sight.
  Enemy projectiles and remains are likewise hidden. Static gatherable nodes
  and buildings that have been discovered remain drawn after current sight is
  lost, while mobile units and moving Gaia animals require live sight.
- The AI consults its own explored map for resources and resource-oriented
  drop-site placement. Ground workers also verify that a candidate path
  reaches the node's interaction boundary; a closest-point route ending at an
  ocean shore is not accepted as reachability.
- AI scouts choose unexplored destinations from their own persistent fog map.
  Attack targets must be currently visible, and non-air attackers must be able
  to route to their weapon range.
- Unexplored terrain is fully shrouded, explored but non-visible terrain is
  dimmed, and visible terrain is unmodified. A continuous screen-space mask
  samples and interpolates the surrounding tile states after reversing the
  isometric projection. This preserves tile-accurate simulation while
  avoiding stair-stepped boundaries and prevents elevated terrain or blend
  sprites from leaking through seams between fog diamonds.
- `FORCEEXPLORE` reveals terrain, static resources, and buildings to the human
  player without exposing mobile enemy units. `FORCESIGHT` reveals terrain and
  grants live object vision to that player. Neither cheat changes any other
  player's explored or currently-visible grid.

## Production exits

The completion activity at `0x56e390` asks `0x558810` for an exit before it
creates a unit. If no position is returned, the activity reports failure and
does not remove the completed queue item. A later update retries it.

`0x558810` derives its search from the producer's rectangular X/Y collision
half-size and the produced unit's X/Y collision half-size. Candidate spacing is
the produced unit's full collision diameter plus `0.1` tile. It searches the
immediate perimeter; a gather point changes the preferred side and position.
Every candidate is checked through the produced unit's placement/collision
method. The search is bounded rather than expanding outward until some distant
position succeeds.

Engine contract:

- Produced units occupy distinct valid positions around the building.
- Existing units, static obstructions, terrain, map edges, and same-update
  production reservations can make a position unavailable.
- A completed item stays at the front of the queue with zero time remaining
  when no immediate position is available.
- Freeing one position allows exactly the waiting item to complete on a later
  update.
- Rally orders are issued only after the unit is successfully created.

## Building placement

The preview routine at `0x5fc7b0` and execution routine at `0x618e70` both
invoke the unit master's coordinate adjustment before its placement validator.
The adjustment applies to every building, not only walls and gates: a footprint
with a fractional half-size is centred on a tile centre, while an integral
half-size is centred on a tile corner.

Placement uses the rectangular `clearanceSize`, which can differ from
`collisionSize`. The diagonal gate variants 665 and 673 are the decisive case:
their collision is `1 x 1`, but their placement clearance is `2 x 2`.
Every tile covered by the clearance rectangle is checked against the unit's
terrain-restriction row. `placementTerrain` lists alternative required
footprint terrains, while `placementSideTerrain` requires at least one
neighbouring terrain of either listed type. Shipyards use water `1/4` under
their entire footprint and shore `2/35` beside it.

DAT `hillMode` supplies the elevation rule. Mode 0 is unrestricted, modes 1
and 2 require a level footprint, and mode 3 allows at most one elevation level
between the lowest and highest footprint corner. Ordinary foundations paint
their DAT `foundationTerrainId`; terrain 27 changes to snow foundation 36 over
snow. Farms retain their separate staged terrain, and shore buildings do not
replace their water.

The original preview draws the complete standing silhouette and applies a
valid/invalid color to it. Invalid placement sets the red tint state; valid
placement uses a player-color-derived tint. It does not substitute a
construction sprite or communicate validity solely through a synthetic
outline and cross.

Engine contract:

- Placement preview and execution share identical snapping and validation.
- Map edges and obstructions use rectangular clearance, including gates whose
  clearance is larger than their collision box.
- Every footprint tile must be buildable and satisfy explicit placement
  terrain; side-terrain and elevation rules are checked across the complete
  footprint.
- Mobile units and static obstructions make any overlapping placement invalid.
- Walls show each dragged segment with its own valid or invalid silhouette.
- Farms have no usable standing SLP because their completed presentation is
  terrain. Their preview therefore draws the complete DAT `1.5 x 1.5`
  clearance diamond in the same valid/invalid color instead of disappearing.
- A committed ordinary building paints its foundation terrain immediately.

## Command cursors

`mcursors.shp` resource 51000 contains distinct semantic frames. Named
captures from the original establish frame 0 as the normal pointer used for
ordinary movement; frame 3 as contextual worker build/repair/resource work;
frame 4 as Attack Ground; frame 7 as explicit Build/Repair; frame 8 as Attack;
frame 9 as Guard; frame 11 as Follow; frame 13 as Garrison; and frame 18 as
Patrol and gather-point placement. Frame 2 is a one-pixel invisible frame and
must not be used as an order marker. Every frame is positioned from its SLP
hotspot rather than being manually centered.

The repair and destroy command-panel icons remain frames 28 and 59 of command
sheet 50721, as constructed by the executable with help strings 4927 and
4941. Worker build portraits use the civilization's building sheet
(`53241`-`53248`) and the DAT `iconId` directly; the frame index is not
one-based.
Attack stances are offered only to eligible combat units; workers and
buildings do not show the stance button or stance status.

The executable builds the ordinary mobile-unit commands together at
`0x503aca`: Stop uses command-sheet frame 3, panel action 5, and help string
4905; Patrol uses frame 6, action `0x26`, and help 4938; Guard uses frame 7,
action `0x24`, and help 4936; Follow uses frame 8, action `0x25`, and help
4937. Attack Ground remains frame 60/action `0x17`. The Vita presentation
keeps those original icons and localized strings in one command grid so the
five commands remain accessible without shrinking their touch targets.

Patrol alternates between the unit's order-time position and the selected
point, temporarily engaging enemies permitted by its stance before resuming.
Guard follows a friendly object, acquires threats around that object, and
returns to it after combat. Follow maintains spacing from a friendly object
without adding Guard's protected-object acquisition. Stop clears movement,
combat, work, garrison approach, patrol, guard, follow, and pending fire.
Explicit replacement orders clear patrol/guard/follow; automatic stance
attacks do not.

Idle units have no sample or ambient wander order. They move only for a
player, AI, scenario, work, combat, formation, or short friendly
collision-resolution order. Generated sandbox units are placed in distinct
starting slots instead of depending on later wandering to separate them.
While a formation is marching, every member that is following its moving slot
retains the Walk state even when it consumes that frame's short slot path; the
standing graphic is not substituted between frames.

## Combat targeting and building approach

Attack-task validation at `0x5b9930` delegates action 7 to the class filter at
`0x41c530`. The original does not decide air eligibility from `flyMode` or
damage classes alone. Target classes 43, 48, 59, 62, 63, and 64 are aircraft.
Dedicated anti-air source classes 9, 16, 33, 40, and 55 can attack only those
classes. Source classes 48, 57, 62, 63, and 64 can attack either layer; most
other source classes reject aircraft. Source class 4, target unit 696, and
target classes 21 and 28 are explicitly rejected.

Units 500 and 603 are executable exceptions when player attribute 31 is
positive. DAT technology 164, Walker Research, applies effect 129 command
`type 1, resource 31, +1`, establishing the unlock rather than requiring a
synthetic unit flag. Heavy Assault Mech 603 then uses air projectile 992
(`PROJ-MH3TE-AIR`) instead of its ground projectile 198. Other projectile
changes continue to use researched unit attribute 16. Researched range,
line-of-sight, minimum range, and reload use attributes 12, 1, 20, and 10.

Manual orders, automatic acquisition, retaliation, rally-point dispatch, and
active-order validation all use the same eligibility predicate. An ineligible
member of a mixed selection receives no attack order or attack acknowledgement;
eligible members still do. Attack animation and projectile graphics and sounds
remain DAT-driven, including the alternate Heavy Assault Mech projectile.

Passive `ANIMAL-*` classes 1, 2, 4, and 60 are excluded from proactive
military and building acquisition. An explicit military attack remains valid
and temporarily prevents proximity capture from replacing that order.
Hostile Gaia predators are class 5: they acquire player units, and attacked
player units retaliate against them.

Attackers assigned to a building retain distinct explicit approach points on
its rectangular collision perimeter. A point is reserved against friendly
units already attacking that building, checked for terrain and obstruction,
and replaced when progress stalls. Pathfinding must not collapse the point
back to the building's generic footprint goal, because that makes every unit
converge on the same nearest edge or corner.

Engine contract:

- Ground-only units cannot manually or automatically attack aircraft.
- Dedicated anti-air units cannot attack ground targets.
- Walker Research enables Assault Mechs to target aircraft; Heavy Assault
  Mechs use projectile 992 for air and their ordinary projectile for ground.
- Research-adjusted range, sight, minimum range, reload, and projectile values
  are used by simulation as well as the information panel.
- Crowds attacking large or rectangular buildings occupy distinct passable
  perimeter points and retry blocked points without stacking.
- Passive livestock and harvest animals are manual targets only; hostile
  class-5 Gaia predators participate in automatic attack and retaliation.

## Attack Ground and firing presentation

The executable constructs Attack Ground at `0x503aca` as panel action `0x17`
with command-sheet frame 60, name string 4123, help string 4923, and network
command `0x6b`. Its packet contains the selected object IDs and a world-space
X/Y point. Original named cursor capture identifies `mcursors.shp` frame 4
rather than the ordinary
attack cursor.

Eligibility is data-driven: a mobile combat unit must have a valid projectile
and a non-zero DAT `blastWidth`. This includes bombers, Grenade Troopers,
Assault Mechs, artillery, Air Cruisers, their hero/scenario variants, Ewok
Glider 1273, Ewok Catapult 1275, and cheat units Blockade Runner 1580, Star
Destroyer 1586, and Death Star 1587. Killer Ewok 1204, Bongo Marauder 1314,
Decimator 545, ordinary direct-fire units, and buildings do not qualify.

An Attack Ground order retains the point instead of synthesizing an object
target. The unit enters maximum range, backs out to its researched minimum
range when necessary, faces the point, and repeats its attack until another
order replaces it. Targetless projectiles retain their DAT flight, impact
graphic, impact sound, blast width, and blast level. Selection changes,
explicit cancellation, and replacement commands leave targeting mode
cleanly.

Projectile release begins the DAT attack graphic and waits
`frameDelay * attackGraphic.frameDuration`. The attack graphic completes once
instead of looping for the whole reload interval. Mobile units with
`totalProjectiles > 1` launch the clamped DAT count, using
`secondaryProjectileUnit` for additional bolts where supplied. Heavy
Artillery therefore launches projectile 656 followed by 369; the existing
building/garrison volley path remains separately sequenced.

`projectileSpawningArea[0..1]` are bounded source offsets rotated by the
attacker's facing. The third value is independent target scatter and may be
zero. Bongo Marauder 1314 uses a 2-by-2 spawning area with zero scatter, so
the zero value must never be used as a divisor for the source offsets.

Engine contract:

- Every unit with original blast-projectile eligibility shows command icon 60
  and accepts a ground point with cursor frame 4.
- Ground-point orders do not require or retain a live object target.
- Range, minimum range, reload, projectile substitution, attack graphics,
  launch sounds, impact graphics, impact sounds, and blast behavior remain
  DAT-driven.
- Projectile release occurs at the DAT frame delay for ordinary attacks and
  Attack Ground, and multi-projectile mobile units launch their DAT count.

## Garrison rally targets and interface feedback

Action-3 tasks in the produced unit's own header determine compatible
garrison containers, including ships (class 17), buildings (18), Assault
Mechs (53), and air transports (59). Building `garrisonType` masks retain the
Genie categories: workers 1, infantry (including Grenade Troopers) 2, mounted
troopers 4, Force users 8, and livestock 16. Dedicated garrison targeting and
production rally points both use the same compatibility and capacity
reservation path. A rally target may therefore be a different compatible
building or a mobile transport; incoming units reserve capacity before they
arrive. The original boarding cursor is `mcursors.shp` frame 13.

A unit's own carrying capacity does not make it a transport for boarding
eligibility. Mech Factory units 469, 485, and 500 and Heavy Weapons Factory
units 631, 651, and 681 retain explicit action-3 tasks for both class-17 sea
transports and class-59 air transports, even when the boarding unit can carry
units itself. Transport nesting remains rejected because the transports do not
declare the reciprocal action-3 task.

Animal Nursery unit 319 accepts class-1 livestock through category 16. Each
occupant adds the building's DAT `workRate` to food per second. The selection
panel shows the occupants in the same clickable portrait strip as every other
garrison, allows one animal to be ejected by pressing its portrait, and shows
the current aggregate food-per-second rate. Ejecting an animal immediately
reduces that rate.

Completed non-transport buildings with garrison capacity enter a critical
damage lock at or below 20 percent health. Every occupant is ejected through a
normal passable exit and new garrison orders are rejected with
`BUILDING TOO DAMAGED TO GARRISON`. If an exit is temporarily blocked,
evacuation retries on later updates. Repairing the building above 20 percent
clears the lock. The threshold is deterministic and covered, but still needs
confirmation against the original executable.

The original interface sound table maps `button1.wav`, `button2.wav`, and
`cantdo.wav` to resources 50300, 50301, and 50303. Successful menu choices use
50300, cancel/back uses 50301, and rejected actions such as full queues,
resource shortages, and invalid/full garrison targets use 50303. Resource
shortages use strings 3001-3004 and production queue full uses string 3088.
Proximity capture of livestock by the local player plays `capsheep.wav`
resource 50355 once for that ownership transition.

## Population, attack alerts, and instruction colors

DAT resource-storage type 4 is population accounting. Negative storage
consumes population and positive storage supplies capacity. Completed
buildings supply capacity; foundations do not. The effective capacity is the
sum of positive storage clamped to the scenario population limit. Existing
units may remain above that value, but a completed production item waits at
100 percent until enough capacity exists. The original housing warning uses
string 3005 and `needhous.wav` resource 50354.

The executable interface table at `0x544ba0` binds `atakwarn.wav` to resource
50315. Hostile damage to a local object emits that warning and a visible
red `YOUR ARMIES ARE UNDER ATTACK BY <player name>` notice. The attacker uses
the scenario player name when present and otherwise the civilization name.
A ten-second cooldown prevents repeated damage ticks from restarting the alert.

The same interface table binds `archupg.wav` to resource 50325. Completing
queued Tech Level technologies 1, 2, or 3 plays that cue. Local ordinary
research still reports `<technology> COMPLETE`; ordinary research by other
players is silent. A remote Tech Level completion is the sole remote research
announcement and reads `<player name> ADVANCED TO <Tech Level>`. Those effects
directly replace base Command Center 109 at each level
(`109 -> 71`, `109 -> 141`, and `109 -> 142`), so replacement caching aliases
the previously displayed level to the newest result instead of preserving the
first replacement.

Enemy selections retain identity, ownership, health, and shields, but hide
unit/research queue names and progress. Construction progress remains visible.
The custom `MANY BOTHANS` cheat toggles `ENEMY PRODUCTION INTELLIGENCE` and
reveals the hidden queue strip without changing fog or object visibility.

Display Instruction effects retain their source player when supplied.
Campaign dialogue often leaves that field unset, so the speaker prefix before
the colon is matched against scenario player names and active DAT unit display
names. Title variants such as `Lord Vader` and `Darth Vader` match by surname
when unambiguous. The complete instruction is rendered with that player's
in-game color; unmatched narration retains the neutral instruction color.

## Workers

Workers are class 58 units for every player. Worker simulation is not tied to
the local player; local ownership controls command input and UI availability,
not gathering, construction, repair, or job variants.

The base worker changes to task-specific hidden variants from the DAT:

- Variant 2 builds through action 101.
- Variant 3 farms unit 50 through action 5.
- Variants 4/5/6/8/9/11 gather the resource classes listed by their action-5
  or action-110 tasks.
- Variant 7 hunts the live-animal classes listed by action 110.
- Variant 10 repairs through action 106.

Farm collision is `1.5 x 1.5` tiles and has no obstruction. The farmer variant
has zero work range against the farm unit, allowing its activity to occupy and
move between positions on the field instead of remaining parked outside the
farm footprint.

All base and task-specific worker masters expose a five-tile DAT search radius.
Continued-work searches use that value rather than a separate hard-coded
radius. Resource continuation retains the current worker variant: carbon
collectors may continue to any class supported by the carbon variant, hunters
to the live-animal classes supported by the hunter variant, and so on. Merely
sharing a resource type is insufficient (a food gatherer does not
automatically switch from forage to hunting or farming).

Engine contract:

- Every player's class-58 units execute worker jobs.
- An explicit construction assignment clears gathering, resource-work,
  drop-off, combat, garrison, and farm-movement state immediately while
  preserving carried resources.
- Construction, gathering, carrying, and repair graphics come from the
  corresponding DAT worker variant and task.
- A carbon worker plays the task's initial working interval against the
  standing tree before the tree becomes its non-obstructing felled carbon
  pile; carbon collection begins from that pile. The carbon task stores a
  one-second work value, working/carrying graphics 10441/10432, and its
  gathering/deposit sounds in the DAT.
- Class-52 infantry with Trade Federation or Confederacy unit-master
  civilization tags are mechanical droids and can be repaired. The master tag
  is retained after conversion, unlike the owning player's civilization.
- Farmers enter the walkable farm footprint and periodically change working
  position while continuing to gather.
- A partially loaded worker continues onto the nearest compatible resource in
  its five-tile search radius without depositing first. It deposits when full
  or when no compatible resource remains.
- A completed farm is worked immediately by its builder. A prepaid reseed
  returns an exhausted farm to construction, then the same worker rebuilds and
  resumes farming it.
- After another building completes, its builder may chain to the nearest
  friendly foundation inside the five-tile search radius. If none exists, a
  completed drop site may send the builder to the nearest compatible resource
  inside the same radius.
- Clone Campaigns assault mechs (class 53) and air transports (class 59) are
  also repairable.
- Utility Trawler 13 is class 14 rather than a land worker. Its own task header
  supplies action 101 construction and action 106 repair, so build/repair UI,
  placement assignment, work animation, and contextual commands are
  capability-driven. Its build menu uses naval structures whose
  `trainLocationId` is 13; after the Shipyard's automatic technology 27,
  Aqua Harvester 199 and Sensor Buoy 1576 are available. Utility Trawlers
  repair owned or allied naval classes 13 through 17.
