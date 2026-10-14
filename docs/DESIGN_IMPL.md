Here is a verified Quint spec for an elevator group: [paste spec/elevator.qnt]
Here is the original design doc: [paste docs/DESIGN_SPEC.md]

Generate a C++20 implementation with these requirements:

1. Types in `include/elevator/types.hpp`, mirroring the Quint types:
   - `enum class Dir { Up, Down }`, `Door { Open, Closed }`, `Motor { Stopped, Running }`
   - `enum class Heading { GoingUp, GoingDown, Idle }` for `Going(Up) | Going(Down) | Idle`
   - structs `HallCall`, `ElevatorState`, `PositionReport`, `DispatcherState`
     with the same field names as the Quint records
   - message structs `HallCallMsg`, `AssignMsg`, `StatusMsg`, `ServedMsg`
     and `using Message = std::variant<...>`
   - Quint `Set` -> `std::set`, Quint map -> `std::map`, elevator name -> `std::string`
   - defaulted `operator<=>` / `operator==` on every type
   - `struct Config { numFloors = 4; elevators = {"A", "B"}; bottom(); top(); }`

2. A class `ElevatorSystem` in `include/elevator/elevator_system.hpp`.

3. Methods corresponding to each Quint action, same name, same parameters.
   Every method returns false and leaves the state untouched if the
   precondition fails; otherwise it applies the effect and returns true.
   A method given an unknown elevator name returns false. Anything a method
   does not mention stays unchanged.

   - `void init()`: puts the system into the initial state. Every car is on
     the bottom floor with the door closed, the motor stopped, heading `Idle`,
     no car calls, no hall stops and `statusSeq` 0. The dispatcher has nothing
     pending, nothing assigned, and knows every elevator to be on the bottom
     floor with `seq` 0. No hall lamp is lit and the network is empty. The
     constructor calls it.

   - `bool pressHallButton(HallCall c)`: a passenger presses a hall button.
     Precondition: `c` is a valid hall call and its lamp is not lit.
     Effect: the lamp of `c` is lit and a `HallCallMsg` with `c` is put into
     the network.

   - `bool pressCarButton(ElevatorId e, int f)`: a passenger in car `e`
     selects floor `f`; allowed while the car is moving.
     Precondition: `f` is a floor of the building, it is not the car's
     current floor, and it is not already among the car's `carCalls`.
     Effect: `f` is added to the car's `carCalls`.

   - `bool receiveHallCall(HallCall c)`: the dispatcher receives a hall call.
     Precondition: a `HallCallMsg` with `c` is in the network.
     Effect: that message is removed from the network. If `c` is neither in
     `pending` nor a key of `assigned`, it is added to `pending`; otherwise
     the dispatcher does not change.

   - `bool assignTo(HallCall c, ElevatorId e)`: the dispatcher gives a pending
     call to elevator `e`. This also implements Quint `assignCall(c)`, whose
     nondeterministic choice `target` is passed in as `e`.
     Precondition: `c` is in `pending` and `e` is one of
     `closestElevators(dispatcher, c.floor)`.
     Effect: `c` is removed from `pending`, `assigned[c]` becomes `e`, and an
     `AssignMsg` for `e` and `c` is put into the network.

   - `bool receiveStatus(StatusMsg s)`: the dispatcher receives a position
     report.
     Precondition: `s` is in the network.
     Effect: `s` is removed from the network. If `s.seq` is greater than the
     `seq` in `known[s.elevator]`, then `known[s.elevator]` becomes `s.floor`
     and `s.seq`. Otherwise the report is outdated and is ignored.

   - `bool receiveServed(ServedMsg s)`: the dispatcher learns that a call was
     served.
     Precondition: `s` is in the network.
     Effect: `s` is removed from the network, `s.call` is removed from
     `assigned`, and the lamp of `s.call` goes off.

   - `bool receiveAssign(AssignMsg a)`: an elevator receives an assignment.
     Precondition: `a` is in the network.
     Effect: `a` is removed from the network and `a.call` is added to the
     `hallStops` of the car `a.elevator`.

   - `bool departure(ElevatorId e)`: car `e` starts moving.
     Precondition: the motor is stopped, the door is closed, the car has at
     least one target, and its current floor is not one of its targets.
     Effect: the motor is running and the heading becomes `GoingUp` or
     `GoingDown` according to `nextDir` of the car.

   - `bool travel(ElevatorId e)`: car `e` moves by one floor.
     Precondition: the motor is running.
     Effect: the new floor is the current floor plus `delta` of the heading.
     The car's floor becomes the new floor. If the new floor is one of the
     car's targets, computed before the move, the motor stops; otherwise it
     keeps running. `statusSeq` increases by one, and a `StatusMsg` with the
     car's name, the new floor and the new `statusSeq` is put into the
     network.

   - `bool openDoor(ElevatorId e)`: car `e` opens its doors at a target floor.
     Precondition: the motor is stopped, the door is closed, and the current
     floor is one of the car's targets.
     Effect: the served calls are the car's `hallStops` on the current floor.
     The current floor is removed from `carCalls` and the served calls are
     removed from `hallStops`. The door opens. If the car has no targets left,
     the heading becomes `Idle`; otherwise it stays as it was. For every
     served call a `ServedMsg` with the car's name and that call is put into
     the network.

   - `bool closeDoor(ElevatorId e)`: car `e` closes its doors.
     Precondition: the door is open.
     Effect: the door is closed.

   - `bool deliver(Message m)`: the network delivers message `m`. This also
     implements Quint `deliverAny`, whose nondeterministic choice `m` is
     passed in.
     Effect: calls `receiveHallCall`, `receiveAssign`, `receiveStatus` or
     `receiveServed` depending on the kind of `m`, and returns its result.

   `assignCall`, `deliverAny` and `step` get no method of their own: their
   nondeterministic choice is made by the caller.

4. Public const accessors for every Quint state variable:
   - `elevators() const`   (maps to Quint var `elevators`)
   - `dispatcher() const`  (maps to Quint var `dispatcher`)
   - `hallLamps() const`   (maps to Quint var `hallLamps`)
   - `network() const`     (maps to Quint var `network`)
   These accessors MUST be named exactly as the Quint variables are named,
   because the C++ test driver will compare them to ITF trace state by name.

5. Pure helpers as free functions with the Quint names:
   - `delta(Heading)`: 1 for `GoingUp`, -1 for `GoingDown`, 0 for `Idle`.
   - `isValidHallCall(Config, HallCall)`: true when the floor exists, the call
     is not `Up` on the top floor, and not `Down` on the bottom floor.
   - `allHallCalls(Config)` (Quint `HALL_CALLS`): every valid hall call.
   - `targets(ElevatorState)`: the car's `carCalls` together with the floors
     of all its `hallStops`.
   - `nextDir(ElevatorState)`, the SCAN rule: if the car is going up and has
     a target above it, `Up`; otherwise, if it is going down and has a target
     below it, `Down`; otherwise, if it has any target above it, `Up`;
     otherwise `Down`.
   - `closestElevators(Config, DispatcherState, floor)`: the elevators whose
     floor in `known` is at the smallest distance from `floor`; all of them
     when several are equally close. Uses only the dispatcher's knowledge.

6. Preconditions enforced identically to the Quint guards. `travel` has no
   shaft-bounds check, exactly like the spec.

7. Comments on each method citing the Quint action name it implements.

8. Implementation split by participant:
   - `src/elevator_system.cpp`: helpers, `init`, `deliver`
   - `src/environment.cpp`: `pressHallButton`, `pressCarButton`
   - `src/dispatcher.cpp`: `receiveHallCall`, `assignTo`, `receiveStatus`, `receiveServed`
   - `src/controller.cpp`: `receiveAssign`, `departure`, `travel`, `openDoor`, `closeDoor`

9. `include/` and `src/` contain production code only: no invariants, no JSON,
   no trace parsing, no test helpers.
