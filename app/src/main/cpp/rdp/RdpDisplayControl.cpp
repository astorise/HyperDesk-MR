#include "RdpDisplayControl.h"
#include "../scene/MonitorLayout.h"
#include "../util/Logger.h"

#include <algorithm>
#include <cstring>

// Physical dimensions of a notional 24" display at 96 DPI.
static constexpr uint32_t kPhysWidthMm  = 527;
static constexpr uint32_t kPhysHeightMm = 296;

RdpDisplayControl::RdpDisplayControl(MonitorLayout& layout)
    : layout_(&layout) {}

void RdpDisplayControl::SetMonitorConfigAppliedCallback(std::function<void(uint32_t)> callback) {
    monitorConfigAppliedCallback_ = std::move(callback);
}

void RdpDisplayControl::Attach(DispClientContext* ctx) {
    ctx_         = ctx;
    ctx_->custom = this;  // store back-pointer for the static callback

    // Register the CAPS callback.
    ctx_->DisplayControlCaps = OnDisplayControlCaps;
    LOGI("DisplayControl: channel attached");
}

// ── CAPS PDU callback (Task 4) ────────────────────────────────────────────────

UINT RdpDisplayControl::OnDisplayControlCaps(DispClientContext* ctx,
                                              UINT maxNumMonitors,
                                              UINT maxMonitorAreaFactorA,
                                              UINT /*maxMonitorAreaFactorB*/) {
    auto* self = static_cast<RdpDisplayControl*>(ctx->custom);
    self->maxMonitors_ = maxNumMonitors;

    LOGI("DisplayControl: CAPS received — MaxNumMonitors=%u MaxMonitorArea=%u",
         maxNumMonitors, maxMonitorAreaFactorA);

    const uint32_t requestedCount =
        std::max<uint32_t>(1u, std::min<uint32_t>(self->requestedMonitorCount_,
                                                  MonitorLayout::kMaxMonitors));
    const uint32_t monitorCount = std::min<uint32_t>(maxNumMonitors, requestedCount);
    if (monitorCount == 0) {
        LOGE("DisplayControl: server reports zero monitors");
        self->ActivateMonitorCount(0);
        return CHANNEL_RC_OK;
    }

    if (monitorCount < MonitorLayout::kMaxMonitors) {
        LOGW("DisplayControl: server supports only %u monitor(s), using degraded layout",
             monitorCount);
    }

    return self->SendMonitorLayout(monitorCount);
}

// ── LAYOUT PDU (Task 5) ───────────────────────────────────────────────────────

UINT RdpDisplayControl::SendMonitorLayout(uint32_t monitorCount) {
    if (!ctx_) {
        LOGE("DisplayControl: SendMonitorLayout called before Attach");
        return ERROR_INTERNAL_ERROR;
    }

    auto entries = BuildLayoutPDU(monitorCount);
    if (entries.empty()) {
        LOGE("DisplayControl: no monitor layout entries to send");
        return ERROR_INVALID_DATA;
    }

    UINT result = ctx_->SendMonitorLayout(ctx_,
                                          static_cast<UINT32>(entries.size()),
                                          entries.data());
    if (result == CHANNEL_RC_OK) {
        LOGI("DisplayControl: LAYOUT PDU sent (%zu monitors)", entries.size());
        ActivateMonitorCount(static_cast<uint32_t>(entries.size()));
    } else {
        LOGE("DisplayControl: SendMonitorLayout failed: %u", result);
    }
    return result;
}

void RdpDisplayControl::ActivateMonitorCount(uint32_t monitorCount) {
    if (!layout_) return;
    const uint32_t capped =
        std::max<uint32_t>(1u, std::min<uint32_t>(monitorCount, MonitorLayout::kMaxMonitors));
    requestedMonitorCount_ = capped;
    layout_->SetActiveCount(capped);
    if (monitorConfigAppliedCallback_) {
        monitorConfigAppliedCallback_(capped);
    }
}

bool RdpDisplayControl::RequestMonitorCount(uint32_t monitorCount) {
    const uint32_t capped =
        std::max<uint32_t>(1u, std::min<uint32_t>(monitorCount, MonitorLayout::kMaxMonitors));
    requestedMonitorCount_ = capped;

    if (!ctx_) {
        ActivateMonitorCount(capped);
        return true;
    }

    const UINT rc = SendMonitorLayout(capped);
    return (rc == CHANNEL_RC_OK);
}

std::vector<DISPLAY_CONTROL_MONITOR_LAYOUT>
RdpDisplayControl::BuildLayoutPDU(uint32_t monitorCount) const {
    std::vector<DISPLAY_CONTROL_MONITOR_LAYOUT> entries;
    const uint32_t capped = std::min<uint32_t>(monitorCount, MonitorLayout::kMaxMonitors);
    entries.reserve(capped);

    // Desktop X order:
    //   monitor 1 @ x=0, monitor 0 @ x=1920, monitor 2 @ x=3840, monitor 3 @ x=5760.
    // Monitor 0 remains primary.
    for (uint32_t i = 0; i < capped; ++i) {
        INT32 left;
        switch (i) {
            case 0:  left = 1920;  break;  // center
            case 1:  left = 0;     break;  // left
            case 2:  left = 3840;  break;  // right
            case 3:  left = 5760;  break;  // far-left in VR arc
            default: left = static_cast<INT32>(i * 1920); break;
        }

        DISPLAY_CONTROL_MONITOR_LAYOUT m{};
        m.Flags              = (i == 0) ? DISPLAY_CONTROL_MONITOR_PRIMARY : 0;
        m.Left               = left;
        m.Top                = 0;
        m.Width              = 1920;
        m.Height             = 1080;
        m.PhysicalWidth      = kPhysWidthMm;
        m.PhysicalHeight     = kPhysHeightMm;
        m.Orientation        = ORIENTATION_LANDSCAPE;
        m.DesktopScaleFactor = 100;
        m.DeviceScaleFactor  = 100;
        entries.push_back(m);
    }
    return entries;
}
