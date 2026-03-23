#pragma once

#include <openxr/openxr.h>

#include <array>
#include <cstdint>

class XrContext;
class XrPassthrough;
class MonitorLayout;
class FrustumCuller;
class VirtualMonitor;

// XrCompositor drives the per-frame composition loop:
//   1. Frustum-cull monitors → pause/resume decoders via VirtualMonitor.
//   2. For each active, visible monitor: call VirtualMonitor::GetCompositionLayer().
//   3. Build the layer array: passthrough (first) + up to 16 XrCompositionLayerQuad.
//   4. Call xrEndFrame.
class XrCompositor {
public:
    XrCompositor(XrContext&                        ctx,
                 XrPassthrough&                    passthrough,
                 MonitorLayout&                    layout,
                 FrustumCuller&                    culler,
                 std::array<VirtualMonitor*, 16>   monitors);

    ~XrCompositor();

    // Called once per iteration of the frame loop.
    void RenderFrame(const XrFrameState& frameState);

private:
    XrContext&                       ctx_;
    XrPassthrough&                   passthrough_;
    MonitorLayout&                   layout_;
    FrustumCuller&                   culler_;
    std::array<VirtualMonitor*, 16>  monitors_;

    // Reusable per-frame layer storage (no heap allocations in the hot path).
    std::array<const XrCompositionLayerBaseHeader*, 17> layerPtrs_{};
};
