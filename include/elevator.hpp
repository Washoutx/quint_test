#pragma once

#include <set>

namespace elevator {

constexpr int BOTTOM = 0;
constexpr int TOP = 4;

enum class Direction { Up, Down, Idle };

enum class Door { Open, Closed };

struct ElevatorState {
    int floor = BOTTOM;
    Door door = Door::Closed;
    Direction direction = Direction::Idle;
    std::set<int> requests;

    bool operator==(const ElevatorState&) const = default;
};

Direction nextDir(int floor, Direction direction, const std::set<int>& requests);

// Every action mirrors the Quint action of the same name: if its guard does not
// hold it returns false and leaves the state untouched.
class Elevator {
public:
    Elevator() { init(); }

    const ElevatorState& state() const { return s_; }

    void init();
    bool request(int f);
    bool openDoor();
    bool closeDoor();
    bool move();

private:
    ElevatorState s_;
};

}  // namespace elevator
