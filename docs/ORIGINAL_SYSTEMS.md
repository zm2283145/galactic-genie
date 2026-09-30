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
| Gather points | Commands at `0x502580`/`0x5bcb30`; spawned-unit dispatch at `0x56e390` | Verified and covered |
| Gates | State update at `0x558390`; placement at `0x60c100` | Verified and covered |
| Shields and power | Coverage/status logic at `0x54bc40`/`0x55ec20` | Partially verified; mobile damage bleed-through remains open |
| Garrison fire | Volley logic at `0x55be20`/`0x55c0c0` | Verified and covered |
| Walls | Command executor at `0x5ba900`; preview at `0x5fc180` | Verified and covered |
| Formations | Layout/update routines at `0x478e30`, `0x479760`, `0x47c280`, `0x47e780`, `0x480060` | Layout verified; automatic line/column switching remains open |
| Pathfinding | Long-range routines at `0x4982f0`, `0x498820`, `0x4989f0`, `0x499010` | Partially verified and covered |
| Unit information UI | Panel rows at `0x5d98a0`/`0x5db840`; resource rows at `0x5daf72` | Partially verified |
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
