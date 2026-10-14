#include "elevator/elevator_system.hpp"

#include <algorithm>
#include <cstdlib>
#include <utility>

namespace elevator {

int delta(Heading h) {
    switch (h) {
        case Heading::GoingUp:
            return 1;
        case Heading::GoingDown:
            return -1;
        case Heading::Idle:
            return 0;
    }
    return 0;
}

bool isValidHallCall(const Config& cfg, const HallCall& c) {
    return c.floor >= cfg.bottom() && c.floor <= cfg.top() &&
           !(c.floor == cfg.top() && c.dir == Dir::Up) &&
           !(c.floor == cfg.bottom() && c.dir == Dir::Down);
}

std::vector<HallCall> allHallCalls(const Config& cfg) {
    std::vector<HallCall> calls;
    for (int f = cfg.bottom(); f <= cfg.top(); ++f) {
        for (Dir d : {Dir::Up, Dir::Down}) {
            HallCall c{f, d};
            if (isValidHallCall(cfg, c)) calls.push_back(c);
        }
    }
    return calls;
}

std::set<int> targets(const ElevatorState& es) {
    std::set<int> ts = es.carCalls;
    for (const auto& c : es.hallStops) ts.insert(c.floor);
    return ts;
}

Dir nextDir(const ElevatorState& es) {
    const auto ts = targets(es);
    const bool above = std::any_of(ts.begin(), ts.end(), [&](int t) { return t > es.floor; });
    const bool below = std::any_of(ts.begin(), ts.end(), [&](int t) { return t < es.floor; });
    if (es.heading == Heading::GoingUp && above) return Dir::Up;
    if (es.heading == Heading::GoingDown && below) return Dir::Down;
    if (above) return Dir::Up;
    return Dir::Down;
}

std::set<ElevatorId> closestElevators(const Config& cfg, const DispatcherState& d, int floor) {
    auto distance = [&](const ElevatorId& e) { return std::abs(d.known.at(e).floor - floor); };
    std::set<ElevatorId> best;
    for (const auto& e : cfg.elevators) {
        const bool minimal = std::all_of(cfg.elevators.begin(), cfg.elevators.end(),
                                         [&](const ElevatorId& o) { return distance(e) <= distance(o); });
        if (minimal) best.insert(e);
    }
    return best;
}

ElevatorSystem::ElevatorSystem(Config cfg) : cfg_(std::move(cfg)) { init(); }

bool ElevatorSystem::hasElevator(const ElevatorId& e) const { return s_.elevators.contains(e); }

void ElevatorSystem::init() {
    s_ = SystemState{};
    for (const auto& e : cfg_.elevators) {
        s_.elevators[e] = ElevatorState{
            .floor = cfg_.bottom(),
            .door = Door::Closed,
            .motor = Motor::Stopped,
            .heading = Heading::Idle,
            .carCalls = {},
            .hallStops = {},
            .statusSeq = 0,
        };
        s_.dispatcher.known[e] = PositionReport{.floor = cfg_.bottom(), .seq = 0};
    }
}

bool ElevatorSystem::deliver(const Message& m) {
    return std::visit(
        [this](const auto& msg) -> bool {
            using T = std::decay_t<decltype(msg)>;
            if constexpr (std::is_same_v<T, HallCallMsg>) return receiveHallCall(msg.call);
            if constexpr (std::is_same_v<T, AssignMsg>) return receiveAssign(msg);
            if constexpr (std::is_same_v<T, StatusMsg>) return receiveStatus(msg);
            if constexpr (std::is_same_v<T, ServedMsg>) return receiveServed(msg);
        },
        m);
}

}  // namespace elevator
