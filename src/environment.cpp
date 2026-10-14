#include "elevator/elevator_system.hpp"

namespace elevator {

bool ElevatorSystem::pressHallButton(const HallCall& c) {
    if (!isValidHallCall(cfg_, c) || s_.hallLamps.contains(c)) return false;
    s_.hallLamps.insert(c);
    s_.network.insert(HallCallMsg{c});
    return true;
}

bool ElevatorSystem::pressCarButton(const ElevatorId& e, int f) {
    if (!hasElevator(e)) return false;
    auto& es = s_.elevators.at(e);
    if (f < cfg_.bottom() || f > cfg_.top() || f == es.floor || es.carCalls.contains(f)) return false;
    es.carCalls.insert(f);
    return true;
}

}  // namespace elevator
