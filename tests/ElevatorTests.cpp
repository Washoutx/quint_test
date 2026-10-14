// Unit tests for pure helpers and the scenarios from docs/DESIGN_SPEC.md,
// mirroring the `run` tests in spec/elevator.qnt.

#include <gtest/gtest.h>

#include "TestSupport.hpp"
#include "elevator/elevator_system.hpp"

namespace {

using namespace elevator;

const HallCall call2Down{2, Dir::Down};

bool safety(const ElevatorSystem& sys) { return test::violatedInvariants(sys.config(), sys.state()).empty(); }

TEST(Helpers, Delta) {
    EXPECT_EQ(delta(Heading::GoingUp), 1);
    EXPECT_EQ(delta(Heading::GoingDown), -1);
    EXPECT_EQ(delta(Heading::Idle), 0);
}

TEST(Helpers, ValidHallCalls) {
    const Config cfg;
    EXPECT_FALSE(isValidHallCall(cfg, {0, Dir::Down}));
    EXPECT_FALSE(isValidHallCall(cfg, {3, Dir::Up}));
    EXPECT_FALSE(isValidHallCall(cfg, {4, Dir::Down}));
    EXPECT_TRUE(isValidHallCall(cfg, {0, Dir::Up}));
    EXPECT_TRUE(isValidHallCall(cfg, {3, Dir::Down}));
    EXPECT_EQ(allHallCalls(cfg).size(), 6u);
}

TEST(Helpers, Targets) {
    ElevatorState es;
    es.carCalls = {1, 3};
    es.hallStops = {{2, Dir::Up}, {3, Dir::Down}};
    EXPECT_EQ(targets(es), (std::set<int>{1, 2, 3}));
}

TEST(Helpers, NextDirScan) {
    ElevatorState es;
    es.floor = 2;
    es.carCalls = {0, 3};
    es.heading = Heading::GoingUp;
    EXPECT_EQ(nextDir(es), Dir::Up);
    es.heading = Heading::GoingDown;
    EXPECT_EQ(nextDir(es), Dir::Down);
    es.heading = Heading::Idle;
    EXPECT_EQ(nextDir(es), Dir::Up);
    es.carCalls = {0};
    es.heading = Heading::GoingUp;
    EXPECT_EQ(nextDir(es), Dir::Down);
}

TEST(Helpers, ClosestElevators) {
    const Config cfg;
    DispatcherState d;
    d.known = {{"A", {0, 0}}, {"B", {2, 1}}};
    EXPECT_EQ(closestElevators(cfg, d, 3), (std::set<ElevatorId>{"B"}));
    EXPECT_EQ(closestElevators(cfg, d, 1), (std::set<ElevatorId>{"A", "B"}));
}

TEST(Scenarios, HallCallLifecycle) {
    ElevatorSystem sys;
    ASSERT_TRUE(sys.pressHallButton(call2Down));
    ASSERT_TRUE(sys.deliver(HallCallMsg{call2Down}));
    EXPECT_EQ(sys.state().dispatcher.pending, std::set<HallCall>{call2Down});
    ASSERT_TRUE(sys.assignTo(call2Down, "A"));
    ASSERT_TRUE(sys.deliver(AssignMsg{"A", call2Down}));
    ASSERT_TRUE(sys.departure("A"));
    EXPECT_EQ(sys.state().elevators.at("A").heading, Heading::GoingUp);
    ASSERT_TRUE(sys.travel("A"));
    EXPECT_EQ(sys.state().elevators.at("A").motor, Motor::Running);
    ASSERT_TRUE(sys.travel("A"));
    EXPECT_EQ(sys.state().elevators.at("A").floor, 2);
    EXPECT_EQ(sys.state().elevators.at("A").motor, Motor::Stopped);
    ASSERT_TRUE(sys.openDoor("A"));
    EXPECT_EQ(sys.state().elevators.at("A").door, Door::Open);
    EXPECT_EQ(sys.state().elevators.at("A").heading, Heading::Idle);
    ASSERT_TRUE(sys.deliver(ServedMsg{"A", call2Down}));
    EXPECT_TRUE(sys.state().hallLamps.empty());
    EXPECT_TRUE(sys.state().dispatcher.assigned.empty());
    EXPECT_TRUE(safety(sys));
}

TEST(Scenarios, LitButtonIsIgnored) {
    ElevatorSystem sys;
    ASSERT_TRUE(sys.pressHallButton(call2Down));
    const auto before = sys.state();
    EXPECT_FALSE(sys.pressHallButton(call2Down));
    EXPECT_EQ(sys.state(), before);
}

TEST(Scenarios, NoDepartureWithOpenDoor) {
    ElevatorSystem sys;
    ASSERT_TRUE(sys.pressCarButton("A", 3));
    ASSERT_TRUE(sys.departure("A"));
    ASSERT_TRUE(sys.travel("A"));
    ASSERT_TRUE(sys.pressCarButton("A", 0));
    ASSERT_TRUE(sys.travel("A"));
    ASSERT_TRUE(sys.travel("A"));
    EXPECT_EQ(sys.state().elevators.at("A").floor, 3);
    ASSERT_TRUE(sys.openDoor("A"));
    const auto before = sys.state();
    EXPECT_FALSE(sys.departure("A"));
    EXPECT_EQ(sys.state(), before);
}

TEST(Scenarios, ScanKeepsDirection) {
    ElevatorSystem sys;
    ASSERT_TRUE(sys.pressCarButton("A", 2));
    ASSERT_TRUE(sys.departure("A"));
    ASSERT_TRUE(sys.travel("A"));
    ASSERT_TRUE(sys.pressCarButton("A", 0));
    ASSERT_TRUE(sys.travel("A"));
    ASSERT_TRUE(sys.openDoor("A"));
    ASSERT_TRUE(sys.closeDoor("A"));
    ASSERT_TRUE(sys.departure("A"));
    EXPECT_EQ(sys.state().elevators.at("A").heading, Heading::GoingDown);
    EXPECT_TRUE(safety(sys));
}

TEST(Scenarios, ClosestElevatorIsChosen) {
    ElevatorSystem sys;
    const HallCall call2Up{2, Dir::Up};
    ASSERT_TRUE(sys.pressCarButton("B", 3));
    ASSERT_TRUE(sys.departure("B"));
    ASSERT_TRUE(sys.travel("B"));
    ASSERT_TRUE(sys.deliver(StatusMsg{"B", 1, 1}));
    ASSERT_TRUE(sys.pressHallButton(call2Up));
    ASSERT_TRUE(sys.deliver(HallCallMsg{call2Up}));
    EXPECT_FALSE(sys.assignTo(call2Up, "A"));
    EXPECT_TRUE(sys.assignTo(call2Up, "B"));
}

TEST(Scenarios, StaleStatusIsDiscarded) {
    ElevatorSystem sys;
    ASSERT_TRUE(sys.pressCarButton("A", 3));
    ASSERT_TRUE(sys.departure("A"));
    ASSERT_TRUE(sys.travel("A"));
    ASSERT_TRUE(sys.travel("A"));
    ASSERT_TRUE(sys.deliver(StatusMsg{"A", 2, 2}));
    ASSERT_TRUE(sys.deliver(StatusMsg{"A", 1, 1}));
    EXPECT_EQ(sys.state().dispatcher.known.at("A"), (PositionReport{2, 2}));
}

}  // namespace
