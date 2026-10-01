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
| Shields and power | Eligibility `0x54bc40`; regeneration `0x54ed30`; drain `0x54ee70`; damage `0x5517f0`/`0x444820` | Verified and covered; no mobile bleed-through |
| Garrison fire | Volley logic at `0x55be20`/`0x55c0c0` | Verified and covered |
| Walls | Command executor at `0x5ba900`; preview at `0x5fc180` | Verified and covered |
| Formations | Layout/update routines at `0x478e30`, `0x479760`, `0x47c280`, `0x47e780`, `0x480060` | Layout verified; automatic line/column switching remains open |
| Pathfinding | Long-range routines at `0x4982f0`, `0x498820`, `0x4989f0`, `0x499010` | Partially verified and covered |
| Unit information UI | Panel rows at `0x5d98a0`/`0x5db840`; resource rows at `0x5daf72` | Partially verified |
| Workers, fishing, and Utility Trawlers | DAT class-58 workers; unit 13 action-5/action-101/action-106 tasks; resource 17; naval train locations | DAT contract verified and covered |
| Combat target eligibility | Attack-task validation at `0x5b9930`; class filter at `0x41c530`; DAT effect 129 | Verified and covered |
| Building attack approach | DAT rectangular collision footprints; attack action and path goal state | Covered; exact original slot ordering remains open |
| Attack Ground | Command-panel construction at `0x503aca`; opcode `0x6b`; DAT blast/projectile fields | Verified and covered |
| Firing presentation | DAT attack graphic, frame delay, projectile totals, spawning area, secondary projectile, and impact data | Partially verified and covered |
| Accuracy and misses | DAT `accuracyPercent`, `accuracyDispersion`, projectile speed, and saved simulation RNG | Data contract covered; exact executable RNG/range/elevation formula remains open |
| Blast damage | Candidate loop `0x55dcf0`; defense gate `0x55de37`; footprint distance `0x55dead`; RNG `0x55df1b` | Verified and covered |
| Researched unit attributes | DAT effect commands 0/4/5 and attributes 0/1/2/5/8/9/10/11/12/13/14/15/16/20 | Implemented consumers covered; unsupported attributes inventoried below |
| AI scripts | Original Computer Expanded/Classic `.per` files, scenario-embedded personalities, executable `data\load\*.per` catalog, and DAT `name2` aliases | Generated and campaign-selected personalities, campaign goals/signals, strategic numbers, economy, build-forward, formations, transport invasions, retreat, and worker shelter covered |
| Civilization technology trees | Each DAT civilization's `techTreeId` effect; type-102 disabled-technology commands | Verified and covered |
| Air/naval transports | DAT `AVAIL-*` technologies and Airbase/Shipyard train locations | Verified and covered |
| Compact island map | Shipyard terrain `1/4`, side terrain `2/35`, and DAT movement restrictions | Covered |
| Skirmish setup and random-map launch | Preset fields at `0x443440`; seeded generation at `0x4940f0`; stock RMS table `0x6963c0`; AI defines at `0x57eff0` | Verified names/contracts; native 2-8-slot generators covered |
| Match-to-menu transition | Cleanup and `"Main Menu"` transition at `0x45f916` | Verified and covered |
| Minimap and fog | `diam_map` draw dispatcher at `0x4560a0`; DAT `minimapMode`/`minimapColor` | Verified and covered |
| Control groups | `groupnum.shp` string at `0x689920`, referenced at `0x42745d` | Verified and covered |
| Pause/options/audio | Pause request path at `0x4359a9`; sound/music controls at `0x42701f`/`0x427247` | Verified and covered |
| Save/load | `"Save Game Screen"` constructor at `0x5286f0` | Native save-v6 plus v1-v5 migrations covered |
| Jedi/Sith conversion | DAT action 104; strings 4125/4925 and 42027/43027; command constructor `0x50301d`; resources 27/35/77/87/178/179/193 | Implemented and covered; exact original probability formula unresolved |
| Holocrons | unit 285, graphic 5200/SLP 2252; action 132 pickup/action 136 Temple delivery; resource 191; `puprelic.wav` xref `0x5e66db` | Implemented and covered |
| Victory selection | setup field `+0x218`, switch `0x57f069`, strings 4327/4321/4329/4330/4331 | Standard/Conquest/Time/Score implemented; Custom remains scenario-defined |
| Stealth/detection | predicates `0x54bab0`/`0x54bbc0`; resources 23/56/58; detector trait bit 8 | Detector classes verified and covered; reveal persistence unresolved |
| Aircraft-specific targeting | action-7 validator `0x5b9930`, class filter `0x41c530`, aircraft classes 43/48/59/62/63/64 | Verified and covered; no fuel mechanic evidenced |
| Startup and frontend routing | executable strings at `0x68e41c`, `0x68b26f`, `0x68e658`, `0x68e558`, `0x68e440`; language IDs 9201-9284/11241-11252 | Verified and covered |
| Stock campaign catalog | six `XCAM*.CPX` archives, 43 SCX entries; localized IDs 35228-35445/36128-36438 | Verified and covered |
| Campaign trigger/runtime conformance | all six XCAM archives; 43 SCX entries; 1,770 triggers, 1,752 conditions, and 5,854 effects | Every stock-used numeric condition/effect type supported; 79 bounded difficulty-relevant initialization/simulation runs covered |
| Campaign progression and saves | campaign-menu strings, ordered CPX entries, original save-screen path `0x5286f0` | Native bounded profile and save-v6 continuation covered |
| Scenario editor entry and storage | strings at `0x68e658`/`0x68e670`/`0x699e68`; `%s.scx`, `trigger_info.txt`, and `trigger_text.txt`; all 43 XCAM payloads | Native editor, lossless imported-source sidecars, validation, and playtest covered; modified SCX writing remains evidence-limited |

## Startup, frontend, and campaign contracts

Research for the native campaign frontend used the same GOG Clone Campaigns
1.1 executable identified below. Preferred-base virtual addresses are:

| Evidence | Address |
|---|---:|
| `Main Menu` | `0x68e41c` |
| `Single Player` | `0x68b26f`, `0x691b1c`, `0x699e54` |
| `Scenario Editor` | `0x68e658`, `0x68e670`, `0x699e68` |
| `Multiplayer` | `0x68c4dc`, `0x68c514`, `0x68e558` |
| `Options` | `0x68aa74` (plus the existing options control paths) |
| `Credits` | `0x68e440`, `0x690ce7`, `0x690d09` |
| `interfac.drs` / `language_x1.dll` | `0x699d9c` / `0x699d7c` |

The localized frontend contract uses IDs 9201 (`Star Wars Galactic
Battlegrounds`), 9202 (`Single Player`), 9203 (`Multiplayer`), 9206
(`Scenario Builder`), 9207 (`Exit`), 9209 (`About`), 9248 (`Credits`),
9272-9284 (save/load/options/restart/resign/main-menu actions), 11241
(`Main Menu`), and 11242 (`Campaigns`). The native frontend falls back to
short built-in labels only when a language DLL is absent or does not contain
the requested ID. Unit, technology, objective, and campaign text continue to
come from the user's installed language/scenario resources.

`INTERFAC.DRS` is 33,609,445 bytes (SHA-256
`c8430f912d1ece721fe5dbc2046c6ffc10adb6a903d3818f10d6137c7620af96`);
its palette resource is 50500 and `mcursors.shp` is SLP 51000. The expansion
overlay `interfac_x1.drs` is 11,663,774 bytes (SHA-256
`0021af7aae53e8324e5d3db624eb688631c29a6a074012b8aa1c5796672cc4e6`).
The original palette, fonts, cursor sheets, interface sounds, localized
strings, and existing selection/command art remain runtime-loaded rather than
copied into this repository.

The executable installation provides `xlogo1.avi` (3,344,600 bytes, 15 fps,
Indeo Video 5 `IV50`, SHA-256
`bca7731fe3f8e267cae3d0b8ce978b621b34f53050f73d3931a64c05d9093a10`)
and `xintro.avi` (22,634,416 bytes, 29.971 fps, `IV50`, SHA-256
`277119975483f919db0346f3275e0a8620ba74c96bb91c7881fc138c6544873e`).
The Vita startup sequence is timed and skippable, never blocks on those
optional files, and reports whether they are installed. It does not embed or
silently substitute proprietary frames; the native presentation remains
available when the legacy codec cannot be decoded.

### Stock XCAM catalog

The catalog scans the installed campaign directory case-insensitively, opens
each `XCAM*.CPX`, validates the CPX table and every SCX payload, and orders
archives numerically and missions by their archive entry. The audited GOG
files are:

| Archive | Missions | SHA-256 |
|---|---:|---|
| `XCAM1.CPX` | 7 | `5a602013fc201d3a833a12939d3edb26fa18eee97a33da959381b816f2e27d48` |
| `XCAM2.CPX` | 7 | `a461717c14bbd68f7ddfc85f07d76d0d12bfe6e206a28d2b6c4820f6a3e7cd1d` |
| `XCAM3.CPX` | 7 | `a12a436528d5e5514e4834567e21f9383d0a43b4ca34a130fee1917080d95f77` |
| `XCAM4.CPX` | 8 | `188e80efda79bb263715e712fe325694078df8c45a1a1a6152fa0329958131fc` |
| `Xcam5.cpx` | 7 | `ade2fb95839b8edf737957c19ea489a7f6665d3fb71a5e58427c1338544b196c` |
| `XCAM8.CPX` | 7 | `fd5e2b2c5c4731abaa854a7a4ccefec7c1a93c82025c7574b6a5c99b49c061aa` |

This is the evidenced 43-mission host-coverage contract. Campaign and mission
names come from IDs 35228-35445; long campaign descriptions come from
36228-36438. Mission briefings, human faction/civilization, map size,
objectives, and previewable structural metadata come from the selected SCX.
Normal profiles unlock entry 1 and then each following entry after completion.
Development access is an explicit profile option rather than fabricated
completion. Difficulty is profile-persisted for scenario conditions.

Campaign progress uses a 64 KiB maximum, versioned checksummed atomic profile.
Match save version 6 retains the bounded archive-name and entry metadata and
adds per-trigger enabled/fired/delay state, current and queued instruction
state, scripted names, freeze state, trigger attack overrides, and deterministic
AI random values. It preserves version 5's independent shield regeneration and
drain timer phases, then adds expanded skirmish slots/setup state. Loading first validates the checksum/version,
reopens the exact discovered mission, and then restores state only when the
initialized campaign context matches. The active instruction resumes without
replaying its already-started one-shot sound; queued dialogue resumes normally.
Version-1 through version-5 saves retain their documented migration paths.

### Stock campaign runtime and AI contract

The six stock archives contain exactly 43 missions, 1,770 triggers, 1,752
conditions, and 5,854 effects. The exact condition IDs used are
`1,3,4,5,6,8,9,10,11,12,13,14,15,17,18,19,20,21,22,23`; the exact effect IDs
used are
`1,2,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,32,33,34,35,36`.
IDs absent from these lists are not claimed merely because a neighboring
opcode exists. The PC conformance gate independently reparses all archives,
asserts support for every encountered ID, validates bounded player/object
references, initializes every mission at difficulty 2 plus all five
difficulties for missions that query difficulty, and runs 79 fixed-step
simulations. The audited corpus contains 18 authored references to objects
absent at initial load; the runtime resolves them deterministically as
deferred/removed references rather than dereferencing stale storage.

The condition contract covers possession/fewer-than counts, typed/grouped
area queries, destruction, accumulated resources/population, completed and
in-progress research, timers, selection, AI signals, defeat, current target,
visibility/nonvisibility, garrisoning, difficulty, foundations, powered
objects, and population-blocked queues. The effect contract covers diplomacy,
research, sound/tribute/gates, trigger control, AI goals, object creation,
tasking, victory/defeat, kill versus silent removal, camera, unload, ownership,
patrol, queued/cleared instructions, freeze, damage, foundations, rename,
signed 16-bit HP/attack deltas, stop/snap view, and technology/unit
enable/disable/flash.
Effect execution follows authored effect order; non-looping triggers fire once,
looping triggers re-arm, delay uses accumulated fixed-step time, and explicit
activation/deactivation updates the target trigger without invalidating the
current ordered pass.

### Scenario editor and SCX preservation contract

The final editor audit used the installed GOG Clone Campaigns
`battlegrounds_x1.exe` (2,813,952 bytes) and the original resources under
`Game\Data`, `Game\Campaign`, and the three installed language DLLs. Addresses
are preferred-base virtual addresses; file offsets are given where only string
placement, not a code path, is established.

| Evidence | Location |
|---|---:|
| `Scenario Editor` | `0x68e658`, `0x68e670`, `0x699e68` |
| `Scenario Editor Open` | file offset `0x28e658` |
| `Scenario Editor Screen` | file offset `0x28e670` |
| `Scenario Editor Menu` | file offset `0x299e68` |
| `Campaign Editor Screen` | file offset `0x290c44` |
| `ScenarioEditorInfo` | file offset `0x291080` |

The executable also names `Scenario Menu Dialog`, `Select Scenario Screen`,
`Campaign Selection Screen`, `Campaign Game Screen`, `Saving campaign`,
`%s.scx`, `trigger_info.txt`, and `trigger_text.txt`. Together with localized
ID 9206 (`Scenario Builder`), this establishes an editor route, scenario
selection/save naming, and trigger-support resources. It does **not** establish
the complete original control geometry, mouse/keyboard behavior, or a
byte-perfect SCX writer. The native editor therefore uses the repository's
shared Vita-bounded panel/tab/tooltip framework instead of inventing an
unverified pixel copy.

The installed campaign directory contains no loose stock `.scx` files. Its 43
SCX payloads are the 7/7/7/8/7/7 entries in `XCAM1`, `XCAM2`, `XCAM3`,
`XCAM4`, `XCAM5`, and `XCAM8`. Those payloads establish the editable known
model and trigger semantics recorded above: authored order, looping and
activation state, player/resources/diplomacy/technology restrictions, objects,
map/elevation, cameras, objectives/messages, embedded AI, and global victory.
Unknown condition/effect forms are marked read-only in the editor. Their
numeric fields, selected-object lists, and the complete source SCX remain in
the native document rather than being discarded.

Editable files use the native `.swscenario` format. It has an eight-byte
magic, format/document versions, 32 MiB file/payload cap, 16 MiB original-SCX
cap, bounded strings/collections/opaque records, FNV-1a payload checksum,
strict truncation/trailing-data/version rejection, and temporary-file atomic
replacement with rollback backup. Snapshots use the same validated encoding;
undo/redo is capped at 32 entries and 24 MiB in the Vita editor. File names are
sanitized for control characters, separators, trailing dots/spaces, Windows
device names, and length. Title-specific documents and autosaves are accompanied
by canonical `recent/last.swscenario` and `recovery/autosave.swscenario`
snapshots so restart does not require knowing the previous title. Loading also
accepts a valid rollback `.bak` after an interrupted replacement. Dirty
replacement and existing-file overwrite remain explicit user decisions.

SCX compatibility is deliberately asymmetric:

- Import parses the evidenced 1.21 SCX structure and embeds the complete
  original byte stream.
- Native saves preserve known edits, opaque native records, and that immutable
  source stream.
- Export writes the original SCX bytes only when a semantic fingerprint proves
  that no editable value changed. Generated or modified documents are refused
  with an explicit instruction to keep the native sidecar.
- No generated or modified file is labeled stock-compatible `.scx` without an
  evidenced writer. This supersedes no part of the gameplay-save contract;
  `.swscenario`, `.save`, and original `.scx` are distinct formats.

Playtest converts a validated editor snapshot into the normal scenario runtime.
It uses a separate playtest save, outcome state, and pause menu; restart rebuilds
from the snapshot, and return clears simulation/audio/AI/fog/transient state
before restoring the still-open editor document. Playtest outcomes never
advance campaign progress or replace the normal skirmish continuation.

SCX player payloads preserve embedded personality source/name/type, starting
age, disabled technologies/units/buildings, resources, population, diplomacy,
and allied-victory state. Global conquest/score/time settings and all-tech
state are applied before simulation. All active nonhuman stock players select
their scenario personality instead of being forced through Computer Expanded.
The native strategic manager supplies the original executable's built-in
`data\load` behavior while embedded rules provide mission-specific goals and
signals. Optional embedded `(load ...)` references that are not installed are
reported once with player/path context and never reported as successful rules.
The audited set is 66 references to 16 distinct names:
`GE4-tower`, `GE7-population`, `RA1-research`, `RA8-build`, `RA8-init`,
`RA8-train`, `TF7-Player4`, the six `*-no-upgrade`/`*-no-transport`
production variants, `homebase-no-farms`, `troop-center-no-air`, and
`troop-center-no-mounted`. They are absent from both the XCAM payloads and the
installed `Game\AI`; scenario unit/technology disables and the native
strategic manager remain authoritative, and each absence is surfaced rather
than fabricated as a parsed rule. Unknown facts remain three-valued `Unknown`;
unsupported actions return false and are logged once with source, line, and
arguments.

The bounded stock run reaches no unsupported AI form. Campaign-used
`event-detected trigger N` is driven by SCX AI-goal effects and can be
acknowledged; AI-to-scenario `set-signal` state is separate from goals;
player-resigned, taunt absence, market availability/trades,
campaign resource grants, forage distance, and unpowered-building pressure
have native facts/actions. AI event sets and deterministic generated random
values are part of save v4.

Executable evidence corroborates that the standard modules are native load
catalog entries rather than files shipped in `Game\AI`. In the audited PE,
`.data` maps raw `0x289000` to preferred VA `0x689000`; representative strings
are `data\load\randomgame.per` at file offset `0x29ab9c`/VA `0x69ab9c`,
`constants.per` at `0x29ae3c`/`0x69ae3c`, `building-count.per` at
`0x29aeb8`/`0x69aeb8`, and `attack.per` at `0x29aed8`/`0x69aed8`. The complete
executable string inventory also names age advancement, resources, diplomacy,
escrow, map/civilization loads, production buildings, population, research,
resign, strategic-number modules, and land/naval attack modules. These are
modeled by the native economy/strategy/military passes; disk-based Expanded
and Classic personalities remain selectable for generated skirmishes.

`build-forward` is distinct from ordinary base construction. It chooses only a
currently fog-visible hostile objective, advances a bounded 65 percent from
the AI base toward it, then uses ordinary terrain, reachability, shoreline,
power, builder, reservation, population, and resource-cost validation. If no
hostile objective is known, it safely falls back to ordinary placement rather
than leaking hidden positions.

## Conversion, Holocrons, victory, stealth, and aircraft

This milestone rechecked the GOG Clone Campaigns executable and DAT before
changing behavior:

- `battlegrounds_x1.exe`: 2,813,952-byte PE32/i386 image, preferred base
  `0x400000`, entry `0x636601`, `.text` at `0x401000`, SHA-256
  `30fac6f443391e1e3a8f29887f85f4c5061ca9e12397a4b50db1ffda0d633761`.
- `genie_x1.dat`: SHA-256
  `9d2917c6c67e9df7af656459486f8e102406ad4a7299e7d0a85daac75c9c4b7d`.
- Addresses below are preferred-base virtual addresses. DAT task fields and
  localized resource IDs are recorded directly rather than inferred from
  genre conventions.

### Jedi/Sith conversion

Sith Apprentice 180 and Sith Master 115 are representative class-50,
trait-32 Temple units. Both have action 104 with `workValue1=4` and
`workValue2=10`; the Master uses `targetDiplomacy=2`, while the Apprentice
uses 0. Localized string 4125 names Convert, 4925 supplies its help, and
42027 names the Force Power gauge. String 43027 states that it must be at
100 percent before another conversion and that a successful conversion
consumes the charge.

The executable's Convert command constructor at `0x50301d` pushes help
string 4925, panel action `0x1d`, and command-sheet frame `0x0e`. Frame 14
of original interface SLP 50721 is therefore the Convert icon; frame 5 is
explicitly marked unused in the original sheet. The selected Force user's
gauge is derived from action 104's recharge duration: ready is 100 percent,
a successful conversion resets it to 0 percent, and the existing Stamina
multiplier accelerates its return to 100 percent. Convert remains visible
but is dimmed and unavailable while this charge is incomplete.

The technology/resource contract is:

- Concentration 152 sets player resource 87 and permits most buildings and
  heavy units.
- Force Influence 156 sets resource 27 and permits other Jedi/Sith targets.
- Stamina 153 adds 3 to resource 35; its text promises 50% faster recharge.
- Faith in the Force 155 affects resources 77, 178, and 179; its text promises
  50% greater resistance.
- Meditation 500 sets resource 193 and describes group conversion exhausting
  only one converter.

The native implementation uses action-104 presence for converters, the DAT
task work range and graphics/sounds, deterministic timed work, and the DAT
recharge duration. Faith in the Force extends required work by 50%; Stamina's
`+3` produces 1.5x recharge speed. Force Influence and Concentration gate the
target categories described above. The command cancels on invalid visibility,
diplomacy, target state, or a replacement order. Conversion is a temporary
stealth break.

Ownership transfer is centralized for conversion and scenario Change
Ownership effects. It clears invalid combat/work/guard/follow/garrison orders,
selection and local control-group entries; removes old AI group/transport
assignments; updates annexes, secured Holocrons, color/diplomacy-derived
behavior, dynamic population/accounting, shield/power/technology caches,
occupancy, adjacency, and fog. Carried Holocrons drop. Occupants are ejected
from a converted building/transport where a valid position exists, otherwise
they transfer with the container rather than remaining a hostile hidden stale
reference. Production queues stay with the converted completed building;
foundations are explicitly ineligible.

Unresolved evidence is not filled with a guessed random formula. The exact
original success-probability/RNG function, complete immunity table, and
conversion cursor frame remain unproven. `unconv.txt` in the
local installation is Expanded Fronts data and is not treated as a complete
Clone Campaigns immunity table. Hero-mode targets are conservatively immune
until executable evidence establishes per-record behavior. Meditation's
multi-converter charge-sharing rule is documented but not claimed because the
native deterministic implementation does not use an original random roll.

### Holocron lifecycle

Holocron 285 is `OBJ-HOLOCRON`, class 28, language ID 5502, with 30 HP, LOS 7,
selection sound 542, standing graphic 5200, walking graphic 5201, and the
single-frame original SLP 2252 (`41x40`, hotspot `22,30`). Base Force-user
headers contain action 132 targeting unit 285; those tasks have no proceeding,
working, carrying, or gathering graphic. The carried-state task uses action
136 targeting Temple 104. Temple
104 is class 18 with 2,500 HP, LOS 5, zero ordinary garrison capacity, cost
180 carbon/25 nova, and build time 60. Holocron delivery therefore has its own
relationship and does not pretend to consume normal garrison capacity.

Pickup/deposit strings are 3722/3922 and 3732/3932. Strings 3820 and 26357
describe Temple immunity/Nova generation; 41106 says that dropping loses
control. Player resource 191 is the authoritative per-Holocron Nova rate:
45/minute normally and 60/minute for the Naboo and Republic civilization
records. `puprelic.wav` is named at `0x69a09c`, referenced at `0x5e66db`, and
is sound resource 50365.

Generated native maps place five Gaia Holocrons at deterministic, separated,
symmetrically paired normalized sites, searching outward for valid terrain.
The count of five is corroborated by the local EF random-map scripts; it is an
integration/count reference, not falsely presented as proof of every stock
Clone Campaigns map distribution rule. Grasslands and Archipelago tests both
require five valid instances.

A Holocron is not disclosed until ordinary sight discovers it. Discovery
persists in explored world/minimap presentation, but `FORCEEXPLORE` alone does
not mark undiscovered Holocrons and native AI never targets one without
current sight. Action-132 carriers pick up exactly one, retain their own
standing/walking animation, and draw the original Holocron graphic at the
carrier anchor with foreground sort bias so it floats visibly in front.
The old procedural gold cross and carrier-graphic substitution are not part
of the DAT contract. Pickup also plays the original sound before delivery to
a friendly completed Temple. Death,
conversion, invalid garrison/transport state, or loss of carrier eligibility
drops it. Temple destruction ejects all secured Holocrons; Temple ownership
changes transfer them. The authoritative carrier/Temple links and trickle
timing survive save/load and reset without duplicating objects.

### Victory conditions

The setup victory field is at `+0x218`; the switch at `0x57f069` identifies:

| Value | Original setup choice | Localized ID |
|---:|---|---:|
| 0 | Standard | 4327 |
| 1 | Conquest | 4321 |
| 2 | Time Limit | 4329 |
| 3 | Score | 4330 |
| 4 | Custom | 4331 |

This corrects the earlier native assumption that the setup offered only
Conquest plus a Command Center mode. Standard means the first military
conquest, all-Holocron control, or completed Monument countdown. Score also
permits military conquest. Monument Race and Defend Monument are separate
game types, not aliases for victory-dropdown values. Native Command Center
victory remains as an explicitly named compatibility/testing mode rather than
being attributed to setup value 4.

Monument 276 is class 18 with 3,000 HP, LOS 6, icon 9, cost 3,000 each of
carbon/ore/nova, build time 4,000, and technology 23. `WON1.MP3`, `lost.mp3`,
and `countdown.mp3` are the evidenced outcome/countdown streams.

Native Standard control countdowns reset immediately on Monument destruction,
ownership loss, Holocron theft/drop, or team-state change. Mutual allies with
allied-victory enabled combine secured Holocrons and share the result.
Time-limit and score outcomes use authoritative object/resource/research
scores; simultaneous ties resolve to the lowest player number. Objective UI
shows the live control countdown, remaining time, or score threshold. Outcome
continues through the existing original win/loss streams and post-match panel.
Victory runtime is save-versioned.

The exact stock Standard countdown duration and complete executable score
weighting formula remain unresolved. The native compatibility defaults are
therefore explicitly 600 seconds, 3,600 seconds, and 4,000 score; tests inject
short thresholds rather than claiming those defaults are executable-proven.
Custom remains scenario/trigger-defined and is not exposed as a falsely
working skirmish option.

### Stealth and detection

Perception technology 158 sets resource 58; its language text says Masters
detect stealth/submerged units. Mind Trick 159 sets resource 56; its text
grants Master stealth, and the Clone Campaigns overlay also grants Jedi
Starfighter stealth. Stealth drops while attacking or converting. Trait bit 8
identifies inherent detectors in the audited records: Sensor Buoy 1576 has the
bit and LOS 9, and localized descriptions identify Sentry Posts and Dark
Troopers as detectors. Interface strings 43209/43210 name the stealth and
detector status indicators.

Detection uses an active detector's researched DAT LOS, shares with allies,
and does not persist after leaving range. Undetected enemies are filtered
before world/minimap rendering, selection, contextual commands, automatic
acquisition/retaliation, conversion, projectiles/remains ownership queries,
alerts, and AI target searches. Garrisoned detectors do not reveal.
`FORCESIGHT` is the documented local-player bypass; `FORCEEXPLORE` is not.

Exact original reveal persistence and any detector radius independent of LOS
remain unresolved, so the native contract deliberately uses no persistence
and detector LOS rather than inventing a genre-standard value.

### Aircraft-specific audit

Attack validation at `0x5b9930` delegates action 7 to the class filter at
`0x41c530`. Target classes 43, 48, 59, 62, 63, and 64 are explicitly aircraft.
Representative records are Bomber 762 (class 43, fly mode 1, restriction 23),
Fighter 773 (class 48), Air Transport 1036 (class 59, action 12, zero ordinary
garrison capacity), and Jedi Starfighters 641/638 (class 64). Technology 73,
Shield Modifications, uses player resource 38 and is restricted to the
evidenced fighter/bomber classes.

The bounded audit confirmed that the existing shared systems already cover
flight terrain/ground-occupancy bypass, air/ground target compatibility,
DAT projectile/selection/render behavior, class-specific aircraft shields and
drain/regeneration, formations, transport boarding/ejection, mechanical
repair eligibility, patrol/guard/follow, death/remains, fog/detection,
minimap, AI production/use, and save/load. The milestone retains those paths
and adds stealth for the evidenced Jedi Starfighter class. No SWGB executable,
DAT task, language, sound, scenario, or RMS evidence for fuel was found, so no
fuel or docking assumption was added.

The closely related audit also connected original AI facts
`hold-holocrons` (`0x6934b0`, xref `0x5811a7`) and
`enemy-captured-holocrons` (`0x69399c`, xref `0x580abb`) to authoritative
carried/secured state. The Jedi Temple AI table is at `0x5ff648`; the Monument
table is at `0x5ff6ac`. Larger unrelated gaps remain the general campaign AI
fact/action surface, full trigger catalog, and exact score/probability
formulas; these were not expanded without evidence.

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
  selection, formations, control groups, conversion work/recharge, Holocron
  carrier/Temple relationships, and victory countdown state.
- Version 2 accepts version-1 generated-skirmish files through an explicit
  Conquest/Command Center enum migration and safe defaults for the new fields.
  Unknown versions and inconsistent stable-ID relationships fail with a clear
  error before replacing the running match.
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

The stock x1 executable contains a contiguous random-map filename table in
`.data` beginning at raw offset/RVA `0x2963c0` (preferred VA `0x6963c0`).
Verified entries, in table order, are:

`Hoth`, `Yavin`, `EndorMoon`, `Kashyyyk`, `TeamSpaceSatellites`, `Raiders`,
`IceLake`, `Motherlode`, `Arena`, `Savannah`, `Swamp`, `Flats`, `Tundra`,
`TeamLandSatellites`, `Rivers`, `SearchAndDestroy`, `PlanetsAndMoons`,
`LargeSea`, `SpaceSatellites`, `LandSatellites`, `Precipice`, `NovaAssault`,
`Fortress`, `NovaLake`, `SpaceMass`, `LandMass`, `Shoreline`, `Forest`, `Sea`,
`WaterMass`, `Desert`, and `BlindRandom` (each stored as an `.rms` filename).
Representative exact entries are `Arena.rms` at `0x296450`/`0x696450`,
`Fortress.rms` at `0x2964a8`/`0x6964a8`, `Forest.rms` at
`0x2964f8`/`0x6964f8`, and `BlindRandom.rms` at
`0x29652d`/`0x69652d`. The native generator uses the exact evidenced names
Savannah, Forest, Desert, Tundra, Swamp, Rivers, Shoreline, Sea, and Land Mass
for its corresponding biome/topology families. It does not claim to execute
the unavailable stock RMS scripts or reproduce unevidenced per-script tables.

Stock setup/debug strings in the same image verify `Game Speed` at
`0x289704`/`0x689704` and a field group near
`0x29a8b8`/`0x69a8b8` containing `Map Type:`, `Map Size:`, `Scenario:`, and
`Number Players:`. The x1 language resource verifies ID 30163's Blind Random
description, ID 30172's Death Match starting-stockpile description, ID 30211's
same-team/allied-victory tooltip, ID 30551's per-player population-limit
tooltip, ID 31031's standard-game/custom-scenario description, and ID 26339's
Standard Monument victory description. These establish the existence of those
setup concepts. They do **not** establish numeric map dimensions, player
maximum, resource values, age bounds, speed multipliers, or reveal defaults.
The exposed 2-8 slots, 64/96/128/160 presets, resource/population ranges, and
speed multipliers are therefore documented native Vita-safe limits rather than
misrepresented as recovered stock constants. Death Match, Regicide, Scenario,
and Blind Random are not advertised as implemented modes until their complete
setup and outcome contracts are recovered.

The installed `Game\Random` directory contains only `[EF]` scripts and an
Expanding Fronts README. Those scripts verify RMS grammar such as
`<PLAYER_SETUP>`, `random_placement`, `create_player_lands`, map-size symbols
through `GIGANTIC_MAP`, player-distance constraints, terrain restrictions,
Gaia-only objects, and player-relative food/carbon/nova/ore/fish placement.
They are useful format evidence but are explicitly excluded from stock x1 name
or rule claims.

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

- `Savannah`, `Forest`, `Desert`, `Tundra`, `Swamp`, `Rivers`, `Shoreline`,
  `Sea`, and `Land Mass` are distinct seeded families named from the recovered
  stock filename table. Forest/Swamp add bounded carbon density away from
  starts; Rivers/Swamp preserve inland water; Shoreline has a variable coast;
  Sea creates a separate island around every active start; the remaining
  families retain connected land with distinct terrain/elevation mixes.
- `Compact Two Islands` retains the existing full-height channel, deep center,
  and shore columns as a selectable regression map.
- Two through eight deterministic slot IDs map directly to simulation players.
  Every active slot has Closed/Human/Computer state, name, unique color,
  civilization, personality, difficulty, team, and allied-victory state.
  A match requires exactly one local human and a hostile computer. Same nonzero team
  produces mutual alliance; team zero remains free-for-all. Locked generated
  teams reject trigger diplomacy changes, while unlocked matches use the
  ordinary authoritative diplomacy paths.
- Every active player receives equal food, carbon, ore, and nova patches.
  Host validation checks that ordinary workers have a route to every resource
  class and that Sea/Shoreline/Compact starts have a DAT-valid Shipyard
  footprint: water terrain `1/4` with adjacent shore terrain `2/35`.
- Generation is bounded and fails explicitly if slot density, a starting
  economy, or required shoreline cannot be validated. The lobby preview uses
  the same deterministic terrain classifier and start-position contract as the
  full generator, and invalid settings produce an error rather than a malformed
  map.
- Exposed Vita presets are 64, 96, 128, and 160 tiles. The internal 48-tile
  size remains only for historical regression/save coverage. More than four
  players requires at least 96 tiles and more than six requires 128. The
  `test-maps-modes` gate records actual capacities for terrain, elevation,
  17-player explored/visible arrays, objects, projectiles/remains, and spatial
  grids; the 160-tile preset is accepted only under a 64 MiB native-map
  allocation gate. Larger original size symbols remain unsupported instead of
  risking the Vita heap.
- Starting resources/population/Tech Level bounds, ending Tech Level, reveal,
  locked teams, allied victory, cheats, game speed, Time target, and Score
  target are authoritative settings. Normal/Explored/All Visible feed the
  shared-vision tile queries; speed scales simulation time; cheats gate the
  generated-match menu; starting/ending Tech Level researches and disables the
  corresponding age technologies; every field is retained by save/restart.
- Command Center victory uses the selected compatibility condition directly;
  Conquest keeps the existing last-hostile-assets contract. Standard combines
  Conquest, Monument, and Holocron control; Time Limit and Score are separate
  working choices. Original Custom remains trigger/scenario-defined and is
  not presented as a working generated-skirmish alias.

Native save version 6 appends all eight bounded slot records and expanded
setup fields to the version-1-through-version-5 settings prefix, and preserves
reveal, speed, cheats, and team-lock runtime state. Versions 1-5 are decoded
with their original enum values and migrated to the equivalent two-slot
layout. Unsupported historical dimensions produce a clear setup error instead
of an unsafe allocation.

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

Unit-effect commands are applied only when their unit ID/class selector matches
and their operation is set (type 0), add (type 4), or multiply (type 5). The
simulation currently consumes attributes 0 (hit points), 1 (line of sight), 2
(garrison capacity), 5 (movement speed), 8 (packed armor), 9 (packed attack),
10 (reload), 11 (accuracy), 12 (range), 13 (work rate), 14 (resource carrying
capacity), 15 (base armor), 16 (projectile substitution), and 20 (minimum
range). Work rate affects gathering, construction, and repair; capacity affects
both authoritative carrying limits and the carried-resource panel. Garrison
capacity is used by boarding, reservations, production rally targets, and UI
availability rather than only by display code. The Clone Campaigns technology
set has no applicable attribute-15 command for an extant unit, but the consumer
is retained for scenario/mod DAT compatibility.

Player resource commands are not interchangeable with unit attributes. Known
gameplay consumers include current stocks 0-3, population headroom 4,
technology/attack eligibility such as Walker Research attribute 31, and
aircraft shields attribute 38. Conversion consumes the evidenced resources
27, 35, 77, 87, 178, and 179; Holocron Nova generation consumes resource 191.
Other parsed resource IDs include scenario counters, AI/internal state, and
one-off mechanics; they remain unsupported until a DAT reference and
executable behavior establish a safe semantic. Unknown commands remain parsed
and available for inspection rather than being blanket-applied as multipliers.

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
Convert is constructed separately at `0x50301d` with frame 14/action `0x1d`
and help 4925. Its icon is shown disabled with the live Force Power percentage
until the selected Force user reaches 100 percent.

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

## Fog-of-war rendering

World visibility remains the tile-authoritative explored/currently-visible
state described above. The presentation samples that state into a bounded
quarter-resolution screen-space RGBA texture and draws one overlay quad.
The texture is regenerated only when visibility, camera origin, zoom, player,
screen dimensions, or exploration-cheat state changes. This preserves the
existing smooth tile-boundary interpolation and elevation correction while
removing the former row-run rectangle fan-out that issued thousands of
individual overlay draws per frame on Vita.

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

## Projectile accuracy, misses, and blast limits

The original data disproves the earlier assumption that every normal attack
hits its selected object. `accuracyPercent` varies materially (observed values
include 45, 50, 75, 85, and 100), `accuracyDispersion` includes non-zero 0.10
and 0.33 records, and projectile motion uses the projectile unit's ordinary
DAT speed. Accuracy is evaluated when a shot is released, after researched
attribute 11. The saved deterministic simulation RNG selects hit or miss.
A miss receives a fixed point outside the target footprint, scaled by
dispersion and range, and travels to that point; it does not silently damage
the original target. Non-smart projectiles also snapshot a fixed aim point.
The fixed point and RNG state originated in save version 1 and remain
unchanged in version 2, so active misses continue identically across
save/load.

Direct-fire attacks retain their immediate damage path. Projectile attacks
retain frame delay, spawn offsets, arc/velocity, secondary projectiles,
minimum range, garrison volleys, Attack Ground, impact graphics/sounds,
shield absorption, diplomacy checks, and the existing DAT blast-width/level
path. Splash is evaluated only at the projectile's actual impact. Exact executable
details for the projectile miss distribution, range/elevation/target-motion
modifiers, secondary-bolt accuracy, and ballistic use of `projectileArc`
remain unresolved and are explicitly not claimed as bit-exact.

The blast candidate loop at `0x55dcf0` is conclusive. `0x55de37-0x55de49`
rejects a candidate when its `blastDefenseLevel` is less than the source
`blastAttackLevel`; there is no special “level 3 means target only” branch.
The direct victim passed by the ordinary projectile path is excluded at
`0x55de27` and receives its separately calculated direct hit. Candidate
distance at `0x55dead-0x55df15` is measured from the impact point to the
candidate footprint: each absolute axis delta is reduced by that candidate's
collision half-size, clamped to zero, squared, and compared with
`blastWidth * blastWidth`. No center-distance falloff is applied.

For attacks with effective maximum range at most one, the allied predicate at
`0x55de63-0x55de8a` excludes the owner's own and allied objects; enemy and
neutral objects remain eligible. Ranged splash bypasses that diplomacy gate
and can damage friendly objects. Each eligible secondary victim receives its
own deterministic `accuracyPercent` roll at `0x55df1b-0x55df46`, then normal
attack-class damage through vtable slot `+0xe8`; shields therefore process
before hit points. Class 35 has an additional designated-primary restriction
at `0x55de90-0x55dea7`. Native coverage verifies the defense-level boundary,
melee/ranged diplomacy difference, neutral damage, candidate-footprint
geometry, direct-target exclusion, and deterministic statistical boundaries.

## Shield and detector fidelity audit

Shield eligibility at `0x54bc40` produces a boolean per-player map-cell mask.
Overlapping generators therefore do not add shield capacity. The native
source lookup now prefers any powered overlapping field, so a closer
unpowered generator cannot mask a farther powered field; if no powered source
remains, retained shields enter the drain path. Generator destruction and
power loss use the same transition.

Regeneration at `0x54ed30-0x54ee68` accumulates a per-object timer and, once
per player resource-25 interval, adds by current shield points: 2 below 100,
4 from 100, 8 from 1000, 12 from 2000, 16 from 3000, and 20 from 4000,
capped at maximum HP. Every playable civilization has resource 25 equal to
one second. Drain at `0x54ee70-0x54eef8` is a separate one-second timer and
subtracts player resource 26, whose base is 40. Superconducting Shields
(technology 570, effect 588) multiplies resource 26 by 0.5, proving the
20-point drain. The same effect multiplies resource 10 by 1.2, but the
executable audit did not establish that resource's shield-regeneration
consumer, so no additional multiplier is guessed. Save version 5 preserves
both timer phases.

Damage handlers `0x5517f0` and the mobile override at `0x444820` subtract
positive damage from shields first and apply only overflow to HP. The mobile
damage calculator at `0x4449c0` clamps a computed hit to at least one before
shield processing; it is not one-HP shield leakage. The earlier compatibility
leak and erroneous 3008 regeneration threshold were removed. Deterministic
coverage includes unit/building absorption, exact overflow, all tier
boundaries, delayed drain, Superconducting drain, overlapping fields,
generator loss, and save/load timer continuity.

The detector predicate at `0x54bbc0` returns true for DAT trait bit 8; classes
50/51 also detect when player resource 58 (Force Perception) is positive, and
classes 11/13/15/16 detect when player resource 23 is positive. Native tests
cover both resource-gated class families and allied detector sharing. The
stealth predicate at `0x54bab0` corroborates trait bit 4 and resources 56/58,
but no separate reveal-radius formula or post-detection persistence state was
found. Detection therefore continues to use current detector LOS with no
invented persistence.

## Mechanical-fidelity audit limits

The GOG Clone Campaigns 1.1 image was rechecked with focused callers and data
tables for every normal-play ledger area requested by this milestone. The
audit established the blast, shield, and detector contracts above and
reconfirmed formation dispatch at `0x480060`, formation layouts at
`0x478e30`/`0x47a1b0`/`0x479760`/`0x479f10`, cardinal-neighbor A* with a
Euclidean heuristic at `0x498820`, resolution/proximity handling at
`0x499010`, conversion action 104's 4-second work/10-second recharge DAT
contract, and the victory switch at `0x57f069`.

Evidence did not establish the exact projectile miss-point construction,
range/elevation/motion accuracy modifiers, ballistic `projectileArc` formula,
conversion success RNG or full immunity table, automatic formation
line/column switching, complete path terrain-cost formula, original stealth
reveal persistence, or stock victory countdown/score weights and tie rule.
The audit also found no stronger evidence that would justify replacing the
current tested conversion, formation/pathing, victory, attack-slot,
utility-trawler, task-list, fog, or AI compatibility behavior. Those paths are
retained and covered rather than relabeled as exact. Accuracy is tested over a
fixed 96-seed corpus against the DAT percentage boundary; it is deterministic,
not a flaky random assertion.

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
  supplies action 5 gathering, action 101 construction, and action 106 repair,
  so fishing/build/repair UI, placement assignment, work animation, cursors,
  sounds, and contextual commands are capability-driven. Unit 13 has terrain
  restriction 13, search radius 5, capacity 15, work rate 0.430, speed 1.26,
  and a 45-carbon cost. Its action-5 tasks target fish classes 23/24/25 and
  Aqua Harvester units 199/278. They consume raw resource 17 and output
  resource 0 (food), with observed task work values 1.750/1.000/1.750 for the
  three fish classes. Its build menu uses naval structures whose
  `trainLocationId` is 13; after the Shipyard's automatic technology 27,
  Aqua Harvester 199 and Sensor Buoy 1576 are available. Utility Trawlers
  repair owned or allied naval classes 13 through 17 at the shared original
  repair cost/rate path.
- Aqua Harvester 199 is type 80, class 7, terrain restriction 13, creation
  location 13, and stores 15 units of resource 17. It costs 90 carbon, takes
  50 seconds to build, and depletes into unit 278. This corrects the prior
  assumption that every class-7 object is a farm: farm reseeding, terrain
  painting, in-footprint worker movement, and destruction now identify farms
  by their unit identity/name, so Aqua Harvesters remain water resource
  buildings.
- Archipelago and Compact Islands generation discovers extant Gaia
  class-23/24/25 resource-17 units from the loaded DAT and places deterministic
  water resources. Idle AI task-capable water gatherers select only visible,
  reachable resource-17 targets. Gather state, carried food, exhaustion and
  nearby retargeting use the normal deterministic work state and persist in
  save version 2; version-1 files migrate explicitly.
