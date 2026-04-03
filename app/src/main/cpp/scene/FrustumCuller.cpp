#include "FrustumCuller.h"
#include "MonitorLayout.h"
#include "../codec/MediaCodecDecoder.h"
#include "../util/Logger.h"

#include <cmath>

// Quest 3 approximate horizontal half-FOV in degrees.
static constexpr float kHalfFovDeg = 55.0f;

FrustumCuller::FrustumCuller(float fovSlackRadians) {
    const float halfFovRad = kHalfFovDeg * (M_PI / 180.0f);
    cosThreshold_ = std::cos(halfFovRad + fovSlackRadians);
    // Initialise all hysteresis counters to "already visible."
    for (int& h : hysteresis_) h = kHysteresisFrames;
    LOGI("FrustumCuller: cosThreshold=%.4f (halfFov=%.1f° + slack=%.3f rad)",
         cosThreshold_, kHalfFovDeg, fovSlackRadians);
}

// ── TestMonitor ───────────────────────────────────────────────────────────────

FrustumCuller::CullResult FrustumCuller::TestMonitor(
    std::span<const XrView, 2> views,
    const XrVector3f& monitorWorldPos) const {

    XrVector3f eyePos{}, forward{};
    ComputeCyclopsView(views, eyePos, forward);

    XrVector3f toMonitor = Normalize(Sub(monitorWorldPos, eyePos));
    float dp = Dot(forward, toMonitor);

    return CullResult{dp >= cosThreshold_, dp};
}

// ── UpdateAll ─────────────────────────────────────────────────────────────────

void FrustumCuller::UpdateAll(std::span<const XrView, 2> views,
                               const MonitorLayout& layout,
                               std::array<MediaCodecDecoder*, MonitorLayout::kMaxMonitors>& decoders) {
    XrVector3f eyePos{}, forward{};
    ComputeCyclopsView(views, eyePos, forward);

    for (uint32_t i = 0; i < MonitorLayout::kMaxMonitors; ++i) {
        if (!decoders[i]) continue;

        const MonitorDescriptor& m = layout.GetMonitor(i);
        const XrVector3f& pos = m.worldPose.position;

        XrVector3f toMonitor = Normalize(Sub(pos, eyePos));
        float dp = Dot(forward, toMonitor);
        bool visible = (dp >= cosThreshold_);

        if (visible) {
            hysteresis_[i] = kHysteresisFrames;
            if (!decoders[i]->IsRunning()) {
                decoders[i]->Resume();
                LOGD("FrustumCuller: monitor %u resumed (dp=%.3f)", i, dp);
            }
        } else {
            if (hysteresis_[i] > 0) {
                --hysteresis_[i];
            } else if (decoders[i]->IsRunning()) {
                decoders[i]->Pause();
                LOGD("FrustumCuller: monitor %u paused (dp=%.3f)", i, dp);
            }
        }
    }
}

// ── Math helpers ──────────────────────────────────────────────────────────────

void FrustumCuller::ComputeCyclopsView(std::span<const XrView, 2> views,
                                        XrVector3f& outEyePos,
                                        XrVector3f& outForward) const {
    // Cyclopean position: average of left and right eye positions.
    outEyePos = Scale(Add(views[0].pose.position, views[1].pose.position), 0.5f);
    // Forward direction derived from the left eye orientation.
    outForward = QuatRotateForward(views[0].pose.orientation);
}

XrVector3f FrustumCuller::QuatRotateForward(const XrQuaternionf& q) {
    // Rotate {0, 0, -1} (OpenXR -Z forward) by quaternion q.
    // Result = q * (0,0,-1,0) * q^-1  (quaternion sandwich product).
    const float x = q.x, y = q.y, z = q.z, w = q.w;
    // Expanded rotation of (0, 0, -1):
    return XrVector3f{
         2.0f * (x * z + w * y),
         2.0f * (y * z - w * x),
        -(1.0f - 2.0f * (x * x + y * y))
    };
}

XrVector3f FrustumCuller::Sub(XrVector3f a, XrVector3f b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

float FrustumCuller::Dot(XrVector3f a, XrVector3f b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

XrVector3f FrustumCuller::Normalize(XrVector3f v) {
    float len = std::sqrt(Dot(v, v));
    if (len < 1e-6f) return {0.f, 0.f, -1.f};
    float inv = 1.0f / len;
    return {v.x * inv, v.y * inv, v.z * inv};
}

XrVector3f FrustumCuller::Scale(XrVector3f v, float s) {
    return {v.x * s, v.y * s, v.z * s};
}

XrVector3f FrustumCuller::Add(XrVector3f a, XrVector3f b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
