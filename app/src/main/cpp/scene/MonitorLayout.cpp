#include "MonitorLayout.h"
#include "../util/Logger.h"

#include <cmath>
#include <stdexcept>

MonitorLayout::MonitorLayout() {
    for (uint32_t i = 0; i < kMaxMonitors; ++i) {
        monitors_[i].index        = i;
        monitors_[i].rdpSurfaceId = UINT32_MAX;
        monitors_[i].active       = false;
    }
}

void MonitorLayout::BuildDefaultLayout() {
    // Grid centre is at (0, 0, kDepth).
    // Total grid width  = (kGridCols - 1) * kHSpacing = 3 * 2.0 = 6.0m
    // Total grid height = (kGridRows - 1) * kVSpacing = 3 * 1.15 = 3.45m
    // Top-left monitor centre: (-3.0, +1.725, kDepth)
    const float xStart = -static_cast<float>(kGridCols - 1) * kHSpacing / 2.0f;
    const float yStart =  static_cast<float>(kGridRows - 1) * kVSpacing / 2.0f;

    for (uint32_t i = 0; i < kMaxMonitors; ++i) {
        const uint32_t col = i % kGridCols;
        const uint32_t row = i / kGridCols;

        MonitorDescriptor& m = monitors_[i];
        m.index = i;

        // World position: centred in the grid, at kDepth metres ahead.
        float x = xStart + static_cast<float>(col) * kHSpacing;
        float y = yStart - static_cast<float>(row) * kVSpacing;

        // Identity quaternion — all monitors face the viewer (toward +Z).
        m.worldPose.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
        m.worldPose.position    = {x, y, kDepth};
        m.sizeMeters            = {1.92f, 1.08f};
        m.forwardNormal         = {0.0f, 0.0f, 1.0f};  // points toward +Z (toward viewer)
    }

    LOGI("MonitorLayout: 4×4 grid built — x∈[%.2f, %.2f] y∈[%.2f, %.2f] z=%.2f",
         xStart, -xStart, -yStart, yStart, kDepth);
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
    if (monitorIndex >= kMaxMonitors) return;
    monitors_[monitorIndex].rdpSurfaceId = rdpSurfaceId;
    LOGI("MonitorLayout: monitor %u bound to RDP surface %u", monitorIndex, rdpSurfaceId);
}

void MonitorLayout::SetAllActive() {
    for (auto& m : monitors_) m.active = true;
}
