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
    static constexpr uint32_t kMaxMonitors = 16;
    static constexpr uint32_t kGridCols = 16;
    static constexpr uint32_t kGridRows = 1;
    // Horizontal angular spacing between adjacent monitor columns (decagon step).
    static constexpr float kAngularStepRadians = 0.62831853f;  // 36° = 2π/10

    // Physical spacing between monitor centers (meters).
    static constexpr float kHSpacing = 2.0f;   // 1.92m screen + 0.08m gap
    static constexpr float kVSpacing = 1.15f;  // 1.08m screen + 0.07m gap

    // Distance from the STAGE origin to the monitor plane (negative = in front).
    static constexpr float kDepth = -2.5f;

    MonitorLayout();

    // Recompute monitor poses on the curved wall from the current anchor,
    // split mode, and angular spacing.
    void BuildDefaultLayout();

    // Anchor monitor[0] to the current headset heading while keeping the wall upright.
    void AnchorPrimaryToHeadPose(const XrPosef& headPose);

    const MonitorDescriptor& GetMonitor(uint32_t index) const;
    std::span<const MonitorDescriptor> GetAllMonitors() const;

    // Called by RdpDisplayControl when the server assigns a GFX surface ID.
    void BindSurface(uint32_t monitorIndex, uint32_t rdpSurfaceId);

    // Mark the first N monitors as active and the rest inactive.
    void SetActiveCount(uint32_t count);
    uint32_t GetActiveCount() const { return activeCount_; }
    void SetMonitorActive(uint32_t index, bool active);
    bool IsMonitorActive(uint32_t index) const;

    // Toggle visual split mode:
    // - false: one horizontal row (default)
    // - true: two rows
    void SetSplitRows(bool enabled);
    bool IsSplitRows() const { return splitRows_; }

    // Move the anchored wall in local wall space.
    // rightMeters: +right / -left
    // upMeters: +up / -down
    // towardViewerMeters: +toward viewer / -away from viewer
    void NudgeAnchor(float rightMeters, float upMeters, float towardViewerMeters);
    // Rotate the anchored wall around the vertical axis.
    void RotateAnchorYaw(float yawRadians);
    // Rotate around a world-space pivot (typically headset position) so the
    // curved wall keeps its orbit around the viewer.
    void RotateAnchorYawAroundPivot(float yawRadians, const XrVector3f& pivotPosition);

    // Mark all monitors as active (called after layout PDU is sent).
    void SetAllActive();

private:
    void ApplyPrimaryAnchor();
    void RefreshActiveFlagsFromMask();

    std::array<MonitorDescriptor, kMaxMonitors> monitors_{};
    XrVector3f                                  primaryAnchorPosition_{};
    XrQuaternionf                               primaryAnchorOrientation_{0.0f, 0.0f, 0.0f, 1.0f};
    bool                                        hasPrimaryAnchor_ = false;
    bool                                        splitRows_ = false;
    uint32_t                                    activeMask_ = 0;
    uint32_t                                    activeCount_ = 0;
};
