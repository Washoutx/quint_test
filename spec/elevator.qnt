// -*- mode: Bluespec; -*-
/**
 * Elevator group control system.
 *
 * A building has NUM_FLOORS floors and a group of elevators. On every floor
 * there are hall buttons (Up / Down). Inside every car there is a panel with
 * one button per floor (car calls).
 *
 * Components:
 *   - Hall panels: when a passenger presses a hall button, its lamp lights up
 *     and a HallCall message is sent to the dispatcher.
 *   - Dispatcher: collects hall calls and assigns each of them to exactly one
 *     elevator, choosing the one that is (according to its possibly stale
 *     knowledge) closest to the calling floor.
 *   - Elevators: each car runs its own controller. It receives assignments,
 *     moves floor by floor using the SCAN ("elevator") algorithm, opens its
 *     doors at target floors and reports its position and served calls back
 *     to the dispatcher.
 *
 * Communication between the dispatcher and the elevators is asynchronous:
 * messages are kept in a set and may be delivered in any order and with any
 * delay. Messages are never lost.
 *
 * Car calls are handled locally by the car (they never go through the
 * dispatcher), exactly as in real installations.
 */
module elevator {

  // ---------------------------------------------------------------------------
  // Parameters
  // ---------------------------------------------------------------------------

  pure val NUM_FLOORS = 4
  pure val BOTTOM = 0
  pure val TOP = NUM_FLOORS - 1
  pure val FLOORS = BOTTOM.to(TOP)
  pure val ELEVATORS = Set("A", "B")

  // ---------------------------------------------------------------------------
  // Types
  // ---------------------------------------------------------------------------

  type Dir = Up | Down

  // Direction the car (elevator cabin) is committed to. It is set on departure
  // and reset to Idle when the car runs out of work. An Idle car may already
  // have new targets that arrived while it was stopped.
  type Heading = Going(Dir) | Idle

  type Door = Open | Closed

  type Motor = Stopped | Running

  // A request made from a hall panel: "I am on floor `floor` and want to go `dir`".
  type HallCall = { floor: int, dir: Dir }

  type ElevatorState = {
    // Current floor. While running, this is the last floor the car passed.
    floor: int,
    door: Door,
    motor: Motor,
    heading: Heading,
    // Floors requested from inside the car.
    carCalls: Set[int],
    // Hall calls assigned to this car by the dispatcher and not yet served.
    hallStops: Set[HallCall],
    // Sequence number of the last status report, used to discard stale reports.
    statusSeq: int,
  }

  // The dispatcher's (possibly outdated) view of an elevator's position.
  type PositionReport = { floor: int, seq: int }

  type DispatcherState = {
    // Hall calls received but not yet assigned to any elevator.
    pending: Set[HallCall],
    // Hall calls assigned to an elevator and not yet confirmed as served.
    assigned: HallCall -> str,
    // Last known position of every elevator.
    known: str -> PositionReport,
  }

  type Message =
    // Hall panel -> dispatcher
    | HallCallMsg(HallCall)
    // Dispatcher -> elevator
    | AssignMsg({ elevator: str, call: HallCall })
    // Elevator -> dispatcher
    | StatusMsg({ elevator: str, floor: int, seq: int })
    // Elevator -> dispatcher
    | ServedMsg({ elevator: str, call: HallCall })

  // ---------------------------------------------------------------------------
  // State variables
  // ---------------------------------------------------------------------------

  var elevators: str -> ElevatorState
  var dispatcher: DispatcherState
  // Hall button lamps that are currently lit.
  var hallLamps: Set[HallCall]
  // Messages in flight.
  var network: Set[Message]

  // ---------------------------------------------------------------------------
  // Pure helpers
  // ---------------------------------------------------------------------------

  pure def abs(x: int): int = if (x < 0) 0 - x else x

  // There is no Down button on the bottom floor and no Up button on the top floor.
  pure def isValidHallCall(c: HallCall): bool =
    FLOORS.contains(c.floor)
      and not(c.floor == TOP and c.dir == Up)
      and not(c.floor == BOTTOM and c.dir == Down)

  pure val HALL_CALLS: Set[HallCall] =
    FLOORS
      .map(f => Set({ floor: f, dir: Up }, { floor: f, dir: Down }))
      .flatten()
      .filter(isValidHallCall)

  // All floors at which the car has to stop.
  pure def targets(es: ElevatorState): Set[int] =
    es.carCalls.union(es.hallStops.map(c => c.floor))

  // SCAN: keep going in the current direction while there is work ahead,
  // otherwise reverse. Only meaningful when the car has a target elsewhere.
  pure def nextDir(es: ElevatorState): Dir = {
    val ts = targets(es)
    val above = ts.exists(t => t > es.floor)
    val below = ts.exists(t => t < es.floor)
    if (es.heading == Going(Up) and above) Up
    else if (es.heading == Going(Down) and below) Down
    else if (above) Up
    else Down
  }

  pure def delta(h: Heading): int =
    match h {
      | Going(d) => if (d == Up) 1 else -1
      | Idle => 0
    }

  // Removes a key from a map.
  pure def without(m: HallCall -> str, k: HallCall): HallCall -> str =
    m.keys().exclude(Set(k)).mapBy(x => m.get(x))

  // Elevators with the minimal distance (by last known position) to a floor.
  pure def closestElevators(d: DispatcherState, floor: int): Set[str] =
    ELEVATORS.filter(e =>
      ELEVATORS.forall(o =>
        abs(d.known.get(e).floor - floor) <= abs(d.known.get(o).floor - floor)
      )
    )

  // ---------------------------------------------------------------------------
  // Initial state
  // ---------------------------------------------------------------------------

  action init = all {
    elevators' = ELEVATORS.mapBy(_ => {
      floor: BOTTOM,
      door: Closed,
      motor: Stopped,
      heading: Idle,
      carCalls: Set(),
      hallStops: Set(),
      statusSeq: 0,
    }),
    dispatcher' = {
      pending: Set(),
      assigned: Map(),
      known: ELEVATORS.mapBy(_ => { floor: BOTTOM, seq: 0 }),
    },
    hallLamps' = Set(),
    network' = Set(),
  }

  // ---------------------------------------------------------------------------
  // Environment: passengers
  // ---------------------------------------------------------------------------

  // A passenger presses a hall button. Pressing an already lit button has no effect.
  action pressHallButton(c: HallCall): bool = all {
    HALL_CALLS.contains(c),
    not(hallLamps.contains(c)),
    hallLamps' = hallLamps.union(Set(c)),
    network' = network.union(Set(HallCallMsg(c))),
    elevators' = elevators,
    dispatcher' = dispatcher,
  }

  // A passenger inside car `e` presses the button for floor `f`.
  action pressCarButton(e: str, f: int): bool = {
    val es = elevators.get(e)
    all {
      FLOORS.contains(f),
      f != es.floor,
      not(es.carCalls.contains(f)),
      elevators' = elevators.set(e, { ...es, carCalls: es.carCalls.union(Set(f)) }),
      dispatcher' = dispatcher,
      hallLamps' = hallLamps,
      network' = network,
    }
  }

  // ---------------------------------------------------------------------------
  // Dispatcher
  // ---------------------------------------------------------------------------

  action receiveHallCall(c: HallCall): bool = {
    val known = dispatcher.pending.contains(c) or dispatcher.assigned.keys().contains(c)
    all {
      network.contains(HallCallMsg(c)),
      network' = network.exclude(Set(HallCallMsg(c))),
      dispatcher' = if (known) dispatcher
                    else { ...dispatcher, pending: dispatcher.pending.union(Set(c)) },
      elevators' = elevators,
      hallLamps' = hallLamps,
    }
  }

  // Assigns a pending call to a specific elevator. Only the closest elevators qualify.
  action assignTo(c: HallCall, e: str): bool = all {
    dispatcher.pending.contains(c),
    closestElevators(dispatcher, c.floor).contains(e),
    dispatcher' = {
      ...dispatcher,
      pending: dispatcher.pending.exclude(Set(c)),
      assigned: dispatcher.assigned.put(c, e),
    },
    network' = network.union(Set(AssignMsg({ elevator: e, call: c }))),
    elevators' = elevators,
    hallLamps' = hallLamps,
  }

  // Ties between equally close elevators are resolved non-deterministically.
  action assignCall(c: HallCall): bool = {
    nondet target = closestElevators(dispatcher, c.floor).oneOf()
    assignTo(c, target)
  }

  action receiveStatus(s: { elevator: str, floor: int, seq: int }): bool = {
    val fresh = s.seq > dispatcher.known.get(s.elevator).seq
    all {
      network.contains(StatusMsg(s)),
      network' = network.exclude(Set(StatusMsg(s))),
      dispatcher' = if (fresh)
                      { ...dispatcher,
                        known: dispatcher.known.set(s.elevator, { floor: s.floor, seq: s.seq }) }
                    else dispatcher,
      elevators' = elevators,
      hallLamps' = hallLamps,
    }
  }

  // The call is confirmed as served: forget it and switch the hall lamp off.
  action receiveServed(s: { elevator: str, call: HallCall }): bool = all {
    network.contains(ServedMsg(s)),
    network' = network.exclude(Set(ServedMsg(s))),
    dispatcher' = { ...dispatcher, assigned: without(dispatcher.assigned, s.call) },
    hallLamps' = hallLamps.exclude(Set(s.call)),
    elevators' = elevators,
  }

  // ---------------------------------------------------------------------------
  // Elevator controller
  // ---------------------------------------------------------------------------

  action receiveAssign(a: { elevator: str, call: HallCall }): bool = {
    val es = elevators.get(a.elevator)
    all {
      network.contains(AssignMsg(a)),
      network' = network.exclude(Set(AssignMsg(a))),
      elevators' = elevators.set(a.elevator,
                                 { ...es, hallStops: es.hallStops.union(Set(a.call)) }),
      dispatcher' = dispatcher,
      hallLamps' = hallLamps,
    }
  }

  // The car starts moving towards its next target. Work on the current floor
  // must be done first (see openDoor).
  action departure(e: str): bool = {
    val es = elevators.get(e)
    val ts = targets(es)
    all {
      es.motor == Stopped,
      es.door == Closed,
      ts != Set(),
      not(ts.contains(es.floor)),
      elevators' = elevators.set(e, { ...es, motor: Running, heading: Going(nextDir(es)) }),
      dispatcher' = dispatcher,
      hallLamps' = hallLamps,
      network' = network,
    }
  }

  // The car moves by one floor. If the new floor is a target, it stops there.
  // Every floor change is reported to the dispatcher.
  action travel(e: str): bool = {
    val es = elevators.get(e)
    val newFloor = es.floor + delta(es.heading)
    val seq = es.statusSeq + 1
    all {
      es.motor == Running,
      elevators' = elevators.set(e, {
        ...es,
        floor: newFloor,
        motor: if (targets(es).contains(newFloor)) Stopped else Running,
        statusSeq: seq,
      }),
      network' = network.union(Set(StatusMsg({ elevator: e, floor: newFloor, seq: seq }))),
      dispatcher' = dispatcher,
      hallLamps' = hallLamps,
    }
  }

  // The car opens its doors at a target floor. All car calls and hall calls
  // for this floor are served at once.
  action openDoor(e: str): bool = {
    val es = elevators.get(e)
    val served = es.hallStops.filter(c => c.floor == es.floor)
    val remaining = { ...es,
                      carCalls: es.carCalls.exclude(Set(es.floor)),
                      hallStops: es.hallStops.exclude(served) }
    all {
      es.motor == Stopped,
      es.door == Closed,
      targets(es).contains(es.floor),
      elevators' = elevators.set(e, {
        ...remaining,
        door: Open,
        heading: if (targets(remaining) == Set()) Idle else es.heading,
      }),
      network' = network.union(served.map(c => ServedMsg({ elevator: e, call: c }))),
      dispatcher' = dispatcher,
      hallLamps' = hallLamps,
    }
  }

  action closeDoor(e: str): bool = {
    val es = elevators.get(e)
    all {
      es.door == Open,
      elevators' = elevators.set(e, { ...es, door: Closed }),
      dispatcher' = dispatcher,
      hallLamps' = hallLamps,
      network' = network,
    }
  }

  // ---------------------------------------------------------------------------
  // Network
  // ---------------------------------------------------------------------------

  action deliver(m: Message): bool =
    match m {
      | HallCallMsg(c) => receiveHallCall(c)
      | AssignMsg(a) => receiveAssign(a)
      | StatusMsg(s) => receiveStatus(s)
      | ServedMsg(s) => receiveServed(s)
    }

  action deliverAny: bool = all {
    network != Set(),
    nondet m = network.oneOf()
    deliver(m),
  }

  // ---------------------------------------------------------------------------
  // Transition relation
  // ---------------------------------------------------------------------------

  action step = {
    nondet e = ELEVATORS.oneOf()
    nondet f = FLOORS.oneOf()
    nondet c = HALL_CALLS.oneOf()
    any {
      pressHallButton(c),
      pressCarButton(e, f),
      assignCall(c),
      departure(e),
      travel(e),
      openDoor(e),
      closeDoor(e),
      deliverAny,
    }
  }

  // ---------------------------------------------------------------------------
  // Safety invariants
  // ---------------------------------------------------------------------------

  // A car never moves with open doors.
  val doorsClosedWhileRunning =
    ELEVATORS.forall(e => elevators.get(e).motor == Running implies elevators.get(e).door == Closed)

  // A car never leaves the shaft.
  val staysInShaft =
    ELEVATORS.forall(e => FLOORS.contains(elevators.get(e).floor))

  // A running car always has a direction.
  val runningHasHeading =
    ELEVATORS.forall(e => elevators.get(e).motor == Running implies elevators.get(e).heading != Idle)

  // A running car always has a target ahead of it, so it never runs aimlessly.
  val runningHasTargetAhead =
    ELEVATORS.forall(e => {
      val es = elevators.get(e)
      es.motor == Running implies targets(es).exists(t => (t - es.floor) * delta(es.heading) > 0)
    })

  // Every hall call is owned by at most one elevator (in flight or accepted).
  val noDoubleService =
    HALL_CALLS.forall(c => {
      val owners = ELEVATORS.filter(e =>
        elevators.get(e).hallStops.contains(c)
          or network.contains(AssignMsg({ elevator: e, call: c }))
      )
      owners.size() <= 1
    })

  // The dispatcher's assignment always matches the elevator that owns the call.
  val assignmentConsistent =
    ELEVATORS.forall(e =>
      elevators.get(e).hallStops.forall(c =>
        dispatcher.assigned.keys().contains(c) and dispatcher.assigned.get(c) == e
      )
    )

  // No lost calls: every lit hall lamp is tracked by exactly one stage of the
  // pipeline (message to the dispatcher, pending, or assigned).
  val noLostHallCall =
    hallLamps.forall(c => {
      val inFlight = if (network.contains(HallCallMsg(c))) 1 else 0
      val pending = if (dispatcher.pending.contains(c)) 1 else 0
      val assigned = if (dispatcher.assigned.keys().contains(c)) 1 else 0
      inFlight + pending + assigned == 1
    })

  // Every assigned call is on its way to, held by, or confirmed by its elevator.
  val assignedCallIsTracked =
    dispatcher.assigned.keys().forall(c => {
      val e = dispatcher.assigned.get(c)
      network.contains(AssignMsg({ elevator: e, call: c }))
        or elevators.get(e).hallStops.contains(c)
        or network.contains(ServedMsg({ elevator: e, call: c }))
    })

  // A lamp is switched off only after the call has been served.
  val noTrackingWithoutLamp =
    dispatcher.pending.union(dispatcher.assigned.keys()).forall(c => hallLamps.contains(c))

  val safety = and {
    doorsClosedWhileRunning,
    staysInShaft,
    runningHasHeading,
    runningHasTargetAhead,
    noDoubleService,
    assignmentConsistent,
    noLostHallCall,
    assignedCallIsTracked,
    noTrackingWithoutLamp,
  }

  // ---------------------------------------------------------------------------
  // Liveness (requires fairness assumptions; check with `quint verify`)
  // ---------------------------------------------------------------------------

  temporal hallCallsEventuallyServed =
    HALL_CALLS.forall(c => always(hallLamps.contains(c) implies eventually(not(hallLamps.contains(c)))))

  temporal carCallsEventuallyServed =
    ELEVATORS.forall(e =>
      FLOORS.forall(f =>
        always(elevators.get(e).carCalls.contains(f)
               implies eventually(not(elevators.get(e).carCalls.contains(f))))
      )
    )

  // ---------------------------------------------------------------------------
  // Witnesses: these are expected to be violated. Running
  //   quint run elevator.qnt --invariant=<witness>
  // produces an example trace that reaches the interesting situation.
  // ---------------------------------------------------------------------------

  // Both cars are moving at the same time.
  val witnessBothRunning =
    not(ELEVATORS.forall(e => elevators.get(e).motor == Running))

  // Some elevator has served a hall call and is reporting it to the dispatcher.
  val witnessHallCallServed =
    not(network.exists(m => match m { | ServedMsg(_) => true | _ => false }))

  // The dispatcher acts on a stale position of some elevator.
  val witnessStaleKnowledge =
    ELEVATORS.forall(e => dispatcher.known.get(e).floor == elevators.get(e).floor)

  // ---------------------------------------------------------------------------
  // Tests
  // ---------------------------------------------------------------------------

  pure val call2Down = { floor: 2, dir: Down }

  // A single hall call travels through the whole pipeline.
  run hallCallLifecycleTest =
    init
      .then(pressHallButton(call2Down))
      .then(deliver(HallCallMsg(call2Down)))
      .expect(dispatcher.pending == Set(call2Down))
      .then(assignTo(call2Down, "A"))
      .then(deliver(AssignMsg({ elevator: "A", call: call2Down })))
      .then(departure("A"))
      .expect(elevators.get("A").heading == Going(Up))
      .then(travel("A"))
      .expect(elevators.get("A").motor == Running)
      .then(travel("A"))
      .expect(elevators.get("A").floor == 2 and elevators.get("A").motor == Stopped)
      .then(openDoor("A"))
      .expect(elevators.get("A").door == Open and elevators.get("A").heading == Idle)
      .then(deliver(ServedMsg({ elevator: "A", call: call2Down })))
      .expect(hallLamps == Set() and dispatcher.assigned == Map())
      .expect(safety)

  // Pressing a lit hall button does nothing.
  run litButtonIsIgnoredTest =
    init
      .then(pressHallButton(call2Down))
      .then(pressHallButton(call2Down))
      .fail()

  // The car cannot leave while its doors are open.
  run noDepartureWithOpenDoorTest =
    init
      .then(pressCarButton("A", 3))
      .then(departure("A"))
      .then(travel("A"))
      .then(pressCarButton("A", 0))
      .then(travel("A"))
      .then(travel("A"))
      .expect(elevators.get("A").floor == 3)
      .then(openDoor("A"))
      .then(departure("A"))
      .fail()

  // SCAN: a car going up serves the upper floor before reversing.
  run scanKeepsDirectionTest =
    init
      .then(pressCarButton("A", 2))
      .then(departure("A"))
      .then(travel("A"))
      .then(pressCarButton("A", 0))
      .then(travel("A"))
      .then(openDoor("A"))
      .then(closeDoor("A"))
      .then(departure("A"))
      .expect(elevators.get("A").heading == Going(Down))
      .expect(safety)

  // A hall call is assigned to the closest elevator according to the dispatcher.
  run closestElevatorIsChosenTest =
    init
      .then(pressCarButton("B", 3))
      .then(departure("B"))
      .then(travel("B"))
      .then(deliver(StatusMsg({ elevator: "B", floor: 1, seq: 1 })))
      .then(pressHallButton({ floor: 2, dir: Up }))
      .then(deliver(HallCallMsg({ floor: 2, dir: Up })))
      .then(assignTo({ floor: 2, dir: Up }, "A"))
      .fail()
}
