#include "RdpDisplayControl.h"
#include "../scene/MonitorLayout.h"
#include "../util/Logger.h"

#include <algorithm>
#include <cstring>

// Physical dimensions of a notional 24" display at 96 DPI.
static constexpr uint32_t kPhysWidthMm  = 527;
static constexpr uint32_t kPhysHeightMm = 296;

namespace {
// Simple sequential layout: monitor i occupies desktop x = [i*1920, (i+1)*1920).
// mon0 (primary) is always at x=0, matching Windows' requirement that the
// primary monitor sits at the origin.
INT32 DesktopLeftForMonitor(uint32_t monitorIdx, uint32_t /*monitorCount*/) {
    return static_cast<INT32>(monitorIdx * 1920u);
}
}  // namespace

RdpDisplayControl::RdpDisplayControl(MonitorLayout& layout)
    : layout_(&layout) {}

void RdpDisplayControl::SetMonitorConfigAppliedCallback(std::function<void(uint32_t)> callback) {
    monitorConfigAppliedCallback_ = std::move(callback);
}

void RdpDisplayControl::Attach(DispClientContext* ctx) {
    ctx_         = ctx;
    ctx_->custom = this;  // store back-pointer for the static callback
    // New channel attachment after reconnect: clear stale server cap until CAPS arrives.
    maxMonitors_ = 0;

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

    if (maxNumMonitors < requestedCount) {
        LOGW("DisplayControl: server caps monitor count to %u (requested=%u)",
             maxNumMonitors, requestedCount);
    } else if (requestedCount < MonitorLayout::kMaxMonitors) {
        LOGI("DisplayControl: applying initial monitor request %u/%u",
             requestedCount, MonitorLayout::kMaxMonitors);
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

void RdpDisplayControl::SetRequestedMonitorCount(uint32_t monitorCount) {
    requestedMonitorCount_ =
        std::max<uint32_t>(1u, std::min<uint32_t>(monitorCount, MonitorLayout::kMaxMonitors));
}

bool RdpDisplayControl::RequestMonitorCount(uint32_t monitorCount) {
    const uint32_t requestedCapped =
        std::max<uint32_t>(1u, std::min<uint32_t>(monitorCount, MonitorLayout::kMaxMonitors));
    const uint32_t serverCap = (maxMonitors_ == 0u)
        ? MonitorLayout::kMaxMonitors
        : maxMonitors_;
    const uint32_t capped = std::min<uint32_t>(requestedCapped, serverCap);
    requestedMonitorCount_ = capped;

    if (capped != requestedCapped) {
        LOGW("DisplayControl: requested %u monitor(s), capped to %u by server",
             requestedCapped, capped);
        ActivateMonitorCount(capped);
        return false;
    }

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

    struct LayoutCandidate {
        uint32_t monitorIdx;
        INT32 left;
    };
    std::vector<LayoutCandidate> ordered;
    ordered.reserve(capped);
    for (uint32_t i = 0; i < capped; ++i) {
        ordered.push_back({i, DesktopLeftForMonitor(i, capped)});
    }
    std::sort(ordered.begin(), ordered.end(),
              [](const LayoutCandidate& a, const LayoutCandidate& b) {
                  return a.left < b.left;
              });

    // Send entries left-to-right.  mon0 is always at x=0 and is always primary.
    constexpr uint32_t primaryMonitorIdx = 0u;
    for (const LayoutCandidate& candidate : ordered) {
        DISPLAY_CONTROL_MONITOR_LAYOUT m{};
        m.Flags              =
            (candidate.monitorIdx == primaryMonitorIdx) ? DISPLAY_CONTROL_MONITOR_PRIMARY : 0;
        m.Left               = candidate.left;
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
