#include "XrCompositor.h"

#include "XrContext.h"
#include "XrPassthrough.h"
#include "StatusOverlay.h"
#include "CursorOverlay.h"
#include "ImGuiToolbar.h"
#include "../codec/MediaCodecDecoder.h"
#include "../rdp/RdpInputForwarder.h"
#include "../scene/FrustumCuller.h"
#include "../scene/MonitorLayout.h"
#include "../scene/VirtualMonitor.h"
#include "../util/Logger.h"

#include <algorithm>
#include <cstdio>

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

        // Carousel: hide monitors whose center angle exceeds ±90°.
        if (!layout_.IsMonitorInView(i)) {
            continue;
        }

        // Cylinder layer: each screen spans one configured arc step.
        constexpr float kCylinderRadius = 1.6f;
        const float centralAngle = MonitorLayout::kAngularStepRadians;
        const float aspectRatio  = 16.0f / 9.0f;

        const XrCompositionLayerCylinderKHR* layer = monitors_[i]->GetCompositionLayer(
            ctx_.GetWorldSpace(),
            mon.worldPose,
            kCylinderRadius,
            centralAngle,
            aspectRatio);
        if (!layer) {
            continue;
        }

        layerPtrs_[layerCount++] =
            reinterpret_cast<const XrCompositionLayerBaseHeader*>(layer);
    }

    // ImGui toolbar — render after monitors, before cursor.
    // The toolbar stays fixed at the center of the FOV (unscrolled anchor),
    // independent of the carousel scroll offset.
    if (imguiToolbar_ && imguiToolbar_->IsReady()) {
        const MonitorDescriptor& mon0 = layout_.GetMonitor(0);
        if (mon0.active) {
            if (inputForwarder_) {
                float u = 0.0f, v = 0.0f;
                const bool inside = inputForwarder_->GetToolbarCursor(u, v);
                imguiToolbar_->SetMouseInput(u, v, inside,
                                             inputForwarder_->IsLeftButtonDown());
            }

            const XrPosef toolbarPose = layout_.GetToolbarAnchorPose();
            constexpr float kCylinderRadius = 1.6f;
            constexpr float kAspectRatio = 16.0f / 9.0f;
            if (auto* tbLayer = imguiToolbar_->GetCompositionLayer(
                    ctx_.GetWorldSpace(), toolbarPose,
                    kCylinderRadius, MonitorLayout::kAngularStepRadians, kAspectRatio)) {
                layerPtrs_[layerCount++] =
                    reinterpret_cast<const XrCompositionLayerBaseHeader*>(tbLayer);
            }
        }
    }

    // Cursor overlay — render after monitors so it appears on top.
    // Uses the unscrolled anchor pose + explicit scroll offset so the cursor
    // tracks the scrolled wall correctly on all monitors.
    if (cursorOverlay_ && inputForwarder_) {
        int32_t cx, cy;
        inputForwarder_->GetCursorPosition(cx, cy);
        cursorOverlay_->SetPosition(cx, cy);

        // Debug HUD: show live cursor/scroll info on StatusOverlay lines 6-7.
        if (statusOverlay_) {
            const float scrollDeg =
                layout_.GetScrollYaw() * 180.0f / 3.14159265f;
            const uint32_t monIdx = static_cast<uint32_t>(std::min(
                std::max(cx, 0) / 1920,
                static_cast<int32_t>(MonitorLayout::kMaxMonitors - 1)));
            const float localU =
                static_cast<float>(cx - static_cast<int32_t>(monIdx) * 1920) / 1920.0f;
            // Match CursorOverlay: world yaw = i*step - scrollYaw + (u-0.5)*step.
            const float monYawDeg =
                static_cast<float>(monIdx) * MonitorLayout::kAngularStepRadians
                * 180.0f / 3.14159265f;
            const float cursorAngleDeg =
                monYawDeg - scrollDeg
                + (localU - 0.5f) * (MonitorLayout::kAngularStepRadians
                                     * 180.0f / 3.14159265f);
            char buf[96];
            std::snprintf(buf, sizeof(buf),
                          "cur=(%d,%d) mon=%u U=%.2f",
                          cx, cy, monIdx, localU);
            statusOverlay_->SetStatusLine(6, buf);
            std::snprintf(buf, sizeof(buf),
                          "scroll=%.1f° yaw=%.1f°",
                          scrollDeg, cursorAngleDeg);
            statusOverlay_->SetStatusLine(7, buf);
        }

        const MonitorDescriptor& mon0 = layout_.GetMonitor(0);
        if (mon0.active) {
            const XrPosef anchorPose = layout_.GetToolbarAnchorPose();
            constexpr float kCylinderRadius = 1.6f;
            constexpr float kAspectRatio = 16.0f / 9.0f;

            if (auto* cursorLayer = cursorOverlay_->GetCompositionLayer(
                    ctx_.GetWorldSpace(), anchorPose,
                    kCylinderRadius, MonitorLayout::kAngularStepRadians, kAspectRatio,
                    layout_.IsSplitRows(),
                    layout_.GetScrollYaw())) {
                layerPtrs_[layerCount++] =
                    reinterpret_cast<const XrCompositionLayerBaseHeader*>(cursorLayer);
            }
        }
    }

    ctx_.EndFrame(frameState, layerCount, layerPtrs_.data());
}
