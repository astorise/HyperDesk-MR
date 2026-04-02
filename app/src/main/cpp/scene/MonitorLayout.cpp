#include "MonitorLayout.h"

#include "../util/Logger.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr float kPrimaryDistance = -MonitorLayout::kDepth;

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

XrVector3f CanonicalPosition(uint32_t index) {
    const float xStart = -static_cast<float>(MonitorLayout::kGridCols - 1) *
                         MonitorLayout::kHSpacing / 2.0f;
    const float yStart = static_cast<float>(MonitorLayout::kGridRows - 1) *
                         MonitorLayout::kVSpacing / 2.0f;

    const uint32_t col = index % MonitorLayout::kGridCols;
    const uint32_t row = index / MonitorLayout::kGridCols;

    return {
        xStart + static_cast<float>(col) * MonitorLayout::kHSpacing,
        yStart - static_cast<float>(row) * MonitorLayout::kVSpacing,
        MonitorLayout::kDepth
    };
}

XrVector3f HeadForward(const XrQuaternionf& q) {
    return {
         2.0f * (q.x * q.z + q.w * q.y),
         2.0f * (q.y * q.z - q.w * q.x),
        -(1.0f - 2.0f * (q.x * q.x + q.y * q.y))
    };
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
    const float xStart = -static_cast<float>(kGridCols - 1) * kHSpacing / 2.0f;
    const float yStart = static_cast<float>(kGridRows - 1) * kVSpacing / 2.0f;

    for (uint32_t i = 0; i < kMaxMonitors; ++i) {
        const uint32_t col = i % kGridCols;
        const uint32_t row = i / kGridCols;

        MonitorDescriptor& m = monitors_[i];
        m.index = i;

        const float x = xStart + static_cast<float>(col) * kHSpacing;
        const float y = yStart - static_cast<float>(row) * kVSpacing;

        m.worldPose.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
        m.worldPose.position = {x, y, kDepth};
        m.sizeMeters = {1.92f, 1.08f};
        m.forwardNormal = {0.0f, 0.0f, 1.0f};
    }

    ApplyPrimaryAnchor();

    LOGI("MonitorLayout: 4x4 grid built - x=[%.2f, %.2f] y=[%.2f, %.2f] z=%.2f",
         xStart, -xStart, -yStart, yStart, kDepth);
}

void MonitorLayout::AnchorPrimaryToHeadPose(const XrPosef& headPose) {
    const bool hadPrimaryAnchor = hasPrimaryAnchor_;
    const XrVector3f previousAnchorPosition = primaryAnchorPosition_;

    XrVector3f horizontalForward = HeadForward(headPose.orientation);
    horizontalForward.y = 0.0f;
    horizontalForward = Normalize(horizontalForward, {0.0f, 0.0f, -1.0f});

    primaryAnchorOrientation_ = YawOnlyWallOrientation(horizontalForward);
    primaryAnchorPosition_ = Add(headPose.position, Scale(horizontalForward, kPrimaryDistance));
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
    const XrVector3f viewerNormal = Normalize(
        RotateVector(primaryAnchorOrientation_, {0.0f, 0.0f, 1.0f}),
        {0.0f, 0.0f, 1.0f});

    for (uint32_t i = 0; i < kMaxMonitors; ++i) {
        MonitorDescriptor& monitor = monitors_[i];
        const XrVector3f relative = Sub(CanonicalPosition(i), canonicalPrimary);
        monitor.worldPose.position = Add(
            primaryAnchorPosition_,
            RotateVector(primaryAnchorOrientation_, relative));
        monitor.worldPose.orientation = primaryAnchorOrientation_;
        monitor.forwardNormal = viewerNormal;
    }
}
