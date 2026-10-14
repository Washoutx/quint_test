#include "elevator/elevator_system.hpp"

namespace elevator {

bool ElevatorSystem::receiveHallCall(const HallCall& c) {
    if (!s_.network.contains(HallCallMsg{c})) return false;
    s_.network.erase(HallCallMsg{c});
    auto& d = s_.dispatcher;
    if (!d.pending.contains(c) && !d.assigned.contains(c)) d.pending.insert(c);
    return true;
}

bool ElevatorSystem::assignTo(const HallCall& c, const ElevatorId& e) {
    auto& d = s_.dispatcher;
    if (!d.pending.contains(c) || !closestElevators(cfg_, d, c.floor).contains(e)) return false;
    d.pending.erase(c);
    d.assigned[c] = e;
    s_.network.insert(AssignMsg{e, c});
    return true;
}

bool ElevatorSystem::receiveStatus(const StatusMsg& s) {
    if (!s_.network.contains(s)) return false;
    s_.network.erase(s);
    auto& report = s_.dispatcher.known.at(s.elevator);
    if (s.seq > report.seq) report = PositionReport{.floor = s.floor, .seq = s.seq};
    return true;
}

bool ElevatorSystem::receiveServed(const ServedMsg& s) {
    if (!s_.network.contains(s)) return false;
    s_.network.erase(s);
    s_.dispatcher.assigned.erase(s.call);
    s_.hallLamps.erase(s.call);
    return true;
}

}  // namespace elevator
