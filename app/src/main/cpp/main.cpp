#include <android_native_app_glue.h>

#include "util/Logger.h"
#include "util/ErrorUtils.h"
#include "xr/XrContext.h"
#include "xr/XrPassthrough.h"
#include "xr/XrCompositor.h"
#include "xr/StatusOverlay.h"
#include "rdp/RdpConnectionManager.h"
#include "rdp/RdpDisplayControl.h"
#include "camera/QrScanner.h"
#include "scene/MonitorLayout.h"
#include "scene/FrustumCuller.h"
#include "scene/VirtualMonitor.h"

#include <array>
#include <cstdlib>
#include <memory>

#ifdef __ANDROID__
#include <jni.h>
// jniVm is the global JavaVM pointer in winpr/libwinpr/utils/android.c.
// winpr_jni_attach_thread asserts it is non-null before any JNI call.
extern "C" JavaVM* jniVm;
#endif

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
    std::unique_ptr<StatusOverlay>        statusOverlay;
    std::unique_ptr<QrScanner>            qrScanner;

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

    // Register the JavaVM with WinPR so winpr_jni_attach_thread (used by
    // Unicode conversion, timezone, etc.) can attach to the JVM.
    // Also set HOME so WinPR's GetKnownPath can resolve config paths.
#ifdef __ANDROID__
    jniVm = app->activity->vm;
    if (app->activity->internalDataPath)
        setenv("HOME", app->activity->internalDataPath, 0);
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

    // ── Status overlay (renders between passthrough and monitors) ─────────
    state.statusOverlay = std::make_unique<StatusOverlay>(*state.xrContext, 512, 64);
    state.statusOverlay->SetMessage("Ready to Scan...");
    state.compositor->SetStatusOverlay(state.statusOverlay.get());

    // RDP error callback — show error text in the status overlay.
    state.rdpManager->SetErrorCallback([&state](uint32_t errorCode) {
        const char* msg = ErrorUtils::RdpErrorToString(errorCode);
        if (msg) {
            LOGE("RDP error: %s (0x%08X)", msg, errorCode);
            state.statusOverlay->SetMessage(msg);
        }
    });

    // Start QR scanner — connection is triggered when a valid QR code is scanned.
    state.qrScanner = std::make_unique<QrScanner>(app->activity);
    state.qrScanner->SetConnectCallback(
        [&state](const RdpConnectionManager::ConnectionParams& params) {
            LOGI("QR scan complete — connecting to %s:%u", params.hostname.c_str(), params.port);
            state.xrContext->TriggerHapticPulse(0.8f, 200000000);  // 200ms pulse
            state.statusOverlay->SetMessage("Connecting...");
            state.rdpManager->Connect(params);
            // Stop camera to save battery after successful connection.
            state.qrScanner->Stop();
        });
    if (state.qrScanner->Start()) {
        LOGI("QR scanner active — point headset at a QR code to connect");
    } else {
        LOGE("QR scanner failed to start — check CAMERA permission");
    }

    // ── Main loop ─────────────────────────────────────────────────────────────
    uint64_t frameCount = 0;
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

        // Must submit frames after xrBeginSession for the runtime to
        // transition through SYNCHRONIZED → VISIBLE → FOCUSED.
        if (!state.xrContext->IsSessionRunning()) continue;

        XrFrameState frameState{XR_TYPE_FRAME_STATE};
        bool shouldRender = state.xrContext->BeginFrame(frameState);

        if (shouldRender && state.sessionActive) {
            state.xrContext->SyncActions();
            state.compositor->RenderFrame(frameState);
        } else {
            // Submit empty frame — required by spec after xrBeginFrame.
            state.xrContext->EndFrame(frameState, 0, nullptr);
        }

        // Periodic scanner status indicator (~every 5s at 72fps).
        if (++frameCount % 360 == 0) {
            if (state.qrScanner && state.qrScanner->IsRunning()) {
                LOGI("QR scanner scanning...");
            }
            // Hide the status overlay once RDP is connected.
            if (state.rdpManager->IsConnected() && state.statusOverlay->IsVisible()) {
                state.statusOverlay->SetMessage("");
                LOGI("RDP connected — status overlay hidden");
            }
        }
    }

    // ── Teardown ──────────────────────────────────────────────────────────────
    if (state.qrScanner) state.qrScanner->Stop();
    state.rdpManager->Disconnect();
    state.passthrough->Pause();

    LOGI("HyperDesk-MR shutting down");
}
