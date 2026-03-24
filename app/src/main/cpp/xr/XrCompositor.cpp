#include "XrCompositor.h"
#include "XrContext.h"
#include "XrPassthrough.h"
#include "../scene/MonitorLayout.h"
#include "../scene/FrustumCuller.h"
#include "../scene/VirtualMonitor.h"
#include "../codec/MediaCodecDecoder.h"
#include "../util/Logger.h"

XrCompositor::XrCompositor(
    XrContext&                       ctx,
    XrPassthrough&                   passthrough,
    MonitorLayout&                   layout,
    FrustumCuller&                   culler,
    std::array<VirtualMonitor*, 16>  monitors)
    : ctx_(ctx), passthrough_(passthrough), layout_(layout),
      culler_(culler), monitors_(monitors) {
    LOGI("XrCompositor: ready with %u VirtualMonitor slots", MonitorLayout::kMaxMonitors);
}

XrCompositor::~XrCompositor() = default;

// ── RenderFrame ───────────────────────────────────────────────────────────────

// Task 9: the render loop calls GetCompositionLayer() on each active VirtualMonitor.
// Task 10: GetCompositionLayer() guarantees a fully-populated, non-null layer or nullptr.
void XrCompositor::RenderFrame(const XrFrameState& frameState) {
    // Locate stereo views for the predicted display time.
    std::array<XrView, 2> views{XrView{XR_TYPE_VIEW}, XrView{XR_TYPE_VIEW}};
    if (!ctx_.LocateViews(frameState.predictedDisplayTime, views)) {
        ctx_.EndFrame(frameState, 0, nullptr);
        return;
    }

    // Step 1: Frustum culling — pause/resume decoders for off-FOV monitors.
    std::array<MediaCodecDecoder*, 16> decoders{};
    for (uint32_t i = 0; i < MonitorLayout::kMaxMonitors; ++i)
        decoders[i] = monitors_[i] ? monitors_[i]->GetDecoder() : nullptr;
    culler_.UpdateAll(std::span<const XrView, 2>(views), layout_, decoders);

    // Step 2: Build composition layers.
    uint32_t layerCount = 0;

    // Passthrough layer is always first.
    layerPtrs_[layerCount++] =
        reinterpret_cast<const XrCompositionLayerBaseHeader*>(passthrough_.GetLayer());

    // Add a quad layer for each active, visible monitor.
    for (uint32_t i = 0; i < MonitorLayout::kMaxMonitors; ++i) {
        const MonitorDescriptor& mon = layout_.GetMonitor(i);
        if (!mon.active || !monitors_[i]) continue;

        // Task 9: call GetCompositionLayer() on VirtualMonitor.
        // Task 10: layer is null if decoder not running or swapchain unavailable — skip safely.
        const XrCompositionLayerQuad* layer = monitors_[i]->GetCompositionLayer(
            ctx_.GetWorldSpace(),
            mon.worldPose,
            {mon.sizeMeters.x, mon.sizeMeters.y});
        if (!layer) continue;

        layerPtrs_[layerCount++] =
            reinterpret_cast<const XrCompositionLayerBaseHeader*>(layer);
    }

    // Step 3: Submit to the OpenXR compositor.
    ctx_.EndFrame(frameState, layerCount, layerPtrs_.data());
}
