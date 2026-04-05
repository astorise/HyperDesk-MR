#include "MonitorLayout.h"

#include "../util/Logger.h"

#include <algorithm>
#include <cmath>

namespace {

// ── Decagon geometry ─────────────────────────────────────────────────────────
// 3 screens forming 3 consecutive sides of a regular 10-sided polygon.
// sin(π/10) = sin(18°) ≈ 0.30902
constexpr float kSinPiOver10 = 0.30902f;
// Angle between adjacent screen centers as seen from the decagon center.
constexpr float kDecagonStep = 2.0f * static_cast<float>(M_PI) / 10.0f;  // 36°
// Distance from the viewer to the screen plane (meters).
// Previously derived from kHSpacing (~3.24m); halved for comfortable viewing.
constexpr float kDecagonRadius = 1.6f;

XrVector3f Add(XrVector3f a, XrVector3f b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

XrVector3f Sub(XrVector3f a, XrVector3f b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

XrVector3f Scale(XrVector3f v, float s) {
    return {v.x * s, v.y * s, v.z * s};
}

float Dot(XrVector3f a, XrVector3f b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

XrVector3f Normalize(XrVector3f v, XrVector3f fallback) {
    const float lenSq = Dot(v, v);
    if (lenSq <= 1e-6f) {
        return fallback;
    }

    const float invLen = 1.0f / std::sqrt(lenSq);
    return {v.x * invLen, v.y * invLen, v.z * invLen};
}

XrVector3f RotateVector(const XrQuaternionf& q, XrVector3f v) {
    const XrVector3f u{q.x, q.y, q.z};
    const float s = q.w;

    const XrVector3f crossUV{
        u.y * v.z - u.z * v.y,
        u.z * v.x - u.x * v.z,
        u.x * v.y - u.y * v.x
    };
    const XrVector3f crossUCrossUV{
        u.y * crossUV.z - u.z * crossUV.y,
        u.z * crossUV.x - u.x * crossUV.z,
        u.x * crossUV.y - u.y * crossUV.x
    };

    return Add(v, Add(Scale(crossUV, 2.0f * s), Scale(crossUCrossUV, 2.0f)));
}

// Quaternion multiply: result = a * b
XrQuaternionf QuatMul(const XrQuaternionf& a, const XrQuaternionf& b) {
    return {
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z
    };
}

// Y-axis rotation quaternion from angle in radians.
XrQuaternionf YawQuat(float yaw) {
    const float half = yaw * 0.5f;
    return {0.0f, std::sin(half), 0.0f, std::cos(half)};
}

// Yaw angle for each monitor on the decagonal arc.
// Monitor 0 = center (0°), 1 = left (+36°), 2 = right (-36°).
// Positive yaw rotates the screen's front face to the right (toward center
// for the left screen), keeping all panels facing the viewer at the origin.
float MonitorYaw(uint32_t index) {
    switch (index) {
        case 1:  return  kDecagonStep;  // left
        case 2:  return -kDecagonStep;  // right
        default: return  0.0f;          // center
    }
}

// Position on the decagonal arc (user is at the origin, screens face inward).
// Position angle is the negative of the face yaw: the left screen sits at -X
// but its face yaw is positive (rotated to face the viewer at center).
XrVector3f CanonicalPosition(uint32_t index) {
    const float posAngle = -MonitorYaw(index);
    return {
        kDecagonRadius * std::sin(posAngle),
        0.0f,
       -kDecagonRadius * std::cos(posAngle)
    };
}

XrVector3f HeadForward(const XrQuaternionf& q) {
    return RotateVector(q, {0.0f, 0.0f, -1.0f});
}

XrQuaternionf YawOnlyWallOrientation(XrVector3f horizontalForward) {
    horizontalForward = Normalize(horizontalForward, {0.0f, 0.0f, -1.0f});
    const float yaw = std::atan2(-horizontalForward.x, -horizontalForward.z);
    const float halfYaw = yaw * 0.5f;
    return {0.0f, std::sin(halfYaw), 0.0f, std::cos(halfYaw)};
}

}  // namespace

MonitorLayout::MonitorLayout() {
    for (uint32_t i = 0; i < kMaxMonitors; ++i) {
        monitors_[i].index = i;
        monitors_[i].rdpSurfaceId = UINT32_MAX;
        monitors_[i].active = false;
    }
}

void MonitorLayout::BuildDefaultLayout() {
    for (uint32_t i = 0; i < kMaxMonitors; ++i) {
        MonitorDescriptor& m = monitors_[i];
        m.index = i;

        const float yaw = MonitorYaw(i);
        m.worldPose.position = CanonicalPosition(i);
        m.worldPose.orientation = YawQuat(yaw);
        m.sizeMeters = {1.92f, 1.08f};
        // Normal points from screen toward the decagon center (the viewer).
        m.forwardNormal = {std::sin(yaw), 0.0f, std::cos(yaw)};
    }

    ApplyPrimaryAnchor();

    LOGI("MonitorLayout: decagon arc (3 panels, R=%.2f, step=%.1f°)",
         kDecagonRadius, kDecagonStep * 180.0f / static_cast<float>(M_PI));
}

void MonitorLayout::AnchorPrimaryToHeadPose(const XrPosef& headPose) {
    const bool hadPrimaryAnchor = hasPrimaryAnchor_;
    const XrVector3f previousAnchorPosition = primaryAnchorPosition_;

    XrVector3f horizontalForward = HeadForward(headPose.orientation);
    horizontalForward.y = 0.0f;
    horizontalForward = Normalize(horizontalForward, {0.0f, 0.0f, -1.0f});

    primaryAnchorOrientation_ = YawOnlyWallOrientation(horizontalForward);
    primaryAnchorPosition_ = Add(headPose.position, Scale(horizontalForward, kDecagonRadius));
    primaryAnchorPosition_.y = headPose.position.y;
    hasPrimaryAnchor_ = true;

    BuildDefaultLayout();

    const float dx = primaryAnchorPosition_.x - previousAnchorPosition.x;
    const float dy = primaryAnchorPosition_.y - previousAnchorPosition.y;
    const float dz = primaryAnchorPosition_.z - previousAnchorPosition.z;
    const float moveSq = dx * dx + dy * dy + dz * dz;
    if (!hadPrimaryAnchor || moveSq > 0.0025f) {
        LOGI("MonitorLayout: primary anchored to scan heading at (%.2f, %.2f, %.2f)",
             primaryAnchorPosition_.x,
             primaryAnchorPosition_.y,
             primaryAnchorPosition_.z);
    }
}

const MonitorDescriptor& MonitorLayout::GetMonitor(uint32_t index) const {
    if (index >= kMaxMonitors) {
        LOGE("MonitorLayout::GetMonitor index %u out of range", index);
        return monitors_[0];
    }
    return monitors_[index];
}

std::span<const MonitorDescriptor> MonitorLayout::GetAllMonitors() const {
    return {monitors_.data(), kMaxMonitors};
}

void MonitorLayout::BindSurface(uint32_t monitorIndex, uint32_t rdpSurfaceId) {
    if (monitorIndex >= kMaxMonitors) {
        return;
    }

    monitors_[monitorIndex].rdpSurfaceId = rdpSurfaceId;
    LOGI("MonitorLayout: monitor %u bound to RDP surface %u", monitorIndex, rdpSurfaceId);
}

void MonitorLayout::SetActiveCount(uint32_t count) {
    const uint32_t capped = std::min(count, kMaxMonitors);

    BuildDefaultLayout();

    if (capped == 1 && !hasPrimaryAnchor_) {
        monitors_[0].worldPose.position = {0.0f, 0.0f, kDepth};
    }

    for (uint32_t i = 0; i < kMaxMonitors; ++i) {
        monitors_[i].active = (i < capped);
    }
    const XrVector3f& primaryPos = monitors_[0].worldPose.position;
    LOGI("MonitorLayout: %u monitor(s) active primary=(%.2f, %.2f, %.2f) anchored=%d",
         capped,
         primaryPos.x,
         primaryPos.y,
         primaryPos.z,
         hasPrimaryAnchor_ ? 1 : 0);
}

void MonitorLayout::SetAllActive() {
    SetActiveCount(kMaxMonitors);
}

void MonitorLayout::ApplyPrimaryAnchor() {
    if (!hasPrimaryAnchor_) {
        return;
    }

    const XrVector3f canonicalPrimary = CanonicalPosition(0);

    for (uint32_t i = 0; i < kMaxMonitors; ++i) {
        MonitorDescriptor& monitor = monitors_[i];

        // Position: rotate the offset from primary around the anchor.
        const XrVector3f relative = Sub(CanonicalPosition(i), canonicalPrimary);
        monitor.worldPose.position = Add(
            primaryAnchorPosition_,
            RotateVector(primaryAnchorOrientation_, relative));

        // Orientation: anchor yaw * per-monitor yaw on the decagonal arc.
        const XrQuaternionf perMonitorYaw = YawQuat(MonitorYaw(i));
        monitor.worldPose.orientation = QuatMul(primaryAnchorOrientation_, perMonitorYaw);

        // Forward normal: from screen toward viewer (rotated).
        monitor.forwardNormal = RotateVector(monitor.worldPose.orientation, {0.0f, 0.0f, 1.0f});
    }
}
