#include <android_native_app_glue.h>

#include "util/Logger.h"
#include "xr/XrContext.h"
#ifdef __ANDROID__
#include <winpr/android.h>
#endif
#include "xr/XrPassthrough.h"
#include "xr/XrCompositor.h"
#include "rdp/RdpConnectionManager.h"
#include "rdp/RdpDisplayControl.h"
#include "scene/MonitorLayout.h"
#include "scene/FrustumCuller.h"
#include "scene/VirtualMonitor.h"

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

    // One VirtualMonitor per slot — owns codec + surface bridge + swapchain.
    std::array<std::unique_ptr<VirtualMonitor>, MonitorLayout::kMaxMonitors> monitors;
    std::array<VirtualMonitor*, MonitorLayout::kMaxMonitors>                  monitorPtrs{};

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

    // Register the JavaVM with WinPR so its JNI-dependent subsystems
    // (timezone, locale, etc.) can attach to the JVM before FreeRDP initialises.
#ifdef __ANDROID__
    winpr_InitializeJvm(app->activity->vm);
#endif

    AppState state;
    app->userData  = &state;
    app->onAppCmd  = handle_app_cmd;

    // ── Initialise OpenXR ────────────────────────────────────────────────────
    state.xrContext = std::make_unique<XrContext>(app);
    state.xrContext->CreateInstance();
    state.xrContext->CreateVulkanObjects();
    state.xrContext->CreateSession();
    state.xrContext->InitializePassthrough();

    state.passthrough = std::make_unique<XrPassthrough>(*state.xrContext);
    state.passthrough->Start();

    state.monitorLayout = std::make_unique<MonitorLayout>();
    state.monitorLayout->BuildDefaultLayout();

    state.frustumCuller = std::make_unique<FrustumCuller>();

    // ── Initialise VirtualMonitor objects ────────────────────────────────────
    // Phase 1: codec + surface bridge (no XrSession dependency).
    for (uint32_t i = 0; i < MonitorLayout::kMaxMonitors; ++i) {
        state.monitors[i] = std::make_unique<VirtualMonitor>(i, 1920, 1080);
        state.monitors[i]->InitCodec();
        // Phase 2: OpenXR swapchain (requires XrSession created above).
        state.monitors[i]->InitXr(*state.xrContext);
        state.monitorPtrs[i] = state.monitors[i].get();
    }

    // ── RDP subsystem ────────────────────────────────────────────────────────
    state.displayControl = std::make_unique<RdpDisplayControl>(*state.monitorLayout);
    state.rdpManager     = std::make_unique<RdpConnectionManager>(
        *state.displayControl,
        state.monitorPtrs.data(),
        static_cast<uint32_t>(state.monitorPtrs.size()));

    // ── XrCompositor ─────────────────────────────────────────────────────────
    state.compositor = std::make_unique<XrCompositor>(
        *state.xrContext,
        *state.passthrough,
        *state.monitorLayout,
        *state.frustumCuller,
        state.monitorPtrs);

    // Connect to RDP server. Address/credentials come from config in production.
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
        int events = 0;
        android_poll_source* source = nullptr;
        while (ALooper_pollOnce(0, nullptr, &events, reinterpret_cast<void**>(&source)) >= 0) {
            if (source) source->process(app, source);
            if (app->destroyRequested) {
                state.running = false;
                break;
            }
        }

        if (!state.running) break;

        bool exitRequested = false;
        state.xrContext->PollEvents(exitRequested, state.sessionActive);
        if (exitRequested) break;

        if (!state.sessionActive) continue;

        XrFrameState frameState{XR_TYPE_FRAME_STATE};
        if (!state.xrContext->BeginFrame(frameState)) continue;
        state.compositor->RenderFrame(frameState);
    }

    // ── Teardown ──────────────────────────────────────────────────────────────
    state.rdpManager->Disconnect();
    state.passthrough->Pause();

    LOGI("HyperDesk-MR shutting down");
}
