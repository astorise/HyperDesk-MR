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
                                      DISPLAY_CONTROL_MONITOR_LAYOUT_PDU* pdu) {
        auto* self = static_cast<MockDispCtx*>(ctx->custom);
        self->sendCallCount++;
        if (pdu && pdu->Monitors) {
            self->capturedMonitors.assign(
                pdu->Monitors,
                pdu->Monitors + pdu->NumMonitors);
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

TEST_F(RdpDisplayControlTest, SendsExactly16Monitors) {
    ctrl.SendMonitorLayout();
    ASSERT_EQ(mock.capturedMonitors.size(), 16u);
}

TEST_F(RdpDisplayControlTest, Monitor0_HasPrimaryFlag) {
    ctrl.SendMonitorLayout();
    EXPECT_TRUE(mock.capturedMonitors[0].Flags & DISPLAY_CONTROL_MONITOR_PRIMARY);
}

TEST_F(RdpDisplayControlTest, OtherMonitors_NoPrimaryFlag) {
    ctrl.SendMonitorLayout();
    for (size_t i = 1; i < mock.capturedMonitors.size(); ++i) {
        EXPECT_FALSE(mock.capturedMonitors[i].Flags & DISPLAY_CONTROL_MONITOR_PRIMARY)
            << "monitor " << i;
    }
}

TEST_F(RdpDisplayControlTest, AllMonitors_1920x1080) {
    ctrl.SendMonitorLayout();
    for (size_t i = 0; i < mock.capturedMonitors.size(); ++i) {
        EXPECT_EQ(mock.capturedMonitors[i].Width,  1920u) << "monitor " << i;
        EXPECT_EQ(mock.capturedMonitors[i].Height, 1080u) << "monitor " << i;
    }
}

TEST_F(RdpDisplayControlTest, Monitor0_GridCoordinates_TopLeft) {
    ctrl.SendMonitorLayout();
    EXPECT_EQ(mock.capturedMonitors[0].Left, 0);
    EXPECT_EQ(mock.capturedMonitors[0].Top,  0);
}

TEST_F(RdpDisplayControlTest, Monitor3_GridCoordinates_TopRight) {
    ctrl.SendMonitorLayout();
    // col=3, row=0 → Left=3*1920, Top=0
    EXPECT_EQ(mock.capturedMonitors[3].Left, 3 * 1920);
    EXPECT_EQ(mock.capturedMonitors[3].Top,  0);
}

TEST_F(RdpDisplayControlTest, Monitor4_GridCoordinates_SecondRow) {
    ctrl.SendMonitorLayout();
    // col=0, row=1 → Left=0, Top=1*1080
    EXPECT_EQ(mock.capturedMonitors[4].Left, 0);
    EXPECT_EQ(mock.capturedMonitors[4].Top,  1 * 1080);
}

TEST_F(RdpDisplayControlTest, Monitor15_GridCoordinates_BottomRight) {
    ctrl.SendMonitorLayout();
    // col=3, row=3 → Left=3*1920, Top=3*1080
    EXPECT_EQ(mock.capturedMonitors[15].Left, 3 * 1920);
    EXPECT_EQ(mock.capturedMonitors[15].Top,  3 * 1080);
}

TEST_F(RdpDisplayControlTest, AllMonitors_LandscapeOrientation) {
    ctrl.SendMonitorLayout();
    for (size_t i = 0; i < mock.capturedMonitors.size(); ++i) {
        EXPECT_EQ(mock.capturedMonitors[i].Orientation, ORIENTATION_LANDSCAPE)
            << "monitor " << i;
    }
}

TEST_F(RdpDisplayControlTest, AllMonitors_ScaleFactors100) {
    ctrl.SendMonitorLayout();
    for (size_t i = 0; i < mock.capturedMonitors.size(); ++i) {
        EXPECT_EQ(mock.capturedMonitors[i].DesktopScaleFactor, 100u) << "monitor " << i;
        EXPECT_EQ(mock.capturedMonitors[i].DeviceScaleFactor,  100u) << "monitor " << i;
    }
}

TEST_F(RdpDisplayControlTest, SendMonitorLayout_CallCount_One) {
    ctrl.SendMonitorLayout();
    EXPECT_EQ(mock.sendCallCount, 1);
}

// ── OnDisplayControlCaps callback ────────────────────────────────────────────

TEST_F(RdpDisplayControlTest, OnCaps_16Monitors_TriggersLayoutSend) {
    DISPLAY_CONTROL_CAPS_PDU caps{};
    caps.MaxNumMonitors       = 16;
    caps.MaxMonitorAreaFactorA = 8192;

    UINT result = RdpDisplayControl::OnDisplayControlCaps(&mock.ctx, &caps);

    EXPECT_EQ(result, CHANNEL_RC_OK);
    EXPECT_EQ(mock.sendCallCount, 1);
    EXPECT_EQ(mock.capturedMonitors.size(), 16u);
}

TEST_F(RdpDisplayControlTest, OnCaps_32Monitors_TriggersLayoutSend) {
    DISPLAY_CONTROL_CAPS_PDU caps{};
    caps.MaxNumMonitors = 32;

    RdpDisplayControl::OnDisplayControlCaps(&mock.ctx, &caps);
    EXPECT_EQ(mock.sendCallCount, 1);
}

TEST_F(RdpDisplayControlTest, OnCaps_TooFewMonitors_NoLayoutSent) {
    DISPLAY_CONTROL_CAPS_PDU caps{};
    caps.MaxNumMonitors = 8;

    UINT result = RdpDisplayControl::OnDisplayControlCaps(&mock.ctx, &caps);

    EXPECT_EQ(result, CHANNEL_RC_OK);
    EXPECT_EQ(mock.sendCallCount, 0);
}

TEST_F(RdpDisplayControlTest, OnCaps_Exactly15Monitors_NoLayoutSent) {
    DISPLAY_CONTROL_CAPS_PDU caps{};
    caps.MaxNumMonitors = 15;

    RdpDisplayControl::OnDisplayControlCaps(&mock.ctx, &caps);
    EXPECT_EQ(mock.sendCallCount, 0);
}

TEST_F(RdpDisplayControlTest, SendBeforeAttach_ReturnsError) {
    MonitorLayout  layout2;
    RdpDisplayControl ctrl2{layout2};
    layout2.BuildDefaultLayout();
    // Not attached — must not crash and must return a non-OK code.
    UINT result = ctrl2.SendMonitorLayout();
    EXPECT_NE(result, CHANNEL_RC_OK);
}

// ── Grid coordinate exhaustive check ─────────────────────────────────────────

TEST_F(RdpDisplayControlTest, AllMonitors_CorrectGridCoordinates) {
    ctrl.SendMonitorLayout();
    ASSERT_EQ(mock.capturedMonitors.size(), 16u);

    for (uint32_t i = 0; i < 16u; ++i) {
        const uint32_t col      = i % 4;
        const uint32_t row      = i / 4;
        const INT32    expLeft  = static_cast<INT32>(col * 1920);
        const INT32    expTop   = static_cast<INT32>(row * 1080);

        EXPECT_EQ(mock.capturedMonitors[i].Left, expLeft) << "monitor " << i;
        EXPECT_EQ(mock.capturedMonitors[i].Top,  expTop)  << "monitor " << i;
    }
}
