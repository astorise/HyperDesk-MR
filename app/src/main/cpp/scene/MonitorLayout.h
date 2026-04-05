#pragma once

#include <openxr/openxr.h>

#include <array>
#include <cstdint>
#include <span>

// Describes one virtual monitor: its world-space pose, pixel dimensions,
// and the RDP GFX surface ID bound to it.
struct MonitorDescriptor {
    uint32_t    index;         // 0-15
    uint32_t    rdpSurfaceId;  // FreeRDP GFX surface ID; UINT32_MAX = unbound
    XrPosef     worldPose;     // center pose in STAGE reference space
    XrVector2f  sizeMeters;    // physical size of the quad in meters
    XrVector3f  forwardNormal; // unit normal pointing toward the viewer
    bool        active;        // true after DisplayControl negotiation
};

class MonitorLayout {
public:
    static constexpr uint32_t kMaxMonitors = 3;
    static constexpr uint32_t kGridCols = 3;
    static constexpr uint32_t kGridRows = 1;

    // Physical spacing between monitor centers (meters).
    static constexpr float kHSpacing = 1.55f;  // decagon R ≈ 2.5m
    static constexpr float kVSpacing = 1.15f;  // 1.08m screen + 0.07m gap

    // Distance from the STAGE origin to the monitor plane (negative = in front).
    static constexpr float kDepth = -2.5f;

    MonitorLayout();

    // Compute a 4-column x 4-row grid centered on (0, 0, kDepth).
    // Row 0 is topmost. Column 0 is leftmost.
    void BuildDefaultLayout();

    // Anchor monitor[0] to the current headset heading while keeping the wall upright.
    void AnchorPrimaryToHeadPose(const XrPosef& headPose);

    const MonitorDescriptor& GetMonitor(uint32_t index) const;
    std::span<const MonitorDescriptor> GetAllMonitors() const;

    // Called by RdpDisplayControl when the server assigns a GFX surface ID.
    void BindSurface(uint32_t monitorIndex, uint32_t rdpSurfaceId);

    // Mark the first N monitors as active and the rest inactive.
    void SetActiveCount(uint32_t count);

    // Mark all monitors as active (called after layout PDU is sent).
    void SetAllActive();

private:
    void ApplyPrimaryAnchor();

    std::array<MonitorDescriptor, kMaxMonitors> monitors_{};
    XrVector3f                                  primaryAnchorPosition_{};
    XrQuaternionf                               primaryAnchorOrientation_{0.0f, 0.0f, 0.0f, 1.0f};
    bool                                        hasPrimaryAnchor_ = false;
};
