#include <android_native_app_glue.h>

#include "util/Logger.h"
#include "xr/XrContext.h"
#include "xr/XrPassthrough.h"
#include "xr/XrCompositor.h"
#include "rdp/RdpConnectionManager.h"
#include "rdp/RdpDisplayControl.h"
#include "codec/MediaCodecDecoder.h"
#include "codec/CodecSurfaceBridge.h"
#include "scene/MonitorLayout.h"
#include "scene/FrustumCuller.h"

#include <array>
#include <memory>

// ── Application state ─────────────────────────────────────────────────────────

struct AppState {
    std::unique_ptr<XrContext>            xrContext;
    std::unique_ptr<XrPassthrough>        passthrough;
    std::unique_ptr<MonitorLayout>        monitorLayout;
    std::unique_ptr<FrustumCuller>        frustumCuller;
    std::unique_ptr<RdpDisplayControl>    displayControl;
    std::unique_ptr<RdpConnectionManager> rdpManager;
    std::array<std::unique_ptr<CodecSurfaceBridge>, MonitorLayout::kMaxMonitors> bridges;
    std::array<std::unique_ptr<MediaCodecDecoder>,  MonitorLayout::kMaxMonitors> decoders;
    std::unique_ptr<XrCompositor>         compositor;

    bool running       = true;
    bool sessionActive = false;
};

// ── Android lifecycle callback ────────────────────────────────────────────────

static void handle_app_cmd(android_app* app, int32_t cmd) {
    auto* state = static_cast<AppState*>(app->userData);
    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            LOGI("APP_CMD_INIT_WINDOW");
            break;
        case APP_CMD_TERM_WINDOW:
            LOGI("APP_CMD_TERM_WINDOW");
            break;
        case APP_CMD_DESTROY:
            LOGI("APP_CMD_DESTROY");
            state->running = false;
            break;
        default:
            break;
    }
}

// ── Entry point ───────────────────────────────────────────────────────────────

void android_main(android_app* app) {
    LOGI("HyperDesk-MR starting");

    AppState state;
    app->userData  = &state;
    app->onAppCmd  = handle_app_cmd;

    // ── Initialise subsystems ─────────────────────────────────────────────────
    state.xrContext = std::make_unique<XrContext>(app);
    state.xrContext->CreateInstance();
    state.xrContext->CreateVulkanObjects();
    state.xrContext->CreateSession();
    state.xrContext->InitializePassthrough();

    state.passthrough  = std::make_unique<XrPassthrough>(*state.xrContext);
    state.passthrough->Start();

    state.monitorLayout = std::make_unique<MonitorLayout>();
    state.monitorLayout->BuildDefaultLayout();

    state.frustumCuller = std::make_unique<FrustumCuller>();

    // Create one CodecSurfaceBridge and MediaCodecDecoder per monitor slot.
    // Decoders start stopped; they are started when FreeRDP assigns a surface.
    for (uint32_t i = 0; i < MonitorLayout::kMaxMonitors; ++i) {
        state.bridges[i] = std::make_unique<CodecSurfaceBridge>(1920, 1080, i);
        state.decoders[i] = std::make_unique<MediaCodecDecoder>(
            state.bridges[i]->GetCodecOutputSurface(), i);
        state.decoders[i]->Configure(1920, 1080);
    }

    // RDP subsystem — connect in the background on its own thread.
    state.displayControl = std::make_unique<RdpDisplayControl>(*state.monitorLayout);
    state.rdpManager     = std::make_unique<RdpConnectionManager>(*state.displayControl);

    // Build raw decoder pointers for XrCompositor.
    std::array<MediaCodecDecoder*, MonitorLayout::kMaxMonitors> decoderPtrs{};
    for (uint32_t i = 0; i < MonitorLayout::kMaxMonitors; ++i) {
        decoderPtrs[i] = state.decoders[i].get();
    }

    state.compositor = std::make_unique<XrCompositor>(
        *state.xrContext,
        *state.passthrough,
        *state.monitorLayout,
        *state.frustumCuller,
        state.bridges,
        decoderPtrs);

    // Connect to RDP server. Address/credentials would come from config in production.
    RdpConnectionManager::ConnectionParams params{
        .hostname = "192.168.1.100",
        .port     = 3389,
        .username = "user",
        .password = "password",
        .domain   = "",
    };
    state.rdpManager->Connect(params);

    // ── Main loop ─────────────────────────────────────────────────────────────
    while (state.running) {
        // Process Android events.
        int events = 0;
        android_poll_source* source = nullptr;
        while (ALooper_pollAll(0, nullptr, &events, reinterpret_cast<void**>(&source)) >= 0) {
            if (source) source->process(app, source);
            if (app->destroyRequested) {
                state.running = false;
                break;
            }
        }

        if (!state.running) break;

        // Process OpenXR events and drive session state machine.
        bool exitRequested = false;
        state.xrContext->PollEvents(exitRequested, state.sessionActive);
        if (exitRequested) break;

        if (!state.sessionActive) continue;

        // Execute one render frame.
        XrFrameState frameState{XR_TYPE_FRAME_STATE};
        if (!state.xrContext->BeginFrame(frameState)) continue;
        state.compositor->RenderFrame(frameState);
    }

    // ── Teardown ──────────────────────────────────────────────────────────────
    state.rdpManager->Disconnect();
    state.passthrough->Pause();

    LOGI("HyperDesk-MR shutting down");
}
