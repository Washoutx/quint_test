#pragma once

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

#include "elevator.hpp"

namespace elevator::test {

// Safety invariants from spec/elevator.qnt. Returns the names of the violated
// ones; empty means `safety` holds.
inline std::vector<std::string> violatedInvariants(const ElevatorState& s) {
    std::vector<std::string> violated;
    if (s.floor < BOTTOM || s.floor > TOP) violated.emplace_back("floorInRange");
    if (std::any_of(s.requests.begin(), s.requests.end(), [](int r) { return r < BOTTOM || r > TOP; })) {
        violated.emplace_back("requestsInRange");
    }
    if (s.door == Door::Open && s.requests.contains(s.floor)) violated.emplace_back("openDoorFloorIsServed");
    return violated;
}

inline std::string toString(const ElevatorState& s) {
    std::ostringstream out;
    out << "{ floor: " << s.floor << ", door: " << (s.door == Door::Open ? "Open" : "Closed") << ", direction: "
        << (s.direction == Direction::Up ? "Up" : s.direction == Direction::Down ? "Down" : "Idle")
        << ", requests: {";
    for (auto it = s.requests.begin(); it != s.requests.end(); ++it) {
        out << (it == s.requests.begin() ? "" : ", ") << *it;
    }
    out << "} }";
    return out.str();
}

}  // namespace elevator::test
