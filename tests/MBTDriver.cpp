// Model-based test driver: replays ITF traces produced by
//   quint run spec/elevator.qnt --mbt --out-itf=traces/trace_{seq}.itf.json
// against the C++ implementation and compares the full state after every step.

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <vector>

#include "TestSupport.hpp"
#include "elevator.hpp"

#ifndef MBT_DEFAULT_TRACES_DIR
#define MBT_DEFAULT_TRACES_DIR "traces"
#endif

namespace {

namespace fs = std::filesystem;
using nlohmann::json;
using namespace elevator;
using elevator::test::toString;
using elevator::test::violatedInvariants;

// ---------------------------------------------------------------------------
// ITF parsing
// ---------------------------------------------------------------------------

int toInt(const json& j) {
    if (j.is_number_integer()) return j.get<int>();
    return std::stoi(j.at("#bigint").get<std::string>());
}

std::string tagOf(const json& j) { return j.at("tag").get<std::string>(); }

ElevatorState parseState(const json& j) {
    ElevatorState s;
    s.floor = toInt(j.at("floor"));

    const auto door = tagOf(j.at("door"));
    if (door == "Open") s.door = Door::Open;
    else if (door == "Closed") s.door = Door::Closed;
    else throw std::runtime_error("unknown Door: " + door);

    const auto dir = tagOf(j.at("direction"));
    if (dir == "Up") s.direction = Direction::Up;
    else if (dir == "Down") s.direction = Direction::Down;
    else if (dir == "Idle") s.direction = Direction::Idle;
    else throw std::runtime_error("unknown Direction: " + dir);

    for (const auto& r : j.at("requests").at("#set")) s.requests.insert(toInt(r));
    return s;
}

// ---------------------------------------------------------------------------
// Trace discovery
// ---------------------------------------------------------------------------

fs::path tracesDir() {
    if (const char* dir = std::getenv("MBT_TRACES_DIR")) return dir;
    return MBT_DEFAULT_TRACES_DIR;
}

std::vector<std::string> discoverTraces() {
    std::vector<std::string> traces;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(tracesDir(), ec)) {
        if (entry.is_regular_file() && entry.path().filename().string().ends_with(".itf.json")) {
            traces.push_back(entry.path().string());
        }
    }
    std::sort(traces.begin(), traces.end());
    return traces;
}

// ---------------------------------------------------------------------------
// Replay
// ---------------------------------------------------------------------------

// Translates one ITF transition into the corresponding Elevator call.
bool applyStep(Elevator& e, const std::string& action, const json& picks, std::string& call) {
    if (action == "request") {
        const int f = toInt(picks.at("f").at("value"));
        call = "request(" + std::to_string(f) + ")";
        return e.request(f);
    }
    call = action + "()";
    if (action == "openDoor") return e.openDoor();
    if (action == "closeDoor") return e.closeDoor();
    if (action == "move") return e.move();
    throw std::runtime_error("unknown action in trace: " + action);
}

std::string report(const std::string& trace, size_t index, const std::string& call, const ElevatorState& before,
                   const ElevatorState& expected, const ElevatorState& actual) {
    return "\n=== MBT DIVERGENCE ===\n"
           "trace:    " + trace + "\n"
           "step:     " + std::to_string(index) + "\n"
           "C++ call: " + call + "\n"
           "before:   " + toString(before) + "\n"
           "expected: " + toString(expected) + "   (Quint)\n"
           "actual:   " + toString(actual) + "   (C++)\n";
}

class MBTDriver : public ::testing::TestWithParam<std::string> {
public:
    static void TearDownTestSuite() {
        size_t steps = 0;
        for (const auto& [_, n] : actionCounts) steps += n;
        std::cout << "[   MBT    ] replayed " << traces << " traces, " << steps << " steps:";
        for (const auto& [action, n] : actionCounts) std::cout << " " << action << "=" << n;
        std::cout << std::endl;
    }

protected:
    static inline size_t traces = 0;
    static inline std::map<std::string, size_t> actionCounts;
};

TEST_P(MBTDriver, ReplaysTrace) {
    const std::string path = GetParam();
    std::ifstream in(path);
    ASSERT_TRUE(in) << "cannot open " << path;
    const json states = json::parse(in).at("states");
    ASSERT_FALSE(states.empty());

    Elevator e;
    e.init();
    const auto initial = parseState(states.at(0));
    ASSERT_TRUE(e.state() == initial) << report(path, 0, "init()", ElevatorState{}, initial, e.state());

    for (size_t i = 1; i < states.size(); ++i) {
        const json& st = states.at(i);
        const auto action = st.at("mbt::actionTaken").get<std::string>();
        const auto expected = parseState(st);
        const auto before = e.state();

        std::string call;
        const bool enabled = applyStep(e, action, st.at("mbt::nondetPicks"), call);
        ASSERT_TRUE(enabled) << "C++ rejected an action that Quint took (guard mismatch)"
                             << report(path, i, call, before, expected, e.state());
        ASSERT_TRUE(e.state() == expected) << "C++ state differs from Quint state"
                                           << report(path, i, call, before, expected, e.state());

        const auto violated = violatedInvariants(e.state());
        ASSERT_TRUE(violated.empty()) << "C++ invariant violated: " << violated.front()
                                      << report(path, i, call, before, expected, e.state());
        ++actionCounts[action];
    }

    ++traces;
    RecordProperty("steps", static_cast<int>(states.size() - 1));
    std::cout << "[   MBT    ] " << fs::path(path).filename().string() << ": " << states.size() - 1
              << " steps replayed" << std::endl;
}

std::string traceTestName(const ::testing::TestParamInfo<std::string>& info) {
    std::string name = fs::path(info.param).filename().string();
    name = name.substr(0, name.find('.'));
    std::replace_if(name.begin(), name.end(), [](char c) { return !std::isalnum(static_cast<unsigned char>(c)); }, '_');
    return name;
}

INSTANTIATE_TEST_SUITE_P(QuintTraces, MBTDriver, ::testing::ValuesIn(discoverTraces()), traceTestName);
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(MBTDriver);

TEST(MBTDriverSetup, TracesAvailable) {
    EXPECT_FALSE(discoverTraces().empty())
        << "No *.itf.json traces in " << tracesDir() << ". Generate them with scripts/mbt.sh.";
}

}  // namespace
