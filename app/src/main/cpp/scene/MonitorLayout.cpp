#include "MonitorLayout.h"

#include "../util/Logger.h"

#include <algorithm>
#include <bit>
#include <cmath>

namespace {

// ── Decagon geometry ─────────────────────────────────────────────────────────
// Horizontal spacing between adjacent monitor columns.
constexpr float kArcStep = MonitorLayout::kAngularStepRadians;
// Distance from the viewer to the screen plane (meters).
constexpr float kArcRadius = 2.6f;
constexpr float kSplitRowOffsetY = 0.60f;

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

// Yaw angle for each monitor column on the arc.
// Monitor 0 = center (0°), 1 = left (+36°), 2 = right (-36°), 3 = far-left (+72°).
// Positive yaw rotates the screen's front face to the right (toward center
// for the left screen), keeping all panels facing the viewer at the origin.
float MonitorYaw(uint32_t index, bool splitRows) {
    if (splitRows) {
        // In split mode, add screens in vertical pairs:
        // 0(top),1(bottom), then 2(top),3(bottom), ...
        // Columns advance continuously to the right.
        const uint32_t column = index / 2;
        return -static_cast<float>(column) * kArcStep;
    }

    if (index == 0) {
        return 0.0f;
    }

    const uint32_t ring = (index + 1u) / 2u;
    const float magnitude = static_cast<float>(ring) * kArcStep;
    const bool isLeft = (index & 1u) == 1u;
    return isLeft ? magnitude : -magnitude;
}

// For cylinder layers, horizontal spread is controlled by per-monitor yaw.
// Split mode only offsets monitors vertically.
XrVector3f CanonicalPosition(uint32_t index, bool splitRows) {
    if (!splitRows) {
        return {0.0f, 0.0f, 0.0f};
    }

    const bool topRow = (index % 2u) == 0u;
    return {0.0f, topRow ? +kSplitRowOffsetY : -kSplitRowOffsetY, 0.0f};
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

        const float yaw = MonitorYaw(i, splitRows_);
        m.worldPose.position = CanonicalPosition(i, splitRows_);
        m.worldPose.orientation = YawQuat(yaw);
        m.sizeMeters = {1.92f, 1.08f};
        // Normal points from screen toward the arc center (the viewer).
        m.forwardNormal = {std::sin(yaw), 0.0f, std::cos(yaw)};
    }

    ApplyPrimaryAnchor();

    LOGI("MonitorLayout: arc layout (%u panels max, R=%.2f, step=%.1f°)",
         kMaxMonitors,
         kArcRadius, kArcStep * 180.0f / static_cast<float>(M_PI));
}

void MonitorLayout::AnchorPrimaryToHeadPose(const XrPosef& headPose) {
    const bool hadPrimaryAnchor = hasPrimaryAnchor_;
    const XrVector3f previousAnchorPosition = primaryAnchorPosition_;

    XrVector3f horizontalForward = HeadForward(headPose.orientation);
    horizontalForward.y = 0.0f;
    horizontalForward = Normalize(horizontalForward, {0.0f, 0.0f, -1.0f});

    primaryAnchorOrientation_ = YawOnlyWallOrientation(horizontalForward);
    // Cylinder center is 0.5m behind the viewer so screens feel further away.
    primaryAnchorPosition_ = Add(headPose.position, Scale(horizontalForward, -0.5f));
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
    activeMask_ = 0;
    for (uint32_t i = 0; i < capped; ++i) {
        activeMask_ |= (1u << i);
    }
    activeCount_ = capped;

    BuildDefaultLayout();

    if (capped == 1 && !hasPrimaryAnchor_) {
        monitors_[0].worldPose.position = {0.0f, 0.0f, kDepth};
    }

    RefreshActiveFlagsFromMask();
    const XrVector3f& primaryPos = monitors_[0].worldPose.position;
    LOGI("MonitorLayout: %u monitor(s) active primary=(%.2f, %.2f, %.2f) anchored=%d",
         capped,
         primaryPos.x,
         primaryPos.y,
         primaryPos.z,
         hasPrimaryAnchor_ ? 1 : 0);
}

void MonitorLayout::SetMonitorActive(uint32_t index, bool active) {
    if (index >= kMaxMonitors) {
        return;
    }

    if (active) {
        activeMask_ |= (1u << index);
    } else {
        activeMask_ &= ~(1u << index);
    }

    activeCount_ = static_cast<uint32_t>(std::popcount(activeMask_));
    monitors_[index].active = active;
}

bool MonitorLayout::IsMonitorActive(uint32_t index) const {
    if (index >= kMaxMonitors) {
        return false;
    }
    return (activeMask_ & (1u << index)) != 0;
}

void MonitorLayout::SetSplitRows(bool enabled) {
    if (splitRows_ == enabled) {
        return;
    }

    splitRows_ = enabled;
    BuildDefaultLayout();
    RefreshActiveFlagsFromMask();

    LOGI("MonitorLayout: split rows %s", splitRows_ ? "enabled" : "disabled");
}

void MonitorLayout::SetAllActive() {
    SetActiveCount(kMaxMonitors);
}

void MonitorLayout::ApplyPrimaryAnchor() {
    if (!hasPrimaryAnchor_) {
        return;
    }

    const XrVector3f canonicalPrimary = CanonicalPosition(0, splitRows_);

    for (uint32_t i = 0; i < kMaxMonitors; ++i) {
        MonitorDescriptor& monitor = monitors_[i];

        // Position: rotate the offset from primary around the anchor.
        const XrVector3f relative = Sub(CanonicalPosition(i, splitRows_), canonicalPrimary);
        monitor.worldPose.position = Add(
            primaryAnchorPosition_,
            RotateVector(primaryAnchorOrientation_, relative));

        // Orientation: anchor yaw * per-monitor yaw on the arc.
        const XrQuaternionf perMonitorYaw = YawQuat(MonitorYaw(i, splitRows_));
        monitor.worldPose.orientation = QuatMul(primaryAnchorOrientation_, perMonitorYaw);

        // Forward normal: from screen toward viewer (rotated).
        monitor.forwardNormal = RotateVector(monitor.worldPose.orientation, {0.0f, 0.0f, 1.0f});
    }
}

void MonitorLayout::NudgeAnchor(float rightMeters, float upMeters, float towardViewerMeters) {
    if (!hasPrimaryAnchor_) {
        primaryAnchorPosition_ = monitors_[0].worldPose.position;
        primaryAnchorOrientation_ = monitors_[0].worldPose.orientation;
        hasPrimaryAnchor_ = true;
    }

    const XrVector3f right = RotateVector(primaryAnchorOrientation_, {1.0f, 0.0f, 0.0f});
    const XrVector3f up = RotateVector(primaryAnchorOrientation_, {0.0f, 1.0f, 0.0f});
    const XrVector3f towardViewer = RotateVector(primaryAnchorOrientation_, {0.0f, 0.0f, 1.0f});

    primaryAnchorPosition_ = Add(
        primaryAnchorPosition_,
        Add(
            Scale(right, rightMeters),
            Add(Scale(up, upMeters), Scale(towardViewer, towardViewerMeters))));

    ApplyPrimaryAnchor();
}

void MonitorLayout::RotateAnchorYaw(float yawRadians) {
    if (!hasPrimaryAnchor_) {
        primaryAnchorPosition_ = monitors_[0].worldPose.position;
        primaryAnchorOrientation_ = monitors_[0].worldPose.orientation;
        hasPrimaryAnchor_ = true;
    }

    if (std::fabs(yawRadians) <= 1e-6f) {
        return;
    }

    primaryAnchorOrientation_ = QuatMul(YawQuat(yawRadians), primaryAnchorOrientation_);
    ApplyPrimaryAnchor();
}

void MonitorLayout::RotateAnchorYawAroundPivot(float yawRadians, const XrVector3f& pivotPosition) {
    if (!hasPrimaryAnchor_) {
        primaryAnchorPosition_ = monitors_[0].worldPose.position;
        primaryAnchorOrientation_ = monitors_[0].worldPose.orientation;
        hasPrimaryAnchor_ = true;
    }

    if (std::fabs(yawRadians) <= 1e-6f) {
        return;
    }

    const XrQuaternionf yawQuat = YawQuat(yawRadians);
    const XrVector3f relative = Sub(primaryAnchorPosition_, pivotPosition);
    primaryAnchorPosition_ = Add(pivotPosition, RotateVector(yawQuat, relative));
    primaryAnchorOrientation_ = QuatMul(yawQuat, primaryAnchorOrientation_);
    ApplyPrimaryAnchor();
}

void MonitorLayout::RefreshActiveFlagsFromMask() {
    for (uint32_t i = 0; i < kMaxMonitors; ++i) {
        monitors_[i].active = (activeMask_ & (1u << i)) != 0;
    }
}
