#include <gtest/gtest.h>
#include "scene/MonitorLayout.h"

// ── Expected grid constants ───────────────────────────────────────────────────
// xStart = -(4-1)*2.0/2 = -3.0
// yStart =  (4-1)*1.15/2 = 1.725
// Monitor i → col = i%4, row = i/4
//             x   = -3.0 + col * 2.0
//             y   =  1.725 - row * 1.15
//             z   = -2.5

static constexpr float kEps    = 1e-4f;
static constexpr float kXStart = -3.0f;
static constexpr float kYStart =  1.725f;
static constexpr float kDepth  = -2.5f;

class MonitorLayoutTest : public ::testing::Test {
protected:
    MonitorLayout layout;
    void SetUp() override { layout.BuildDefaultLayout(); }
};

// ── Initial state (before BuildDefaultLayout) ─────────────────────────────────

TEST(MonitorLayoutInit, AllMonitorsInitiallyInactive) {
    MonitorLayout layout;  // not built yet
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

// ── Grid geometry ─────────────────────────────────────────────────────────────

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
        EXPECT_NEAR(q.x, 0.f, kEps) << "monitor " << i;
        EXPECT_NEAR(q.y, 0.f, kEps) << "monitor " << i;
        EXPECT_NEAR(q.z, 0.f, kEps) << "monitor " << i;
        EXPECT_NEAR(q.w, 1.f, kEps) << "monitor " << i;
    }
}

TEST_F(MonitorLayoutTest, AllMonitorsSameSize) {
    for (uint32_t i = 0; i < MonitorLayout::kMaxMonitors; ++i) {
        const auto& sz = layout.GetMonitor(i).sizeMeters;
        EXPECT_NEAR(sz.x, 1.92f, kEps) << "monitor " << i;
        EXPECT_NEAR(sz.y, 1.08f, kEps) << "monitor " << i;
    }
}

TEST_F(MonitorLayoutTest, TopLeftCorner_Monitor0) {
    const auto& pos = layout.GetMonitor(0).worldPose.position;
    EXPECT_NEAR(pos.x, kXStart, kEps);
    EXPECT_NEAR(pos.y, kYStart, kEps);
}

TEST_F(MonitorLayoutTest, TopRightCorner_Monitor3) {
    const auto& pos = layout.GetMonitor(3).worldPose.position;
    EXPECT_NEAR(pos.x, 3.0f,   kEps);  // -3 + 3*2 = 3
    EXPECT_NEAR(pos.y, kYStart, kEps);
}

TEST_F(MonitorLayoutTest, BottomLeftCorner_Monitor12) {
    const auto& pos = layout.GetMonitor(12).worldPose.position;
    EXPECT_NEAR(pos.x, kXStart, kEps);
    EXPECT_NEAR(pos.y, -kYStart, kEps);  // 1.725 - 3*1.15 = -1.725
}

TEST_F(MonitorLayoutTest, BottomRightCorner_Monitor15) {
    const auto& pos = layout.GetMonitor(15).worldPose.position;
    EXPECT_NEAR(pos.x,  3.0f,    kEps);
    EXPECT_NEAR(pos.y, -kYStart, kEps);
}

TEST_F(MonitorLayoutTest, InnerMonitor5_Row1Col1) {
    // row=1, col=1 → x = -3.0 + 1*2.0 = -1.0, y = 1.725 - 1*1.15 = 0.575
    const auto& pos = layout.GetMonitor(5).worldPose.position;
    EXPECT_NEAR(pos.x, -1.0f,  kEps);
    EXPECT_NEAR(pos.y,  0.575f, kEps);
}

TEST_F(MonitorLayoutTest, InnerMonitor10_Row2Col2) {
    // row=2, col=2 → x = -3.0 + 2*2.0 = 1.0, y = 1.725 - 2*1.15 = -0.575
    const auto& pos = layout.GetMonitor(10).worldPose.position;
    EXPECT_NEAR(pos.x,  1.0f,  kEps);
    EXPECT_NEAR(pos.y, -0.575f, kEps);
}

TEST_F(MonitorLayoutTest, HorizontalSpacingUniform) {
    // Adjacent monitors in the same row are kHSpacing apart.
    for (uint32_t col = 0; col < MonitorLayout::kGridCols - 1; ++col) {
        float x0 = layout.GetMonitor(col).worldPose.position.x;
        float x1 = layout.GetMonitor(col + 1).worldPose.position.x;
        EXPECT_NEAR(x1 - x0, MonitorLayout::kHSpacing, kEps) << "col " << col;
    }
}

TEST_F(MonitorLayoutTest, VerticalSpacingUniform) {
    // Adjacent monitors in the same column are kVSpacing apart (downward).
    for (uint32_t row = 0; row < MonitorLayout::kGridRows - 1; ++row) {
        float y0 = layout.GetMonitor(row * 4).worldPose.position.y;
        float y1 = layout.GetMonitor((row + 1) * 4).worldPose.position.y;
        EXPECT_NEAR(y0 - y1, MonitorLayout::kVSpacing, kEps) << "row " << row;
    }
}

// ── Mutations ─────────────────────────────────────────────────────────────────

TEST_F(MonitorLayoutTest, SetAllActive_MakesAllActive) {
    layout.SetAllActive();
    for (uint32_t i = 0; i < MonitorLayout::kMaxMonitors; ++i) {
        EXPECT_TRUE(layout.GetMonitor(i).active) << "monitor " << i;
    }
}

TEST_F(MonitorLayoutTest, BindSurface_SetsSurfaceId) {
    layout.BindSurface(7, 42u);
    EXPECT_EQ(layout.GetMonitor(7).rdpSurfaceId, 42u);
    // Other monitors unaffected.
    EXPECT_EQ(layout.GetMonitor(6).rdpSurfaceId, UINT32_MAX);
}

TEST_F(MonitorLayoutTest, BindSurface_OutOfRange_NocrCrash) {
    EXPECT_NO_FATAL_FAILURE(layout.BindSurface(100, 1));
}

TEST_F(MonitorLayoutTest, GetMonitor_OutOfRange_ReturnsSomething) {
    EXPECT_NO_FATAL_FAILURE(layout.GetMonitor(999));
}
