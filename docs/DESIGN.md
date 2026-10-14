# Single Elevator: Design Document

**design doc → Quint spec → C++ implementation → Quint traces → C++ replay**.

---

## 1. Scope

One elevator serves a building with floors 0 to 4. Passengers request floors
(there is no distinction between hall and car buttons, a request is just
"stop at floor `f`"). The elevator moves one floor at a time, opens its door
at requested floors, and uses SCAN: it keeps its direction while there are
requests ahead and reverses otherwise.

---

## 2. Parameters

| Name     | Value                     |
|----------|---------------------------|
| `BOTTOM` | 0, the lowest floor       |
| `TOP`    | 4, the highest floor      |
| `FLOORS` | all floors from 0 to 4    |

---

## 3. State

The direction is one of `Up`, `Down`, or `Idle`.
The door is either `Open` or `Closed`.

| Variable    | Type             | Meaning |
|-------------|------------------|---------|
| `floor`     | integer          | current floor |
| `door`      | `Door`           | door state |
| `direction` | `Direction`      | current direction; `Idle` when there is nothing left to do after serving a floor |
| `requests`  | set of integers  | requested floors not yet served |

**Initial state:** the elevator is on floor 0 with the door closed, its
direction is `Idle`, and there are no requests.

---

## 4. Actions

Every step executes exactly one action whose guard holds. The choice is
non-deterministic. Everything not mentioned in an effect stays unchanged.

### `request(f)`
A passenger requests floor `f`.
- **Guard:** `f` is one of the building's floors, it is not already
  requested, and it is not the floor where the elevator currently stands
  with its door open.
- **Effect:** floor `f` is added to the requests.

### `openDoor`
- **Guard:** the door is closed and the current floor is requested.
- **Effect:** the door opens and the current floor is removed from the
  requests. If no requests remain, the direction becomes `Idle`; otherwise
  the direction stays the same.

### `closeDoor`
- **Guard:** the door is open.
- **Effect:** the door closes.

### `move`
- **Guard:** the door is closed, there is at least one request, and the
  current floor is not requested (it must be served first).
- **Effect:** the elevator picks its direction with SCAN (see below), stores
  it, and moves one floor in that direction: one floor up for `Up`, one floor
  down for `Down`.

### SCAN: `nextDir(floor, direction, requests)`
1. If the elevator is going up and some request is above the current floor,
   keep going up.
2. Otherwise, if it is going down and some request is below the current
   floor, keep going down.
3. Otherwise, if some request is above the current floor, go up.
4. Otherwise go down.

`move` deliberately has no bounds check. Staying inside the building is a
property of the algorithm, checked by the `floorInRange` invariant.

---

## 5. Invariants (must hold in every reachable state)

| Name | Description |
|------|-------------|
| `floorInRange` | the elevator is always on one of the building's floors |
| `requestsInRange` | every request is for one of the building's floors |
| `openDoorFloorIsServed` | when the door is open, the current floor is not among the requests |

`safety` means that all three invariants hold at the same time.

**Liveness (informal):** if the passengers stop making requests, every
request is eventually served.

---

## 6. Test scenarios

1. **Serve one request:** request floor 3, then move three times. The
   elevator is on floor 3 going up. After opening the door, the door is open,
   there are no requests, and the direction is `Idle`.
2. **SCAN keeps direction:** request floor 3, move once (the elevator is on
   floor 1), request floor 0, move twice (the elevator is on floor 3), open
   and close the door, then move. The elevator now goes down: it is on
   floor 2 with direction `Down`.
3. **No move with open door:** request floor 1, move, open the door, request
   floor 3. Moving is not allowed until the door is closed.
4. **Duplicate request is ignored:** requesting floor 2 a second time is not
   allowed.
