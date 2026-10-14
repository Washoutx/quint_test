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
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "TestSupport.hpp"
#include "elevator/elevator_system.hpp"

#ifndef MBT_DEFAULT_TRACES_DIR
#define MBT_DEFAULT_TRACES_DIR "traces"
#endif

namespace {

namespace fs = std::filesystem;
using nlohmann::json;
using namespace elevator;
using elevator::test::toJson;
using elevator::test::violatedInvariants;

// ---------------------------------------------------------------------------
// ITF parsing
// ---------------------------------------------------------------------------

[[noreturn]] void fail(const std::string& what, const json& j) {
    throw std::runtime_error("ITF: " + what + ": " + j.dump());
}

const json& setItems(const json& j) {
    if (!j.is_object() || !j.contains("#set")) fail("expected a set", j);
    return j.at("#set");
}

const json& mapItems(const json& j) {
    if (!j.is_object() || !j.contains("#map")) fail("expected a map", j);
    return j.at("#map");
}

int toInt(const json& j) {
    if (j.is_number_integer()) return j.get<int>();
    if (j.is_object() && j.contains("#bigint")) return std::stoi(j.at("#bigint").get<std::string>());
    fail("expected an integer", j);
}

std::string tagOf(const json& j) {
    if (!j.is_object() || !j.contains("tag")) fail("expected a variant", j);
    return j.at("tag").get<std::string>();
}

// Payload of `Some(x)`, or nullptr for `None` or a missing pick.
const json* pick(const json& picks, const std::string& name) {
    if (!picks.contains(name)) return nullptr;
    const auto& opt = picks.at(name);
    return tagOf(opt) == "Some" ? &opt.at("value") : nullptr;
}

const json& requirePick(const json& picks, const std::string& name) {
    const json* v = pick(picks, name);
    if (!v) fail("missing nondet pick '" + name + "'", picks);
    return *v;
}

Dir parseDir(const json& j) {
    const auto tag = tagOf(j);
    if (tag == "Up") return Dir::Up;
    if (tag == "Down") return Dir::Down;
    fail("unknown Dir", j);
}

Heading parseHeading(const json& j) {
    const auto tag = tagOf(j);
    if (tag == "Idle") return Heading::Idle;
    if (tag == "Going") return parseDir(j.at("value")) == Dir::Up ? Heading::GoingUp : Heading::GoingDown;
    fail("unknown Heading", j);
}

Door parseDoor(const json& j) {
    const auto tag = tagOf(j);
    if (tag == "Open") return Door::Open;
    if (tag == "Closed") return Door::Closed;
    fail("unknown Door", j);
}

Motor parseMotor(const json& j) {
    const auto tag = tagOf(j);
    if (tag == "Stopped") return Motor::Stopped;
    if (tag == "Running") return Motor::Running;
    fail("unknown Motor", j);
}

HallCall parseHallCall(const json& j) { return HallCall{toInt(j.at("floor")), parseDir(j.at("dir"))}; }

ElevatorState parseElevatorState(const json& j) {
    ElevatorState es;
    es.floor = toInt(j.at("floor"));
    es.door = parseDoor(j.at("door"));
    es.motor = parseMotor(j.at("motor"));
    es.heading = parseHeading(j.at("heading"));
    for (const auto& f : setItems(j.at("carCalls"))) es.carCalls.insert(toInt(f));
    for (const auto& c : setItems(j.at("hallStops"))) es.hallStops.insert(parseHallCall(c));
    es.statusSeq = toInt(j.at("statusSeq"));
    return es;
}

DispatcherState parseDispatcherState(const json& j) {
    DispatcherState d;
    for (const auto& c : setItems(j.at("pending"))) d.pending.insert(parseHallCall(c));
    for (const auto& kv : mapItems(j.at("assigned"))) d.assigned[parseHallCall(kv.at(0))] = kv.at(1).get<std::string>();
    for (const auto& kv : mapItems(j.at("known"))) {
        const auto& r = kv.at(1);
        d.known[kv.at(0).get<std::string>()] = PositionReport{toInt(r.at("floor")), toInt(r.at("seq"))};
    }
    return d;
}

Message parseMessage(const json& j) {
    const auto tag = tagOf(j);
    const auto& v = j.at("value");
    if (tag == "HallCallMsg") return HallCallMsg{parseHallCall(v)};
    if (tag == "AssignMsg") return AssignMsg{v.at("elevator").get<std::string>(), parseHallCall(v.at("call"))};
    if (tag == "StatusMsg") return StatusMsg{v.at("elevator").get<std::string>(), toInt(v.at("floor")), toInt(v.at("seq"))};
    if (tag == "ServedMsg") return ServedMsg{v.at("elevator").get<std::string>(), parseHallCall(v.at("call"))};
    fail("unknown Message", j);
}

SystemState parseState(const json& j) {
    SystemState s;
    for (const auto& kv : mapItems(j.at("elevators"))) {
        s.elevators[kv.at(0).get<std::string>()] = parseElevatorState(kv.at(1));
    }
    s.dispatcher = parseDispatcherState(j.at("dispatcher"));
    for (const auto& c : setItems(j.at("hallLamps"))) s.hallLamps.insert(parseHallCall(c));
    for (const auto& m : setItems(j.at("network"))) s.network.insert(parseMessage(m));
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

struct StepResult {
    bool enabled = false;
    std::string call;
};

// Translates one ITF transition into the corresponding ElevatorSystem call.
StepResult applyStep(ElevatorSystem& sys, const std::string& action, const json& picks, const SystemState& expected) {
    auto elevatorPick = [&] { return requirePick(picks, "e").get<std::string>(); };

    if (action == "pressHallButton") {
        const auto c = parseHallCall(requirePick(picks, "c"));
        return {sys.pressHallButton(c), "pressHallButton(" + toJson(c).dump() + ")"};
    }
    if (action == "pressCarButton") {
        const auto e = elevatorPick();
        const int f = toInt(requirePick(picks, "f"));
        return {sys.pressCarButton(e, f), "pressCarButton(" + e + ", " + std::to_string(f) + ")"};
    }
    if (action == "assignCall") {
        const auto c = parseHallCall(requirePick(picks, "c"));
        std::string target;
        if (const json* t = pick(picks, "target")) {
            target = t->get<std::string>();
        } else if (auto it = expected.dispatcher.assigned.find(c); it != expected.dispatcher.assigned.end()) {
            target = it->second;
        } else {
            return {false, "assignCall(" + toJson(c).dump() + "): no target in trace"};
        }
        return {sys.assignTo(c, target), "assignTo(" + toJson(c).dump() + ", " + target + ")"};
    }
    if (action == "departure") {
        const auto e = elevatorPick();
        return {sys.departure(e), "departure(" + e + ")"};
    }
    if (action == "travel") {
        const auto e = elevatorPick();
        return {sys.travel(e), "travel(" + e + ")"};
    }
    if (action == "openDoor") {
        const auto e = elevatorPick();
        return {sys.openDoor(e), "openDoor(" + e + ")"};
    }
    if (action == "closeDoor") {
        const auto e = elevatorPick();
        return {sys.closeDoor(e), "closeDoor(" + e + ")"};
    }
    if (action == "deliverAny") {
        const auto m = parseMessage(requirePick(picks, "m"));
        return {sys.deliver(m), "deliver(" + toJson(m).dump() + ")"};
    }
    throw std::runtime_error("unknown action in trace: " + action);
}

std::string report(const std::string& trace, size_t index, const std::string& action, const std::string& call,
                   const SystemState& before, const SystemState& expected, const SystemState& actual) {
    std::ostringstream out;
    out << "\n=== MBT DIVERGENCE ===\n"
        << "trace:    " << trace << "\n"
        << "step:     " << index << "\n"
        << "action:   " << action << "\n"
        << "C++ call: " << call << "\n"
        << "--- state before step ---\n"
        << toJson(before).dump(2) << "\n"
        << "--- expected (Quint) ---\n"
        << toJson(expected).dump(2) << "\n"
        << "--- actual (C++) ---\n"
        << toJson(actual).dump(2) << "\n"
        << "--- diff (JSON patch: actual -> expected) ---\n"
        << json::diff(toJson(actual), toJson(expected)).dump(2) << "\n";
    return out.str();
}

std::string differingVars(const ElevatorSystem& sys, const SystemState& expected) {
    std::string out;
    auto check = [&](bool same, const char* var) {
        if (!same) out += (out.empty() ? "" : ", ") + std::string(var);
    };
    check(sys.elevators() == expected.elevators, "elevators");
    check(sys.dispatcher() == expected.dispatcher, "dispatcher");
    check(sys.hallLamps() == expected.hallLamps, "hallLamps");
    check(sys.network() == expected.network, "network");
    return out;
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
    ASSERT_EQ(states.at(0).at("mbt::actionTaken").get<std::string>(), "init")
        << path << ": trace was not generated with --mbt";

    ElevatorSystem sys;
    sys.init();
    const auto initial = parseState(states.at(0));
    const auto initDiff = differingVars(sys, initial);
    ASSERT_TRUE(initDiff.empty()) << "C++ state differs from Quint state in: " << initDiff
                                  << report(path, 0, "init", "init()", SystemState{}, initial, sys.state());

    for (size_t i = 1; i < states.size(); ++i) {
        const json& st = states.at(i);
        const auto action = st.at("mbt::actionTaken").get<std::string>();
        const auto expected = parseState(st);
        const auto before = sys.state();

        const auto result = applyStep(sys, action, st.at("mbt::nondetPicks"), expected);
        ASSERT_TRUE(result.enabled) << "C++ rejected an action that Quint took (guard mismatch)"
                                    << report(path, i, action, result.call, before, expected, sys.state());
        const auto diff = differingVars(sys, expected);
        ASSERT_TRUE(diff.empty()) << "C++ state differs from Quint state in: " << diff
                                  << report(path, i, action, result.call, before, expected, sys.state());

        const auto violated = violatedInvariants(sys.config(), sys.state());
        ASSERT_TRUE(violated.empty()) << "C++ invariant violated: " << violated.front()
                                      << report(path, i, action, result.call, before, expected, sys.state());
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
