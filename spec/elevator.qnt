// -*- mode: Bluespec; -*-
/**
 * A single elevator serving floors BOTTOM..TOP.
 *
 * Passengers request floors. The elevator moves one floor per step, opens its
 * door at requested floors, and uses SCAN: it keeps its direction while there
 * are requests ahead and reverses otherwise.
 *
 * See docs/DESIGN.md.
 */
module elevator {

  pure val BOTTOM = 0
  pure val TOP = 4
  pure val FLOORS = BOTTOM.to(TOP)

  type Direction = Up | Down | Idle

  type Door = Open | Closed

  var floor: int
  var door: Door
  // Idle when there was nothing left to do after serving a floor.
  var direction: Direction
  // Requested floors that have not been served yet.
  var requests: Set[int]

  // SCAN: keep going while there is work ahead, otherwise reverse.
  pure def nextDir(f: int, d: Direction, rs: Set[int]): Direction = {
    val above = rs.exists(r => r > f)
    val below = rs.exists(r => r < f)
    if (d == Up and above) Up
    else if (d == Down and below) Down
    else if (above) Up
    else Down
  }

  action init = all {
    floor' = BOTTOM,
    door' = Closed,
    direction' = Idle,
    requests' = Set(),
  }

  action request(f: int): bool = all {
    FLOORS.contains(f),
    not(requests.contains(f)),
    not(f == floor and door == Open),
    requests' = requests.union(Set(f)),
    floor' = floor,
    door' = door,
    direction' = direction,
  }

  action openDoor: bool = {
    val remaining = requests.exclude(Set(floor))
    all {
      door == Closed,
      requests.contains(floor),
      door' = Open,
      requests' = remaining,
      direction' = if (remaining == Set()) Idle else direction,
      floor' = floor,
    }
  }

  action closeDoor: bool = all {
    door == Open,
    door' = Closed,
    floor' = floor,
    direction' = direction,
    requests' = requests,
  }

  // No bounds check on purpose: staying in the building is checked by floorInRange.
  action move: bool = {
    val d = nextDir(floor, direction, requests)
    all {
      door == Closed,
      requests != Set(),
      not(requests.contains(floor)),
      direction' = d,
      floor' = if (d == Up) floor + 1 else floor - 1,
      door' = door,
      requests' = requests,
    }
  }

  action step = {
    nondet f = FLOORS.oneOf()
    any {
      request(f),
      openDoor,
      closeDoor,
      move,
    }
  }

  // ---------------------------------------------------------------------------
  // Invariants
  // ---------------------------------------------------------------------------

  val floorInRange = FLOORS.contains(floor)

  val requestsInRange = requests.subseteq(FLOORS)

  val openDoorFloorIsServed = door == Open implies not(requests.contains(floor))

  val safety = and {
    floorInRange,
    requestsInRange,
    openDoorFloorIsServed,
  }

  // Witness, expected to be violated: the elevator reaches the top floor with the door open.
  val witnessTopOpen = not(floor == TOP and door == Open)

  // ---------------------------------------------------------------------------
  // Tests
  // ---------------------------------------------------------------------------

  run serveOneRequestTest =
    init
      .then(request(3))
      .then(move)
      .then(move)
      .then(move)
      .expect(floor == 3 and direction == Up)
      .then(openDoor)
      .expect(door == Open and requests == Set() and direction == Idle)

  run scanKeepsDirectionTest =
    init
      .then(request(3))
      .then(move)
      .then(request(0))
      .then(move)
      .then(move)
      .expect(floor == 3)
      .then(openDoor)
      .then(closeDoor)
      .then(move)
      .expect(floor == 2 and direction == Down)

  run noMoveWithOpenDoorTest =
    init
      .then(request(1))
      .then(move)
      .then(openDoor)
      .then(request(3))
      .then(move)
      .fail()

  run duplicateRequestIsIgnoredTest =
    init
      .then(request(2))
      .then(request(2))
      .fail()
}
