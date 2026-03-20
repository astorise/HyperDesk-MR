#pragma once

#include <openxr/openxr.h>

#include <array>
#include <cstdint>
#include <memory>
#include <span>

class XrContext;
class XrPassthrough;
class XrSwapchain;
class MonitorLayout;
class FrustumCuller;
class MediaCodecDecoder;
class CodecSurfaceBridge;

// XrCompositor owns per-monitor XrSwapchains and drives the per-frame
// composition loop:
//   1. Frustum-cull monitors → pause/resume decoders.
//   2. For each visible monitor: acquire the decoded frame, acquire swapchain image,
//      bind the AHardwareBuffer (first frame per slot), release.
//   3. Build the layer array: passthrough (first) + up to 16 XrCompositionLayerQuad.
//   4. Call xrEndFrame.
class XrCompositor {
public:
    XrCompositor(XrContext&   ctx,
                 XrPassthrough& passthrough,
                 MonitorLayout& layout,
                 FrustumCuller& culler,
                 std::array<std::unique_ptr<CodecSurfaceBridge>, 16>& bridges,
                 std::array<MediaCodecDecoder*, 16>                    decoders);

    ~XrCompositor();

    // Called once per iteration of the frame loop.
    void RenderFrame(const XrFrameState& frameState);

private:
    XrContext&      ctx_;
    XrPassthrough&  passthrough_;
    MonitorLayout&  layout_;
    FrustumCuller&  culler_;
    std::array<std::unique_ptr<CodecSurfaceBridge>, 16>& bridges_;
    std::array<MediaCodecDecoder*, 16>                    decoders_;

    // One swapchain per monitor.
    std::array<std::unique_ptr<XrSwapchain>, 16> swapchains_;

    // Per-slot flag: has the AHardwareBuffer been bound to swapchain memory yet?
    // We bind once per slot on first use.
    std::array<bool, 16> slotBound_{};

    // Reusable per-frame layer storage (no allocations in the hot path).
    std::array<XrCompositionLayerQuad,              16> quadLayers_{};
    // +1 for the passthrough layer, +1 spare.
    std::array<const XrCompositionLayerBaseHeader*, 17> layerPtrs_{};

    XrCompositionLayerQuad BuildQuadLayer(uint32_t monitorIndex,
                                          uint32_t swapchainImageIndex) const;
};
