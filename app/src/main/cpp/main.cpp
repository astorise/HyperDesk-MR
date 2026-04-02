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
#include <mutex>

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

    std::mutex                            pendingLayoutAnchorMutex;
    uint32_t                              pendingLayoutAnchorFrames = 0;

    bool running       = true;
    bool sessionActive = false;
    bool codecsReady   = false;
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
    static constexpr uint32_t kQrAnchorFrames = 30;

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
    // Delay codec allocation until after QR scanning to reduce camera pressure.
    for (uint32_t i = 0; i < MonitorLayout::kMaxMonitors; ++i) {
        state.monitors[i] = std::make_unique<VirtualMonitor>(i, 1920, 1080);
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

    // ── Status overlay (debug console between passthrough and monitors) ───
    state.statusOverlay = std::make_unique<StatusOverlay>(*state.xrContext, 1024, 512);
    state.statusOverlay->AddLog("HyperDesk MR ready");
    state.statusOverlay->SetStatusLine(0, "scan: booting");
    state.compositor->SetStatusOverlay(state.statusOverlay.get());

    // RDP error callback — show error text in the status overlay.
    state.rdpManager->SetErrorCallback([&state](uint32_t errorCode) {
        const char* msg = ErrorUtils::RdpErrorToString(errorCode);
        const char* hint = ErrorUtils::RdpErrorHint(errorCode);
        char buf[128];

        state.statusOverlay->SetStatusLine(0, "rdp: connect failed");
        snprintf(buf, sizeof(buf), "rdp: 0x%08X", errorCode);
        state.statusOverlay->SetStatusLine(1, buf);
        if (msg) {
            snprintf(buf, sizeof(buf), "rdp: %s", msg);
            state.statusOverlay->SetStatusLine(2, buf);
        }
        if (hint) {
            snprintf(buf, sizeof(buf), "hint: %s", hint);
            state.statusOverlay->SetStatusLine(3, buf);
        }

        snprintf(buf, sizeof(buf), "[ERR] RDP error 0x%08X", errorCode);
        state.statusOverlay->AddLog(buf);
        if (msg) {
            LOGE("RDP error: %s (0x%08X)", msg, errorCode);
            state.statusOverlay->AddLog(std::string("[ERR] ") + msg);
        }
        if (hint) {
            state.statusOverlay->AddLog(std::string("[WARN] ") + hint);
        }
    });

    // Start QR scanner — connection is triggered when a valid QR code is scanned.
    state.qrScanner = std::make_unique<QrScanner>(app->activity);
    state.qrScanner->SetConnectCallback(
        [&state](const RdpConnectionManager::ConnectionParams& params) {
            LOGI("QR scan complete — connecting to %s:%u", params.hostname.c_str(), params.port);
            state.xrContext->TriggerHapticPulse(0.8f, 200000000);  // 200ms pulse
            char buf[256];
            snprintf(buf, sizeof(buf), "[OK] QR scanned: %s:%u",
                     params.hostname.c_str(), params.port);
            state.statusOverlay->AddLog(buf);
            snprintf(buf, sizeof(buf), "  user=%s domain=%s",
                     params.username.c_str(), params.domain.c_str());
            state.statusOverlay->AddLog(buf);
            {
                std::lock_guard<std::mutex> lock(state.pendingLayoutAnchorMutex);
                state.pendingLayoutAnchorFrames = kQrAnchorFrames;
            }
            state.statusOverlay->AddLog("Aligning screen wall to scan heading...");
            state.statusOverlay->AddLog("Stopping camera...");
            state.qrScanner->Stop();

            if (!state.codecsReady) {
                state.statusOverlay->AddLog("Initializing video decoders...");
                for (uint32_t i = 0; i < MonitorLayout::kMaxMonitors; ++i) {
                    if (!state.monitors[i]->InitCodec()) {
                        LOGE("VirtualMonitor[%u]: InitCodec failed", i);
                        snprintf(buf, sizeof(buf), "[ERR] Decoder init failed on monitor %u", i);
                        state.statusOverlay->AddLog(buf);
                        return;
                    }
                }
                state.codecsReady = true;
                state.statusOverlay->AddLog("[OK] Video decoders ready");
            }

            state.statusOverlay->AddLog("Connecting...");
            state.rdpManager->Connect(params);
        });
    if (state.qrScanner->Start()) {
        LOGI("QR scanner active — point headset at a QR code to connect");
        state.statusOverlay->AddLog("[OK] Camera started");
        state.statusOverlay->AddLog("Point headset at QR code...");
    } else {
        LOGE("QR scanner failed to start — check CAMERA permission");
        state.statusOverlay->AddLog("[ERR] Camera failed to start");
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
            uint32_t anchorFramesRemaining = 0;
            {
                std::lock_guard<std::mutex> lock(state.pendingLayoutAnchorMutex);
                anchorFramesRemaining = state.pendingLayoutAnchorFrames;
            }

            if (anchorFramesRemaining > 0) {
                XrPosef headPose{};
                if (state.xrContext->LocateHeadPose(frameState.predictedDisplayTime, headPose)) {
                    if (anchorFramesRemaining == kQrAnchorFrames) {
                        LOGI("Applying QR wall anchor over %u XR frames", kQrAnchorFrames);
                    }
                    state.monitorLayout->AnchorPrimaryToHeadPose(headPose);
                    {
                        std::lock_guard<std::mutex> lock(state.pendingLayoutAnchorMutex);
                        if (state.pendingLayoutAnchorFrames > 0) {
                            --state.pendingLayoutAnchorFrames;
                            if (state.pendingLayoutAnchorFrames == 0) {
                                LOGI("QR wall anchor locked");
                                state.statusOverlay->AddLog("[OK] Screen wall anchor locked");
                            }
                        }
                    }
                } else {
                    LOGW("Head pose unavailable while anchoring QR wall");
                }
            }

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
            // Keep debug overlay visible when connected (don't hide it).
            if (state.rdpManager->IsConnected()) {
                // Overlay stays visible for debugging.
            }
        }
    }

    // ── Teardown ──────────────────────────────────────────────────────────────
    if (state.qrScanner) state.qrScanner->Stop();
    state.rdpManager->Disconnect();
    state.passthrough->Pause();

    LOGI("HyperDesk-MR shutting down");
}
