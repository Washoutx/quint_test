# Elevator Group Control Design

## Overview

A building with `NUM_FLOORS` floors served by a group of elevators. Every floor
has hall buttons (Up and Down), every car has a panel with one button per floor.

- **Hall panels** light a lamp and send a hall call to the dispatcher.
- **The dispatcher** assigns every hall call to exactly one elevator: the one
  closest to the calling floor according to its own, possibly stale, knowledge.
- **Elevator controllers** move their car floor by floor using SCAN, open the
  doors at target floors, and report position and served calls back.

The dispatcher and the elevators communicate only through an asynchronous
network: messages may be delivered in any order and after any delay, but are
never lost or duplicated. Car buttons are handled locally by the car.

Out of scope: passengers as entities, load, real time, failures, reassigning a
call once assigned, and direction when serving (a stopped car serves all its
hall calls on that floor).

## Types

- `Dir = Up | Down`
- `Heading = Going(Dir) | Idle`: direction the car is committed to. Set on
  departure, reset to `Idle` when the car runs out of work.
- `Door = Open | Closed`
- `Motor = Stopped | Running`
- `HallCall = { floor: int, dir: Dir }`
- `ElevatorState = { floor: int, door: Door, motor: Motor, heading: Heading,
  carCalls: Set[int], hallStops: Set[HallCall], statusSeq: int }`
- `PositionReport = { floor: int, seq: int }`
- `DispatcherState = { pending: Set[HallCall], assigned: HallCall -> str,
  known: str -> PositionReport }`
- `Message =`
  - `HallCallMsg(HallCall)`: hall panel to dispatcher
  - `AssignMsg({ elevator: str, call: HallCall })`: dispatcher to elevator
  - `StatusMsg({ elevator: str, floor: int, seq: int })`: elevator to dispatcher
  - `ServedMsg({ elevator: str, call: HallCall })`: elevator to dispatcher

## Constants

- `NUM_FLOORS = 4`, `BOTTOM = 0`, `TOP = NUM_FLOORS - 1`
- `FLOORS = BOTTOM..TOP`
- `ELEVATORS = { "A", "B" }`

## State Variables

- `elevators` (`str -> ElevatorState`): state of every car.
- `dispatcher` (`DispatcherState`): calls pending, calls assigned, and the last
  known position of every elevator.
- `hallLamps` (`Set[HallCall]`): lit hall button lamps.
- `network` (`Set[Message]`): messages in flight. Sending adds a message,
  delivery removes it; any message may be delivered next.

### Init

- every car: `floor = BOTTOM`, `door = Closed`, `motor = Stopped`,
  `heading = Idle`, `carCalls = {}`, `hallStops = {}`, `statusSeq = 0`
- `dispatcher = { pending: {}, assigned: {}, known: every e -> { floor: BOTTOM, seq: 0 } }`
- `hallLamps = {}`, `network = {}`

## Helpers

- `abs(x)`: absolute value.
- `isValidHallCall(c)`: `c.floor ∈ FLOORS`, not `(c.floor = TOP ∧ c.dir = Up)`,
  not `(c.floor = BOTTOM ∧ c.dir = Down)`.
- `HALL_CALLS`: all valid hall calls.
- `targets(es) = es.carCalls ∪ { c.floor | c ∈ es.hallStops }`
- `delta(h)`: `+1` for `Going(Up)`, `-1` for `Going(Down)`, `0` for `Idle`.
- `nextDir(es)` (SCAN), with `above` / `below` meaning some target is above /
  below `es.floor`:
  1. `heading = Going(Up) ∧ above` → `Up`
  2. `heading = Going(Down) ∧ below` → `Down`
  3. `above` → `Up`
  4. otherwise → `Down`
- `closestElevators(d, floor)`: all `e` minimizing
  `abs(d.known[e].floor - floor)`. Uses only the dispatcher's knowledge.
- `without(m, k)`: map `m` with key `k` removed.

Notation: `es = elevators[e]`. Every operation leaves all variables it does not
mention unchanged.

## Operations

### Passengers

#### pressHallButton(c)
- **Precondition**: `c ∈ HALL_CALLS ∧ c ∉ hallLamps` (pressing a lit button is not possible)
- **Effect**: `hallLamps' = hallLamps ∪ {c}`, `network' = network ∪ {HallCallMsg(c)}`

#### pressCarButton(e, f)
- **Precondition**: `f ∈ FLOORS ∧ f ≠ es.floor ∧ f ∉ es.carCalls` (allowed while moving)
- **Effect**: `es.carCalls' = es.carCalls ∪ {f}`

### Dispatcher

#### receiveHallCall(c)
- **Precondition**: `HallCallMsg(c) ∈ network`
- **Effect**: `network' = network \ {HallCallMsg(c)}`; if `c ∉ pending ∧ c ∉ keys(assigned)`
  then `pending' = pending ∪ {c}`

#### assignTo(c, e)
- **Precondition**: `c ∈ pending ∧ e ∈ closestElevators(dispatcher, c.floor)`
- **Effect**: `pending' = pending \ {c}`, `assigned' = assigned[c ↦ e]`,
  `network' = network ∪ {AssignMsg({ elevator: e, call: c })}`

#### assignCall(c)
- **Effect**: `nondet target ∈ closestElevators(dispatcher, c.floor)`, then `assignTo(c, target)`

#### receiveStatus(s)
- **Precondition**: `StatusMsg(s) ∈ network`
- **Effect**: `network' = network \ {StatusMsg(s)}`; if `s.seq > known[s.elevator].seq`
  then `known'[s.elevator] = { floor: s.floor, seq: s.seq }`, otherwise the report is
  stale and ignored

#### receiveServed(s)
- **Precondition**: `ServedMsg(s) ∈ network`
- **Effect**: `network' = network \ {ServedMsg(s)}`, `assigned' = without(assigned, s.call)`,
  `hallLamps' = hallLamps \ {s.call}` (the lamp goes off only now)

### Elevator controller

#### receiveAssign(a)
- **Precondition**: `AssignMsg(a) ∈ network`
- **Effect**: `network' = network \ {AssignMsg(a)}`,
  `elevators[a.elevator].hallStops' = hallStops ∪ {a.call}`

#### departure(e)
- **Precondition**: `es.motor = Stopped ∧ es.door = Closed ∧ targets(es) ≠ {} ∧ es.floor ∉ targets(es)`
- **Effect**: `es.motor' = Running`, `es.heading' = Going(nextDir(es))`

#### travel(e)
- **Precondition**: `es.motor = Running`
- **Effect**, with `newFloor = es.floor + delta(es.heading)`:
  - `es.floor' = newFloor`
  - `es.motor' = Stopped` if `newFloor ∈ targets(es)`, else `Running`
  - `es.statusSeq' = es.statusSeq + 1`
  - `network' = network ∪ {StatusMsg({ elevator: e, floor: newFloor, seq: es.statusSeq + 1 })}`
- No shaft-bounds check on purpose; INV-02 checks it.

#### openDoor(e)
- **Precondition**: `es.motor = Stopped ∧ es.door = Closed ∧ es.floor ∈ targets(es)`
- **Effect**, with `served = { c ∈ es.hallStops | c.floor = es.floor }`:
  - `es.carCalls' = es.carCalls \ {es.floor}`, `es.hallStops' = es.hallStops \ served`
  - `es.door' = Open`
  - `es.heading' = Idle` if `targets(es') = {}`, else unchanged
  - `network' = network ∪ { ServedMsg({ elevator: e, call: c }) | c ∈ served }`

#### closeDoor(e)
- **Precondition**: `es.door = Open`
- **Effect**: `es.door' = Closed`

### Network

#### deliver(m)
- **Effect**: dispatch on the kind of `m` to `receiveHallCall`, `receiveAssign`,
  `receiveStatus`, or `receiveServed`.

#### deliverAny
- **Precondition**: `network ≠ {}`
- **Effect**: `nondet m ∈ network`, then `deliver(m)`

### step
- `nondet e ∈ ELEVATORS`, `nondet f ∈ FLOORS`, `nondet c ∈ HALL_CALLS`, then exactly one of:
  `pressHallButton(c)`, `pressCarButton(e, f)`, `assignCall(c)`, `departure(e)`,
  `travel(e)`, `openDoor(e)`, `closeDoor(e)`, `deliverAny`.
- All nondet names (`e`, `f`, `c`, `target`, `m`) must be unique: recorded
  executions identify choices by name.

## Safety Invariants

- **INV-01 doorsClosedWhileRunning**: `motor = Running ⇒ door = Closed` for every car.
- **INV-02 staysInShaft**: `floor ∈ FLOORS` for every car.
- **INV-03 runningHasHeading**: `motor = Running ⇒ heading ≠ Idle` for every car.
- **INV-04 runningHasTargetAhead**: `motor = Running ⇒ ∃ t ∈ targets(es): (t - floor) · delta(heading) > 0`.
- **INV-05 noDoubleService**: for every `c ∈ HALL_CALLS`, at most one elevator `e` has
  `c ∈ hallStops(e) ∨ AssignMsg({ elevator: e, call: c }) ∈ network`.
- **INV-06 assignmentConsistent**: `c ∈ hallStops(e) ⇒ assigned[c] = e`.
- **INV-07 noLostHallCall**: every `c ∈ hallLamps` is in exactly one of
  `HallCallMsg(c) ∈ network`, `c ∈ pending`, `c ∈ keys(assigned)`.
- **INV-08 assignedCallIsTracked**: for `assigned[c] = e`: `AssignMsg({e, c}) ∈ network ∨
  c ∈ hallStops(e) ∨ ServedMsg({e, c}) ∈ network`.
- **INV-09 noTrackingWithoutLamp**: `pending ∪ keys(assigned) ⊆ hallLamps`.
- **safety** = INV-01 ∧ … ∧ INV-09.

## Liveness Properties

Hold only under weak fairness of all actions (temporal properties).

- **LIV-01 hallCallsEventuallyServed**: `□(c ∈ hallLamps ⇒ ◇(c ∉ hallLamps))` for every `c`.
- **LIV-02 carCallsEventuallyServed**: `□(f ∈ carCalls(e) ⇒ ◇(f ∉ carCalls(e)))` for every `e`, `f`.

## Witnesses

Expected to be **violated**; a violation proves the situation is reachable.

- **WIT-01 witnessBothRunning**: `¬(∀ e: motor(e) = Running)`
- **WIT-02 witnessHallCallServed**: `¬(∃ ServedMsg ∈ network)`
- **WIT-03 witnessStaleKnowledge**: `∀ e: known[e].floor = floor(e)`

## Test Scenarios

`call2Down = { floor: 2, dir: Down }`.

- **hallCallLifecycleTest**: `pressHallButton(call2Down)` → deliver its `HallCallMsg`
  (`pending = {call2Down}`) → `assignTo(call2Down, "A")` → deliver the `AssignMsg` →
  `departure("A")` (`heading = Going(Up)`) → `travel("A")` (still `Running`) →
  `travel("A")` (`floor = 2`, `Stopped`) → `openDoor("A")` (`Open`, `Idle`) → deliver
  the `ServedMsg`. Expect `hallLamps = {}`, `assigned = {}`, `safety`.
- **litButtonIsIgnoredTest**: `pressHallButton(call2Down)` twice fails.
- **noDepartureWithOpenDoorTest**: `pressCarButton("A", 3)`, `departure("A")`, `travel("A")`,
  `pressCarButton("A", 0)`, `travel("A")` ×2 (`floor = 3`), `openDoor("A")`; then
  `departure("A")` fails.
- **scanKeepsDirectionTest**: `pressCarButton("A", 2)`, `departure("A")`, `travel("A")`,
  `pressCarButton("A", 0)`, `travel("A")`, `openDoor("A")`, `closeDoor("A")`,
  `departure("A")`. Expect `heading = Going(Down)`, `safety`.
- **closestElevatorIsChosenTest**: `pressCarButton("B", 3)`, `departure("B")`, `travel("B")`,
  deliver `StatusMsg({ elevator: "B", floor: 1, seq: 1 })`, `pressHallButton({2, Up})`,
  deliver its `HallCallMsg`; then `assignTo({2, Up}, "A")` fails.

## Bounds for Model Checking

- `NUM_FLOORS = 4`, `ELEVATORS = { "A", "B" }`
- `MAX_STEPS = 60` per simulated execution
- `quint run --invariant=safety --max-samples=1000 --max-steps=60` must report no violation.
- `quint run --invariant=<WIT-xx>` must report a violation.

## Output

A single file `spec/elevator.qnt`, module `elevator`, comments in English, using
exactly the names above. Nothing else.
