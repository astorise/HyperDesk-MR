#include "XrCompositor.h"
#include "XrContext.h"
#include "XrPassthrough.h"
#include "XrSwapchain.h"
#include "../scene/MonitorLayout.h"
#include "../scene/FrustumCuller.h"
#include "../codec/MediaCodecDecoder.h"
#include "../codec/CodecSurfaceBridge.h"
#include "../util/Logger.h"

#include <cstring>

static constexpr uint32_t kMonitorWidth  = 1920;
static constexpr uint32_t kMonitorHeight = 1080;

XrCompositor::XrCompositor(
    XrContext&   ctx,
    XrPassthrough& passthrough,
    MonitorLayout& layout,
    FrustumCuller& culler,
    std::array<std::unique_ptr<CodecSurfaceBridge>, 16>& bridges,
    std::array<MediaCodecDecoder*, 16>                    decoders)
    : ctx_(ctx), passthrough_(passthrough), layout_(layout),
      culler_(culler), bridges_(bridges), decoders_(decoders) {

    // Create one XrSwapchain per monitor slot.
    for (uint32_t i = 0; i < MonitorLayout::kMaxMonitors; ++i) {
        swapchains_[i] = std::make_unique<XrSwapchain>(ctx_, kMonitorWidth, kMonitorHeight, i);
        slotBound_[i]  = false;
    }
    LOGI("XrCompositor: %u swapchains created", MonitorLayout::kMaxMonitors);
}

XrCompositor::~XrCompositor() = default;

// ── RenderFrame ───────────────────────────────────────────────────────────────

void XrCompositor::RenderFrame(const XrFrameState& frameState) {
    // Locate stereo views for the predicted display time.
    std::array<XrView, 2> views{XrView{XR_TYPE_VIEW}, XrView{XR_TYPE_VIEW}};
    if (!ctx_.LocateViews(frameState.predictedDisplayTime, views)) {
        // Submit an empty frame so xrEndFrame doesn't time-out.
        ctx_.EndFrame(frameState, 0, nullptr);
        return;
    }

    // Step 1: Frustum culling — pause/resume decoders for off-FOV monitors.
    culler_.UpdateAll(std::span<const XrView, 2>(views), layout_, decoders_);

    // Step 2: Build composition layers.
    uint32_t layerCount = 0;

    // Passthrough layer is always first.
    layerPtrs_[layerCount++] =
        reinterpret_cast<const XrCompositionLayerBaseHeader*>(passthrough_.GetLayer());

    // Add a quad layer for each active, visible monitor.
    for (uint32_t i = 0; i < MonitorLayout::kMaxMonitors; ++i) {
        const MonitorDescriptor& mon = layout_.GetMonitor(i);
        if (!mon.active) continue;
        if (!decoders_[i] || !decoders_[i]->IsRunning()) continue;

        // Try to acquire a new decoded frame from the zero-copy bridge.
        AHardwareBuffer* ahb = nullptr;
        const bool hasFrame = bridges_[i]->AcquireLatestFrame(&ahb);

        // Acquire the swapchain image for this monitor.
        uint32_t imageIndex = 0;
        if (!swapchains_[i]->AcquireImage(imageIndex)) continue;
        if (!swapchains_[i]->WaitImage())               continue;

        // On first use (or first frame after binding), import the AHardwareBuffer
        // into the swapchain slot's Vulkan memory.
        if (hasFrame && !slotBound_[i]) {
            swapchains_[i]->BindExternalHardwareBuffer(imageIndex, ahb);
            slotBound_[i] = true;
        }

        if (hasFrame) bridges_[i]->ReleaseCurrentBuffer();

        // Build the quad layer for this monitor.
        quadLayers_[i] = BuildQuadLayer(i, imageIndex);
        layerPtrs_[layerCount++] =
            reinterpret_cast<const XrCompositionLayerBaseHeader*>(&quadLayers_[i]);

        swapchains_[i]->ReleaseImage();
    }

    // Step 3: Submit to the OpenXR compositor.
    ctx_.EndFrame(frameState, layerCount,
                  reinterpret_cast<const XrCompositionLayerBaseHeader* const*>(layerPtrs_.data()));
}

// ── BuildQuadLayer ────────────────────────────────────────────────────────────

XrCompositionLayerQuad XrCompositor::BuildQuadLayer(uint32_t monitorIndex,
                                                     uint32_t /*swapchainImageIndex*/) const {
    const MonitorDescriptor& mon = layout_.GetMonitor(monitorIndex);

    XrCompositionLayerQuad layer{XR_TYPE_COMPOSITION_LAYER_QUAD};
    layer.layerFlags  = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
    layer.space       = ctx_.GetWorldSpace();
    layer.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
    layer.subImage    = swapchains_[monitorIndex]->GetSubImage();
    layer.pose        = mon.worldPose;
    layer.size        = mon.sizeMeters;
    return layer;
}
