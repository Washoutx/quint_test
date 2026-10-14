// Scenarios from docs/DESIGN.md section 6, mirroring the `run` tests in spec/elevator.qnt.

#include <gtest/gtest.h>

#include "elevator.hpp"

namespace {

using namespace elevator;

TEST(Elevator, NextDirScan) {
    EXPECT_EQ(nextDir(2, Direction::Up, {0, 4}), Direction::Up);
    EXPECT_EQ(nextDir(2, Direction::Down, {0, 4}), Direction::Down);
    EXPECT_EQ(nextDir(2, Direction::Idle, {0, 4}), Direction::Up);
    EXPECT_EQ(nextDir(2, Direction::Up, {0}), Direction::Down);
}

TEST(Elevator, ServeOneRequest) {
    Elevator e;
    ASSERT_TRUE(e.request(3));
    ASSERT_TRUE(e.move());
    ASSERT_TRUE(e.move());
    ASSERT_TRUE(e.move());
    EXPECT_EQ(e.state().floor, 3);
    EXPECT_EQ(e.state().direction, Direction::Up);
    ASSERT_TRUE(e.openDoor());
    EXPECT_EQ(e.state().door, Door::Open);
    EXPECT_TRUE(e.state().requests.empty());
    EXPECT_EQ(e.state().direction, Direction::Idle);
}

TEST(Elevator, ScanKeepsDirection) {
    Elevator e;
    ASSERT_TRUE(e.request(3));
    ASSERT_TRUE(e.move());
    ASSERT_TRUE(e.request(0));
    ASSERT_TRUE(e.move());
    ASSERT_TRUE(e.move());
    EXPECT_EQ(e.state().floor, 3);
    ASSERT_TRUE(e.openDoor());
    ASSERT_TRUE(e.closeDoor());
    ASSERT_TRUE(e.move());
    EXPECT_EQ(e.state().floor, 2);
    EXPECT_EQ(e.state().direction, Direction::Down);
}

TEST(Elevator, NoMoveWithOpenDoor) {
    Elevator e;
    ASSERT_TRUE(e.request(1));
    ASSERT_TRUE(e.move());
    ASSERT_TRUE(e.openDoor());
    ASSERT_TRUE(e.request(3));
    const auto before = e.state();
    EXPECT_FALSE(e.move());
    EXPECT_EQ(e.state(), before);
}

TEST(Elevator, DuplicateRequestIsIgnored) {
    Elevator e;
    ASSERT_TRUE(e.request(2));
    EXPECT_FALSE(e.request(2));
}

}  // namespace
