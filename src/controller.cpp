#include "elevator/elevator_system.hpp"

namespace elevator {

bool ElevatorSystem::receiveAssign(const AssignMsg& a) {
    if (!s_.network.contains(a) || !hasElevator(a.elevator)) return false;
    s_.network.erase(a);
    s_.elevators.at(a.elevator).hallStops.insert(a.call);
    return true;
}

bool ElevatorSystem::departure(const ElevatorId& e) {
    if (!hasElevator(e)) return false;
    auto& es = s_.elevators.at(e);
    const auto ts = targets(es);
    if (es.motor != Motor::Stopped || es.door != Door::Closed || ts.empty() || ts.contains(es.floor)) {
        return false;
    }
    es.heading = nextDir(es) == Dir::Up ? Heading::GoingUp : Heading::GoingDown;
    es.motor = Motor::Running;
    return true;
}

// No shaft-bounds guard on purpose: a running car always has a target ahead,
// so it never moves past the last floor.
bool ElevatorSystem::travel(const ElevatorId& e) {
    if (!hasElevator(e)) return false;
    auto& es = s_.elevators.at(e);
    if (es.motor != Motor::Running) return false;
    const int newFloor = es.floor + delta(es.heading);
    const bool arrive = targets(es).contains(newFloor);
    es.floor = newFloor;
    es.motor = arrive ? Motor::Stopped : Motor::Running;
    es.statusSeq += 1;
    s_.network.insert(StatusMsg{e, newFloor, es.statusSeq});
    return true;
}

bool ElevatorSystem::openDoor(const ElevatorId& e) {
    if (!hasElevator(e)) return false;
    auto& es = s_.elevators.at(e);
    if (es.motor != Motor::Stopped || es.door != Door::Closed || !targets(es).contains(es.floor)) {
        return false;
    }
    std::set<HallCall> served;
    for (const auto& c : es.hallStops) {
        if (c.floor == es.floor) served.insert(c);
    }
    es.carCalls.erase(es.floor);
    for (const auto& c : served) {
        es.hallStops.erase(c);
        s_.network.insert(ServedMsg{e, c});
    }
    es.door = Door::Open;
    if (targets(es).empty()) es.heading = Heading::Idle;
    return true;
}

bool ElevatorSystem::closeDoor(const ElevatorId& e) {
    if (!hasElevator(e)) return false;
    auto& es = s_.elevators.at(e);
    if (es.door != Door::Open) return false;
    es.door = Door::Closed;
    return true;
}

}  // namespace elevator
