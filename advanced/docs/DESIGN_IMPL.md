# Elevator Group Control: Implementation Workflow

```
DESIGN_SPEC.md ─▶ Claude generates Quint spec ─▶ Quint model-checks
      ─▶ Claude generates C++ ─▶ Quint emits ITF traces ─▶ C++ MBT driver replays
      ─▶ PASS: done   |   FAIL: Claude fixes the C++ ─▶ repeat
```

Repository layout:

```
spec/elevator.qnt          Quint spec (generated in phase 2, never edited to make C++ pass)
docs/DESIGN_SPEC.md        design doc, input of phase 2
include/elevator/          production headers
src/                       production code
tests/TestSupport.hpp      invariants + JSON diagnostics (tests only)
tests/ElevatorTests.cpp    unit and scenario tests
tests/MBTDriver.cpp        replays Quint traces against the C++ code
traces/                    ITF traces from Quint (committed)
scripts/mbt.sh             one iteration of the loop
CMakeLists.txt
```

---

## 1. Phase 1 — The Design Doc

`docs/DESIGN_SPEC.md` describes the behavior only: types, state variables,
operations with preconditions and effects, invariants, witnesses, and test
scenarios. It does not mention any implementation language.

---

## 2. Phase 2 — Claude Generates the Quint Spec

### What to ask Claude

```
Here is the design doc: [paste docs/DESIGN_SPEC.md]

Generate spec/elevator.qnt exactly as the doc requires. Nothing else.
```

---

## 3. Phase 3 — Quint Model-Checks the Spec

The spec must be correct on its own before any C++ exists.

```bash
quint typecheck spec/elevator.qnt
quint test spec/elevator.qnt
quint run spec/elevator.qnt --invariant=safety --max-samples=1000 --max-steps=60
quint run spec/elevator.qnt --invariant=witnessBothRunning     # must be violated
quint run spec/elevator.qnt --invariant=witnessHallCallServed  # must be violated
quint run spec/elevator.qnt --invariant=witnessStaleKnowledge  # must be violated
```

If anything fails here, fix the spec (phase 2), not the code.

---

## 4. Phase 4 — Claude Generates the C++ Implementation

Rather than writing the C++ by hand, ask Claude to derive it from the verified
Quint spec. The implementation then starts with the same structure as the
model, which is what makes trace replay possible.

### What to ask Claude

```
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

3. Methods corresponding to each Quint action, same name, same parameters:
   - `void init()`
   - `bool pressHallButton(HallCall)`, `bool pressCarButton(ElevatorId, int)`
   - `bool receiveHallCall(HallCall)`, `bool assignTo(HallCall, ElevatorId)`,
     `bool receiveStatus(StatusMsg)`, `bool receiveServed(ServedMsg)`
   - `bool receiveAssign(AssignMsg)`, `bool departure(ElevatorId)`, `bool travel(ElevatorId)`,
     `bool openDoor(ElevatorId)`, `bool closeDoor(ElevatorId)`
   - `bool deliver(Message)`
   Every method returns false and leaves the state untouched if the
   precondition fails. `assignCall`, `deliverAny` and `step` get no method:
   their nondeterministic choice is made by the caller.

4. Public const accessors for every Quint state variable:
   - `elevators() const`   (maps to Quint var `elevators`)
   - `dispatcher() const`  (maps to Quint var `dispatcher`)
   - `hallLamps() const`   (maps to Quint var `hallLamps`)
   - `network() const`     (maps to Quint var `network`)
   These accessors MUST be named exactly as the Quint variables are named,
   because the C++ test driver will compare them to ITF trace state by name.

5. Pure helpers as free functions with the Quint names:
   `delta`, `isValidHallCall`, `allHallCalls` (Quint `HALL_CALLS`), `targets`,
   `nextDir`, `closestElevators`.

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
```

### The Generated C++ Header

The critical requirement is that every Quint `var` has a corresponding
accessor with the same name, and every Quint action a method with the same name.

```cpp
class ElevatorSystem {
public:
    explicit ElevatorSystem(Config cfg = {});

    const Config& config() const;
    const SystemState& state() const;

    // One accessor per Quint state variable, named exactly like the variable.
    const std::map<ElevatorId, ElevatorState>& elevators() const;
    const DispatcherState& dispatcher() const;
    const std::set<HallCall>& hallLamps() const;
    const std::set<Message>& network() const;

    // Quint: init
    void init();
    // Quint: pressHallButton(c)
    bool pressHallButton(const HallCall& c);
    // Quint: assignTo(c, e); assignCall(c) picks e and calls this
    bool assignTo(const HallCall& c, const ElevatorId& e);
    // Quint: travel(e)
    bool travel(const ElevatorId& e);
    // ... one method per remaining Quint action
};
```

---

## 5. Phase 5 — Claude Generates the Tests

### What to ask Claude

```
Using spec/elevator.qnt and the C++ headers, generate:

1. `tests/TestSupport.hpp` (header-only, namespace `elevator::test`):
   - one `bool` function per Quint safety invariant, same name,
     taking `(const Config&, const SystemState&)`
   - `violatedInvariants(cfg, state)` returning the names of violated invariants
   - `toJson(...)` for `HallCall`, `Message`, `SystemState` (readable, for reports)

2. `tests/ElevatorTests.cpp` (GoogleTest):
   - unit tests for every pure helper
   - every Quint `run` test, step by step; where Quint expects `.fail()`,
     assert the method returns false and the state is unchanged
   - one extra scenario: a stale StatusMsg is discarded
```

---

## 6. Phase 6 — Quint Emits ITF Traces

```bash
quint run spec/elevator.qnt --mbt \
  --max-samples=50 --n-traces=50 --max-steps=60 \
  --out-itf=traces/trace_{seq}.itf.json
```

Every state in a trace contains the four state variables plus:

- `mbt::actionTaken`: the action of `step` that produced the state (`init` first)
- `mbt::nondetPicks`: one field per nondet name (`e`, `f`, `c`, `target`, `m`),
  each `{"tag":"Some","value":...}` or `{"tag":"None","value":{"#tup":[]}}`

ITF value encoding:

| Quint            | ITF JSON |
|------------------|----------|
| `int`            | `{"#bigint": "3"}` |
| `Set`            | `{"#set": [...]}` |
| map              | `{"#map": [[key, value], ...]}` |
| record           | plain object |
| variant          | `{"tag": "Going", "value": {"tag": "Up", "value": {"#tup": []}}}` |

---

## 7. Phase 7 — Claude Generates the MBT Driver

### What to ask Claude

```
Here is the Quint spec: [paste spec/elevator.qnt]
Here is the C++ header: [paste include/elevator/elevator_system.hpp]
Here is one trace: [paste traces/trace_0.itf.json]

Generate `tests/MBTDriver.cpp` (GoogleTest + nlohmann_json) that:

1. Discovers `*.itf.json` in env `MBT_TRACES_DIR`, falling back to the
   compile definition `MBT_DEFAULT_TRACES_DIR`; one parameterized test per file.
   A separate test fails if no traces are found.
2. Parses ITF into the C++ types (inline in this file, not in src/).
3. Calls `init()` and compares it with state 0.
4. For every next state, maps `mbt::actionTaken` + `mbt::nondetPicks` to a C++ call:
   | actionTaken       | C++ call |
   |-------------------|----------|
   | pressHallButton   | pressHallButton(c) |
   | pressCarButton    | pressCarButton(e, f) |
   | assignCall        | assignTo(c, target) |
   | departure, travel, openDoor, closeDoor | same method with e |
   | deliverAny        | deliver(m) |
5. After each call asserts, in order:
   - the call returned true ("guard mismatch" otherwise)
   - every accessor equals the trace variable of the same name;
     the message lists the differing variables
   - no invariant from TestSupport.hpp is violated
6. On failure prints `=== MBT DIVERGENCE ===` with trace, step, action, C++ call,
   state before, expected (Quint), actual (C++) and `json::diff(actual, expected)`.
7. Prints `[   MBT    ] <file>: <n> steps replayed` per trace and a final
   summary with the count of every action.
```

### The Generated Comparison

Comparison goes through the accessors, variable by variable:

```cpp
check(sys.elevators()  == expected.elevators,  "elevators");
check(sys.dispatcher() == expected.dispatcher, "dispatcher");
check(sys.hallLamps()  == expected.hallLamps,  "hallLamps");
check(sys.network()    == expected.network,    "network");
```

---

## 8. Phase 8 — Build and Run the Loop

`CMakeLists.txt` (CMake 3.24+, C++20) fetches dependencies with `FetchContent`:

- `nlohmann_json` 3.12.0 (`json.tar.xz` release asset)
- GoogleTest 1.17.0

| Target           | Sources                    | Links |
|------------------|----------------------------|-------|
| `elevator`       | `src/*.cpp`                | nothing |
| `elevator_tests` | `tests/ElevatorTests.cpp`  | `elevator`, `nlohmann_json`, `GTest::gtest_main` |
| `mbt_driver`     | `tests/MBTDriver.cpp`      | `elevator`, `nlohmann_json`, `GTest::gtest_main` |

`scripts/mbt.sh` runs one iteration: typecheck, `quint test`, `quint run`
on `safety`, regenerate `traces/`, build, run `elevator_tests`, run
`mbt_driver` (log in `build/mbt_last.log`), print `MBT: PASS/FAIL` and
`UNIT: PASS/FAIL`.

```bash
./scripts/mbt.sh
N_TRACES=100 MAX_STEPS=80 SEED=42 ./scripts/mbt.sh
```

---

## 9. Phase 9 — Claude Fixes the C++

### What to ask Claude

```
The MBT driver failed. Here is the log: [paste first === MBT DIVERGENCE === block]
Here is the Quint spec: [paste spec/elevator.qnt]

Find the Quint action named in the report, compare its guard and effect with
the C++ method that was called, and fix the C++ code. Do not modify the spec,
the traces, or the driver.
```

Repeat phases 6 to 9 until both `MBT` and `UNIT` pass. Commit `traces/` with the
code; `build/`, `_apalache-out/`, `.cache/` are ignored.
