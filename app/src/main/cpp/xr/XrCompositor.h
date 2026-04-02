#pragma once

#include <openxr/openxr.h>

#include <array>
#include <cstdint>
#include <mutex>

class XrContext;
class XrPassthrough;
class MonitorLayout;
class FrustumCuller;
class VirtualMonitor;
class StatusOverlay;

// XrCompositor drives the per-frame composition loop:
//   1. Frustum-cull monitors and pause/resume decoders via VirtualMonitor.
//   2. For each active, visible monitor: call VirtualMonitor::GetCompositionLayer().
//   3. Build the layer array: passthrough + overlay + up to 16 monitor quads.
//   4. Call xrEndFrame.
class XrCompositor {
public:
    XrCompositor(XrContext&                       ctx,
                 XrPassthrough&                   passthrough,
                 MonitorLayout&                   layout,
                 FrustumCuller&                   culler,
                 std::array<VirtualMonitor*, 16>  monitors);

    ~XrCompositor();

    // Called once per iteration of the frame loop.
    void RenderFrame(const XrFrameState& frameState);

    // Set an optional StatusOverlay to render between passthrough and monitors.
    void SetStatusOverlay(StatusOverlay* overlay) { statusOverlay_ = overlay; }

    // Returns the most recent cyclopean head pose sampled during rendering.
    bool TryGetLatestHeadPose(XrPosef& outPose) const;

private:
    XrContext&                      ctx_;
    XrPassthrough&                  passthrough_;
    MonitorLayout&                  layout_;
    FrustumCuller&                  culler_;
    std::array<VirtualMonitor*, 16> monitors_;
    StatusOverlay*                  statusOverlay_ = nullptr;

    // Reusable per-frame layer storage: passthrough + status overlay + 16 monitors.
    std::array<const XrCompositionLayerBaseHeader*, 18> layerPtrs_{};

    mutable std::mutex headPoseMutex_;
    XrPosef            latestHeadPose_{};
    bool               hasLatestHeadPose_ = false;
};
