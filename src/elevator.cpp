#include "elevator.hpp"

#include <algorithm>

namespace elevator {

Direction nextDir(int floor, Direction direction, const std::set<int>& requests) {
    const bool above = std::any_of(requests.begin(), requests.end(), [&](int r) { return r > floor; });
    const bool below = std::any_of(requests.begin(), requests.end(), [&](int r) { return r < floor; });
    if (direction == Direction::Up && above) return Direction::Up;
    if (direction == Direction::Down && below) return Direction::Down;
    if (above) return Direction::Up;
    return Direction::Down;
}

void Elevator::init() { s_ = ElevatorState{}; }

bool Elevator::request(int f) {
    if (f < BOTTOM || f > TOP || s_.requests.contains(f)) return false;
    if (f == s_.floor && s_.door == Door::Open) return false;
    s_.requests.insert(f);
    return true;
}

bool Elevator::openDoor() {
    if (s_.door != Door::Closed || !s_.requests.contains(s_.floor)) return false;
    s_.door = Door::Open;
    s_.requests.erase(s_.floor);
    if (s_.requests.empty()) s_.direction = Direction::Idle;
    return true;
}

bool Elevator::closeDoor() {
    if (s_.door != Door::Open) return false;
    s_.door = Door::Closed;
    return true;
}

// No bounds check on purpose: SCAN only ever moves towards an existing request.
bool Elevator::move() {
    if (s_.door != Door::Closed || s_.requests.empty() || s_.requests.contains(s_.floor)) return false;
    s_.direction = nextDir(s_.floor, s_.direction, s_.requests);
    s_.floor += s_.direction == Direction::Up ? 1 : -1;
    return true;
}

}  // namespace elevator
