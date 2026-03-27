#include "RdpConnectionManager.h"
#include "RdpDisplayControl.h"
#include "../scene/VirtualMonitor.h"
#include "../util/Logger.h"

#include <freerdp/channels/channels.h>
#include <winpr/synch.h>

#include <algorithm>
#include <cstring>

// ── Constructor / Destructor ──────────────────────────────────────────────────

RdpConnectionManager::RdpConnectionManager(RdpDisplayControl& displayControl,
                                             VirtualMonitor* const monitors[],
                                             uint32_t monitorCount)
    : displayControl_(displayControl),
      monitorCount_(std::min(monitorCount, kMaxMonitors)) {
    uint32_t count = std::min(monitorCount, kMaxMonitors);
    for (uint32_t i = 0; i < count; ++i) monitors_[i] = monitors[i];
    std::fill(std::begin(surfaceToMonitor_), std::end(surfaceToMonitor_), UINT32_MAX);
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
    instance_ = freerdp_new();
    if (!instance_) {
        LOGE("freerdp_new() failed");
        return false;
    }

    // Allocate extended context.
    instance_->ContextSize = sizeof(HyperDeskRdpContext);
    if (!freerdp_context_new(instance_)) {
        LOGE("freerdp_context_new() failed");
        freerdp_free(instance_);
        instance_ = nullptr;
        return false;
    }

    context_       = reinterpret_cast<HyperDeskRdpContext*>(instance_->context);
    context_->self = this;
    context_->disp = nullptr;
    context_->gfx  = nullptr;

    // Register lifecycle callbacks.
    instance_->PreConnect       = OnPreConnect;
    instance_->PostConnect      = OnPostConnect;
    instance_->PostDisconnect   = OnPostDisconnect;

    // Register channel connect callback via PubSub (FreeRDP 3.x).
    PubSub_SubscribeChannelConnected(instance_->context->pubSub, OnChannelsConnected);

    SetupSettings(instance_->context->settings, params);

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

    if (!freerdp_connect(instance_)) {
        uint32_t err = freerdp_get_last_error(instance_->context);
        lastError_.store(err);
        LOGE("freerdp_connect() failed — error 0x%08X", err);
        {
            std::lock_guard lock(errorCbMutex_);
            if (errorCallback_) errorCallback_(err);
        }
        return;
    }
    lastError_.store(0);
    connected_.store(true);
    LOGI("RDP connected");

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
            LOGE("freerdp_check_event_handles failed — error 0x%08X — disconnecting", err);
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
    if (instance_) {
        freerdp_context_free(instance_);
        freerdp_free(instance_);
        instance_ = nullptr;
        context_  = nullptr;
    }
}

// ── Static callbacks ──────────────────────────────────────────────────────────

BOOL RdpConnectionManager::OnPreConnect(freerdp* /*instance*/) {
    LOGI("RDP: OnPreConnect");
    return TRUE;
}

BOOL RdpConnectionManager::OnPostConnect(freerdp* /*instance*/) {
    LOGI("RDP: OnPostConnect");
    return TRUE;
}

void RdpConnectionManager::OnPostDisconnect(freerdp* instance) {
    LOGI("RDP: OnPostDisconnect");
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

    if (strcmp(e->name, DISP_DVC_CHANNEL_NAME) == 0) {
        ctx->disp = static_cast<DispClientContext*>(e->pInterface);
        if (ctx->disp) {
            LOGI("RDP: DispClientContext obtained");
            self->displayControl_.Attach(ctx->disp);
        }
    } else if (strcmp(e->name, RDPGFX_DVC_CHANNEL_NAME) == 0) {
        ctx->gfx = static_cast<RdpgfxClientContext*>(e->pInterface);
        if (ctx->gfx) {
            LOGI("RDP: RdpgfxClientContext obtained");
            ctx->gfx->CapsAdvertise      = OnGfxCapsAdvertise;
            ctx->gfx->SurfaceCommand     = OnGfxSurfaceCommand;
            ctx->gfx->CreateSurface      = OnGfxSurfaceCreated;
            ctx->gfx->StartFrame         = OnGfxStartFrame;
            ctx->gfx->MapSurfaceToOutput = OnGfxSurfaceToOutput;
            ctx->gfx->EndFrame           = OnGfxEndFrame;
            ctx->gfx->custom             = self;
        } else {
            LOGW("RDP: GFX channel not available");
        }
    }
}

// ── GFX callbacks ─────────────────────────────────────────────────────────────

// Task 6: CapsAdvertise — log what's being advertised; H.264 is already requested
// via FreeRDP_GfxH264 / FreeRDP_SupportGraphicsPipeline settings.
UINT RdpConnectionManager::OnGfxCapsAdvertise(RdpgfxClientContext* /*gfx*/,
                                               const RDPGFX_CAPS_ADVERTISE_PDU* caps) {
    LOGI("RDP: GFX CapsAdvertise — advertising %u capability set(s) including H.264/AVC",
         caps ? caps->capsSetCount : 0u);
    return CHANNEL_RC_OK;
}

// Task 7: SurfaceCommand — extract the compressed H.264 payload and dispatch it.
UINT RdpConnectionManager::OnGfxSurfaceCommand(RdpgfxClientContext* gfx,
                                                const RDPGFX_SURFACE_COMMAND* cmd) {
    if (cmd->codecId != RDPGFX_CODECID_AVC420 &&
        cmd->codecId != RDPGFX_CODECID_AVC444) {
        return CHANNEL_RC_OK;
    }
    auto* self = static_cast<RdpConnectionManager*>(gfx->custom);

    const auto* avc = static_cast<const RDPGFX_AVC420_BITMAP_STREAM*>(cmd->extra);
    if (!avc || avc->length == 0 || !avc->data) {
        LOGW("RDP: GFX SurfaceCommand codecId=0x%04X: empty or null AVC stream, skipping",
             cmd->codecId);
        return CHANNEL_RC_OK;
    }

    // Tasks 8-10: dispatch to the VirtualMonitor which feeds AMediaCodec.
    self->OnGfxSurface(cmd->surfaceId, avc->data, avc->length, /*pts=*/0);
    return CHANNEL_RC_OK;
}

UINT RdpConnectionManager::OnGfxSurfaceCreated(RdpgfxClientContext* gfx,
                                                const RDPGFX_CREATE_SURFACE_PDU* pdu) {
    auto* self = static_cast<RdpConnectionManager*>(gfx->custom);
    uint32_t slot       = pdu->surfaceId % kMaxMonitors;
    uint32_t monitorIdx = self->nextMonitorIdx_++;
    self->surfaceToMonitor_[slot] = monitorIdx;
    LOGI("RDP: GFX surface created id=%u → monitor[%u] (%ux%u)",
         pdu->surfaceId, monitorIdx, pdu->width, pdu->height);
    return CHANNEL_RC_OK;
}

UINT RdpConnectionManager::OnGfxStartFrame(RdpgfxClientContext* /*gfx*/,
                                            const RDPGFX_START_FRAME_PDU* /*pdu*/) {
    return CHANNEL_RC_OK;
}

UINT RdpConnectionManager::OnGfxSurfaceToOutput(RdpgfxClientContext* gfx,
                                                  const RDPGFX_MAP_SURFACE_TO_OUTPUT_PDU* pdu) {
    (void)gfx;
    (void)pdu;
    // Surface-to-output mapping recorded; actual frame data arrives via the codec callback.
    return CHANNEL_RC_OK;
}

UINT RdpConnectionManager::OnGfxEndFrame(RdpgfxClientContext* /*gfx*/,
                                          const RDPGFX_END_FRAME_PDU* /*pdu*/) {
    return CHANNEL_RC_OK;
}

// Tasks 8-10: look up the VirtualMonitor for this surface and feed the NAL unit.
void RdpConnectionManager::OnGfxSurface(uint32_t surfaceId, const uint8_t* data,
                                         size_t size, int64_t presentationTimeUs) {
    uint32_t slot       = surfaceId % kMaxMonitors;
    uint32_t monitorIdx = surfaceToMonitor_[slot];

    if (monitorIdx == UINT32_MAX || monitorIdx >= monitorCount_) {
        LOGW("RDP: OnGfxSurface: no monitor mapped for surfaceId=%u (slot=%u)", surfaceId, slot);
        return;
    }

    VirtualMonitor* monitor = monitors_[monitorIdx];
    if (!monitor) {
        LOGW("RDP: OnGfxSurface: null monitor at index %u for surfaceId=%u", monitorIdx, surfaceId);
        return;
    }

    // Task 8: AMediaCodec_dequeueInputBuffer is invoked inside SubmitFrame via
    //         the async OnInputAvailable path (or directly if a buffer is pending).
    // Task 9: memcpy of the network payload happens in VirtualMonitor::FeedToCodec.
    // Task 10: AMediaCodec_queueInputBuffer with failure logging is in FeedToCodec.
    monitor->SubmitFrame(data, size, presentationTimeUs);
}
