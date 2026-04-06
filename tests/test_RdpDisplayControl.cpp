#include <gtest/gtest.h>
#include "rdp/RdpDisplayControl.h"
#include "scene/MonitorLayout.h"

#include <vector>

// ── Mock DispClientContext ────────────────────────────────────────────────────

struct MockDispCtx {
    DispClientContext                        ctx{};
    std::vector<DISPLAY_CONTROL_MONITOR_LAYOUT> capturedMonitors;
    int                                      sendCallCount = 0;
    UINT                                     sendReturnValue = CHANNEL_RC_OK;

    MockDispCtx() {
        ctx.custom             = this;
        ctx.SendMonitorLayout  = &MockSendMonitorLayout;
        ctx.DisplayControlCaps = nullptr;  // not needed in unit tests
    }

    static UINT MockSendMonitorLayout(DispClientContext* ctx,
                                      UINT32 numMonitors,
                                      DISPLAY_CONTROL_MONITOR_LAYOUT* monitors) {
        auto* self = static_cast<MockDispCtx*>(ctx->custom);
        self->sendCallCount++;
        if (monitors) {
            self->capturedMonitors.assign(monitors, monitors + numMonitors);
        }
        return self->sendReturnValue;
    }
};

// ── Fixture ───────────────────────────────────────────────────────────────────

class RdpDisplayControlTest : public ::testing::Test {
protected:
    MonitorLayout     layout;
    RdpDisplayControl ctrl{layout};
    MockDispCtx       mock;

    void SetUp() override {
        layout.BuildDefaultLayout();
        ctrl.Attach(&mock.ctx);
    }
};

// ── SendMonitorLayout ─────────────────────────────────────────────────────────

TEST_F(RdpDisplayControlTest, SendsExactly3Monitors) {
    ctrl.SendMonitorLayout(MonitorLayout::kMaxMonitors);
    ASSERT_EQ(mock.capturedMonitors.size(), 3u);
}

TEST_F(RdpDisplayControlTest, Monitor0_HasPrimaryFlag) {
    ctrl.SendMonitorLayout(MonitorLayout::kMaxMonitors);
    EXPECT_TRUE(mock.capturedMonitors[0].Flags & DISPLAY_CONTROL_MONITOR_PRIMARY);
}

TEST_F(RdpDisplayControlTest, OtherMonitors_NoPrimaryFlag) {
    ctrl.SendMonitorLayout(MonitorLayout::kMaxMonitors);
    for (size_t i = 1; i < mock.capturedMonitors.size(); ++i) {
        EXPECT_FALSE(mock.capturedMonitors[i].Flags & DISPLAY_CONTROL_MONITOR_PRIMARY)
            << "monitor " << i;
    }
}

TEST_F(RdpDisplayControlTest, AllMonitors_1920x1080) {
    ctrl.SendMonitorLayout(MonitorLayout::kMaxMonitors);
    for (size_t i = 0; i < mock.capturedMonitors.size(); ++i) {
        EXPECT_EQ(mock.capturedMonitors[i].Width,  1920u) << "monitor " << i;
        EXPECT_EQ(mock.capturedMonitors[i].Height, 1080u) << "monitor " << i;
    }
}

// Monitor 0 (center/primary) at Left=1920, Monitor 1 (left) at Left=0,
// Monitor 2 (right) at Left=3840. All at Top=0.
TEST_F(RdpDisplayControlTest, Monitor0_CenterPosition) {
    ctrl.SendMonitorLayout(MonitorLayout::kMaxMonitors);
    EXPECT_EQ(mock.capturedMonitors[0].Left, 1920);
    EXPECT_EQ(mock.capturedMonitors[0].Top,  0);
}

TEST_F(RdpDisplayControlTest, Monitor1_LeftPosition) {
    ctrl.SendMonitorLayout(MonitorLayout::kMaxMonitors);
    EXPECT_EQ(mock.capturedMonitors[1].Left, 0);
    EXPECT_EQ(mock.capturedMonitors[1].Top,  0);
}

TEST_F(RdpDisplayControlTest, Monitor2_RightPosition) {
    ctrl.SendMonitorLayout(MonitorLayout::kMaxMonitors);
    EXPECT_EQ(mock.capturedMonitors[2].Left, 3840);
    EXPECT_EQ(mock.capturedMonitors[2].Top,  0);
}

TEST_F(RdpDisplayControlTest, AllMonitors_LandscapeOrientation) {
    ctrl.SendMonitorLayout(MonitorLayout::kMaxMonitors);
    for (size_t i = 0; i < mock.capturedMonitors.size(); ++i) {
        EXPECT_EQ(mock.capturedMonitors[i].Orientation, ORIENTATION_LANDSCAPE)
            << "monitor " << i;
    }
}

TEST_F(RdpDisplayControlTest, AllMonitors_ScaleFactors100) {
    ctrl.SendMonitorLayout(MonitorLayout::kMaxMonitors);
    for (size_t i = 0; i < mock.capturedMonitors.size(); ++i) {
        EXPECT_EQ(mock.capturedMonitors[i].DesktopScaleFactor, 100u) << "monitor " << i;
        EXPECT_EQ(mock.capturedMonitors[i].DeviceScaleFactor,  100u) << "monitor " << i;
    }
}

TEST_F(RdpDisplayControlTest, SendMonitorLayout_CallCount_One) {
    ctrl.SendMonitorLayout(MonitorLayout::kMaxMonitors);
    EXPECT_EQ(mock.sendCallCount, 1);
}

// ── OnDisplayControlCaps callback ────────────────────────────────────────────

TEST_F(RdpDisplayControlTest, OnCaps_3Monitors_TriggersLayoutSend) {
    UINT result = RdpDisplayControl::OnDisplayControlCaps(&mock.ctx, 3, 8192, 0);

    EXPECT_EQ(result, CHANNEL_RC_OK);
    EXPECT_EQ(mock.sendCallCount, 1);
    EXPECT_EQ(mock.capturedMonitors.size(), 3u);
}

TEST_F(RdpDisplayControlTest, OnCaps_32Monitors_TriggersLayoutSend) {
    RdpDisplayControl::OnDisplayControlCaps(&mock.ctx, 32, 0, 0);
    EXPECT_EQ(mock.sendCallCount, 1);
    EXPECT_EQ(mock.capturedMonitors.size(), 3u);
}

TEST_F(RdpDisplayControlTest, OnCaps_TooFewMonitors_DegradedLayout) {
    // Server supports only 1 monitor — should still send 1.
    UINT result = RdpDisplayControl::OnDisplayControlCaps(&mock.ctx, 1, 0, 0);
    EXPECT_EQ(result, CHANNEL_RC_OK);
    EXPECT_EQ(mock.sendCallCount, 1);
    EXPECT_EQ(mock.capturedMonitors.size(), 1u);
}

TEST_F(RdpDisplayControlTest, OnCaps_ZeroMonitors_NoLayoutSent) {
    RdpDisplayControl::OnDisplayControlCaps(&mock.ctx, 0, 0, 0);
    EXPECT_EQ(mock.sendCallCount, 0);
}

TEST_F(RdpDisplayControlTest, SendBeforeAttach_ReturnsError) {
    MonitorLayout  layout2;
    RdpDisplayControl ctrl2{layout2};
    layout2.BuildDefaultLayout();
    // Not attached — must not crash and must return a non-OK code.
    UINT result = ctrl2.SendMonitorLayout(MonitorLayout::kMaxMonitors);
    EXPECT_NE(result, CHANNEL_RC_OK);
}
