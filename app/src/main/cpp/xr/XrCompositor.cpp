#include "XrCompositor.h"

#include "XrContext.h"
#include "XrPassthrough.h"
#include "StatusOverlay.h"
#include "../codec/MediaCodecDecoder.h"
#include "../scene/FrustumCuller.h"
#include "../scene/MonitorLayout.h"
#include "../scene/VirtualMonitor.h"
#include "../util/Logger.h"

#include <cmath>

namespace {

XrVector3f AveragePosition(const XrVector3f& a, const XrVector3f& b) {
    return {
        (a.x + b.x) * 0.5f,
        (a.y + b.y) * 0.5f,
        (a.z + b.z) * 0.5f
    };
}

XrQuaternionf NormalizeQuaternion(const XrQuaternionf& q) {
    const float lenSq = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
    if (lenSq <= 1e-6f) {
        return {0.0f, 0.0f, 0.0f, 1.0f};
    }

    const float invLen = 1.0f / std::sqrt(lenSq);
    return {
        q.x * invLen,
        q.y * invLen,
        q.z * invLen,
        q.w * invLen
    };
}

}  // namespace

XrCompositor::XrCompositor(
    XrContext&                       ctx,
    XrPassthrough&                   passthrough,
    MonitorLayout&                   layout,
    FrustumCuller&                   culler,
    std::array<VirtualMonitor*, 16>  monitors)
    : ctx_(ctx),
      passthrough_(passthrough),
      layout_(layout),
      culler_(culler),
      monitors_(monitors) {
    LOGI("XrCompositor: ready with %u VirtualMonitor slots", MonitorLayout::kMaxMonitors);
}

XrCompositor::~XrCompositor() = default;

bool XrCompositor::TryGetLatestHeadPose(XrPosef& outPose) const {
    std::lock_guard<std::mutex> lock(headPoseMutex_);
    if (!hasLatestHeadPose_) {
        return false;
    }

    outPose = latestHeadPose_;
    return true;
}

void XrCompositor::RenderFrame(const XrFrameState& frameState) {
    std::array<XrView, 2> views{XrView{XR_TYPE_VIEW}, XrView{XR_TYPE_VIEW}};
    if (!ctx_.LocateViews(frameState.predictedDisplayTime, views)) {
        ctx_.EndFrame(frameState, 0, nullptr);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(headPoseMutex_);
        latestHeadPose_.position = AveragePosition(
            views[0].pose.position,
            views[1].pose.position);
        latestHeadPose_.orientation = NormalizeQuaternion(views[0].pose.orientation);
        hasLatestHeadPose_ = true;
    }

    std::array<MediaCodecDecoder*, 16> decoders{};
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
