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
    freerdp_settings_set_bool(settings, FreeRDP_SupportGraphicsPipeline, TRUE);
    freerdp_settings_set_bool(settings, FreeRDP_GfxH264,                 TRUE);
    freerdp_settings_set_bool(settings, FreeRDP_GfxAVC444,               FALSE);

    // Disable NLA — OpenSSL on Android lacks the LEGACY provider (MD4)
    // which NTLM password hashing requires.  Fall back to TLS security.
    freerdp_settings_set_bool(settings, FreeRDP_NlaSecurity, FALSE);
    freerdp_settings_set_bool(settings, FreeRDP_TlsSecurity, TRUE);
    freerdp_settings_set_bool(settings, FreeRDP_RdpSecurity, TRUE);

    // Accept self-signed RDP certificates without user prompt.
    freerdp_settings_set_bool(settings, FreeRDP_IgnoreCertificate, TRUE);

    // Set a large desktop area to accommodate 16 monitors at 1920x1080.
    freerdp_settings_set_uint32(settings, FreeRDP_DesktopWidth,  1920 * 4);
    freerdp_settings_set_uint32(settings, FreeRDP_DesktopHeight, 1080 * 4);
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
    prevStartFrame_ = nullptr;
    prevEndFrame_ = nullptr;
    prevMapSurfaceToOutput_ = nullptr;
    prevMapSurfaceToScaledOutput_ = nullptr;
}

// ── Static callbacks ──────────────────────────────────────────────────────────

RdpConnectionManager* RdpConnectionManager::GetSelfFromGfx(RdpgfxClientContext* gfx) {
    if (!gfx || !gfx->custom) return nullptr;

    auto* gdi = static_cast<rdpGdi*>(gfx->custom);
    if (!gdi || !gdi->context) return nullptr;

    auto* ctx = reinterpret_cast<HyperDeskRdpContext*>(gdi->context);
    return ctx ? ctx->self : nullptr;
}

static BOOL OnBeginPaint(rdpContext* /*context*/) { return TRUE; }
static BOOL OnEndPaint(rdpContext* /*context*/)   { return TRUE; }

BOOL RdpConnectionManager::OnPreConnect(freerdp* instance) {
    ScreenLog("[OK] TLS handshake...");
    // FreeRDP requires update callbacks even when using the GFX pipeline.
    rdpUpdate* update = instance->context->update;
    update->BeginPaint = OnBeginPaint;
    update->EndPaint   = OnEndPaint;
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
        if (cmd->codecId == RDPGFX_CODECID_AVC444 ||
            cmd->codecId == RDPGFX_CODECID_AVC444v2) {
            ScreenLog("[WARN] AVC444 stream unsupported on Quest path");
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

    ScreenLog("[OK] ResetGraphics %ux%u monitors=%u",
              pdu->width, pdu->height, pdu->monitorCount);

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
        return self->prevEndFrame_(gfx, pdu);
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
