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
| AI scripts | Original `.per` parser, facts, actions, and managers | Not implemented |

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
- A committed ordinary building paints its foundation terrain immediately.

## Command cursors

`mcursors.shp` resource 51000 contains distinct semantic frames. Frame 2 is
the valid-order confirmation marker; frame 5 is move, 6 gather/drop-off, 7
building placement, 9 repair, 11 attack, 12 garrison, and 18 set gather point.
Frames 2/3/4 are not interchangeable generic action cursors. The repair and
destroy command-panel icons remain frames 28 and 59 of command sheet 50721,
as constructed by the executable with help strings 4927 and 4941.
Attack stances are offered only to eligible combat units; workers and
buildings do not show the stance button or stance status.

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
- Construction, gathering, carrying, and repair graphics come from the
  corresponding DAT worker variant and task.
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
