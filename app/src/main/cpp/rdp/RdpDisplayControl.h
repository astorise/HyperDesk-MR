#pragma once

#include <freerdp/channels/disp.h>
#include <freerdp/client/disp.h>
#include <cstdint>
#include <functional>
#include <vector>

class MonitorLayout;

// RdpDisplayControl handles the MS-RDPEDISP virtual channel:
//   1. Receives DISPLAYCONTROL_CAPS_PDU from the server (Task 4).
//   2. Sends DISPLAYCONTROL_MONITOR_LAYOUT_PDU defining the 4×4 grid (Task 5).
class RdpDisplayControl {
public:
    static constexpr uint32_t kDefaultMonitorCount = 3;

    explicit RdpDisplayControl(MonitorLayout& layout);
    ~RdpDisplayControl() = default;

    // Called once the DispClientContext is available (from OnChannelsConnected).
    void Attach(DispClientContext* ctx);

    // Sends up to monitorCount monitors to the server.
    UINT SendMonitorLayout(uint32_t monitorCount);

    // Fallback used when the server exposes fewer monitors or no disp channel.
    void ActivateMonitorCount(uint32_t monitorCount);
    bool RequestMonitorCount(uint32_t monitorCount);

    void SetMonitorConfigAppliedCallback(std::function<void(uint32_t)> callback);

    // ── FreeRDP C-style callback ───────────────────────────────────────────────
    // Registered on the DispClientContext.  FreeRDP passes the context pointer;
    // we recover `this` from ctx->custom.
    static UINT OnDisplayControlCaps(DispClientContext* ctx,
                                     UINT maxNumMonitors,
                                     UINT maxMonitorAreaFactorA,
                                     UINT maxMonitorAreaFactorB);

private:
    MonitorLayout*     layout_      = nullptr;
    DispClientContext* ctx_         = nullptr;
    uint32_t           maxMonitors_ = 0;
    uint32_t           requestedMonitorCount_ = kDefaultMonitorCount;
    std::function<void(uint32_t)> monitorConfigAppliedCallback_;

    std::vector<DISPLAY_CONTROL_MONITOR_LAYOUT> BuildLayoutPDU(uint32_t monitorCount) const;
};
