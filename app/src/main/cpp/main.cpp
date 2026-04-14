#include <android_native_app_glue.h>

#include "util/Logger.h"
#include "util/ErrorUtils.h"
#include "xr/XrContext.h"
#include "xr/XrPassthrough.h"
#include "xr/XrCompositor.h"
#include "xr/StatusOverlay.h"
#include "xr/CursorOverlay.h"
#include "xr/ImGuiToolbar.h"
#include "rdp/RdpConnectionManager.h"
#include "rdp/RdpDisplayControl.h"
#include "rdp/RdpInputForwarder.h"
#include "rdp/EvdevMouseReader.h"
#include "camera/QrScanner.h"
#include "scene/MonitorLayout.h"
#include "scene/FrustumCuller.h"
#include "scene/VirtualMonitor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
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
    std::array<std::unique_ptr<RdpConnectionManager>, MonitorLayout::kMaxMonitors> secondaryRdpManagers;
    std::unique_ptr<RdpInputForwarder>    inputForwarder;
    std::unique_ptr<EvdevMouseReader>     evdevMouse;

    // One VirtualMonitor per slot — owns codec + surface bridge + swapchain.
    std::array<std::unique_ptr<VirtualMonitor>, MonitorLayout::kMaxMonitors> monitors;
    std::array<VirtualMonitor*, MonitorLayout::kMaxMonitors>                  monitorPtrs{};

    std::unique_ptr<XrCompositor>         compositor;
    std::unique_ptr<StatusOverlay>        statusOverlay;
    std::unique_ptr<CursorOverlay>        cursorOverlay;
    std::unique_ptr<ImGuiToolbar>         imguiToolbar;
    std::unique_ptr<QrScanner>            qrScanner;

    std::mutex                            latestHeadPoseMutex;
    XrPosef                              latestHeadPose{{0.0f, 0.0f, 0.0f, 1.0f},
                                                        {0.0f, 0.0f, 0.0f}};
    bool                                 latestHeadPoseValid = false;
    std::array<XrPosef, 2>               latestEyePoses{{
                                            {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}},
                                            {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}}
                                        }};
    std::array<bool, 2>                  latestEyePosesValid{{false, false}};

    std::mutex                           pendingLayoutAnchorMutex;
    XrPosef                             lastLayoutAnchorPose{{0.0f, 0.0f, 0.0f, 1.0f},
                                                             {0.0f, 0.0f, 0.0f}};
    bool                                lastLayoutAnchorPoseValid = false;
    XrPosef                             pendingLayoutAnchorPose{{0.0f, 0.0f, 0.0f, 1.0f},
                                                                {0.0f, 0.0f, 0.0f}};
    bool                                pendingLayoutAnchorPoseValid = false;
    uint32_t                            pendingLayoutAnchorFrames = 0;

    bool running       = true;
    bool sessionActive = false;
    bool codecsReady   = false;

    // Reconnection state.
    RdpConnectionManager::ConnectionParams lastConnParams;
    bool                                   hasConnParams = false;
    bool                                   wasEverConnected = false;
    bool                                   suppressAutoReconnect = false;
    uint64_t                               reconnectCooldownFrame = 0;
    uint64_t                               qrRetryFrame = 0;
    bool                                   splitRowsEnabled = false;
    bool                                   qrAddConnectionMode = false;
    uint32_t                               qrTargetMonitor = 0;
    bool                                   dragWasHeld = false;
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

static int32_t handle_input(android_app* a, AInputEvent* event) {
    auto* s = static_cast<AppState*>(a->userData);
    if (!event) return 0;

    const int32_t type = AInputEvent_getType(event);
    const int32_t source = AInputEvent_getSource(event);
    LOGD("handle_input: type=%d source=0x%08X", type, source);

    if (s && s->inputForwarder && s->inputForwarder->OnInputEvent(event))
        return 1;
    return 0;
}

// ── Entry point ───────────────────────────────────────────────────────────────

namespace {

XrVector3f RotateByQuaternion(const XrQuaternionf& q, XrVector3f v) {
    const XrVector3f u{q.x, q.y, q.z};
    const float s = q.w;

    const XrVector3f crossUV{
        u.y * v.z - u.z * v.y,
        u.z * v.x - u.x * v.z,
        u.x * v.y - u.y * v.x
    };
    const XrVector3f crossUCrossUV{
        u.y * crossUV.z - u.z * crossUV.y,
        u.z * crossUV.x - u.x * crossUV.z,
        u.x * crossUV.y - u.y * crossUV.x
    };

    return {
        v.x + 2.0f * (s * crossUV.x + crossUCrossUV.x),
        v.y + 2.0f * (s * crossUV.y + crossUCrossUV.y),
        v.z + 2.0f * (s * crossUV.z + crossUCrossUV.z)
    };
}

float Dot(XrVector3f a, XrVector3f b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

XrVector3f NormalizeOrFallback(XrVector3f v, XrVector3f fallback) {
    const float lenSq = Dot(v, v);
    if (lenSq <= 1e-6f) {
        return fallback;
    }
    const float invLen = 1.0f / std::sqrt(lenSq);
    return {v.x * invLen, v.y * invLen, v.z * invLen};
}

#ifdef __ANDROID__
struct ScopedJniEnv {
    JavaVM* vm = nullptr;
    JNIEnv* env = nullptr;
    bool attached = false;

    explicit ScopedJniEnv(JavaVM* javaVm) : vm(javaVm) {
        if (!vm) return;
        if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_OK) {
            return;
        }
        if (vm->AttachCurrentThread(&env, nullptr) == JNI_OK) {
            attached = true;
        } else {
            env = nullptr;
        }
    }

    ~ScopedJniEnv() {
        if (vm && attached) {
            vm->DetachCurrentThread();
        }
    }
};

bool AdjustMediaVolume(ANativeActivity* activity, bool increase) {
    if (!activity || !activity->vm || !activity->clazz) {
        return false;
    }

    ScopedJniEnv jni(activity->vm);
    if (!jni.env) {
        return false;
    }

    JNIEnv* env = jni.env;
    jclass activityClass = env->GetObjectClass(activity->clazz);
    if (!activityClass) {
        return false;
    }

    jmethodID getSystemService = env->GetMethodID(
        activityClass, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
    if (!getSystemService) {
        env->DeleteLocalRef(activityClass);
        return false;
    }

    jclass contextClass = env->FindClass("android/content/Context");
    if (!contextClass) {
        env->DeleteLocalRef(activityClass);
        return false;
    }
    jfieldID audioServiceField = env->GetStaticFieldID(
        contextClass, "AUDIO_SERVICE", "Ljava/lang/String;");
    if (!audioServiceField) {
        env->DeleteLocalRef(contextClass);
        env->DeleteLocalRef(activityClass);
        return false;
    }
    jobject audioServiceName = env->GetStaticObjectField(contextClass, audioServiceField);
    if (!audioServiceName) {
        env->DeleteLocalRef(contextClass);
        env->DeleteLocalRef(activityClass);
        return false;
    }

    jobject audioManagerObject = env->CallObjectMethod(
        activity->clazz, getSystemService, audioServiceName);
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        env->DeleteLocalRef(audioServiceName);
        env->DeleteLocalRef(contextClass);
        env->DeleteLocalRef(activityClass);
        return false;
    }
    if (!audioManagerObject) {
        env->DeleteLocalRef(audioServiceName);
        env->DeleteLocalRef(contextClass);
        env->DeleteLocalRef(activityClass);
        return false;
    }

    jclass audioManagerClass = env->FindClass("android/media/AudioManager");
    if (!audioManagerClass) {
        env->DeleteLocalRef(audioManagerObject);
        env->DeleteLocalRef(audioServiceName);
        env->DeleteLocalRef(contextClass);
        env->DeleteLocalRef(activityClass);
        return false;
    }

    jmethodID adjustStreamVolume = env->GetMethodID(
        audioManagerClass, "adjustStreamVolume", "(III)V");
    jfieldID streamMusicField = env->GetStaticFieldID(audioManagerClass, "STREAM_MUSIC", "I");
    jfieldID adjustRaiseField = env->GetStaticFieldID(audioManagerClass, "ADJUST_RAISE", "I");
    jfieldID adjustLowerField = env->GetStaticFieldID(audioManagerClass, "ADJUST_LOWER", "I");
    if (!adjustStreamVolume || !streamMusicField || !adjustRaiseField || !adjustLowerField) {
        env->DeleteLocalRef(audioManagerClass);
        env->DeleteLocalRef(audioManagerObject);
        env->DeleteLocalRef(audioServiceName);
        env->DeleteLocalRef(contextClass);
        env->DeleteLocalRef(activityClass);
        return false;
    }

    const jint streamMusic = env->GetStaticIntField(audioManagerClass, streamMusicField);
    const jint direction = increase
        ? env->GetStaticIntField(audioManagerClass, adjustRaiseField)
        : env->GetStaticIntField(audioManagerClass, adjustLowerField);
    env->CallVoidMethod(audioManagerObject, adjustStreamVolume, streamMusic, direction, 0);
    const bool success = !env->ExceptionCheck();
    if (!success) {
        env->ExceptionDescribe();
        env->ExceptionClear();
    }

    env->DeleteLocalRef(audioManagerClass);
    env->DeleteLocalRef(audioManagerObject);
    env->DeleteLocalRef(audioServiceName);
    env->DeleteLocalRef(contextClass);
    env->DeleteLocalRef(activityClass);
    return success;
}
#else
bool AdjustMediaVolume(ANativeActivity*, bool) {
    return false;
}
#endif

}  // namespace

void android_main(android_app* app) {
    LOGI("HyperDesk-MR starting");
    static constexpr uint32_t kQrAnchorFrames = 30;
    static constexpr uint32_t kLayoutRefreshAnchorFrames = 8;
    static constexpr uint32_t kQrRestartCooldownFrames = 120;  // ~1.6s @72Hz

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
    app->onInputEvent = handle_input;

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
    state.displayControl->SetMonitorConfigAppliedCallback(
        [&state](uint32_t monitorCount) {
            if (state.inputForwarder) {
                const uint32_t desktopMonitors =
                    std::max<uint32_t>(1u, std::min<uint32_t>(monitorCount, MonitorLayout::kMaxMonitors));
                state.inputForwarder->SetDesktopSize(desktopMonitors * 1920u, 1080u);
                LOGI("Updated RDP input desktop to %u x 1080 (%u monitor layout)",
                     desktopMonitors * 1920u, monitorCount);
            }

            std::lock_guard<std::mutex> lock(state.pendingLayoutAnchorMutex);
            if (!state.lastLayoutAnchorPoseValid) {
                return;
            }

            state.pendingLayoutAnchorPose = state.lastLayoutAnchorPose;
            state.pendingLayoutAnchorPoseValid = true;
            state.pendingLayoutAnchorFrames =
                std::max(state.pendingLayoutAnchorFrames, kLayoutRefreshAnchorFrames);
            LOGI("Queued wall re-anchor after display layout update (%u monitor(s))",
                 monitorCount);
        });
    state.rdpManager     = std::make_unique<RdpConnectionManager>(
        *state.displayControl,
        state.monitorPtrs.data(),
        static_cast<uint32_t>(state.monitorPtrs.size()));
    state.inputForwarder = std::make_unique<RdpInputForwarder>();
    state.rdpManager->SetInputForwarder(state.inputForwarder.get());
    state.evdevMouse = std::make_unique<EvdevMouseReader>(*state.inputForwarder);
    if (state.evdevMouse->Start()) {
        LOGI("EvdevMouseReader started — Bluetooth mouse active");
    } else {
        LOGW("EvdevMouseReader: no mouse found (will retry on RDP connect)");
    }

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

    state.cursorOverlay = std::make_unique<CursorOverlay>(
        *state.xrContext, app->activity->assetManager);
    state.compositor->SetCursorOverlay(state.cursorOverlay.get(), state.inputForwarder.get());

    // Dear ImGui toolbar (action buttons under the central monitor).
    state.imguiToolbar = std::make_unique<ImGuiToolbar>(
        *state.xrContext, app->activity->assetManager);
    state.compositor->SetImGuiToolbar(state.imguiToolbar.get());

    // RDP error callback — show error text in the status overlay.
    state.rdpManager->SetErrorCallback([&state](uint32_t errorCode) {
        const char* msg = ErrorUtils::RdpErrorToString(errorCode);
        const char* hint = ErrorUtils::RdpErrorHint(errorCode);
        char buf[128];
        state.suppressAutoReconnect = false;

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
            const uint8_t scanCameraPosition = state.qrScanner->GetActiveCameraPosition();
            XrPosef scanAnchorPose{};
            bool haveScanAnchorPose = false;
            const char* scanAnchorSource = "xr-head";
            {
                std::lock_guard<std::mutex> lock(state.latestHeadPoseMutex);
                if (scanCameraPosition <= 1 && state.latestEyePosesValid[scanCameraPosition]) {
                    scanAnchorPose = state.latestEyePoses[scanCameraPosition];
                    haveScanAnchorPose = true;
                    scanAnchorSource = (scanCameraPosition == 0) ? "qr-eye-left" : "qr-eye-right";
                } else if (state.latestHeadPoseValid) {
                    scanAnchorPose = state.latestHeadPose;
                    haveScanAnchorPose = true;
                }
            }
            {
                std::lock_guard<std::mutex> lock(state.pendingLayoutAnchorMutex);
                state.lastLayoutAnchorPose = scanAnchorPose;
                state.lastLayoutAnchorPoseValid = haveScanAnchorPose;
                state.pendingLayoutAnchorFrames = kQrAnchorFrames;
                state.pendingLayoutAnchorPose = scanAnchorPose;
                state.pendingLayoutAnchorPoseValid = haveScanAnchorPose;
            }
            if (haveScanAnchorPose) {
                state.monitorLayout->AnchorPrimaryToHeadPose(scanAnchorPose);
                LOGI("Queued QR wall anchor from %s (camera position=%u)",
                     scanAnchorSource,
                     static_cast<unsigned>(scanCameraPosition));
                state.statusOverlay->AddLog("[OK] Screen wall anchored to scan");
            } else {
                LOGW("No latest XR pose available at QR scan; waiting for render pose");
                state.statusOverlay->AddLog("[WARN] Waiting for XR pose to anchor wall");
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

            const bool addSecondary =
                state.qrAddConnectionMode && state.rdpManager->IsConnected();
            const uint32_t targetMonitor = state.qrTargetMonitor;
            state.qrAddConnectionMode = false;
            state.qrTargetMonitor = 0;

            if (addSecondary) {
                if (targetMonitor == 0 || targetMonitor >= MonitorLayout::kMaxMonitors) {
                    state.statusOverlay->AddLog("[ERR] Invalid target monitor for QR connection");
                    return;
                }

                state.statusOverlay->AddLog("Connecting secondary RDP session...");
                if (state.secondaryRdpManagers[targetMonitor]) {
                    state.secondaryRdpManagers[targetMonitor]->Disconnect();
                    state.secondaryRdpManagers[targetMonitor].reset();
                }

                VirtualMonitor* oneMonitor[] = { state.monitorPtrs[targetMonitor] };
                auto secondary = std::make_unique<RdpConnectionManager>(
                    *state.displayControl,
                    oneMonitor,
                    1u,
                    /*manageDisplayLayout=*/false,
                    /*attachInputForwarder=*/false);
                secondary->SetErrorCallback([&state, targetMonitor](uint32_t errorCode) {
                    char errBuf[160];
                    snprintf(errBuf, sizeof(errBuf),
                             "[ERR] RDP monitor %u error 0x%08X",
                             static_cast<unsigned>(targetMonitor), errorCode);
                    state.statusOverlay->AddLog(errBuf);
                });

                if (!secondary->Connect(params)) {
                    state.statusOverlay->AddLog("[ERR] Failed to start secondary RDP connection");
                    return;
                }

                state.secondaryRdpManagers[targetMonitor] = std::move(secondary);
                state.monitorLayout->SetMonitorActive(targetMonitor, true);
                snprintf(buf, sizeof(buf), "[OK] Secondary connection on monitor %u",
                         static_cast<unsigned>(targetMonitor));
                state.statusOverlay->AddLog(buf);
            } else {
                state.statusOverlay->AddLog("Connecting...");
                if (state.rdpManager->IsConnected()) {
                    state.statusOverlay->AddLog("[WARN] Replacing current RDP session");
                    state.suppressAutoReconnect = true;
                    state.rdpManager->Disconnect();
                }
                state.lastConnParams = params;
                state.hasConnParams = true;
                state.wasEverConnected = false;
                if (!state.rdpManager->Connect(params)) {
                    state.statusOverlay->AddLog("[ERR] Failed to start RDP connection");
                    state.suppressAutoReconnect = false;
                    return;
                }
                state.suppressAutoReconnect = false;
            }

            // Retry evdev mouse if it wasn't found at startup.
            if (state.evdevMouse && !state.evdevMouse->IsRunning()) {
                state.evdevMouse->Start();
            }
        });
    if (state.qrScanner->Start()) {
        LOGI("QR scanner active — point headset at a QR code to connect");
        state.statusOverlay->AddLog("[OK] Camera started");
        state.statusOverlay->AddLog("Point headset at QR code...");
    } else {
        LOGE("QR scanner failed to start — check CAMERA permission");
        state.statusOverlay->AddLog("[ERR] Camera failed to start");
        state.statusOverlay->AddLog("[WARN] Waiting for camera permission...");
        state.qrRetryFrame = kQrRestartCooldownFrames;
    }

    auto getFrontMonitorIndex = [&state]() -> uint32_t {
        XrPosef headPose{};
        {
            std::lock_guard<std::mutex> lock(state.latestHeadPoseMutex);
            if (!state.latestHeadPoseValid) {
                return 0;
            }
            headPose = state.latestHeadPose;
        }

        XrVector3f headForward = RotateByQuaternion(headPose.orientation, {0.0f, 0.0f, -1.0f});
        headForward = NormalizeOrFallback(headForward, {0.0f, 0.0f, -1.0f});

        uint32_t bestIndex = 0;
        float bestDot = -2.0f;
        for (uint32_t i = 0; i < MonitorLayout::kMaxMonitors; ++i) {
            const MonitorDescriptor& mon = state.monitorLayout->GetMonitor(i);
            if (!mon.active) {
                continue;
            }

            const XrVector3f toMonitor{
                -mon.forwardNormal.x,
                -mon.forwardNormal.y,
                -mon.forwardNormal.z
            };
            const float align = Dot(headForward, NormalizeOrFallback(toMonitor, {0.0f, 0.0f, -1.0f}));
            if (align > bestDot) {
                bestDot = align;
                bestIndex = i;
            }
        }
        return bestIndex;
    };

    auto applyMonitorCount = [&state](uint32_t requestedCount) -> bool {
        const uint32_t capped =
            std::max<uint32_t>(1, std::min<uint32_t>(requestedCount, MonitorLayout::kMaxMonitors));
        const uint32_t currentCount =
            std::max<uint32_t>(1u, state.monitorLayout->GetActiveCount());

        for (uint32_t i = capped; i < MonitorLayout::kMaxMonitors; ++i) {
            if (state.secondaryRdpManagers[i]) {
                state.secondaryRdpManagers[i]->Disconnect();
                state.secondaryRdpManagers[i].reset();
            }
        }

        // Grow by reconnecting with a larger initial monitor declaration.
        if (capped > currentCount
            && state.rdpManager->IsConnected()
            && state.rdpManager->HasLastConnectParams()) {
            const uint32_t previousInitial = state.rdpManager->GetInitialMonitorCount();

            state.displayControl->SetRequestedMonitorCount(capped);
            state.rdpManager->SetInitialMonitorCount(capped);
            state.statusOverlay->AddLog("[OK] Reconnecting RDP to apply monitor growth...");

            state.suppressAutoReconnect = true;
            state.rdpManager->Disconnect();
            if (!state.rdpManager->ConnectLast()) {
                state.rdpManager->SetInitialMonitorCount(previousInitial);
                state.displayControl->SetRequestedMonitorCount(previousInitial);
                state.suppressAutoReconnect = false;
                return false;
            }
            return true;
        }

        if (!state.displayControl->RequestMonitorCount(capped)) {
            return false;
        }
        state.rdpManager->SetInitialMonitorCount(capped);

        const uint32_t applied = state.monitorLayout->GetActiveCount();
        if (applied != capped) {
            LOGW("Monitor layout request mismatch: requested=%u applied=%u",
                 capped, applied);
            return false;
        }

        return true;
    };

    auto chooseSecondaryTargetMonitor = [&state]() -> uint32_t {
        auto isAvailable = [&state](uint32_t idx) -> bool {
            if (state.monitorLayout->IsMonitorActive(idx)) {
                return false;
            }
            if (state.secondaryRdpManagers[idx] && state.secondaryRdpManagers[idx]->IsConnected()) {
                return false;
            }
            return true;
        };

        // Keep growth to the right first, then use the left slot (monitor 1).
        for (uint32_t idx = 2u; idx < MonitorLayout::kMaxMonitors; ++idx) {
            if (isAvailable(idx)) {
                return idx;
            }
        }
        if (1u < MonitorLayout::kMaxMonitors && isAvailable(1u)) {
            return 1u;
        }
        return 0u;
    };

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
            XrPosef currentHeadPose{};
            const bool currentHeadPoseValid =
                state.xrContext->LocateHeadPose(frameState.predictedDisplayTime, currentHeadPose);
            std::array<XrView, 2> currentViews{XrView{XR_TYPE_VIEW}, XrView{XR_TYPE_VIEW}};
            const bool currentViewsValid =
                state.xrContext->LocateViews(frameState.predictedDisplayTime, currentViews);
            if (currentHeadPoseValid || currentViewsValid) {
                std::lock_guard<std::mutex> lock(state.latestHeadPoseMutex);
                if (currentHeadPoseValid) {
                    state.latestHeadPose = currentHeadPose;
                    state.latestHeadPoseValid = true;
                }
                if (currentViewsValid) {
                    state.latestEyePoses[0] = currentViews[0].pose;
                    state.latestEyePoses[1] = currentViews[1].pose;
                    state.latestEyePosesValid[0] = true;
                    state.latestEyePosesValid[1] = true;
                }
            }

            uint32_t anchorFramesRemaining = 0;
            XrPosef queuedAnchorPose{};
            bool queuedAnchorPoseValid = false;
            {
                std::lock_guard<std::mutex> lock(state.pendingLayoutAnchorMutex);
                anchorFramesRemaining = state.pendingLayoutAnchorFrames;
                queuedAnchorPose = state.pendingLayoutAnchorPose;
                queuedAnchorPoseValid = state.pendingLayoutAnchorPoseValid;
            }

            if (anchorFramesRemaining > 0) {
                XrPosef anchorPose = currentHeadPose;
                bool anchorPoseValid = currentHeadPoseValid;
                if (anchorFramesRemaining == kQrAnchorFrames && queuedAnchorPoseValid) {
                    anchorPose = queuedAnchorPose;
                    anchorPoseValid = true;
                }

                if (anchorPoseValid) {
                    if (anchorFramesRemaining == kQrAnchorFrames) {
                        LOGI("Applying QR wall anchor over %u XR frames", kQrAnchorFrames);
                    } else if (anchorFramesRemaining == kLayoutRefreshAnchorFrames) {
                        LOGI("Reapplying QR wall anchor after display reconfiguration");
                    }
                    state.monitorLayout->AnchorPrimaryToHeadPose(anchorPose);
                    {
                        std::lock_guard<std::mutex> lock(state.pendingLayoutAnchorMutex);
                        state.lastLayoutAnchorPose = anchorPose;
                        state.lastLayoutAnchorPoseValid = true;
                        state.pendingLayoutAnchorPoseValid = false;
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

            // Update carousel scroll: smoothly slide the monitor wall when
            // the cursor approaches the ±60° edge of the visible arc.
            if (state.inputForwarder) {
                int32_t cx, cy;
                state.inputForwarder->GetCursorPosition(cx, cy);
                const uint32_t cursorMonIdx = static_cast<uint32_t>(std::min<int32_t>(
                    std::max<int32_t>(cx / 1920, 0),
                    static_cast<int32_t>(MonitorLayout::kMaxMonitors - 1)));
                state.monitorLayout->UpdateCarousel(cursorMonIdx);
            }

            // Head-tracking scroll: turn head past 75° to scroll the wall.
            if (currentHeadPoseValid) {
                state.monitorLayout->UpdateHeadScroll(currentHeadPose);
            }

            // Keep the toolbar hit-band aligned with the scrolled wall.
            // Toolbar stays visually fixed at yaw 0 (anchor forward). When the
            // wall scrolls by +scrollYaw, monitor 0's visible center rotates
            // to world yaw -scrollYaw, so the desktop-X that sits in front of
            // the user (world yaw 0) is 960 + (scrollYaw/step)*1920.
            // Cursor formula: world yaw = i*step - scrollYaw + (u-0.5)*step;
            // solving yaw=0 on mon 0 yields u = 0.5 + scrollYaw/step,
            // so desktopX shifts by +scrollYaw/step*1920.
            if (state.inputForwarder) {
                const float scrollYaw = state.monitorLayout->GetScrollYaw();
                const int32_t toolbarOffsetX = static_cast<int32_t>(
                    scrollYaw / MonitorLayout::kAngularStepRadians * 1920.0f);
                state.inputForwarder->SetToolbarOffsetX(toolbarOffsetX);
            }

            state.xrContext->SyncActions();
            state.compositor->RenderFrame(frameState);
        } else {
            // Submit empty frame — required by spec after xrBeginFrame.
            state.xrContext->EndFrame(frameState, 0, nullptr);
        }

        // ── Track connection state for auto-reconnect ──────────────────────
        if (state.imguiToolbar && state.imguiToolbar->IsReady()) {
            if (state.imguiToolbar->PollDragDoubleClicked()) {
                XrPosef headPose{};
                bool haveHeadPose = false;
                {
                    std::lock_guard<std::mutex> lock(state.latestHeadPoseMutex);
                    if (state.latestHeadPoseValid) {
                        headPose = state.latestHeadPose;
                        haveHeadPose = true;
                    }
                }

                if (haveHeadPose) {
                    state.monitorLayout->ResetScroll();
                    state.monitorLayout->AnchorPrimaryToHeadPose(headPose);
                    state.statusOverlay->AddLog("[OK] View reset from headset orientation");
                    state.xrContext->TriggerHapticPulse(0.5f, 100000000);
                } else {
                    state.statusOverlay->AddLog("[WARN] Head pose unavailable for reset");
                }
            }

            const int clicked = state.imguiToolbar->PollClickedButton();
            if (clicked >= 0) {
                const uint32_t frontMonitor = getFrontMonitorIndex();
                char buf[160];

                switch (clicked) {
                    case ImGuiToolbar::BtnAdd: {
                        const uint32_t currentCount = state.monitorLayout->GetActiveCount();
                        if (currentCount >= MonitorLayout::kMaxMonitors) {
                            state.statusOverlay->AddLog("[WARN] No more screens available");
                        } else {
                            if (applyMonitorCount(currentCount + 1)) {
                                snprintf(buf, sizeof(buf), "[OK] Added screen (front monitor=%u)",
                                         static_cast<unsigned>(frontMonitor));
                                state.statusOverlay->AddLog(buf);
                                state.xrContext->TriggerHapticPulse(0.5f, 100000000);
                            } else {
                                state.statusOverlay->AddLog("[ERR] Failed to apply monitor layout");
                            }
                        }
                        break;
                    }
                    case ImGuiToolbar::BtnRemove: {
                        const uint32_t currentCount = state.monitorLayout->GetActiveCount();
                        if (currentCount <= 1) {
                            state.statusOverlay->AddLog("[WARN] At least one screen must stay active");
                        } else {
                            if (applyMonitorCount(currentCount - 1)) {
                                snprintf(buf, sizeof(buf), "[OK] Removed screen (front monitor=%u)",
                                         static_cast<unsigned>(frontMonitor));
                                state.statusOverlay->AddLog(buf);
                                state.xrContext->TriggerHapticPulse(0.5f, 100000000);
                            } else {
                                state.statusOverlay->AddLog("[ERR] Failed to apply monitor layout");
                            }
                        }
                        break;
                    }
                    case ImGuiToolbar::BtnDrag: {
                        state.statusOverlay->AddLog("[OK] Hold drag and move mouse to rotate/reposition");
                        state.statusOverlay->AddLog("[OK] Double-click drag to reset from headset orientation");
                        break;
                    }
                    case ImGuiToolbar::BtnQrCode: {
                        if (state.qrScanner->IsRunning()) {
                            state.statusOverlay->AddLog("[WARN] QR scanner already active");
                        } else if (state.rdpManager->IsConnected()) {
                            const uint32_t targetMonitor = chooseSecondaryTargetMonitor();
                            if (targetMonitor == 0u) {
                                state.statusOverlay->AddLog("[WARN] No free screen for a new RDP connection");
                                break;
                            }
                            state.qrAddConnectionMode = true;
                            state.qrTargetMonitor = targetMonitor;
                            if (state.qrScanner->Start()) {
                                char msg[128];
                                snprintf(msg, sizeof(msg),
                                         "[OK] QR scanner started (new connection on monitor %u)",
                                         static_cast<unsigned>(targetMonitor));
                                state.statusOverlay->AddLog(msg);
                                state.statusOverlay->AddLog("Scan QR to add an RDP connection");
                                state.qrRetryFrame = frameCount + kQrRestartCooldownFrames;
                                state.xrContext->TriggerHapticPulse(0.5f, 100000000);
                            } else {
                                state.qrAddConnectionMode = false;
                                state.qrTargetMonitor = 0;
                                state.statusOverlay->AddLog("[ERR] Unable to start QR scanner");
                                state.qrRetryFrame = frameCount + kQrRestartCooldownFrames;
                            }
                        } else if (state.qrScanner->Start()) {
                            state.qrAddConnectionMode = false;
                            state.qrTargetMonitor = 0;
                            state.statusOverlay->AddLog("[OK] QR scanner started");
                            state.statusOverlay->AddLog("Scan QR to connect RDP");
                            state.qrRetryFrame = frameCount + kQrRestartCooldownFrames;
                            state.xrContext->TriggerHapticPulse(0.5f, 100000000);
                        } else {
                            state.qrAddConnectionMode = false;
                            state.qrTargetMonitor = 0;
                            state.statusOverlay->AddLog("[ERR] Unable to start QR scanner");
                            state.qrRetryFrame = frameCount + kQrRestartCooldownFrames;
                        }
                        break;
                    }
                    case ImGuiToolbar::BtnSplitScreen: {
                        state.splitRowsEnabled = !state.splitRowsEnabled;
                        state.monitorLayout->SetSplitRows(state.splitRowsEnabled);
                        state.statusOverlay->AddLog(
                            state.splitRowsEnabled
                                ? "[OK] Split mode: 2 rows"
                                : "[OK] Split mode: 1 row");
                        state.xrContext->TriggerHapticPulse(0.5f, 100000000);
                        break;
                    }
                    case ImGuiToolbar::BtnVolumeDown: {
                        if (AdjustMediaVolume(app->activity, true)) {
                            snprintf(buf, sizeof(buf), "[OK] Volume up (front monitor=%u)",
                                     static_cast<unsigned>(frontMonitor));
                            state.statusOverlay->AddLog(buf);
                        } else {
                            state.statusOverlay->AddLog("[ERR] Failed to change volume");
                        }
                        break;
                    }
                    case ImGuiToolbar::BtnVolumeUp: {
                        if (AdjustMediaVolume(app->activity, false)) {
                            snprintf(buf, sizeof(buf), "[OK] Volume down (front monitor=%u)",
                                     static_cast<unsigned>(frontMonitor));
                            state.statusOverlay->AddLog(buf);
                        } else {
                            state.statusOverlay->AddLog("[ERR] Failed to change volume");
                        }
                        break;
                    }
                    default:
                        break;
                }
            }

            if (state.inputForwarder && state.imguiToolbar->IsDragHeld()) {
                if (!state.dragWasHeld) {
                    state.dragWasHeld = true;
                    state.inputForwarder->ResetMotionAccumulators();
                    state.statusOverlay->AddLog("[OK] Drag active");
                }

                int32_t dx = 0;
                int32_t dy = 0;
                state.inputForwarder->ConsumeCursorDelta(dx, dy);
                const int32_t wheelSteps = state.inputForwarder->ConsumeWheelSteps();

                constexpr float kDragYawPerPixel = 0.0015f;
                constexpr float kDragVerticalMetersPerPixel = 0.0015f;
                constexpr float kZoomMetersPerWheelStep = 0.10f;
                if (dx != 0 || dy != 0 || wheelSteps != 0) {
                    if (dx != 0) {
                        const float yawDelta = static_cast<float>(-dx) * kDragYawPerPixel;
                        XrPosef headPose{};
                        bool haveHeadPose = false;
                        {
                            std::lock_guard<std::mutex> lock(state.latestHeadPoseMutex);
                            if (state.latestHeadPoseValid) {
                                headPose = state.latestHeadPose;
                                haveHeadPose = true;
                            }
                        }
                        if (haveHeadPose) {
                            state.monitorLayout->RotateAnchorYawAroundPivot(
                                yawDelta, headPose.position);
                        } else {
                            state.monitorLayout->RotateAnchorYaw(yawDelta);
                        }
                    }
                    state.monitorLayout->NudgeAnchor(
                        0.0f,
                        static_cast<float>(-dy) * kDragVerticalMetersPerPixel,
                        static_cast<float>(wheelSteps) * kZoomMetersPerWheelStep);
                }
            } else if (state.dragWasHeld) {
                state.dragWasHeld = false;
                state.statusOverlay->AddLog("[OK] Drag released");
            }
        }

        if (state.rdpManager->IsConnected()) {
            state.wasEverConnected = true;
            if (state.suppressAutoReconnect) {
                state.suppressAutoReconnect = false;
            }
        }
        if (state.qrScanner && !state.hasConnParams && !state.qrScanner->IsRunning()
            && frameCount >= state.qrRetryFrame) {
            if (state.qrScanner->Start()) {
                LOGI("QR scanner started after retry");
                state.statusOverlay->AddLog("[OK] Camera started");
                state.statusOverlay->AddLog("Point headset at QR code...");
            } else {
                LOGW("QR scanner retry failed");
                state.statusOverlay->AddLog("[WARN] Camera unavailable; retrying...");
            }
            state.qrRetryFrame = frameCount + kQrRestartCooldownFrames;
        }

        // ── Auto-reconnect when RDP session drops ─────────────────────────
        // Only reconnect after a previously successful connection drops —
        // never during the initial connect (wasEverConnected guards this).
        if (state.hasConnParams && state.wasEverConnected
            && !state.suppressAutoReconnect
            && !state.rdpManager->IsConnected()
            && frameCount > state.reconnectCooldownFrame) {
            LOGI("RDP disconnected — attempting reconnection");
            state.statusOverlay->AddLog("[WARN] RDP disconnected — reconnecting...");
            state.rdpManager->Disconnect();  // clean up previous session
            state.rdpManager->Connect(state.lastConnParams);
            // Cooldown: wait ~5s (360 frames at 72 Hz) before next retry.
            state.reconnectCooldownFrame = frameCount + 360;
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
    if (state.evdevMouse) state.evdevMouse->Stop();
    if (state.qrScanner) state.qrScanner->Stop();
    for (auto& mgr : state.secondaryRdpManagers) {
        if (mgr) {
            mgr->Disconnect();
            mgr.reset();
        }
    }
    state.rdpManager->Disconnect();
    state.passthrough->Pause();

    LOGI("HyperDesk-MR shutting down");
}
