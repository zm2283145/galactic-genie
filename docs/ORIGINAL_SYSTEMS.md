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
| Workers | DAT class 58 and `UNIT-WORKER[A/B]1..11` task variants | Partially verified and covered |
| Combat target eligibility | Attack-task validation at `0x5b9930`; class filter at `0x41c530`; DAT effect 129 | Verified and covered |
| Building attack approach | DAT rectangular collision footprints; attack action and path goal state | Covered; exact original slot ordering remains open |
| Attack Ground | Command-panel construction at `0x503aca`; opcode `0x6b`; DAT blast/projectile fields | Verified and covered |
| Firing presentation | DAT attack graphic, frame delay, projectile totals, secondary projectile, and impact data | Partially verified and covered |
| AI scripts | Original Computer Expanded `.per` files and DAT `name2` aliases | First economy slice implemented and covered |

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

The first economy slice supports goals, strategic numbers, Tech Level and age
time, difficulty, population and housing, resource amounts, unit/building
counts, technology completion/availability, affordability, and Boolean and
numeric comparisons. Its actions set goals and strategic numbers, disable
rules, build, train, and research through the same placement, resource,
population, production, and research systems used by the player. Gather
percentages retask real idle workers to compatible original resource nodes.
Original script symbols resolve against both DAT unit names and the secondary
AI names stored in `name2`.

Engine contract:

- AI construction uses normal placement validation, creates a real foundation,
  charges normal DAT costs, and assigns an available worker.
- Training and research use normal building queues and population/resource
  checks.
- Gather allocations preserve builders, repairers, and garrisoned workers and
  are rebalanced at a bounded cadence.
- Unsupported facts block a rule and unsupported actions are reported once;
  neither is silently treated as success.
- Vita loads player 2's `Computer Expanded.per` from
  `ux0:data/swgb/AI`; deployment copies the original `.per` directory only
  when the user explicitly selects `-AiData`.
- Scenario personality references, timers, escrow, strategic build-forward
  placement, military managers, and the remaining facts/actions are still
  open.

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

Animal Nursery unit 319 accepts class-1 livestock through category 16. Each
occupant adds the building's DAT `workRate` to food per second. The selection
panel shows the occupants in the same clickable portrait strip as every other
garrison, allows one animal to be ejected by pressing its portrait, and shows
the current aggregate food-per-second rate. Ejecting an animal immediately
reduces that rate.

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
under-attack notice, with a cooldown so repeated damage ticks do not restart
the alert.

The same interface table binds `archupg.wav` to resource 50325. Completing
queued Tech Level technologies 1, 2, or 3 for the local player plays that cue.
Those effects directly replace base Command Center 109 at each level
(`109 -> 71`, `109 -> 141`, and `109 -> 142`), so replacement caching aliases
the previously displayed level to the newest result instead of preserving the
first replacement.

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
