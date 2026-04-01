#pragma once

#include <freerdp/freerdp.h>
#include <freerdp/client/rdpgfx.h>
#include <freerdp/client/disp.h>
#include <freerdp/client/channels.h>
#include <winpr/crt.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

class RdpDisplayControl;
class RdpConnectionManager;
class VirtualMonitor;

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

    // monitors: array of kMaxMonitors VirtualMonitor* pointers (may contain nulls).
    RdpConnectionManager(RdpDisplayControl& displayControl,
                         VirtualMonitor* const monitors[], uint32_t monitorCount);
    ~RdpConnectionManager();

    // Callback invoked on the RDP thread when connection fails or disconnects.
    // Argument is the FreeRDP error code from freerdp_get_last_error().
    using ErrorCallback = std::function<void(uint32_t errorCode)>;
    void SetErrorCallback(ErrorCallback cb);

    bool Connect(const ConnectionParams& params);
    void Disconnect();
    bool IsConnected() const { return connected_.load(); }

    // Last FreeRDP error code captured on connection failure (0 = no error).
    uint32_t GetLastError() const { return lastError_.load(); }

    // Called from the FreeRDP GFX callback to push a compressed NAL unit to
    // the decoder for the given RDP surface ID.
    void OnGfxSurface(uint32_t surfaceId, const uint8_t* data, size_t size,
                      int64_t presentationTimeUs);

    // ── FreeRDP C-style static callbacks ──────────────────────────────────────
    static BOOL OnPreConnect(freerdp* instance);
    static BOOL OnPostConnect(freerdp* instance);
    static void OnPostDisconnect(freerdp* instance);
    static void OnChannelsConnected(void* context, const ChannelConnectedEventArgs* e);

    // GFX pipeline callbacks.
    static UINT OnGfxCapsAdvertise(RdpgfxClientContext* gfx,
                                   const RDPGFX_CAPS_ADVERTISE_PDU* caps);
    static UINT OnGfxSurfaceCommand(RdpgfxClientContext* gfx,
                                    const RDPGFX_SURFACE_COMMAND* cmd);
    static UINT OnGfxSurfaceCreated(RdpgfxClientContext* gfx,
                                    const RDPGFX_CREATE_SURFACE_PDU* pdu);
    static UINT OnGfxDeleteSurface(RdpgfxClientContext* gfx,
                                   const RDPGFX_DELETE_SURFACE_PDU* pdu);
    static UINT OnGfxResetGraphics(RdpgfxClientContext* gfx,
                                   const RDPGFX_RESET_GRAPHICS_PDU* pdu);
    static UINT OnGfxStartFrame(RdpgfxClientContext* gfx,
                                const RDPGFX_START_FRAME_PDU* pdu);
    static UINT OnGfxSurfaceToOutput(RdpgfxClientContext* gfx,
                                     const RDPGFX_MAP_SURFACE_TO_OUTPUT_PDU* pdu);
    static UINT OnGfxSurfaceToScaledOutput(RdpgfxClientContext* gfx,
                                           const RDPGFX_MAP_SURFACE_TO_SCALED_OUTPUT_PDU* pdu);
    static UINT OnGfxEndFrame(RdpgfxClientContext* gfx,
                              const RDPGFX_END_FRAME_PDU* pdu);

private:
    static RdpConnectionManager* GetSelfFromGfx(RdpgfxClientContext* gfx);

    RdpDisplayControl&   displayControl_;
    freerdp*             instance_  = nullptr;
    HyperDeskRdpContext* context_   = nullptr;
    std::thread          rdpThread_;
    std::atomic<bool>    connected_{false};
    std::atomic<bool>    stopFlag_{false};
    std::atomic<uint32_t> lastError_{0};
    ErrorCallback        errorCallback_;
    std::mutex           errorCbMutex_;

    void SetupSettings(rdpSettings* settings, const ConnectionParams& params);
    void RunEventLoop();

    static constexpr uint32_t kMaxMonitors = 16;

    // VirtualMonitor pointers passed in at construction time.
    VirtualMonitor* monitors_[kMaxMonitors]{};
    uint32_t        monitorCount_ = 0;

    // Maps exact RDP surface IDs to monitor array indices.
    // A small fixed table is sufficient because the app only exposes 16 monitors.
    uint32_t surfaceIds_[kMaxMonitors]{};
    uint32_t surfaceToMonitor_[kMaxMonitors]{};
    uint32_t monitorFrameCount_[kMaxMonitors]{};
    uint32_t nextMonitorIdx_ = 0;

    using GfxCreateSurfaceCallback =
        UINT (*)(RdpgfxClientContext*, const RDPGFX_CREATE_SURFACE_PDU*);
    using GfxDeleteSurfaceCallback =
        UINT (*)(RdpgfxClientContext*, const RDPGFX_DELETE_SURFACE_PDU*);
    using GfxResetGraphicsCallback =
        UINT (*)(RdpgfxClientContext*, const RDPGFX_RESET_GRAPHICS_PDU*);
    using GfxMapSurfaceToOutputCallback =
        UINT (*)(RdpgfxClientContext*, const RDPGFX_MAP_SURFACE_TO_OUTPUT_PDU*);
    using GfxMapSurfaceToScaledOutputCallback =
        UINT (*)(RdpgfxClientContext*, const RDPGFX_MAP_SURFACE_TO_SCALED_OUTPUT_PDU*);

    GfxCreateSurfaceCallback          prevCreateSurface_ = nullptr;
    GfxDeleteSurfaceCallback          prevDeleteSurface_ = nullptr;
    GfxResetGraphicsCallback          prevResetGraphics_ = nullptr;
    GfxMapSurfaceToOutputCallback     prevMapSurfaceToOutput_ = nullptr;
    GfxMapSurfaceToScaledOutputCallback prevMapSurfaceToScaledOutput_ = nullptr;
};
