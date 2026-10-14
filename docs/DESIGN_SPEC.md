# Elevator Group Control Design

## Overview

A building with four floors is served by two elevators, called "A" and "B".
Every floor has hall buttons for going up and going down, and every car has a
panel with one button per floor.

- **Hall panels**: when a passenger presses a hall button, its lamp lights up
  and a hall call is sent to the dispatcher.
- **The dispatcher** collects hall calls and gives each of them to exactly one
  elevator: the one that is closest to the calling floor, judged by the last
  position the dispatcher has heard from each elevator. That knowledge may be
  out of date.
- **Elevator controllers**: every car moves floor by floor using the SCAN
  algorithm (keep going in the same direction while there is work ahead, then
  turn around), opens its doors at the floors it has to serve, and reports its
  position and the calls it has served back to the dispatcher.

The dispatcher and the elevators talk only through an asynchronous network.
Messages can arrive in any order and after any delay, but they are never lost
and never duplicated. Car buttons are handled by the car itself and never go
through the dispatcher.

Out of scope: passengers as individuals, load, real time (speed, door timers),
failures of any part, moving a call to another elevator once it has been
given out, and the direction of a hall call when serving it (a car that stops
at a floor serves all of its hall calls there).

## Types

- `Dir`: `Up` or `Down`.
- `Heading`: either `Going` with a `Dir`, or `Idle`. It is the direction the
  car is committed to. It is chosen when the car departs and goes back to
  `Idle` when the car has no more work. An `Idle` car may already have new
  work that arrived while it was standing still.
- `Door`: `Open` or `Closed`.
- `Motor`: `Stopped` or `Running`.
- `HallCall`: a record with a `floor` and a `dir`, meaning "someone on this
  floor wants to go in this direction".
- `ElevatorState`: a record describing one car:
  - `floor`: the floor the car is at; while moving, the last floor it reached,
  - `door`: whether the door is open or closed,
  - `motor`: whether the car is moving,
  - `heading`: the direction the car is committed to,
  - `carCalls`: the floors selected inside the car that have not been served,
  - `hallStops`: the hall calls given to this car that have not been served,
  - `statusSeq`: the number of the last position report this car has sent.
- `PositionReport`: a record with a `floor` and a `seq`: where the dispatcher
  believes an elevator is, and which report that belief came from.
- `DispatcherState`: a record with
  - `pending`: hall calls received but not yet given to any elevator,
  - `assigned`: a map from a hall call to the name of the elevator it was
    given to, kept until the elevator confirms the call was served,
  - `known`: a map from every elevator name to its last known position report.
- `Message`: one of four kinds:
  - `HallCallMsg` carries a `HallCall`, from a hall panel to the dispatcher,
  - `AssignMsg` carries an `elevator` name and a `call`, from the dispatcher
    to that elevator,
  - `StatusMsg` carries an `elevator` name, a `floor` and a `seq`, from an
    elevator to the dispatcher,
  - `ServedMsg` carries an `elevator` name and a `call`, from an elevator to
    the dispatcher.

## Constants

- `NUM_FLOORS` is 4.
- `BOTTOM` is floor 0 and `TOP` is the highest floor, floor 3.
- `FLOORS` are all floors from the bottom to the top.
- `ELEVATORS` are the names "A" and "B".

## State Variables

- `elevators`: the state of every car, by name.
- `dispatcher`: the dispatcher's state.
- `hallLamps`: the hall button lamps that are currently lit.
- `network`: the messages currently in flight. Sending puts a message into
  it, delivering takes one out. Any message in it may be delivered next.

### Initial state

- Both cars stand on the bottom floor with the door closed, the motor
  stopped, heading `Idle`, no car calls, no hall stops, and report number 0.
- The dispatcher has nothing pending and nothing assigned, and believes both
  elevators are on the bottom floor, with report number 0.
- No hall lamp is lit and the network is empty.

## Helpers

- `abs`: the absolute value of a number.
- `isValidHallCall`: a hall call is valid when its floor exists, it is not an
  up call on the top floor, and it is not a down call on the bottom floor.
- `HALL_CALLS`: all valid hall calls.
- `targets`: the floors a car has to stop at: all its car calls together with
  the floors of all its hall stops.
- `delta`: how a heading moves the car: one floor up for going up, one floor
  down for going down, nothing for idle.
- `nextDir` (SCAN): the direction a car should take next.
  1. If it is going up and has a target above it, it goes up.
  2. Otherwise, if it is going down and has a target below it, it goes down.
  3. Otherwise, if it has any target above it, it goes up.
  4. Otherwise it goes down.
- `closestElevators`: takes the dispatcher state and a floor, and returns the
  elevators whose position, as the
  dispatcher knows it, is nearest to that floor. When several are equally
  near, all of them are returned. Only the dispatcher's knowledge is used,
  never the real positions of the cars.
- `without`: a map of assigned calls with one call removed.

Every operation below leaves unchanged everything it does not mention.

The receive operations take the content carried by a message, not the
message itself: `receiveHallCall` takes the hall call, and `receiveStatus`,
`receiveServed` and `receiveAssign` take the record carried by their kind of
message. The message they look for in the network is that content wrapped in
the matching kind of message.

## Operations

### Passengers

#### pressHallButton(c)
- **Precondition**: `c` is a valid hall call and its lamp is not lit. Pressing
  a button that is already lit is therefore not possible.
- **Effect**: the lamp of `c` lights up and a `HallCallMsg` for `c` is sent.

#### pressCarButton(e, f)
- **Precondition**: floor `f` exists, the car `e` is not on that floor, and `f`
  is not already selected in that car. This is allowed while the car moves.
- **Effect**: `f` is added to the car calls of `e`.

### Dispatcher

#### receiveHallCall(c)
- **Precondition**: a `HallCallMsg` for `c` is in the network.
- **Effect**: the message is taken out of the network. If the dispatcher has
  neither pending nor assigned `c` already, `c` becomes pending. Otherwise
  nothing else changes.

#### assignTo(c, e)
- **Precondition**: `c` is pending and `e` is one of the closest elevators to
  the floor of `c`.
- **Effect**: `c` is no longer pending, it is recorded as assigned to `e`, and
  an `AssignMsg` telling `e` to serve `c` is sent.

#### assignCall(c)
- **Effect**: one of the closest elevators to the floor of `c` is chosen non
  deterministically, this choice is named `target`, and `assignTo(c, target)`
  is performed.

#### receiveStatus(s)
- **Precondition**: the `StatusMsg` `s` is in the network.
- **Effect**: the message is taken out of the network. If its report number is
  higher than the one the dispatcher knows for that elevator, the dispatcher
  now believes the elevator is on the reported floor, with the new report
  number. Otherwise the report is outdated and is ignored.

#### receiveServed(s)
- **Precondition**: the `ServedMsg` `s` is in the network.
- **Effect**: the message is taken out of the network, the call is no longer
  assigned, and its hall lamp goes off. The lamp goes off only now, not when
  the doors open; until then, pressing the same hall button again has no
  effect.

### Elevator controller

#### receiveAssign(a)
- **Precondition**: the `AssignMsg` `a` is in the network.
- **Effect**: the message is taken out of the network and the call is added to
  the hall stops of the elevator named in it.

#### departure(e)
- **Precondition**: the car `e` is stopped, its door is closed, it has at
  least one target, and its current floor is not a target. Work on the
  current floor has to be done first, by opening the doors.
- **Effect**: the motor starts and the heading becomes going in the direction
  chosen by `nextDir`.

#### travel(e)
- **Precondition**: the car `e` is running.
- **Effect**:
  - the car moves one floor in the direction of its heading,
  - if the new floor is one of its targets (as they were before this move),
    the motor stops; otherwise the car keeps running,
  - its report number goes up by one,
  - a `StatusMsg` with the car's name, the new floor and the new report number
    is sent to the dispatcher.
- There is deliberately no check that the new floor exists. That a car never
  leaves the building follows from the rest of the rules and is checked by
  INV-02.

#### openDoor(e)
- **Precondition**: the car `e` is stopped, its door is closed, and its current
  floor is one of its targets.
- **Effect**:
  - the car call for the current floor is removed,
  - all hall stops of the car on the current floor are served and removed,
  - the door opens,
  - if the car has no targets left, its heading becomes `Idle`; otherwise the
    heading stays as it was,
  - a `ServedMsg` is sent for every hall call that was served.

#### closeDoor(e)
- **Precondition**: the door of the car `e` is open.
- **Effect**: the door closes.

### Network

#### deliver(m)
- **Effect**: performs the reaction that matches the kind of message `m`:
  `receiveHallCall`, `receiveAssign`, `receiveStatus` or `receiveServed`.

#### deliverAny
- **Precondition**: the network is not empty.
- **Effect**: one message from the network is chosen non deterministically,
  this choice is named `m`, and `deliver(m)` is performed.

### step

First an elevator named `e`, a floor named `f` and a hall call named `c` are
chosen non deterministically. Then exactly one of these happens:
`pressHallButton(c)`, `pressCarButton(e, f)`, `assignCall(c)`,
`departure(e)`, `travel(e)`, `openDoor(e)`, `closeDoor(e)` or `deliverAny`.

Every non deterministic choice has its own unique name (`e`, `f`, `c`,
`target`, `m`), because recorded executions identify choices by name.

## Safety Invariants

- **INV-01 doorsClosedWhileRunning**: a moving car always has its door closed.
- **INV-02 staysInShaft**: every car is always on a floor that exists.
- **INV-03 runningHasHeading**: a moving car is never idle.
- **INV-04 runningHasTargetAhead**: a moving car always has some target ahead
  of it in the direction it is going.
- **INV-05 noDoubleService**: no hall call belongs to more than one elevator,
  counting both the elevators' hall stops and assignment messages on the way.
- **INV-06 assignmentConsistent**: when a call is among an elevator's hall
  stops, the dispatcher has it assigned to that same elevator.
- **INV-07 noLostHallCall**: every lit lamp is tracked in exactly one place:
  as a hall call message on the way, as pending, or as assigned.
- **INV-08 assignedCallIsTracked**: every assigned call is on its way to its
  elevator, held in that elevator's hall stops, or on its way back as a served
  message.
- **INV-09 noTrackingWithoutLamp**: the dispatcher never tracks a call, pending
  or assigned, whose lamp is off.
- **safety**: all of the above together.

## Liveness Properties

These hold only if every action that stays possible eventually happens.

- **LIV-01 hallCallsEventuallyServed**: every lit hall lamp eventually goes off.
- **LIV-02 carCallsEventuallyServed**: every floor selected in a car is
  eventually served.

## Witnesses

Each witness is written as an invariant stating that the situation never
happens, so it is expected to be violated: a violation proves that the
situation can happen.

- **WIT-01 witnessBothRunning**: both cars are moving at the same time.
- **WIT-02 witnessHallCallServed**: some served message is in the network.
- **WIT-03 witnessStaleKnowledge**: the dispatcher believes some elevator is on
  a different floor than it really is.

## Test Scenarios

`call2Down` is the hall call on floor 2 going down. Every test starts from the
initial state. Messages are delivered with `deliver`, giving the exact message.
"Not possible" means the test ends with the expectation that the last step
fails.

- **hallCallLifecycleTest**: the hall button for `call2Down` is pressed and its
  message delivered, so it is the only pending call. It is assigned to "A"
  with `assignTo` and the assignment delivered. "A" departs going up, travels (still running),
  travels again (now on floor 2 and stopped), and opens its doors (door open,
  heading idle). The served message is delivered. At the end no lamp is lit,
  nothing is assigned, and `safety` holds.
- **litButtonIsIgnoredTest**: pressing the hall button for `call2Down` twice in
  a row is not possible.
- **noDepartureWithOpenDoorTest**: in "A" floor 3 is selected; "A" departs and
  travels once; floor 0 is selected; "A" travels twice (now on floor 3) and
  opens its doors. Departing now is not possible.
- **scanKeepsDirectionTest**: in "A" floor 2 is selected; "A" departs and
  travels once; floor 0 is selected; "A" travels, opens and closes its doors,
  and departs. It is now going down, and `safety` holds.
- **closestElevatorIsChosenTest**: in "B" floor 3 is selected; "B" departs and
  travels once; the dispatcher receives the report that "B" is on floor 1
  (report number 1); the hall button for floor 2 going up is pressed and its
  message delivered. Assigning that call to "A" with `assignTo` is not
  possible.

## Bounds for Model Checking

- 4 floors and 2 elevators.
- Random simulation of up to 60 steps per execution must never violate
  `safety`.
- Random simulation must find a violation of every witness.

## Output

A single file `spec/elevator.qnt` with one module named `elevator`, comments
in English, using exactly the names given above. Nothing else.
