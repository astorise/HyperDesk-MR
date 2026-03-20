#include "RdpConnectionManager.h"
#include "RdpDisplayControl.h"
#include "../util/Logger.h"

#include <freerdp/channels/channels.h>
#include <freerdp/gdi/gdi.h>
#include <freerdp/client/channels.h>

#include <cstring>

// ── Constructor / Destructor ──────────────────────────────────────────────────

RdpConnectionManager::RdpConnectionManager(RdpDisplayControl& displayControl)
    : displayControl_(displayControl) {
    std::memset(surfaceToMonitor_, 0, sizeof(surfaceToMonitor_));
}

RdpConnectionManager::~RdpConnectionManager() {
    Disconnect();
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

    // Register channel connect callback via the channels layer.
    instance_->context->channels->onChannelsConnected = OnChannelsConnected;

    SetupSettings(instance_->settings, params);

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

    // NLA (Network Level Authentication) — enable for modern Windows hosts.
    freerdp_settings_set_bool(settings, FreeRDP_NlaSecurity, TRUE);

    // Set a large desktop area to accommodate 16 monitors at 1920x1080.
    freerdp_settings_set_uint32(settings, FreeRDP_DesktopWidth,  1920 * 4);
    freerdp_settings_set_uint32(settings, FreeRDP_DesktopHeight, 1080 * 4);
}

// ── RunEventLoop ──────────────────────────────────────────────────────────────

void RdpConnectionManager::RunEventLoop() {
    LOGI("RDP thread: connecting to %s",
         freerdp_settings_get_string(instance_->settings, FreeRDP_ServerHostname));

    if (!freerdp_connect(instance_)) {
        LOGE("freerdp_connect() failed");
        return;
    }
    connected_.store(true);
    LOGI("RDP connected");

    while (!stopFlag_.load()) {
        DWORD waitResult = freerdp_get_event_handles(instance_->context, nullptr, 0);
        (void)waitResult;
        if (!freerdp_check_event_handles(instance_->context)) {
            LOGE("freerdp_check_event_handles failed — disconnecting");
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

BOOL RdpConnectionManager::OnPreConnect(freerdp* instance) {
    LOGI("RDP: OnPreConnect");
    freerdp_client_load_addins(instance->context->channels, instance->settings);
    return TRUE;
}

BOOL RdpConnectionManager::OnPostConnect(freerdp* instance) {
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

void RdpConnectionManager::OnChannelsConnected(freerdp* instance, rdpChannels* channels) {
    LOGI("RDP: OnChannelsConnected");
    auto* ctx = reinterpret_cast<HyperDeskRdpContext*>(instance->context);
    if (!ctx || !ctx->self) return;

    RdpConnectionManager* self = ctx->self;

    // Obtain DisplayControl virtual channel context.
    ctx->disp = static_cast<DispClientContext*>(
        freerdp_client_channel_get_interface(channels, DISP_DVC_CHANNEL_NAME));
    if (ctx->disp) {
        LOGI("RDP: DispClientContext obtained");
        self->displayControl_.Attach(ctx->disp);
    } else {
        LOGW("RDP: DisplayControl channel not available");
    }

    // Obtain GFX pipeline context.
    ctx->gfx = static_cast<RdpGfxClientContext*>(
        freerdp_client_channel_get_interface(channels, RDPGFX_DVC_CHANNEL_NAME));
    if (ctx->gfx) {
        LOGI("RDP: RdpGfxClientContext obtained");
        ctx->gfx->SurfaceCreated     = OnGfxSurfaceCreated;
        ctx->gfx->StartFrame         = OnGfxStartFrame;
        ctx->gfx->SurfaceToOutput    = OnGfxSurfaceToOutput;
        ctx->gfx->EndFrame           = OnGfxEndFrame;
        // Store back-pointer to self in gfx context's custom data.
        ctx->gfx->custom = self;
    } else {
        LOGW("RDP: GFX channel not available");
    }
}

// ── GFX callbacks ─────────────────────────────────────────────────────────────

UINT RdpConnectionManager::OnGfxSurfaceCreated(RdpGfxClientContext* gfx,
                                                const RDPGFX_CREATE_SURFACE_PDU* pdu) {
    auto* self = static_cast<RdpConnectionManager*>(gfx->custom);
    uint32_t idx = pdu->surfaceId % kMaxMonitors;
    self->surfaceToMonitor_[idx] = pdu->surfaceId;
    LOGI("RDP: GFX surface created id=%u, w=%u h=%u", pdu->surfaceId, pdu->width, pdu->height);
    return CHANNEL_RC_OK;
}

UINT RdpConnectionManager::OnGfxStartFrame(RdpGfxClientContext* /*gfx*/,
                                            const RDPGFX_START_FRAME_PDU* /*pdu*/) {
    return CHANNEL_RC_OK;
}

UINT RdpConnectionManager::OnGfxSurfaceToOutput(RdpGfxClientContext* gfx,
                                                  const RDPGFX_SURFACE_TO_OUTPUT_PDU* pdu) {
    (void)gfx;
    (void)pdu;
    // Surface-to-output mapping recorded; actual frame data arrives via the codec callback.
    return CHANNEL_RC_OK;
}

UINT RdpConnectionManager::OnGfxEndFrame(RdpGfxClientContext* /*gfx*/,
                                          const RDPGFX_END_FRAME_PDU* /*pdu*/) {
    return CHANNEL_RC_OK;
}

void RdpConnectionManager::OnGfxSurface(uint32_t surfaceId, const uint8_t* /*data*/,
                                         size_t /*size*/, int64_t /*presentationTimeUs*/) {
    // Dispatch to the decoder for this surface.
    // The actual decoder array is owned by main.cpp; in production, pass it in at
    // construction time or via a callback registered by the application layer.
    LOGD("RDP: GFX surface data for surfaceId=%u", surfaceId);
}
