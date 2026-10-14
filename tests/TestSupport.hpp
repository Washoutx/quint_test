#pragma once

#include <algorithm>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "elevator/elevator_system.hpp"

namespace elevator::test {

// ---------------------------------------------------------------------------
// Safety invariants from spec/elevator.qnt
// ---------------------------------------------------------------------------

inline bool doorsClosedWhileRunning(const Config&, const SystemState& s) {
    return std::all_of(s.elevators.begin(), s.elevators.end(), [](const auto& kv) {
        return kv.second.motor != Motor::Running || kv.second.door == Door::Closed;
    });
}

inline bool staysInShaft(const Config& cfg, const SystemState& s) {
    return std::all_of(s.elevators.begin(), s.elevators.end(), [&](const auto& kv) {
        return kv.second.floor >= cfg.bottom() && kv.second.floor <= cfg.top();
    });
}

inline bool runningHasHeading(const Config&, const SystemState& s) {
    return std::all_of(s.elevators.begin(), s.elevators.end(), [](const auto& kv) {
        return kv.second.motor != Motor::Running || kv.second.heading != Heading::Idle;
    });
}

inline bool runningHasTargetAhead(const Config&, const SystemState& s) {
    return std::all_of(s.elevators.begin(), s.elevators.end(), [](const auto& kv) {
        const auto& es = kv.second;
        if (es.motor != Motor::Running) return true;
        const auto ts = targets(es);
        return std::any_of(ts.begin(), ts.end(), [&](int t) { return (t - es.floor) * delta(es.heading) > 0; });
    });
}

inline bool noDoubleService(const Config& cfg, const SystemState& s) {
    for (const auto& c : allHallCalls(cfg)) {
        int owners = 0;
        for (const auto& [e, es] : s.elevators) {
            if (es.hallStops.contains(c) || s.network.contains(AssignMsg{e, c})) ++owners;
        }
        if (owners > 1) return false;
    }
    return true;
}

inline bool assignmentConsistent(const Config&, const SystemState& s) {
    for (const auto& [e, es] : s.elevators) {
        for (const auto& c : es.hallStops) {
            auto it = s.dispatcher.assigned.find(c);
            if (it == s.dispatcher.assigned.end() || it->second != e) return false;
        }
    }
    return true;
}

inline bool noLostHallCall(const Config&, const SystemState& s) {
    return std::all_of(s.hallLamps.begin(), s.hallLamps.end(), [&](const HallCall& c) {
        const int stages = int(s.network.contains(HallCallMsg{c})) + int(s.dispatcher.pending.contains(c)) +
                           int(s.dispatcher.assigned.contains(c));
        return stages == 1;
    });
}

inline bool assignedCallIsTracked(const Config&, const SystemState& s) {
    for (const auto& [c, e] : s.dispatcher.assigned) {
        auto it = s.elevators.find(e);
        const bool held = it != s.elevators.end() && it->second.hallStops.contains(c);
        if (!s.network.contains(AssignMsg{e, c}) && !held && !s.network.contains(ServedMsg{e, c})) return false;
    }
    return true;
}

inline bool noTrackingWithoutLamp(const Config&, const SystemState& s) {
    for (const auto& c : s.dispatcher.pending) {
        if (!s.hallLamps.contains(c)) return false;
    }
    for (const auto& [c, _] : s.dispatcher.assigned) {
        if (!s.hallLamps.contains(c)) return false;
    }
    return true;
}

// Names of the violated invariants; empty means `safety` holds.
inline std::vector<std::string> violatedInvariants(const Config& cfg, const SystemState& s) {
    using Check = bool (*)(const Config&, const SystemState&);
    static const std::pair<const char*, Check> checks[] = {
        {"doorsClosedWhileRunning", doorsClosedWhileRunning},
        {"staysInShaft", staysInShaft},
        {"runningHasHeading", runningHasHeading},
        {"runningHasTargetAhead", runningHasTargetAhead},
        {"noDoubleService", noDoubleService},
        {"assignmentConsistent", assignmentConsistent},
        {"noLostHallCall", noLostHallCall},
        {"assignedCallIsTracked", assignedCallIsTracked},
        {"noTrackingWithoutLamp", noTrackingWithoutLamp},
    };
    std::vector<std::string> violated;
    for (const auto& [name, check] : checks) {
        if (!check(cfg, s)) violated.emplace_back(name);
    }
    return violated;
}

// ---------------------------------------------------------------------------
// Human-readable JSON of the state, used in failure reports
// ---------------------------------------------------------------------------

using nlohmann::json;

inline const char* name(Dir d) { return d == Dir::Up ? "Up" : "Down"; }

inline const char* name(Heading h) {
    switch (h) {
        case Heading::GoingUp:
            return "Going(Up)";
        case Heading::GoingDown:
            return "Going(Down)";
        case Heading::Idle:
            return "Idle";
    }
    return "?";
}

inline json toJson(const HallCall& c) { return {{"floor", c.floor}, {"dir", name(c.dir)}}; }

inline json toJson(const Message& m) {
    return std::visit(
        [](const auto& msg) -> json {
            using T = std::decay_t<decltype(msg)>;
            if constexpr (std::is_same_v<T, HallCallMsg>) return {{"HallCallMsg", toJson(msg.call)}};
            if constexpr (std::is_same_v<T, AssignMsg>) {
                return {{"AssignMsg", {{"elevator", msg.elevator}, {"call", toJson(msg.call)}}}};
            }
            if constexpr (std::is_same_v<T, StatusMsg>) {
                return {{"StatusMsg", {{"elevator", msg.elevator}, {"floor", msg.floor}, {"seq", msg.seq}}}};
            }
            if constexpr (std::is_same_v<T, ServedMsg>) {
                return {{"ServedMsg", {{"elevator", msg.elevator}, {"call", toJson(msg.call)}}}};
            }
        },
        m);
}

inline json toJson(const SystemState& s) {
    json elevators = json::object();
    for (const auto& [e, es] : s.elevators) {
        json hallStops = json::array();
        for (const auto& c : es.hallStops) hallStops.push_back(toJson(c));
        elevators[e] = {
            {"floor", es.floor},
            {"door", es.door == Door::Open ? "Open" : "Closed"},
            {"motor", es.motor == Motor::Running ? "Running" : "Stopped"},
            {"heading", name(es.heading)},
            {"carCalls", es.carCalls},
            {"hallStops", hallStops},
            {"statusSeq", es.statusSeq},
        };
    }

    json pending = json::array();
    for (const auto& c : s.dispatcher.pending) pending.push_back(toJson(c));
    json assigned = json::array();
    for (const auto& [c, e] : s.dispatcher.assigned) assigned.push_back({{"call", toJson(c)}, {"elevator", e}});
    json known = json::object();
    for (const auto& [e, r] : s.dispatcher.known) known[e] = {{"floor", r.floor}, {"seq", r.seq}};

    json lamps = json::array();
    for (const auto& c : s.hallLamps) lamps.push_back(toJson(c));
    json network = json::array();
    for (const auto& m : s.network) network.push_back(toJson(m));

    return {
        {"elevators", elevators},
        {"dispatcher", {{"pending", pending}, {"assigned", assigned}, {"known", known}}},
        {"hallLamps", lamps},
        {"network", network},
    };
}

}  // namespace elevator::test
