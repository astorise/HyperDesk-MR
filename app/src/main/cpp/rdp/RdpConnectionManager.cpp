#include "RdpConnectionManager.h"
#include "RdpDisplayControl.h"
#include "../scene/VirtualMonitor.h"
#include "../util/Logger.h"
#include "../xr/StatusOverlay.h"

#include <freerdp/client.h>
#include <freerdp/channels/channels.h>
#include <freerdp/gdi/gdi.h>
#include <winpr/synch.h>

#include <algorithm>
#include <cstring>
#include <cstdio>

// Log to both logcat and on-screen debug overlay.
static void ScreenLog(const char* fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    LOGI("%s", buf);
    if (StatusOverlay::sInstance) StatusOverlay::sInstance->AddLog(buf);
}

// ── Constructor / Destructor ──────────────────────────────────────────────────

RdpConnectionManager::RdpConnectionManager(RdpDisplayControl& displayControl,
                                             VirtualMonitor* const monitors[],
                                             uint32_t monitorCount)
    : displayControl_(displayControl),
      monitorCount_(std::min(monitorCount, kMaxMonitors)) {
    uint32_t count = std::min(monitorCount, kMaxMonitors);
    for (uint32_t i = 0; i < count; ++i) monitors_[i] = monitors[i];
    std::fill(std::begin(surfaceIds_), std::end(surfaceIds_), UINT32_MAX);
    std::fill(std::begin(surfaceToMonitor_), std::end(surfaceToMonitor_), UINT32_MAX);
    std::fill(std::begin(monitorFrameCount_), std::end(monitorFrameCount_), 0u);
}

RdpConnectionManager::~RdpConnectionManager() {
    Disconnect();
}

void RdpConnectionManager::SetErrorCallback(ErrorCallback cb) {
    std::lock_guard lock(errorCbMutex_);
    errorCallback_ = std::move(cb);
}

// ── Connect ───────────────────────────────────────────────────────────────────

bool RdpConnectionManager::Connect(const ConnectionParams& params) {
    if (instance_ || context_) {
        LOGW("RDP: Connect called while a session object already exists");
        return false;
    }

    stopFlag_.store(false);
    connected_.store(false);
    lastError_.store(0);
    nextMonitorIdx_ = 0;
    std::fill(std::begin(surfaceIds_), std::end(surfaceIds_), UINT32_MAX);
    std::fill(std::begin(surfaceToMonitor_), std::end(surfaceToMonitor_), UINT32_MAX);
    std::fill(std::begin(monitorFrameCount_), std::end(monitorFrameCount_), 0u);

    RDP_CLIENT_ENTRY_POINTS entryPoints{};
    entryPoints.Size        = sizeof(entryPoints);
    entryPoints.Version     = RDP_CLIENT_INTERFACE_VERSION;
    entryPoints.ContextSize = sizeof(HyperDeskRdpContext);

    rdpContext* baseContext = freerdp_client_context_new(&entryPoints);
    if (!baseContext) {
        LOGE("freerdp_client_context_new() failed");
        return false;
    }

    instance_ = freerdp_client_get_instance(baseContext);
    context_  = reinterpret_cast<HyperDeskRdpContext*>(baseContext);
    if (!instance_ || !instance_->context) {
        LOGE("freerdp_client_get_instance() returned null");
        freerdp_client_context_free(baseContext);
        instance_ = nullptr;
        context_  = nullptr;
        return false;
    }

    context_->self = this;
    context_->disp = nullptr;
    context_->gfx  = nullptr;

    // Register lifecycle callbacks.
    instance_->PreConnect       = OnPreConnect;
    instance_->PostConnect      = OnPostConnect;
    instance_->PostDisconnect   = OnPostDisconnect;

    // Register channel connect callback via PubSub (FreeRDP 3.x).
    PubSub_SubscribeChannelConnected(baseContext->pubSub, OnChannelsConnected);

    SetupSettings(baseContext->settings, params);

    // Run FreeRDP event loop on a dedicated thread.
    rdpThread_ = std::thread([this]() { RunEventLoop(); });
    return true;
}

void RdpConnectionManager::SetupSettings(rdpSettings* settings, const ConnectionParams& params) {
    freerdp_settings_set_string(settings, FreeRDP_ServerHostname, params.hostname.c_str());
    freerdp_settings_set_uint32(settings, FreeRDP_ServerPort,     params.port);
    freerdp_settings_set_string(settings, FreeRDP_Username,       params.username.c_str());
    freerdp_settings_set_string(settings, FreeRDP_Password,       params.password.c_str());
    freerdp_settings_set_string(settings, FreeRDP_Domain,         params.domain.c_str());

    // Enable H.264 graphics pipeline.
    freerdp_settings_set_bool(settings, FreeRDP_SupportDynamicChannels,      TRUE);
    freerdp_settings_set_bool(settings, FreeRDP_SupportGraphicsPipeline,     TRUE);
    freerdp_settings_set_bool(settings, FreeRDP_SupportDisplayControl,       TRUE);
    freerdp_settings_set_bool(settings, FreeRDP_DynamicResolutionUpdate,     TRUE);
    freerdp_settings_set_bool(settings, FreeRDP_SurfaceCommandsEnabled,      TRUE);
    freerdp_settings_set_bool(settings, FreeRDP_SupportMonitorLayoutPdu,     TRUE);
    freerdp_settings_set_bool(settings, FreeRDP_RemoteFxCodec,               FALSE);
    freerdp_settings_set_bool(settings, FreeRDP_GfxProgressive,              FALSE);
    freerdp_settings_set_bool(settings, FreeRDP_GfxProgressiveV2,            FALSE);
    freerdp_settings_set_bool(settings, FreeRDP_GfxPlanar,                   FALSE);
    freerdp_settings_set_bool(settings, FreeRDP_GfxThinClient,               FALSE);
    freerdp_settings_set_bool(settings, FreeRDP_GfxSmallCache,               FALSE);
    freerdp_settings_set_bool(settings, FreeRDP_GfxH264,                     TRUE);
    freerdp_settings_set_bool(settings, FreeRDP_GfxAVC444,                   FALSE);
    freerdp_settings_set_bool(settings, FreeRDP_GfxAVC444v2,                 FALSE);

    // Disable NLA — OpenSSL on Android lacks the LEGACY provider (MD4)
    // which NTLM password hashing requires.  Fall back to TLS security.
    freerdp_settings_set_bool(settings, FreeRDP_NlaSecurity, FALSE);
    freerdp_settings_set_bool(settings, FreeRDP_TlsSecurity, TRUE);
    freerdp_settings_set_bool(settings, FreeRDP_RdpSecurity, TRUE);

    // Accept self-signed RDP certificates without user prompt.
    freerdp_settings_set_bool(settings, FreeRDP_IgnoreCertificate, TRUE);

    // Declare 3 monitors at connection time so Windows creates 3 virtual displays.
    freerdp_settings_set_bool(settings, FreeRDP_UseMultimon, TRUE);
    freerdp_settings_set_bool(settings, FreeRDP_ForceMultimon, TRUE);
    freerdp_settings_set_bool(settings, FreeRDP_HasMonitorAttributes, TRUE);

    freerdp_settings_set_uint32(settings, FreeRDP_DesktopWidth,  1920);
    freerdp_settings_set_uint32(settings, FreeRDP_DesktopHeight, 1080);

    // Allocate monitor array: monitor 0 = center (primary), 1 = left, 2 = right.
    const uint32_t numMon = 3;
    freerdp_settings_set_uint32(settings, FreeRDP_MonitorCount, numMon);
    freerdp_settings_set_uint32(settings, FreeRDP_MonitorDefArraySize, numMon);

    auto* monitors = freerdp_settings_get_pointer_writable(settings, FreeRDP_MonitorDefArray);
    auto* monArray = static_cast<rdpMonitor*>(monitors);
    if (monArray) {
        // Monitor 0: center (primary) at x=1920
        monArray[0] = {};
        monArray[0].x          = 1920;
        monArray[0].y          = 0;
        monArray[0].width      = 1920;
        monArray[0].height     = 1080;
        monArray[0].is_primary = 1;
        monArray[0].attributes.physicalWidth  = 527;
        monArray[0].attributes.physicalHeight = 296;
        monArray[0].attributes.orientation    = ORIENTATION_LANDSCAPE;
        monArray[0].attributes.desktopScaleFactor = 100;
        monArray[0].attributes.deviceScaleFactor  = 100;

        // Monitor 1: left at x=0
        monArray[1] = {};
        monArray[1].x          = 0;
        monArray[1].y          = 0;
        monArray[1].width      = 1920;
        monArray[1].height     = 1080;
        monArray[1].is_primary = 0;
        monArray[1].attributes.physicalWidth  = 527;
        monArray[1].attributes.physicalHeight = 296;
        monArray[1].attributes.orientation    = ORIENTATION_LANDSCAPE;
        monArray[1].attributes.desktopScaleFactor = 100;
        monArray[1].attributes.deviceScaleFactor  = 100;

        // Monitor 2: right at x=3840
        monArray[2] = {};
        monArray[2].x          = 3840;
        monArray[2].y          = 0;
        monArray[2].width      = 1920;
        monArray[2].height     = 1080;
        monArray[2].is_primary = 0;
        monArray[2].attributes.physicalWidth  = 527;
        monArray[2].attributes.physicalHeight = 296;
        monArray[2].attributes.orientation    = ORIENTATION_LANDSCAPE;
        monArray[2].attributes.desktopScaleFactor = 100;
        monArray[2].attributes.deviceScaleFactor  = 100;

        LOGI("RDP: 3 monitors declared (left=0, center=1920, right=3840)");
    } else {
        LOGE("RDP: failed to get MonitorDefArray pointer");
    }
}

// ── RunEventLoop ──────────────────────────────────────────────────────────────

void RdpConnectionManager::RunEventLoop() {
    LOGI("RDP thread: connecting to %s",
         freerdp_settings_get_string(instance_->context->settings, FreeRDP_ServerHostname));

    ScreenLog("TCP connecting to %s...",
              freerdp_settings_get_string(instance_->context->settings, FreeRDP_ServerHostname));
    if (!freerdp_connect(instance_)) {
        uint32_t err = freerdp_get_last_error(instance_->context);
        lastError_.store(err);
        ScreenLog("[ERR] freerdp_connect failed 0x%08X", err);
        {
            std::lock_guard lock(errorCbMutex_);
            if (errorCallback_) errorCallback_(err);
        }
        return;
    }
    lastError_.store(0);
    connected_.store(true);
    ScreenLog("[OK] RDP connected");

    while (!stopFlag_.load()) {
        HANDLE handles[MAXIMUM_WAIT_OBJECTS] = {};
        DWORD nCount = freerdp_get_event_handles(instance_->context, handles, MAXIMUM_WAIT_OBJECTS);
        if (nCount == 0) {
            LOGE("freerdp_get_event_handles returned 0 — disconnecting");
            break;
        }

        DWORD waitStatus = WaitForMultipleObjects(nCount, handles, FALSE, 100);
        if (waitStatus == WAIT_FAILED) {
            LOGE("WaitForMultipleObjects failed — disconnecting");
            break;
        }

        if (!freerdp_check_event_handles(instance_->context)) {
            uint32_t err = freerdp_get_last_error(instance_->context);
            lastError_.store(err);
            ScreenLog("[ERR] check_event failed 0x%08X", err);
            {
                std::lock_guard lock(errorCbMutex_);
                if (errorCallback_) errorCallback_(err);
            }
            break;
        }
    }

    freerdp_disconnect(instance_);
    connected_.store(false);
    LOGI("RDP disconnected");
}

// ── Disconnect ────────────────────────────────────────────────────────────────

void RdpConnectionManager::Disconnect() {
    stopFlag_.store(true);
    if (rdpThread_.joinable()) rdpThread_.join();
    connected_.store(false);
    if (context_) {
        freerdp_client_context_free(reinterpret_cast<rdpContext*>(context_));
        instance_ = nullptr;
        context_  = nullptr;
    }
    prevCreateSurface_ = nullptr;
    prevDeleteSurface_ = nullptr;
    prevCapsAdvertise_ = nullptr;
    prevResetGraphics_ = nullptr;
    prevSurfaceCommand_ = nullptr;
    prevStartFrame_ = nullptr;
    prevEndFrame_ = nullptr;
    prevMapSurfaceToOutput_ = nullptr;
    prevMapSurfaceToScaledOutput_ = nullptr;
    softwareFallbackActive_ = false;
    softwareFramePending_ = false;
    softwareFallbackLogged_ = false;
}

// ── Static callbacks ──────────────────────────────────────────────────────────

RdpConnectionManager* RdpConnectionManager::GetSelfFromGfx(RdpgfxClientContext* gfx) {
    if (!gfx || !gfx->custom) return nullptr;

    auto* gdi = static_cast<rdpGdi*>(gfx->custom);
    if (!gdi || !gdi->context) return nullptr;

    auto* ctx = reinterpret_cast<HyperDeskRdpContext*>(gdi->context);
    return ctx ? ctx->self : nullptr;
}

const char* RdpConnectionManager::GfxCodecToString(UINT16 codecId) {
    switch (codecId) {
        case RDPGFX_CODECID_UNCOMPRESSED:
            return "UNCOMPRESSED";
        case RDPGFX_CODECID_CAVIDEO:
            return "CAVIDEO";
        case RDPGFX_CODECID_CLEARCODEC:
            return "CLEARCODEC";
        case RDPGFX_CODECID_CAPROGRESSIVE:
            return "CAPROGRESSIVE";
        case RDPGFX_CODECID_PLANAR:
            return "PLANAR";
        case RDPGFX_CODECID_AVC420:
            return "AVC420";
        case RDPGFX_CODECID_ALPHA:
            return "ALPHA";
        case RDPGFX_CODECID_CAPROGRESSIVE_V2:
            return "CAPROGRESSIVE_V2";
        case RDPGFX_CODECID_AVC444:
            return "AVC444";
        case RDPGFX_CODECID_AVC444v2:
            return "AVC444v2";
        default:
            return "UNKNOWN";
    }
}

static BOOL OnBeginPaint(rdpContext* /*context*/) { return TRUE; }
static BOOL OnEndPaint(rdpContext* /*context*/)   { return TRUE; }
static BOOL OnDesktopResize(rdpContext* /*context*/) { return TRUE; }

BOOL RdpConnectionManager::OnPreConnect(freerdp* instance) {
    ScreenLog("[OK] TLS handshake...");
    // FreeRDP requires update callbacks even when using the GFX pipeline.
    rdpUpdate* update = instance->context->update;
    update->BeginPaint    = OnBeginPaint;
    update->EndPaint      = OnEndPaint;
    update->DesktopResize = OnDesktopResize;
    return TRUE;
}

BOOL RdpConnectionManager::OnPostConnect(freerdp* instance) {
    ScreenLog("[OK] RDP session established — initializing GDI...");
    // gdi_init() is required for FreeRDP's internal plumbing even when
    // graphics arrive via RDPGFX.  SoftwareGdi stays FALSE (default) so
    // no huge framebuffer is allocated — keeps memory free for the camera.
    if (!gdi_init(instance, PIXEL_FORMAT_BGRA32)) {
        ScreenLog("[ERR] gdi_init() failed");
        return FALSE;
    }
    ScreenLog("[OK] GDI initialized");
    return TRUE;
}

void RdpConnectionManager::OnPostDisconnect(freerdp* instance) {
    ScreenLog("[WARN] RDP disconnected");
    auto* ctx = reinterpret_cast<HyperDeskRdpContext*>(instance->context);
    if (ctx && ctx->self) {
        ctx->self->connected_.store(false);
    }
}

// FreeRDP 3.x PubSub callback: receives one channel interface at a time.
void RdpConnectionManager::OnChannelsConnected(void* context,
                                                const ChannelConnectedEventArgs* e) {
    auto* rdpCtx = static_cast<rdpContext*>(context);
    auto* ctx    = reinterpret_cast<HyperDeskRdpContext*>(rdpCtx);
    if (!ctx || !ctx->self) return;

    RdpConnectionManager* self = ctx->self;

    // Let FreeRDP's common client bootstrap the dynamic channel first.
    freerdp_client_OnChannelConnectedEventHandler(context, e);

    ScreenLog("[OK] Channel: %s", e->name);

    if (strcmp(e->name, DISP_DVC_CHANNEL_NAME) == 0) {
        ctx->disp = static_cast<DispClientContext*>(e->pInterface);
        if (ctx->disp) {
            ScreenLog("[OK] Display control ready");
            self->displayControl_.Attach(ctx->disp);
        }
    } else if (strcmp(e->name, RDPGFX_DVC_CHANNEL_NAME) == 0) {
        ctx->gfx = static_cast<RdpgfxClientContext*>(e->pInterface);
        if (ctx->gfx) {
            if (!ctx->gfx->custom && rdpCtx->gdi) {
                ctx->gfx->custom = rdpCtx->gdi;
            }
            self->prevCapsAdvertise_ = ctx->gfx->CapsAdvertise;
            self->prevResetGraphics_ = ctx->gfx->ResetGraphics;
            self->prevCreateSurface_ = ctx->gfx->CreateSurface;
            self->prevDeleteSurface_ = ctx->gfx->DeleteSurface;
            self->prevSurfaceCommand_ = ctx->gfx->SurfaceCommand;
            self->prevStartFrame_ = ctx->gfx->StartFrame;
            self->prevEndFrame_ = ctx->gfx->EndFrame;
            self->prevMapSurfaceToOutput_ = ctx->gfx->MapSurfaceToOutput;
            self->prevMapSurfaceToScaledOutput_ = ctx->gfx->MapSurfaceToScaledOutput;

            ScreenLog("[OK] GFX pipeline ready (H.264)");
            if (ctx->gfx->custom) {
                ScreenLog("[OK] GFX common bootstrap ready");
            } else {
                ScreenLog("[WARN] GFX common bootstrap missing");
            }
            ctx->gfx->CapsAdvertise            = OnGfxCapsAdvertise;
            ctx->gfx->ResetGraphics            = OnGfxResetGraphics;
            ctx->gfx->SurfaceCommand           = OnGfxSurfaceCommand;
            ctx->gfx->CreateSurface            = OnGfxSurfaceCreated;
            ctx->gfx->DeleteSurface            = OnGfxDeleteSurface;
            ctx->gfx->StartFrame               = OnGfxStartFrame;
            ctx->gfx->MapSurfaceToOutput       = OnGfxSurfaceToOutput;
            ctx->gfx->MapSurfaceToScaledOutput = OnGfxSurfaceToScaledOutput;
            ctx->gfx->EndFrame                 = OnGfxEndFrame;
        } else {
            ScreenLog("[WARN] GFX channel not available");
        }
    }
}

// ── GFX callbacks ─────────────────────────────────────────────────────────────

// Task 6: CapsAdvertise — log what's being advertised; H.264 is already requested
// via FreeRDP_GfxH264 / FreeRDP_SupportGraphicsPipeline settings.
UINT RdpConnectionManager::OnGfxCapsAdvertise(RdpgfxClientContext* gfx,
                                               const RDPGFX_CAPS_ADVERTISE_PDU* caps) {
    auto* self = GetSelfFromGfx(gfx);
    if (!self || !caps) {
        return ERROR_INTERNAL_ERROR;
    }

    LOGI("RDP: GFX CapsAdvertise — advertising %u capability set(s) including H.264/AVC",
         caps ? caps->capsSetCount : 0u);

    if (self->prevCapsAdvertise_) {
        return self->prevCapsAdvertise_(gfx, caps);
    }
    return CHANNEL_RC_OK;
}

// Task 7: SurfaceCommand — extract the compressed H.264 payload and dispatch it.
UINT RdpConnectionManager::OnGfxSurfaceCommand(RdpgfxClientContext* gfx,
                                                const RDPGFX_SURFACE_COMMAND* cmd) {
    auto* self = GetSelfFromGfx(gfx);
    if (!self || !cmd) {
        return ERROR_INTERNAL_ERROR;
    }

    if (cmd->codecId != RDPGFX_CODECID_AVC420) {
        ScreenLog("[WARN] GFX codec=%s(%u) surface=%u bytes=%u",
                  GfxCodecToString(cmd->codecId), cmd->codecId,
                  cmd->surfaceId, cmd->length);

        self->softwareFallbackActive_ = true;
        self->softwareFramePending_ = true;
        if (!self->softwareFallbackLogged_) {
            self->softwareFallbackLogged_ = true;
            ScreenLog("[WARN] software GFX fallback (non-H.264 codec)");
        }

        if (self->prevSurfaceCommand_) {
            return self->prevSurfaceCommand_(gfx, cmd);
        }
        return CHANNEL_RC_OK;
    }

    const auto* avc = static_cast<const RDPGFX_AVC420_BITMAP_STREAM*>(cmd->extra);
    if (!avc || avc->length == 0 || !avc->data) {
        ScreenLog("[WARN] AVC420 stream missing data raw=%u", cmd->length);
        return CHANNEL_RC_OK;
    }

    // Tasks 8-10: dispatch to the VirtualMonitor which feeds AMediaCodec.
    self->OnGfxSurface(cmd->surfaceId, avc->data, avc->length, /*pts=*/0);
    return CHANNEL_RC_OK;
}

UINT RdpConnectionManager::OnGfxSurfaceCreated(RdpgfxClientContext* gfx,
                                                const RDPGFX_CREATE_SURFACE_PDU* pdu) {
    auto* self = GetSelfFromGfx(gfx);
    if (!self || !pdu) {
        return ERROR_INTERNAL_ERROR;
    }

    UINT rc = CHANNEL_RC_OK;
    if (self->prevCreateSurface_) {
        rc = self->prevCreateSurface_(gfx, pdu);
        if (rc != CHANNEL_RC_OK) {
            ScreenLog("[ERR] CreateSurface %u failed rc=%u", pdu->surfaceId, rc);
            return rc;
        }
    }

    int slot = -1;
    for (uint32_t i = 0; i < kMaxMonitors; ++i) {
        if (self->surfaceIds_[i] == pdu->surfaceId) {
            slot = static_cast<int>(i);
            break;
        }
    }
    if (slot < 0) {
        for (uint32_t i = 0; i < kMaxMonitors; ++i) {
            if (self->surfaceIds_[i] == UINT32_MAX) {
                slot = static_cast<int>(i);
                break;
            }
        }
    }
    if (slot < 0) {
        ScreenLog("[ERR] surface table full, dropping %u", pdu->surfaceId);
        return rc;
    }

    uint32_t monitorIdx = UINT32_MAX;
    if (self->surfaceToMonitor_[slot] != UINT32_MAX) {
        monitorIdx = self->surfaceToMonitor_[slot];
    } else if (self->nextMonitorIdx_ < self->monitorCount_) {
        monitorIdx = self->nextMonitorIdx_++;
    } else {
        ScreenLog("[WARN] no free monitor slot for surface %u", pdu->surfaceId);
        return rc;
    }

    self->surfaceIds_[slot] = pdu->surfaceId;
    self->surfaceToMonitor_[slot] = monitorIdx;
    ScreenLog("[OK] Surface %u -> monitor[%u] %ux%u",
              pdu->surfaceId, monitorIdx, pdu->width, pdu->height);
    return rc;
}

UINT RdpConnectionManager::OnGfxDeleteSurface(RdpgfxClientContext* gfx,
                                               const RDPGFX_DELETE_SURFACE_PDU* pdu) {
    auto* self = GetSelfFromGfx(gfx);
    if (!self || !pdu) {
        return ERROR_INTERNAL_ERROR;
    }

    for (uint32_t i = 0; i < kMaxMonitors; ++i) {
        if (self->surfaceIds_[i] == pdu->surfaceId) {
            self->surfaceIds_[i] = UINT32_MAX;
            self->surfaceToMonitor_[i] = UINT32_MAX;
            ScreenLog("[WARN] Surface %u deleted", pdu->surfaceId);
            break;
        }
    }

    if (self->prevDeleteSurface_) {
        return self->prevDeleteSurface_(gfx, pdu);
    }
    return CHANNEL_RC_OK;
}

UINT RdpConnectionManager::OnGfxResetGraphics(RdpgfxClientContext* gfx,
                                               const RDPGFX_RESET_GRAPHICS_PDU* pdu) {
    auto* self = GetSelfFromGfx(gfx);
    if (!self || !pdu) {
        return ERROR_INTERNAL_ERROR;
    }

    std::fill(std::begin(self->surfaceIds_), std::end(self->surfaceIds_), UINT32_MAX);
    std::fill(std::begin(self->surfaceToMonitor_), std::end(self->surfaceToMonitor_), UINT32_MAX);
    std::fill(std::begin(self->monitorFrameCount_), std::end(self->monitorFrameCount_), 0u);
    self->nextMonitorIdx_ = 0;
    self->softwareFallbackActive_ = false;
    self->softwareFramePending_ = false;
    self->softwareFallbackLogged_ = false;

    ScreenLog("[OK] ResetGraphics %ux%u monitors=%u",
              pdu->width, pdu->height, pdu->monitorCount);

    if (!self->context_ || !self->context_->disp) {
        const uint32_t fallbackCount =
            std::max<uint32_t>(1u, std::min<uint32_t>(pdu->monitorCount, self->monitorCount_));
        ScreenLog("[WARN] disp channel missing, fallback=%u monitor(s)", fallbackCount);
        self->displayControl_.ActivateMonitorCount(fallbackCount);
    }

    if (self->prevResetGraphics_) {
        return self->prevResetGraphics_(gfx, pdu);
    }
    return CHANNEL_RC_OK;
}

UINT RdpConnectionManager::OnGfxStartFrame(RdpgfxClientContext* gfx,
                                            const RDPGFX_START_FRAME_PDU* pdu) {
    auto* self = GetSelfFromGfx(gfx);
    if (!self || !pdu) {
        return ERROR_INTERNAL_ERROR;
    }

    if (self->prevStartFrame_) {
        return self->prevStartFrame_(gfx, pdu);
    }
    return CHANNEL_RC_OK;
}

UINT RdpConnectionManager::OnGfxSurfaceToOutput(RdpgfxClientContext* gfx,
                                                  const RDPGFX_MAP_SURFACE_TO_OUTPUT_PDU* pdu) {
    auto* self = GetSelfFromGfx(gfx);
    if (!self || !pdu) {
        return ERROR_INTERNAL_ERROR;
    }

    ScreenLog("[OK] Surface %u @ (%d,%d)",
              pdu->surfaceId, pdu->outputOriginX, pdu->outputOriginY);

    if (self->prevMapSurfaceToOutput_) {
        return self->prevMapSurfaceToOutput_(gfx, pdu);
    }
    return CHANNEL_RC_OK;
}

UINT RdpConnectionManager::OnGfxSurfaceToScaledOutput(
        RdpgfxClientContext* gfx,
        const RDPGFX_MAP_SURFACE_TO_SCALED_OUTPUT_PDU* pdu) {
    auto* self = GetSelfFromGfx(gfx);
    if (!self || !pdu) {
        return ERROR_INTERNAL_ERROR;
    }

    ScreenLog("[OK] Surface %u scaled %ux%u @ (%d,%d)",
              pdu->surfaceId, pdu->targetWidth, pdu->targetHeight,
              pdu->outputOriginX, pdu->outputOriginY);

    if (self->prevMapSurfaceToScaledOutput_) {
        return self->prevMapSurfaceToScaledOutput_(gfx, pdu);
    }
    return CHANNEL_RC_OK;
}

UINT RdpConnectionManager::OnGfxEndFrame(RdpgfxClientContext* gfx,
                                          const RDPGFX_END_FRAME_PDU* pdu) {
    auto* self = GetSelfFromGfx(gfx);
    if (!self || !pdu) {
        return ERROR_INTERNAL_ERROR;
    }

    if (self->prevEndFrame_) {
        const UINT rc = self->prevEndFrame_(gfx, pdu);
        if (rc != CHANNEL_RC_OK) {
            return rc;
        }
    }

    if (self->softwareFallbackActive_ && self->softwareFramePending_) {
        self->PushSoftwareFallbackFrame(gfx);
        self->softwareFramePending_ = false;
    }
    return CHANNEL_RC_OK;
}

// Tasks 8-10: look up the VirtualMonitor for this surface and feed the NAL unit.
void RdpConnectionManager::OnGfxSurface(uint32_t surfaceId, const uint8_t* data,
                                         size_t size, int64_t presentationTimeUs) {
    uint32_t monitorIdx = UINT32_MAX;
    for (uint32_t i = 0; i < kMaxMonitors; ++i) {
        if (surfaceIds_[i] == surfaceId) {
            monitorIdx = surfaceToMonitor_[i];
            break;
        }
    }

    if (monitorIdx == UINT32_MAX || monitorIdx >= monitorCount_) {
        LOGW("RDP: OnGfxSurface: no monitor mapped for surfaceId=%u", surfaceId);
        return;
    }

    VirtualMonitor* monitor = monitors_[monitorIdx];
    if (!monitor) {
        LOGW("RDP: OnGfxSurface: null monitor at index %u for surfaceId=%u", monitorIdx, surfaceId);
        return;
    }

    uint32_t frameCount = ++monitorFrameCount_[monitorIdx];
    if (frameCount == 1) {
        ScreenLog("[OK] monitor[%u] first H264 frame (%zu bytes)", monitorIdx, size);
    }

    // Task 8: AMediaCodec_dequeueInputBuffer is invoked inside SubmitFrame via
    //         the async OnInputAvailable path (or directly if a buffer is pending).
    // Task 9: memcpy of the network payload happens in VirtualMonitor::FeedToCodec.
    // Task 10: AMediaCodec_queueInputBuffer with failure logging is in FeedToCodec.
    monitor->SubmitFrame(data, size, presentationTimeUs);
}

void RdpConnectionManager::PushSoftwareFallbackFrame(RdpgfxClientContext* gfx) {
    auto* gdi = gfx ? static_cast<rdpGdi*>(gfx->custom) : nullptr;
    if (!gdi || !gdi->primary_buffer || gdi->width <= 0 || gdi->height <= 0 || gdi->stride == 0) {
        ScreenLog("[ERR] software GFX frame unavailable");
        return;
    }

    const uint32_t gdiW = static_cast<uint32_t>(gdi->width);
    const uint32_t gdiH = static_cast<uint32_t>(gdi->height);
    const uint32_t stride = gdi->stride;
    const uint32_t monW = 1920;

    // RDP desktop layout: monitor 1 @ x=0, monitor 0 @ x=1920, monitor 2 @ x=3840.
    // Map each monitor to its horizontal crop region in the GDI buffer.
    struct MonitorCrop { uint32_t idx; uint32_t offsetX; };
    const MonitorCrop crops[] = {
        {0, 1920},  // center (primary)
        {1, 0},     // left
        {2, 3840},  // right
    };

    for (const auto& crop : crops) {
        if (crop.idx >= monitorCount_ || !monitors_[crop.idx]) continue;
        if (crop.offsetX + monW > gdiW) continue;  // surface too narrow

        const uint8_t* regionPtr = gdi->primary_buffer + static_cast<size_t>(crop.offsetX) * 4u;
        monitors_[crop.idx]->SubmitSoftwareFrame(regionPtr, monW, gdiH, stride);

        if (++monitorFrameCount_[crop.idx] == 1) {
            ScreenLog("[OK] monitor[%u] first software GFX frame (crop x=%u %ux%u)",
                      crop.idx, crop.offsetX, monW, gdiH);
        }
    }
}
