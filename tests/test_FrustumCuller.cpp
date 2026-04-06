#include <gtest/gtest.h>
#include "scene/FrustumCuller.h"
#include "scene/MonitorLayout.h"
#include "codec/MediaCodecDecoder.h"

#include <array>
#include <cmath>

// ── Helpers ───────────────────────────────────────────────────────────────────

// Build a stereo XrView pair with identity orientation (looking down -Z)
// and the given eye positions.
static std::array<XrView, 2> MakeViews(float eyeSeparation = 0.064f) {
    std::array<XrView, 2> views{};
    views[0].type = XR_TYPE_VIEW;
    views[1].type = XR_TYPE_VIEW;
    // Identity quaternion → forward = (0, 0, -1).
    views[0].pose.orientation = {0.f, 0.f, 0.f, 1.f};
    views[1].pose.orientation = {0.f, 0.f, 0.f, 1.f};
    // Typical IPD offset.
    views[0].pose.position = {-eyeSeparation / 2.f, 0.f, 0.f};
    views[1].pose.position = { eyeSeparation / 2.f, 0.f, 0.f};
    return views;
}

// ── Visibility tests ──────────────────────────────────────────────────────────

class FrustumCullerVisibilityTest : public ::testing::Test {
protected:
    FrustumCuller culler;
};

TEST_F(FrustumCullerVisibilityTest, MonitorDirectlyAhead_IsVisible) {
    auto views = MakeViews();
    // Monitor at (0, 0, -2.5) — directly ahead.
    auto result = culler.TestMonitor(views, {0.f, 0.f, -2.5f});
    EXPECT_TRUE(result.visible);
    EXPECT_NEAR(result.dotProduct, 1.0f, 1e-3f);
}

TEST_F(FrustumCullerVisibilityTest, MonitorDirectlyBehind_IsNotVisible) {
    auto views = MakeViews();
    auto result = culler.TestMonitor(views, {0.f, 0.f, 2.5f});
    EXPECT_FALSE(result.visible);
    EXPECT_NEAR(result.dotProduct, -1.0f, 1e-3f);
}

TEST_F(FrustumCullerVisibilityTest, MonitorFarLeft_IsNotVisible) {
    auto views = MakeViews();
    // At (-10, 0, -2.5) the angle from forward is arctan(10/2.5) ≈ 76° — beyond FOV.
    auto result = culler.TestMonitor(views, {-10.f, 0.f, -2.5f});
    EXPECT_FALSE(result.visible);
}

TEST_F(FrustumCullerVisibilityTest, MonitorSlightlyLeft_IsVisible) {
    auto views = MakeViews();
    // At (-1, 0, -2.5): angle ≈ arctan(1/2.5) ≈ 21.8° — well within FOV.
    auto result = culler.TestMonitor(views, {-1.f, 0.f, -2.5f});
    EXPECT_TRUE(result.visible);
}

TEST_F(FrustumCullerVisibilityTest, MonitorSlightlyAbove_IsVisible) {
    auto views = MakeViews();
    auto result = culler.TestMonitor(views, {0.f, 0.5f, -2.5f});
    EXPECT_TRUE(result.visible);
}

TEST_F(FrustumCullerVisibilityTest, MonitorFarAbove_IsNotVisible) {
    auto views = MakeViews();
    // At (0, 8, -2.5): angle ≈ arctan(8/2.5) ≈ 72.6° — beyond FOV.
    auto result = culler.TestMonitor(views, {0.f, 8.f, -2.5f});
    EXPECT_FALSE(result.visible);
}

TEST_F(FrustumCullerVisibilityTest, DotProductOrdering) {
    // A closer-to-centre monitor must have a higher dot product than a more
    // peripheral one.
    auto views = MakeViews();
    auto r_centre = culler.TestMonitor(views, {0.f,    0.f, -2.5f});
    auto r_side   = culler.TestMonitor(views, {-1.0f, 0.f, -2.5f});
    EXPECT_GT(r_centre.dotProduct, r_side.dotProduct);
}

// ── Hysteresis tests ─────────────────────────────────────────────────────────

class FrustumCullerHysteresisTest : public ::testing::Test {
protected:
    static constexpr uint32_t kMaxMonitors = MonitorLayout::kMaxMonitors;

    MonitorLayout layout;
    FrustumCuller culler;
    std::array<std::unique_ptr<MediaCodecDecoder>, kMaxMonitors> decoderOwners;
    std::array<MediaCodecDecoder*, kMaxMonitors>                 decoders{};

    void SetUp() override {
        layout.BuildDefaultLayout();
        layout.SetAllActive();

        for (uint32_t i = 0; i < kMaxMonitors; ++i) {
            decoderOwners[i] = std::make_unique<MediaCodecDecoder>(nullptr, i);
            decoderOwners[i]->Configure(1920, 1080);
            decoderOwners[i]->Start();    // sets running_ = true via stub
            decoders[i] = decoderOwners[i].get();
        }
    }

    std::array<XrView, 2> AheadViews() { return MakeViews(); }
};

TEST_F(FrustumCullerHysteresisTest, AllMonitorsRunning_WhenAllVisible) {
    auto views = AheadViews();
    culler.UpdateAll(views, layout, decoders);
    for (uint32_t i = 0; i < kMaxMonitors; ++i) {
        EXPECT_TRUE(decoders[i]->IsRunning()) << "monitor " << i;
    }
}

TEST_F(FrustumCullerHysteresisTest, VisibleMonitorsNeverPaused) {
    auto views = AheadViews();
    for (int frame = 0; frame < 5; ++frame) {
        culler.UpdateAll(views, layout, decoders);
    }
    for (uint32_t i = 0; i < kMaxMonitors; ++i) {
        EXPECT_TRUE(decoders[i]->IsRunning()) << "monitor " << i << " after 5 visible frames";
    }
}

TEST_F(FrustumCullerHysteresisTest, MonitorResumesImmediately_WhenBecomeVisible) {
    // Pause a decoder manually, then confirm UpdateAll resumes it when visible.
    decoders[0]->Pause();
    ASSERT_FALSE(decoders[0]->IsRunning());

    auto views = AheadViews();
    culler.UpdateAll(views, layout, decoders);  // monitor 0 is visible
    EXPECT_TRUE(decoders[0]->IsRunning());
}

TEST_F(FrustumCullerHysteresisTest, NullDecoderEntries_SkippedSafely) {
    // Null entries in the decoders array must not cause a crash.
    decoders[1] = nullptr;
    auto views = AheadViews();
    EXPECT_NO_FATAL_FAILURE(culler.UpdateAll(views, layout, decoders));
}
