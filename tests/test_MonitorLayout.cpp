#include <gtest/gtest.h>

#include "scene/MonitorLayout.h"

#include <cmath>

static constexpr float kEps = 1e-4f;
static constexpr float kXStart = -3.0f;
static constexpr float kYStart = 1.725f;
static constexpr float kDepth = -2.5f;
static constexpr float kPi = 3.14159265358979323846f;

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
    EXPECT_EQ(layout.GetAllMonitors().size(), 16u);
}

TEST_F(MonitorLayoutTest, AllMonitorsSameDepth) {
    for (uint32_t i = 0; i < MonitorLayout::kMaxMonitors; ++i) {
        EXPECT_NEAR(layout.GetMonitor(i).worldPose.position.z, kDepth, kEps)
            << "monitor " << i;
    }
}

TEST_F(MonitorLayoutTest, AllMonitorsIdentityOrientation) {
    for (uint32_t i = 0; i < MonitorLayout::kMaxMonitors; ++i) {
        const auto& q = layout.GetMonitor(i).worldPose.orientation;
        EXPECT_NEAR(q.x, 0.0f, kEps) << "monitor " << i;
        EXPECT_NEAR(q.y, 0.0f, kEps) << "monitor " << i;
        EXPECT_NEAR(q.z, 0.0f, kEps) << "monitor " << i;
        EXPECT_NEAR(q.w, 1.0f, kEps) << "monitor " << i;
    }
}

TEST_F(MonitorLayoutTest, AllMonitorsSameSize) {
    for (uint32_t i = 0; i < MonitorLayout::kMaxMonitors; ++i) {
        const auto& sz = layout.GetMonitor(i).sizeMeters;
        EXPECT_NEAR(sz.x, 1.92f, kEps) << "monitor " << i;
        EXPECT_NEAR(sz.y, 1.08f, kEps) << "monitor " << i;
    }
}

TEST_F(MonitorLayoutTest, TopLeftCornerMonitor0) {
    const auto& pos = layout.GetMonitor(0).worldPose.position;
    EXPECT_NEAR(pos.x, kXStart, kEps);
    EXPECT_NEAR(pos.y, kYStart, kEps);
}

TEST_F(MonitorLayoutTest, TopRightCornerMonitor3) {
    const auto& pos = layout.GetMonitor(3).worldPose.position;
    EXPECT_NEAR(pos.x, 3.0f, kEps);
    EXPECT_NEAR(pos.y, kYStart, kEps);
}

TEST_F(MonitorLayoutTest, BottomLeftCornerMonitor12) {
    const auto& pos = layout.GetMonitor(12).worldPose.position;
    EXPECT_NEAR(pos.x, kXStart, kEps);
    EXPECT_NEAR(pos.y, -kYStart, kEps);
}

TEST_F(MonitorLayoutTest, BottomRightCornerMonitor15) {
    const auto& pos = layout.GetMonitor(15).worldPose.position;
    EXPECT_NEAR(pos.x, 3.0f, kEps);
    EXPECT_NEAR(pos.y, -kYStart, kEps);
}

TEST_F(MonitorLayoutTest, InnerMonitor5Row1Col1) {
    const auto& pos = layout.GetMonitor(5).worldPose.position;
    EXPECT_NEAR(pos.x, -1.0f, kEps);
    EXPECT_NEAR(pos.y, 0.575f, kEps);
}

TEST_F(MonitorLayoutTest, InnerMonitor10Row2Col2) {
    const auto& pos = layout.GetMonitor(10).worldPose.position;
    EXPECT_NEAR(pos.x, 1.0f, kEps);
    EXPECT_NEAR(pos.y, -0.575f, kEps);
}

TEST_F(MonitorLayoutTest, HorizontalSpacingUniform) {
    for (uint32_t col = 0; col < MonitorLayout::kGridCols - 1; ++col) {
        const float x0 = layout.GetMonitor(col).worldPose.position.x;
        const float x1 = layout.GetMonitor(col + 1).worldPose.position.x;
        EXPECT_NEAR(x1 - x0, MonitorLayout::kHSpacing, kEps) << "col " << col;
    }
}

TEST_F(MonitorLayoutTest, VerticalSpacingUniform) {
    for (uint32_t row = 0; row < MonitorLayout::kGridRows - 1; ++row) {
        const float y0 = layout.GetMonitor(row * 4).worldPose.position.y;
        const float y1 = layout.GetMonitor((row + 1) * 4).worldPose.position.y;
        EXPECT_NEAR(y0 - y1, MonitorLayout::kVSpacing, kEps) << "row " << row;
    }
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
    EXPECT_NEAR(pos.x, 0.0f, kEps);
    EXPECT_NEAR(pos.y, 0.0f, kEps);
    EXPECT_NEAR(pos.z, kDepth, kEps);
    EXPECT_TRUE(layout.GetMonitor(0).active);
    EXPECT_FALSE(layout.GetMonitor(1).active);
}

TEST_F(MonitorLayoutTest, AnchorPrimaryToHeadPosePlacesPrimaryAtScanHeading) {
    XrPosef headPose{};
    headPose.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
    headPose.position = {1.0f, 1.6f, 2.0f};

    layout.AnchorPrimaryToHeadPose(headPose);

    const auto& pos = layout.GetMonitor(0).worldPose.position;
    EXPECT_NEAR(pos.x, 1.0f, kEps);
    EXPECT_NEAR(pos.y, 1.6f, kEps);
    EXPECT_NEAR(pos.z, 2.0f + kDepth, kEps);
}

TEST_F(MonitorLayoutTest, AnchorPrimaryToHeadPoseRotatesWallWithYawOnly) {
    XrPosef headPose{};
    headPose.orientation = YawQuaternion(kPi * 0.5f);
    headPose.position = {0.0f, 1.7f, 0.0f};

    layout.AnchorPrimaryToHeadPose(headPose);

    const auto& primaryPos = layout.GetMonitor(0).worldPose.position;
    EXPECT_NEAR(primaryPos.x, 2.5f, kEps);
    EXPECT_NEAR(primaryPos.y, 1.7f, kEps);
    EXPECT_NEAR(primaryPos.z, 0.0f, kEps);

    const auto& secondaryPos = layout.GetMonitor(1).worldPose.position;
    EXPECT_NEAR(secondaryPos.x, 2.5f, kEps);
    EXPECT_NEAR(secondaryPos.z, 2.0f, kEps);

    const auto& normal = layout.GetMonitor(0).forwardNormal;
    EXPECT_NEAR(normal.x, -1.0f, kEps);
    EXPECT_NEAR(normal.y, 0.0f, kEps);
    EXPECT_NEAR(normal.z, 0.0f, kEps);
}

TEST_F(MonitorLayoutTest, AnchorPrimaryToHeadPoseIgnoresPitchForHorizonAlignment) {
    XrPosef headPose{};
    headPose.orientation = PitchQuaternion(kPi / 6.0f);
    headPose.position = {-0.5f, 1.8f, 0.25f};

    layout.AnchorPrimaryToHeadPose(headPose);

    const auto& pos = layout.GetMonitor(0).worldPose.position;
    EXPECT_NEAR(pos.x, -0.5f, kEps);
    EXPECT_NEAR(pos.y, 1.8f, kEps);
    EXPECT_NEAR(pos.z, 0.25f + kDepth, kEps);

    const auto& normal = layout.GetMonitor(0).forwardNormal;
    EXPECT_NEAR(normal.x, 0.0f, kEps);
    EXPECT_NEAR(normal.y, 0.0f, kEps);
    EXPECT_NEAR(normal.z, 1.0f, kEps);
}

TEST_F(MonitorLayoutTest, AnchorPrimaryToHeadPosePersistsAcrossActiveCountChanges) {
    XrPosef headPose{};
    headPose.orientation = YawQuaternion(kPi * 0.5f);
    headPose.position = {0.25f, 1.65f, -0.5f};

    layout.AnchorPrimaryToHeadPose(headPose);
    layout.SetActiveCount(4);

    const auto& posAfterFour = layout.GetMonitor(0).worldPose.position;
    EXPECT_NEAR(posAfterFour.x, 2.75f, kEps);
    EXPECT_NEAR(posAfterFour.y, 1.65f, kEps);
    EXPECT_NEAR(posAfterFour.z, -0.5f, kEps);

    layout.SetActiveCount(1);

    const auto& posAfterOne = layout.GetMonitor(0).worldPose.position;
    EXPECT_NEAR(posAfterOne.x, 2.75f, kEps);
    EXPECT_NEAR(posAfterOne.y, 1.65f, kEps);
    EXPECT_NEAR(posAfterOne.z, -0.5f, kEps);
}

TEST_F(MonitorLayoutTest, BindSurfaceSetsSurfaceId) {
    layout.BindSurface(7, 42u);
    EXPECT_EQ(layout.GetMonitor(7).rdpSurfaceId, 42u);
    EXPECT_EQ(layout.GetMonitor(6).rdpSurfaceId, UINT32_MAX);
}

TEST_F(MonitorLayoutTest, BindSurfaceOutOfRangeNoCrash) {
    EXPECT_NO_FATAL_FAILURE(layout.BindSurface(100, 1));
}

TEST_F(MonitorLayoutTest, GetMonitorOutOfRangeReturnsSomething) {
    EXPECT_NO_FATAL_FAILURE(layout.GetMonitor(999));
}
