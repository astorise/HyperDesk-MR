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
constexpr float kArcRadius = 1.6f;
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

// Base yaw for a monitor (without scroll).
float MonitorBaseYaw(uint32_t index, bool splitRows) {
    if (splitRows) {
        const uint32_t column = index / 2;
        return -static_cast<float>(column) * kArcStep;
    }
    return -static_cast<float>(index) * kArcStep;
}

// Yaw angle for each monitor column on the arc, including carousel scroll.
// Sequential: mon0 at center (0°), then mon1, mon2, ... extend to the right
// (negative yaw = clockwise = visual right).  scrollYaw shifts the entire
// wall (positive = wall shifts counter-clockwise, revealing right-side monitors).
float MonitorYaw(uint32_t index, bool splitRows, float scrollYaw) {
    return MonitorBaseYaw(index, splitRows) + scrollYaw;
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

        const float yaw = MonitorYaw(i, splitRows_, scrollYaw_);
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
    // Cylinder center is at the viewer's position — screens sit on the
    // cylinder surface at kCylinderRadius distance.
    primaryAnchorPosition_ = headPose.position;
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

        // Orientation: anchor yaw * per-monitor yaw on the arc (includes scroll).
        const XrQuaternionf perMonitorYaw = YawQuat(MonitorYaw(i, splitRows_, scrollYaw_));
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

// ── Carousel ────────────────────────────────────────────────────────────────

void MonitorLayout::UpdateCarousel(uint32_t cursorMonitorIdx) {
    if (cursorMonitorIdx >= kMaxMonitors) return;

    const float baseYaw = MonitorBaseYaw(cursorMonitorIdx, splitRows_);
    const float effectiveYaw = baseYaw + scrollYaw_;

    // Comfort zone: ±60°.  When the cursor monitor's center angle exceeds
    // this threshold the wall scrolls to bring it back to the boundary.
    constexpr float kScrollThreshold = 1.0471975f;  // 60° in radians

    float targetScroll = scrollYaw_;
    if (effectiveYaw < -kScrollThreshold) {
        // Cursor too far right → scroll wall left (increase scrollYaw).
        targetScroll = -kScrollThreshold - baseYaw;
    } else if (effectiveYaw > kScrollThreshold) {
        // Cursor too far left → scroll wall right (decrease scrollYaw).
        targetScroll = kScrollThreshold - baseYaw;
    }

    // Clamp: don't scroll mon0 past center (no monitors to the left of it).
    targetScroll = std::max(0.0f, targetScroll);

    const float prev = scrollYaw_;

    // Smooth exponential interpolation (~0.3 s to 90 % at 72 Hz).
    constexpr float kSmooth = 0.12f;
    scrollYaw_ += (targetScroll - scrollYaw_) * kSmooth;
    if (std::fabs(targetScroll - scrollYaw_) < 0.001f) {
        scrollYaw_ = targetScroll;
    }

    if (scrollYaw_ != prev) {
        BuildDefaultLayout();
    }
}

void MonitorLayout::UpdateHeadScroll(const XrPosef& headPose) {
    if (!hasPrimaryAnchor_) return;

    // Compute the horizontal yaw angle between the head gaze and the wall
    // anchor forward direction (both projected onto the XZ plane).
    XrVector3f headFwd = HeadForward(headPose.orientation);
    headFwd.y = 0.0f;
    headFwd = Normalize(headFwd, {0.0f, 0.0f, -1.0f});

    XrVector3f anchorFwd = RotateVector(primaryAnchorOrientation_, {0.0f, 0.0f, -1.0f});
    anchorFwd.y = 0.0f;
    anchorFwd = Normalize(anchorFwd, {0.0f, 0.0f, -1.0f});

    // Signed yaw difference: positive = head turned left, negative = turned right.
    // cross.y gives the sine of the angle (with correct sign).
    const float dot = Dot(headFwd, anchorFwd);
    const float crossY = anchorFwd.x * headFwd.z - anchorFwd.z * headFwd.x;
    const float angle = std::atan2(crossY, dot);  // radians, signed

    constexpr float kHeadThreshold = 1.3089969f;  // 75° in radians
    constexpr float kMaxScrollSpeed = 0.06f;       // radians per frame at 90°+

    if (std::fabs(angle) <= kHeadThreshold) return;

    // Excess past 75°, normalized: 0 at threshold, 1 at 90°.
    const float excess = std::fabs(angle) - kHeadThreshold;
    constexpr float kRange = 1.5707963f - kHeadThreshold;  // 90° − 75° = 15°
    const float t = std::min(excess / kRange, 1.0f);
    const float speed = kMaxScrollSpeed * t * t;  // quadratic ramp

    // Head turned right (negative angle) → wall follows gaze (decrease scroll
    // so monitors slide right, revealing left-side content).
    // Head turned left (positive angle) → wall follows gaze (increase scroll).
    const float prev = scrollYaw_;
    if (angle > 0.0f) {
        scrollYaw_ += speed;
    } else {
        scrollYaw_ -= speed;
    }

    // Clamp: don't scroll mon0 past center.
    scrollYaw_ = std::max(0.0f, scrollYaw_);

    if (scrollYaw_ != prev) {
        BuildDefaultLayout();
    }
}

bool MonitorLayout::IsMonitorInView(uint32_t index) const {
    if (index >= kMaxMonitors) return false;

    const float effectiveYaw = MonitorBaseYaw(index, splitRows_) + scrollYaw_;
    constexpr float kMaxAngle = 1.5707963f;  // 90° in radians
    return std::fabs(effectiveYaw) <= kMaxAngle;
}

XrPosef MonitorLayout::GetToolbarAnchorPose() const {
    if (!hasPrimaryAnchor_) {
        return monitors_[0].worldPose;
    }

    // Unscrolled mon0 pose: primary anchor orientation with no per-monitor yaw.
    XrPosef pose;
    pose.position = primaryAnchorPosition_;
    pose.orientation = primaryAnchorOrientation_;

    if (splitRows_) {
        pose.position = Add(pose.position,
            RotateVector(primaryAnchorOrientation_, {0.0f, kSplitRowOffsetY, 0.0f}));
    }
    return pose;
}

void MonitorLayout::RefreshActiveFlagsFromMask() {
    for (uint32_t i = 0; i < kMaxMonitors; ++i) {
        monitors_[i].active = (activeMask_ & (1u << i)) != 0;
    }
}
