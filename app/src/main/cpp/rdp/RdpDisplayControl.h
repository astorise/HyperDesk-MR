#pragma once

#include <freerdp/channels/disp.h>
#include <freerdp/client/disp.h>
#include <cstdint>
#include <vector>

class MonitorLayout;

// RdpDisplayControl handles the MS-RDPEDISP virtual channel:
//   1. Receives DISPLAYCONTROL_CAPS_PDU from the server (Task 4).
//   2. Sends DISPLAYCONTROL_MONITOR_LAYOUT_PDU defining the 4×4 grid (Task 5).
class RdpDisplayControl {
public:
    explicit RdpDisplayControl(MonitorLayout& layout);
    ~RdpDisplayControl() = default;

    // Called once the DispClientContext is available (from OnChannelsConnected).
    void Attach(DispClientContext* ctx);

    // Sends the 16-monitor layout PDU to the server.
    // Called automatically from OnDisplayControlCaps when MaxNumMonitors >= 16.
    UINT SendMonitorLayout();

    // ── FreeRDP C-style callback ───────────────────────────────────────────────
    // Registered on the DispClientContext.  FreeRDP passes the context pointer;
    // we recover `this` from ctx->custom.
    static UINT OnDisplayControlCaps(DispClientContext* ctx,
                                     DISPLAY_CONTROL_CAPS_PDU* caps);

private:
    MonitorLayout*     layout_      = nullptr;
    DispClientContext* ctx_         = nullptr;
    uint32_t           maxMonitors_ = 0;

    std::vector<DISPLAY_CONTROL_MONITOR_LAYOUT> BuildLayoutPDU() const;
};
