#pragma once

#include <openxr/openxr.h>

#include <array>
#include <cstdint>
#include <memory>

#include "../scene/MonitorLayout.h"

class XrContext;
class XrPassthrough;
class MonitorLayout;
class FrustumCuller;
class VirtualMonitor;
class StatusOverlay;
class CursorOverlay;
class RdpInputForwarder;

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
                 std::array<VirtualMonitor*, MonitorLayout::kMaxMonitors>  monitors);

    ~XrCompositor();

    // Called once per iteration of the frame loop.
    void RenderFrame(const XrFrameState& frameState);

    // Set an optional StatusOverlay to render between passthrough and monitors.
    void SetStatusOverlay(StatusOverlay* overlay) { statusOverlay_ = overlay; }

    // Set an optional CursorOverlay and input forwarder for mouse cursor rendering.
    void SetCursorOverlay(CursorOverlay* cursor, RdpInputForwarder* forwarder) {
        cursorOverlay_ = cursor;
        inputForwarder_ = forwarder;
    }

private:
    XrContext&                      ctx_;
    XrPassthrough&                  passthrough_;
    MonitorLayout&                  layout_;
    FrustumCuller&                  culler_;
    std::array<VirtualMonitor*, MonitorLayout::kMaxMonitors> monitors_;
    StatusOverlay*                  statusOverlay_ = nullptr;
    CursorOverlay*                  cursorOverlay_ = nullptr;
    RdpInputForwarder*              inputForwarder_ = nullptr;

    // Reusable per-frame layer storage: passthrough + status overlay + monitors + cursor.
    std::array<const XrCompositionLayerBaseHeader*, 3 + MonitorLayout::kMaxMonitors> layerPtrs_{};
};
