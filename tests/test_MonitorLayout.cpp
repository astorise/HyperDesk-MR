#include <gtest/gtest.h>

#include "scene/MonitorLayout.h"

#include <cmath>

static constexpr float kEps = 1e-4f;
static constexpr float kPi = 3.14159265358979323846f;
static constexpr float kDecagonStep = 2.0f * kPi / 10.0f;  // 36°

namespace {

XrQuaternionf YawQuaternion(float radians) {
    const float half = radians * 0.5f;
    return {0.0f, std::sin(half), 0.0f, std::cos(half)};
}

XrQuaternionf PitchQuaternion(float radians) {
    const float half = radians * 0.5f;
    return {std::sin(half), 0.0f, 0.0f, std::cos(half)};
}

}  // namespace

class MonitorLayoutTest : public ::testing::Test {
protected:
    MonitorLayout layout;

    void SetUp() override {
        layout.BuildDefaultLayout();
    }
};

TEST(MonitorLayoutInit, AllMonitorsInitiallyInactive) {
    MonitorLayout layout;
    for (uint32_t i = 0; i < MonitorLayout::kMaxMonitors; ++i) {
        EXPECT_FALSE(layout.GetMonitor(i).active) << "monitor " << i;
    }
}

TEST(MonitorLayoutInit, AllMonitorsInitiallyUnbound) {
    MonitorLayout layout;
    for (uint32_t i = 0; i < MonitorLayout::kMaxMonitors; ++i) {
        EXPECT_EQ(layout.GetMonitor(i).rdpSurfaceId, UINT32_MAX) << "monitor " << i;
    }
}

TEST_F(MonitorLayoutTest, Count) {
    EXPECT_EQ(layout.GetAllMonitors().size(), 3u);
}

// With cylinder layers all monitors share the cylinder center position (origin).
TEST_F(MonitorLayoutTest, AllMonitorsAtOrigin) {
    for (uint32_t i = 0; i < MonitorLayout::kMaxMonitors; ++i) {
        const auto& pos = layout.GetMonitor(i).worldPose.position;
        EXPECT_NEAR(pos.x, 0.0f, kEps) << "monitor " << i;
        EXPECT_NEAR(pos.y, 0.0f, kEps) << "monitor " << i;
        EXPECT_NEAR(pos.z, 0.0f, kEps) << "monitor " << i;
    }
}

// Monitor 0 (center) has identity orientation.
TEST_F(MonitorLayoutTest, Monitor0_IdentityOrientation) {
    const auto& q = layout.GetMonitor(0).worldPose.orientation;
    EXPECT_NEAR(q.x, 0.0f, kEps);
    EXPECT_NEAR(q.y, 0.0f, kEps);
    EXPECT_NEAR(q.z, 0.0f, kEps);
    EXPECT_NEAR(q.w, 1.0f, kEps);
}

// Monitor 1 (left) has +36° yaw.
TEST_F(MonitorLayoutTest, Monitor1_PositiveYawOrientation) {
    const auto& q = layout.GetMonitor(1).worldPose.orientation;
    const auto expected = YawQuaternion(kDecagonStep);
    EXPECT_NEAR(q.x, expected.x, kEps);
    EXPECT_NEAR(q.y, expected.y, kEps);
    EXPECT_NEAR(q.z, expected.z, kEps);
    EXPECT_NEAR(q.w, expected.w, kEps);
}

// Monitor 2 (right) has -36° yaw.
TEST_F(MonitorLayoutTest, Monitor2_NegativeYawOrientation) {
    const auto& q = layout.GetMonitor(2).worldPose.orientation;
    const auto expected = YawQuaternion(-kDecagonStep);
    EXPECT_NEAR(q.x, expected.x, kEps);
    EXPECT_NEAR(q.y, expected.y, kEps);
    EXPECT_NEAR(q.z, expected.z, kEps);
    EXPECT_NEAR(q.w, expected.w, kEps);
}

TEST_F(MonitorLayoutTest, AllMonitorsSameSize) {
    for (uint32_t i = 0; i < MonitorLayout::kMaxMonitors; ++i) {
        const auto& sz = layout.GetMonitor(i).sizeMeters;
        EXPECT_NEAR(sz.x, 1.92f, kEps) << "monitor " << i;
        EXPECT_NEAR(sz.y, 1.08f, kEps) << "monitor " << i;
    }
}

TEST_F(MonitorLayoutTest, Monitor0_ForwardNormalPointsTowardViewer) {
    const auto& n = layout.GetMonitor(0).forwardNormal;
    EXPECT_NEAR(n.x, 0.0f, kEps);
    EXPECT_NEAR(n.y, 0.0f, kEps);
    EXPECT_NEAR(n.z, 1.0f, kEps);  // sin(0), 0, cos(0)
}

TEST_F(MonitorLayoutTest, Monitor1_ForwardNormalRotatedLeft) {
    const auto& n = layout.GetMonitor(1).forwardNormal;
    // Normal = {sin(+36°), 0, cos(+36°)}
    EXPECT_NEAR(n.x, std::sin(kDecagonStep), kEps);
    EXPECT_NEAR(n.y, 0.0f, kEps);
    EXPECT_NEAR(n.z, std::cos(kDecagonStep), kEps);
}

TEST_F(MonitorLayoutTest, Monitor2_ForwardNormalRotatedRight) {
    const auto& n = layout.GetMonitor(2).forwardNormal;
    // Normal = {sin(-36°), 0, cos(-36°)}
    EXPECT_NEAR(n.x, std::sin(-kDecagonStep), kEps);
    EXPECT_NEAR(n.y, 0.0f, kEps);
    EXPECT_NEAR(n.z, std::cos(-kDecagonStep), kEps);
}

TEST_F(MonitorLayoutTest, SetAllActiveMakesAllActive) {
    layout.SetAllActive();
    for (uint32_t i = 0; i < MonitorLayout::kMaxMonitors; ++i) {
        EXPECT_TRUE(layout.GetMonitor(i).active) << "monitor " << i;
    }
}

TEST_F(MonitorLayoutTest, SetActiveCountOneMonitorRecentersPrimary) {
    layout.SetActiveCount(1);

    const auto& pos = layout.GetMonitor(0).worldPose.position;
    // Without an anchor, SetActiveCount(1) places monitor 0 at (0, 0, kDepth).
    EXPECT_NEAR(pos.x, 0.0f, kEps);
    EXPECT_NEAR(pos.y, 0.0f, kEps);
    EXPECT_NEAR(pos.z, MonitorLayout::kDepth, kEps);
    EXPECT_TRUE(layout.GetMonitor(0).active);
    EXPECT_FALSE(layout.GetMonitor(1).active);
    EXPECT_FALSE(layout.GetMonitor(2).active);
}

// AnchorPrimaryToHeadPose with identity orientation looking down -Z.
// Cylinder center = headPos + (-0.5) * forward = (1, 1.6, 2) + (-0.5)*(0,0,-1) = (1, 1.6, 2.5)
TEST_F(MonitorLayoutTest, AnchorPrimaryToHeadPose_IdentityOrientation) {
    XrPosef headPose{};
    headPose.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
    headPose.position = {1.0f, 1.6f, 2.0f};

    layout.AnchorPrimaryToHeadPose(headPose);

    // All monitors get the same position (cylinder center).
    for (uint32_t i = 0; i < MonitorLayout::kMaxMonitors; ++i) {
        const auto& pos = layout.GetMonitor(i).worldPose.position;
        EXPECT_NEAR(pos.x, 1.0f, kEps) << "monitor " << i;
        EXPECT_NEAR(pos.y, 1.6f, kEps) << "monitor " << i;
        EXPECT_NEAR(pos.z, 2.5f, kEps) << "monitor " << i;
    }
}

// AnchorPrimaryToHeadPose with 90° yaw (looking left, -X direction).
TEST_F(MonitorLayoutTest, AnchorPrimaryToHeadPoseRotatesWithYaw) {
    XrPosef headPose{};
    headPose.orientation = YawQuaternion(kPi * 0.5f);
    headPose.position = {0.0f, 1.7f, 0.0f};

    layout.AnchorPrimaryToHeadPose(headPose);

    // Forward = (-1, 0, 0), cylinder center = (0,1.7,0) + (-0.5)*(-1,0,0) = (0.5, 1.7, 0)
    for (uint32_t i = 0; i < MonitorLayout::kMaxMonitors; ++i) {
        const auto& pos = layout.GetMonitor(i).worldPose.position;
        EXPECT_NEAR(pos.x, 0.5f, kEps) << "monitor " << i;
        EXPECT_NEAR(pos.y, 1.7f, kEps) << "monitor " << i;
        EXPECT_NEAR(pos.z, 0.0f, kEps) << "monitor " << i;
    }

    // Monitor 0 forward normal should point toward the viewer (along +X after 90° yaw).
    const auto& normal = layout.GetMonitor(0).forwardNormal;
    EXPECT_NEAR(normal.x, -1.0f, kEps);
    EXPECT_NEAR(normal.y, 0.0f, kEps);
    EXPECT_NEAR(normal.z, 0.0f, kEps);
}

TEST_F(MonitorLayoutTest, AnchorPrimaryToHeadPoseIgnoresPitch) {
    XrPosef headPose{};
    headPose.orientation = PitchQuaternion(kPi / 6.0f);
    headPose.position = {-0.5f, 1.8f, 0.25f};

    layout.AnchorPrimaryToHeadPose(headPose);

    // Pitch should be ignored, so cylinder center = (-0.5, 1.8, 0.25) + (-0.5)*(0,0,-1) = (-0.5, 1.8, 0.75)
    const auto& pos = layout.GetMonitor(0).worldPose.position;
    EXPECT_NEAR(pos.x, -0.5f, kEps);
    EXPECT_NEAR(pos.y, 1.8f, kEps);
    EXPECT_NEAR(pos.z, 0.75f, kEps);

    const auto& normal = layout.GetMonitor(0).forwardNormal;
    EXPECT_NEAR(normal.x, 0.0f, kEps);
    EXPECT_NEAR(normal.y, 0.0f, kEps);
    EXPECT_NEAR(normal.z, 1.0f, kEps);
}

TEST_F(MonitorLayoutTest, AnchorPersistsAcrossActiveCountChanges) {
    XrPosef headPose{};
    headPose.orientation = YawQuaternion(kPi * 0.5f);
    headPose.position = {0.25f, 1.65f, -0.5f};

    layout.AnchorPrimaryToHeadPose(headPose);
    layout.SetActiveCount(3);

    const auto& posAfterThree = layout.GetMonitor(0).worldPose.position;
    // Forward = (-1,0,0), cylinder center = (0.25,1.65,-0.5) + (-0.5)*(-1,0,0) = (0.75, 1.65, -0.5)
    EXPECT_NEAR(posAfterThree.x, 0.75f, kEps);
    EXPECT_NEAR(posAfterThree.y, 1.65f, kEps);
    EXPECT_NEAR(posAfterThree.z, -0.5f, kEps);

    layout.SetActiveCount(1);

    const auto& posAfterOne = layout.GetMonitor(0).worldPose.position;
    EXPECT_NEAR(posAfterOne.x, 0.75f, kEps);
    EXPECT_NEAR(posAfterOne.y, 1.65f, kEps);
    EXPECT_NEAR(posAfterOne.z, -0.5f, kEps);
}

TEST_F(MonitorLayoutTest, BindSurfaceSetsSurfaceId) {
    layout.BindSurface(1, 42u);
    EXPECT_EQ(layout.GetMonitor(1).rdpSurfaceId, 42u);
    EXPECT_EQ(layout.GetMonitor(0).rdpSurfaceId, UINT32_MAX);
}

TEST_F(MonitorLayoutTest, BindSurfaceOutOfRangeNoCrash) {
    EXPECT_NO_FATAL_FAILURE(layout.BindSurface(100, 1));
}

TEST_F(MonitorLayoutTest, GetMonitorOutOfRangeReturnsSomething) {
    EXPECT_NO_FATAL_FAILURE(layout.GetMonitor(999));
}
