#include "XrCompositor.h"

#include "XrContext.h"
#include "XrPassthrough.h"
#include "StatusOverlay.h"
#include "../codec/MediaCodecDecoder.h"
#include "../scene/FrustumCuller.h"
#include "../scene/MonitorLayout.h"
#include "../scene/VirtualMonitor.h"
#include "../util/Logger.h"

XrCompositor::XrCompositor(
    XrContext&                       ctx,
    XrPassthrough&                   passthrough,
    MonitorLayout&                   layout,
    FrustumCuller&                   culler,
    std::array<VirtualMonitor*, MonitorLayout::kMaxMonitors>  monitors)
    : ctx_(ctx),
      passthrough_(passthrough),
      layout_(layout),
      culler_(culler),
      monitors_(monitors) {
    LOGI("XrCompositor: ready with %u VirtualMonitor slots", MonitorLayout::kMaxMonitors);
}

XrCompositor::~XrCompositor() = default;

void XrCompositor::RenderFrame(const XrFrameState& frameState) {
    std::array<XrView, 2> views{XrView{XR_TYPE_VIEW}, XrView{XR_TYPE_VIEW}};
    if (!ctx_.LocateViews(frameState.predictedDisplayTime, views)) {
        ctx_.EndFrame(frameState, 0, nullptr);
        return;
    }

    std::array<MediaCodecDecoder*, MonitorLayout::kMaxMonitors> decoders{};
    for (uint32_t i = 0; i < MonitorLayout::kMaxMonitors; ++i) {
        decoders[i] = monitors_[i] ? monitors_[i]->GetDecoder() : nullptr;
    }
    culler_.UpdateAll(std::span<const XrView, 2>(views), layout_, decoders);

    uint32_t layerCount = 0;

    if (auto* pt = passthrough_.GetLayer()) {
        layerPtrs_[layerCount++] = pt;
    }

    if (statusOverlay_) {
        if (auto* overlay = statusOverlay_->GetCompositionLayer(ctx_.GetWorldSpace())) {
            layerPtrs_[layerCount++] =
                reinterpret_cast<const XrCompositionLayerBaseHeader*>(overlay);
        }
    }

    for (uint32_t i = 0; i < MonitorLayout::kMaxMonitors; ++i) {
        const MonitorDescriptor& mon = layout_.GetMonitor(i);
        if (!mon.active || !monitors_[i]) {
            continue;
        }

        const XrCompositionLayerQuad* layer = monitors_[i]->GetCompositionLayer(
            ctx_.GetWorldSpace(),
            mon.worldPose,
            {mon.sizeMeters.x, mon.sizeMeters.y});
        if (!layer) {
            continue;
        }

        layerPtrs_[layerCount++] =
            reinterpret_cast<const XrCompositionLayerBaseHeader*>(layer);
    }

    ctx_.EndFrame(frameState, layerCount, layerPtrs_.data());
}
