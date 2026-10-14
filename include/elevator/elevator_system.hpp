#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

#include "elevator/types.hpp"

namespace elevator {

// Pure helpers (Quint `pure def`).
int delta(Heading h);
bool isValidHallCall(const Config& cfg, const HallCall& c);
std::vector<HallCall> allHallCalls(const Config& cfg);
std::set<int> targets(const ElevatorState& es);
Dir nextDir(const ElevatorState& es);
std::set<ElevatorId> closestElevators(const Config& cfg, const DispatcherState& d, int floor);

// The elevator group control system. Every public action mirrors the Quint
// action of the same name: if its guard does not hold it returns false and
// leaves the state untouched, otherwise it applies the effect and returns true.
class ElevatorSystem {
public:
    explicit ElevatorSystem(Config cfg = {});

    const Config& config() const { return cfg_; }
    const SystemState& state() const { return s_; }

    // One accessor per Quint state variable, named exactly like the variable.
    const std::map<ElevatorId, ElevatorState>& elevators() const { return s_.elevators; }
    const DispatcherState& dispatcher() const { return s_.dispatcher; }
    const std::set<HallCall>& hallLamps() const { return s_.hallLamps; }
    const std::set<Message>& network() const { return s_.network; }

    // Quint: init
    void init();

    // Quint: pressHallButton(c)
    bool pressHallButton(const HallCall& c);
    // Quint: pressCarButton(e, f)
    bool pressCarButton(const ElevatorId& e, int f);

    // Quint: receiveHallCall(c)
    bool receiveHallCall(const HallCall& c);
    // Quint: assignTo(c, e); assignCall(c) picks e and calls this
    bool assignTo(const HallCall& c, const ElevatorId& e);
    // Quint: receiveStatus(s)
    bool receiveStatus(const StatusMsg& s);
    // Quint: receiveServed(s)
    bool receiveServed(const ServedMsg& s);

    // Quint: receiveAssign(a)
    bool receiveAssign(const AssignMsg& a);
    // Quint: departure(e)
    bool departure(const ElevatorId& e);
    // Quint: travel(e)
    bool travel(const ElevatorId& e);
    // Quint: openDoor(e)
    bool openDoor(const ElevatorId& e);
    // Quint: closeDoor(e)
    bool closeDoor(const ElevatorId& e);

    // Quint: deliver(m); deliverAny picks m and calls this
    bool deliver(const Message& m);

private:
    bool hasElevator(const ElevatorId& e) const;

    Config cfg_;
    SystemState s_;
};

}  // namespace elevator
