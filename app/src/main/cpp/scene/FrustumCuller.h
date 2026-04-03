#pragma once

#include <openxr/openxr.h>
#include <array>
#include <cstdint>
#include <span>

#include "MonitorLayout.h"
class MediaCodecDecoder;

// FrustumCuller performs a per-frame dot-product visibility test for each of
// the 16 virtual monitors.  Monitors outside the headset's field of view have
// their AMediaCodec decoder paused to preserve the thermal/battery budget.
//
// A 2-frame hysteresis counter prevents rapid pause/resume flicker when a
// monitor sits on the edge of the FOV.
class FrustumCuller {
public:
    // fovSlackRadians: extra angular margin added to the FOV half-angle before
    // culling — prevents edge-of-FOV popping.  Defaults to ~8.6°.
    explicit FrustumCuller(float fovSlackRadians = 0.15f);

    struct CullResult {
        bool  visible;
        float dotProduct;
    };

    // Test a single monitor.  views must be the stereo pair from xrLocateViews.
    CullResult TestMonitor(std::span<const XrView, 2> views,
                           const XrVector3f& monitorWorldPos) const;

    // Test all 16 monitors and call Pause()/Resume() on MediaCodecDecoders.
    // decoders may contain null entries (uninitialised slots are skipped).
    void UpdateAll(std::span<const XrView, 2> views,
                   const MonitorLayout& layout,
                   std::array<MediaCodecDecoder*, MonitorLayout::kMaxMonitors>& decoders);

private:
    // cos(halfFovDeg + slack) — precomputed in the constructor.
    // Quest 3 horizontal half-FOV ≈ 55°; we use the larger axis as the cone radius.
    float   cosThreshold_;

    // Per-monitor countdown before an out-of-FOV monitor is actually paused.
    // Reset to kHysteresisFrames whenever the monitor is visible.
    static constexpr int kHysteresisFrames = 2;
    int hysteresis_[MonitorLayout::kMaxMonitors]{};

    // ── Math helpers ──────────────────────────────────────────────────────────

    // Compute the cyclopean (average) eye position and forward direction.
    void ComputeCyclopsView(std::span<const XrView, 2> views,
                             XrVector3f& outEyePos,
                             XrVector3f& outForward) const;

    // Rotate direction {0,0,-1} by a quaternion to get the view forward vector.
    static XrVector3f QuatRotateForward(const XrQuaternionf& q);

    // Vector math helpers.
    static XrVector3f Sub(XrVector3f a, XrVector3f b);
    static float      Dot(XrVector3f a, XrVector3f b);
    static XrVector3f Normalize(XrVector3f v);
    static XrVector3f Scale(XrVector3f v, float s);
    static XrVector3f Add(XrVector3f a, XrVector3f b);
};
