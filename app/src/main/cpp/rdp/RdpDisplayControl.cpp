#include "RdpDisplayControl.h"
#include "../scene/MonitorLayout.h"
#include "../util/Logger.h"

#include <cstring>

// Physical dimensions of a notional 24" display at 96 DPI.
static constexpr uint32_t kPhysWidthMm  = 527;
static constexpr uint32_t kPhysHeightMm = 296;

RdpDisplayControl::RdpDisplayControl(MonitorLayout& layout)
    : layout_(&layout) {}

void RdpDisplayControl::Attach(DispClientContext* ctx) {
    ctx_         = ctx;
    ctx_->custom = this;  // store back-pointer for the static callback

    // Register the CAPS callback.
    ctx_->DisplayControlCaps = OnDisplayControlCaps;
    LOGI("DisplayControl: channel attached");
}

// ── CAPS PDU callback (Task 4) ────────────────────────────────────────────────

UINT RdpDisplayControl::OnDisplayControlCaps(DispClientContext* ctx,
                                              UINT32 maxNumMonitors,
                                              UINT32 maxMonitorAreaFactorA,
                                              UINT32 /*maxMonitorAreaFactorB*/) {
    auto* self = static_cast<RdpDisplayControl*>(ctx->custom);
    self->maxMonitors_ = maxNumMonitors;

    LOGI("DisplayControl: CAPS received — MaxNumMonitors=%u MaxMonitorArea=%u",
         maxNumMonitors, maxMonitorAreaFactorA);

    if (maxNumMonitors < 16) {
        LOGE("DisplayControl: server supports only %u monitors, need 16", maxNumMonitors);
        return CHANNEL_RC_OK;
    }

    return self->SendMonitorLayout();
}

// ── LAYOUT PDU (Task 5) ───────────────────────────────────────────────────────

UINT RdpDisplayControl::SendMonitorLayout() {
    if (!ctx_) {
        LOGE("DisplayControl: SendMonitorLayout called before Attach");
        return ERROR_INTERNAL_ERROR;
    }

    auto entries = BuildLayoutPDU();
    const UINT32 numMonitors = static_cast<UINT32>(entries.size());

    UINT result = ctx_->SendMonitorLayout(ctx_, numMonitors, entries.data());
    if (result == CHANNEL_RC_OK) {
        LOGI("DisplayControl: LAYOUT PDU sent (%u monitors)", numMonitors);
        layout_->SetAllActive();
    } else {
        LOGE("DisplayControl: SendMonitorLayout failed: %u", result);
    }
    return result;
}

std::vector<DISPLAY_CONTROL_MONITOR_LAYOUT>
RdpDisplayControl::BuildLayoutPDU() const {
    std::vector<DISPLAY_CONTROL_MONITOR_LAYOUT> entries;
    entries.reserve(MonitorLayout::kMaxMonitors);

    for (uint32_t i = 0; i < MonitorLayout::kMaxMonitors; ++i) {
        const uint32_t col = i % MonitorLayout::kGridCols;
        const uint32_t row = i / MonitorLayout::kGridCols;

        DISPLAY_CONTROL_MONITOR_LAYOUT m{};
        m.Flags              = (i == 0) ? DISPLAY_CONTROL_MONITOR_PRIMARY : 0;
        m.Left               = static_cast<INT32>(col * 1920);
        m.Top                = static_cast<INT32>(row * 1080);
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
