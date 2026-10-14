#pragma once

#include <compare>
#include <map>
#include <set>
#include <string>
#include <variant>
#include <vector>

namespace elevator {

using ElevatorId = std::string;

enum class Dir { Up, Down };

// Quint: Heading = Going(Dir) | Idle
enum class Heading { GoingUp, GoingDown, Idle };

enum class Door { Open, Closed };

enum class Motor { Stopped, Running };

struct HallCall {
    int floor = 0;
    Dir dir = Dir::Up;

    auto operator<=>(const HallCall&) const = default;
};

struct ElevatorState {
    int floor = 0;
    Door door = Door::Closed;
    Motor motor = Motor::Stopped;
    Heading heading = Heading::Idle;
    std::set<int> carCalls;
    std::set<HallCall> hallStops;
    int statusSeq = 0;

    bool operator==(const ElevatorState&) const = default;
};

struct PositionReport {
    int floor = 0;
    int seq = 0;

    bool operator==(const PositionReport&) const = default;
};

struct DispatcherState {
    std::set<HallCall> pending;
    std::map<HallCall, ElevatorId> assigned;
    std::map<ElevatorId, PositionReport> known;

    bool operator==(const DispatcherState&) const = default;
};

struct HallCallMsg {
    HallCall call;
    auto operator<=>(const HallCallMsg&) const = default;
};

struct AssignMsg {
    ElevatorId elevator;
    HallCall call;
    auto operator<=>(const AssignMsg&) const = default;
};

struct StatusMsg {
    ElevatorId elevator;
    int floor = 0;
    int seq = 0;
    auto operator<=>(const StatusMsg&) const = default;
};

struct ServedMsg {
    ElevatorId elevator;
    HallCall call;
    auto operator<=>(const ServedMsg&) const = default;
};

using Message = std::variant<HallCallMsg, AssignMsg, StatusMsg, ServedMsg>;

struct SystemState {
    std::map<ElevatorId, ElevatorState> elevators;
    DispatcherState dispatcher;
    std::set<HallCall> hallLamps;
    std::set<Message> network;

    bool operator==(const SystemState&) const = default;
};

struct Config {
    int numFloors = 4;
    std::vector<ElevatorId> elevators = {"A", "B"};

    int bottom() const { return 0; }
    int top() const { return numFloors - 1; }
};

}  // namespace elevator
