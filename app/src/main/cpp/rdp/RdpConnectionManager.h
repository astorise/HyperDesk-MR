#pragma once

#include <freerdp/freerdp.h>
#include <freerdp/client/rdpgfx.h>
#include <freerdp/client/disp.h>
#include <freerdp/client/channels.h>
#include <winpr/crt.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

class RdpDisplayControl;
class RdpConnectionManager;

// Extended FreeRDP client context — must begin with rdpClientContext so that
// FreeRDP's internal code can safely cast between the two types.
struct HyperDeskRdpContext {
    rdpClientContext        base;       // MUST be first member
    RdpConnectionManager*   self;
    DispClientContext*       disp;
    RdpgfxClientContext*     gfx;
};

class RdpConnectionManager {
public:
    struct ConnectionParams {
        std::string hostname;
        uint16_t    port     = 3389;
        std::string username;
        std::string password;
        std::string domain;
    };

    explicit RdpConnectionManager(RdpDisplayControl& displayControl);
    ~RdpConnectionManager();

    bool Connect(const ConnectionParams& params);
    void Disconnect();
    bool IsConnected() const { return connected_.load(); }

    // Called from the FreeRDP GFX callback to push a compressed NAL unit to
    // the decoder for the given RDP surface ID.
    void OnGfxSurface(uint32_t surfaceId, const uint8_t* data, size_t size,
                      int64_t presentationTimeUs);

    // ── FreeRDP C-style static callbacks ──────────────────────────────────────
    static BOOL OnPreConnect(freerdp* instance);
    static BOOL OnPostConnect(freerdp* instance);
    static void OnPostDisconnect(freerdp* instance);
    static void OnChannelsConnected(void* context, const ChannelConnectedEventArgs* e);

    // GFX pipeline callback — receives encoded H.264 surfaces.
    static UINT OnGfxSurfaceCreated(RdpgfxClientContext* gfx,
                                    const RDPGFX_CREATE_SURFACE_PDU* pdu);
    static UINT OnGfxStartFrame(RdpgfxClientContext* gfx,
                                const RDPGFX_START_FRAME_PDU* pdu);
    static UINT OnGfxSurfaceToOutput(RdpgfxClientContext* gfx,
                                     const RDPGFX_MAP_SURFACE_TO_OUTPUT_PDU* pdu);
    static UINT OnGfxEndFrame(RdpgfxClientContext* gfx,
                              const RDPGFX_END_FRAME_PDU* pdu);

private:
    RdpDisplayControl&   displayControl_;
    freerdp*             instance_  = nullptr;
    HyperDeskRdpContext* context_   = nullptr;
    std::thread          rdpThread_;
    std::atomic<bool>    connected_{false};
    std::atomic<bool>    stopFlag_{false};

    void SetupSettings(rdpSettings* settings, const ConnectionParams& params);
    void RunEventLoop();

    // Maps RDP surface IDs to monitor indices (populated by OnGfxSurfaceCreated).
    // Indexed by surfaceId % kMaxMonitors for simplicity.
    static constexpr uint32_t kMaxMonitors = 16;
    uint32_t surfaceToMonitor_[kMaxMonitors]{};
};
