#include "CameraManager.h"
#include "../xr/StatusOverlay.h"
#include "../util/Logger.h"

#include <camera/NdkCameraError.h>
#include <camera/NdkCameraMetadata.h>
#include <media/NdkImage.h>

#include <chrono>
#include <cstdio>

static constexpr int32_t kImageWidth  = 640;
static constexpr int32_t kImageHeight = 480;
static constexpr int32_t kMaxImages   = 2;
static constexpr int32_t kPermissionRequestCode = 4242;
static constexpr auto kPermissionRequestCooldown = std::chrono::seconds(5);

// Meta passthrough camera vendor tags.
static constexpr uint32_t kMetaCameraSourceTag   = 0x80004d00;
static constexpr uint32_t kMetaCameraPositionTag = 0x80004d01;

namespace {

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

const char* CameraStatusToString(camera_status_t status) {
    switch (status) {
        case ACAMERA_OK: return "OK";
        case ACAMERA_ERROR_INVALID_PARAMETER: return "INVALID_PARAMETER";
        case ACAMERA_ERROR_CAMERA_DISCONNECTED: return "CAMERA_DISCONNECTED";
        case ACAMERA_ERROR_NOT_ENOUGH_MEMORY: return "NOT_ENOUGH_MEMORY";
        case ACAMERA_ERROR_METADATA_NOT_FOUND: return "METADATA_NOT_FOUND";
        case ACAMERA_ERROR_CAMERA_DEVICE: return "CAMERA_DEVICE";
        case ACAMERA_ERROR_CAMERA_SERVICE: return "CAMERA_SERVICE";
        case ACAMERA_ERROR_SESSION_CLOSED: return "SESSION_CLOSED";
        case ACAMERA_ERROR_INVALID_OPERATION: return "INVALID_OPERATION";
        case ACAMERA_ERROR_STREAM_CONFIGURE_FAIL: return "STREAM_CONFIGURE_FAIL";
        case ACAMERA_ERROR_CAMERA_IN_USE: return "CAMERA_IN_USE";
        case ACAMERA_ERROR_MAX_CAMERA_IN_USE: return "MAX_CAMERA_IN_USE";
        case ACAMERA_ERROR_CAMERA_DISABLED: return "CAMERA_DISABLED";
        case ACAMERA_ERROR_PERMISSION_DENIED: return "PERMISSION_DENIED";
        case ACAMERA_ERROR_UNSUPPORTED_OPERATION: return "UNSUPPORTED_OPERATION";
        case ACAMERA_ERROR_UNKNOWN:
        default:
            return "UNKNOWN";
    }
}

}  // namespace

CameraManager::CameraManager(ANativeActivity* activity)
    : activity_(activity) {}

CameraManager::~CameraManager() {
    Stop();
}

void CameraManager::SetFrameCallback(FrameCallback cb) {
    std::lock_guard lock(callbackMutex_);
    frameCallback_ = std::move(cb);
}

bool CameraManager::Start() {
    if (running_.load()) return true;

    // Recover from stale handles after asynchronous camera disconnect/error callbacks.
    ReleaseResources();
    selectedCameraPosition_.store(255);

    if (!EnsureCameraPermissions()) {
        return false;
    }

    cameraMgr_ = ACameraManager_create();
    if (!cameraMgr_) {
        LOGE("CameraManager: failed to create ACameraManager");
        if (StatusOverlay::sInstance) {
            StatusOverlay::sInstance->SetStatusLine(1, "cam: manager create failed");
            StatusOverlay::sInstance->AddLog("[ERR] Camera manager create failed");
        }
        ReleaseResources();
        return false;
    }

    std::string cameraId = FindCameraId();
    if (cameraId.empty()) {
        LOGE("CameraManager: no camera found");
        if (StatusOverlay::sInstance) {
            StatusOverlay::sInstance->SetStatusLine(1, "cam: no camera found");
            StatusOverlay::sInstance->AddLog("[ERR] No camera found");
        }
        ReleaseResources();
        return false;
    }

    if (!CreateImageReader()) {
        ReleaseResources();
        return false;
    }
    if (!OpenCamera(cameraId)) {
        ReleaseResources();
        return false;
    }
    if (!CreateCaptureSession()) {
        ReleaseResources();
        return false;
    }

    running_.store(true);
    LOGI("CameraManager: started");
    if (StatusOverlay::sInstance) {
        StatusOverlay::sInstance->SetStatusLine(2, "cam: started, waiting session");
    }
    return true;
}

void CameraManager::Stop() {
    running_.store(false);
    ReleaseResources();
    LOGI("CameraManager: stopped");
    if (StatusOverlay::sInstance) {
        StatusOverlay::sInstance->SetStatusLine(2, "cam: stopped");
    }
}

void CameraManager::ReleaseResources() {
    if (captureSession_) {
        ACameraCaptureSession_close(captureSession_);
        captureSession_ = nullptr;
    }
    if (captureRequest_) {
        ACaptureRequest_free(captureRequest_);
        captureRequest_ = nullptr;
    }
    if (outputTarget_) {
        ACameraOutputTarget_free(outputTarget_);
        outputTarget_ = nullptr;
    }
    if (cameraDevice_) {
        ACameraDevice_close(cameraDevice_);
        cameraDevice_ = nullptr;
    }
    if (sessionOutput_) {
        ACaptureSessionOutput_free(sessionOutput_);
        sessionOutput_ = nullptr;
    }
    if (outputContainer_) {
        ACaptureSessionOutputContainer_free(outputContainer_);
        outputContainer_ = nullptr;
    }
    if (imageReader_) {
        AImageReader_delete(imageReader_);
        imageReader_ = nullptr;
    }
    if (cameraMgr_) {
        ACameraManager_delete(cameraMgr_);
        cameraMgr_ = nullptr;
    }
}

bool CameraManager::HasPermission(const char* permission) {
    if (!activity_ || !activity_->vm || !activity_->clazz || !permission) {
        return false;
    }

    ScopedJniEnv jni(activity_->vm);
    if (!jni.env) {
        LOGE("CameraManager: failed to access JNI environment");
        return false;
    }

    JNIEnv* env = jni.env;
    jclass activityClass = env->GetObjectClass(activity_->clazz);
    if (!activityClass) {
        return false;
    }

    jmethodID checkSelfPermission = env->GetMethodID(
        activityClass, "checkSelfPermission", "(Ljava/lang/String;)I");
    if (!checkSelfPermission) {
        env->DeleteLocalRef(activityClass);
        return false;
    }

    jstring permissionString = env->NewStringUTF(permission);
    if (!permissionString) {
        env->DeleteLocalRef(activityClass);
        return false;
    }
    const jint result = env->CallIntMethod(
        activity_->clazz, checkSelfPermission, permissionString);

    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        env->DeleteLocalRef(permissionString);
        env->DeleteLocalRef(activityClass);
        return false;
    }

    env->DeleteLocalRef(permissionString);
    env->DeleteLocalRef(activityClass);
    return result == 0;  // PackageManager.PERMISSION_GRANTED
}

void CameraManager::RequestCameraPermissions(bool requestCamera, bool requestHeadsetCamera) {
    if (!requestCamera && !requestHeadsetCamera) {
        return;
    }
    if (!activity_ || !activity_->vm || !activity_->clazz) {
        return;
    }

    ScopedJniEnv jni(activity_->vm);
    if (!jni.env) {
        return;
    }

    JNIEnv* env = jni.env;
    jclass activityClass = env->GetObjectClass(activity_->clazz);
    if (!activityClass) {
        return;
    }

    jmethodID requestPermissions = env->GetMethodID(
        activityClass, "requestPermissions", "([Ljava/lang/String;I)V");
    if (!requestPermissions) {
        env->DeleteLocalRef(activityClass);
        return;
    }

    jclass stringClass = env->FindClass("java/lang/String");
    if (!stringClass) {
        env->DeleteLocalRef(activityClass);
        return;
    }
    const jsize permissionCount =
        static_cast<jsize>((requestCamera ? 1 : 0) + (requestHeadsetCamera ? 1 : 0));
    jobjectArray permissions = env->NewObjectArray(permissionCount, stringClass, nullptr);
    if (!permissions) {
        env->DeleteLocalRef(stringClass);
        env->DeleteLocalRef(activityClass);
        return;
    }

    jsize permissionIndex = 0;
    jstring cameraPermission = nullptr;
    if (requestCamera) {
        cameraPermission = env->NewStringUTF("android.permission.CAMERA");
        if (!cameraPermission) {
            env->DeleteLocalRef(permissions);
            env->DeleteLocalRef(stringClass);
            env->DeleteLocalRef(activityClass);
            return;
        }
        env->SetObjectArrayElement(permissions, permissionIndex++, cameraPermission);
    }

    jstring headsetCameraPermission = nullptr;
    if (requestHeadsetCamera) {
        headsetCameraPermission = env->NewStringUTF("horizonos.permission.HEADSET_CAMERA");
        if (!headsetCameraPermission) {
            if (cameraPermission) env->DeleteLocalRef(cameraPermission);
            env->DeleteLocalRef(permissions);
            env->DeleteLocalRef(stringClass);
            env->DeleteLocalRef(activityClass);
            return;
        }
        env->SetObjectArrayElement(permissions, permissionIndex++, headsetCameraPermission);
    }

    env->CallVoidMethod(activity_->clazz, requestPermissions, permissions, kPermissionRequestCode);
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
    } else {
        LOGI("CameraManager: requested permissions (CAMERA=%d HEADSET_CAMERA=%d)",
             requestCamera ? 1 : 0,
             requestHeadsetCamera ? 1 : 0);
    }

    if (headsetCameraPermission) env->DeleteLocalRef(headsetCameraPermission);
    if (cameraPermission) env->DeleteLocalRef(cameraPermission);
    env->DeleteLocalRef(permissions);
    env->DeleteLocalRef(stringClass);
    env->DeleteLocalRef(activityClass);
}

bool CameraManager::EnsureCameraPermissions() {
    const bool hasCamera = HasPermission("android.permission.CAMERA");
    const bool hasHeadsetCamera = HasPermission("horizonos.permission.HEADSET_CAMERA");
    // Quest runtime behavior differs across releases: some builds allow
    // passthrough via CAMERA alone, others require HEADSET_CAMERA too.
    if (hasCamera || hasHeadsetCamera) {
        return true;
    }

    const auto now = std::chrono::steady_clock::now();
    if (lastPermissionRequest_.time_since_epoch().count() == 0 ||
        now - lastPermissionRequest_ >= kPermissionRequestCooldown) {
        RequestCameraPermissions(true, true);
        lastPermissionRequest_ = now;
    }

    LOGW("CameraManager: no camera permission granted yet (CAMERA=%d HEADSET_CAMERA=%d)",
         hasCamera ? 1 : 0,
         hasHeadsetCamera ? 1 : 0);
    if (StatusOverlay::sInstance) {
        StatusOverlay::sInstance->SetStatusLine(2, "cam: permission required");
        StatusOverlay::sInstance->AddLog("[WARN] Grant camera permission in headset");
    }
    return false;
}

std::string CameraManager::FindCameraId() {
    ACameraIdList* cameraIds = nullptr;
    if (ACameraManager_getCameraIdList(cameraMgr_, &cameraIds) != ACAMERA_OK || !cameraIds) {
        LOGE("CameraManager: failed to enumerate cameras");
        return {};
    }

    LOGI("CameraManager: %d camera(s) found", cameraIds->numCameras);
    std::string leftPassthroughId;
    uint8_t leftPassthroughPosition = 255;
    std::string anyPassthroughId;
    uint8_t anyPassthroughPosition = 255;
    std::string backFacingId;
    uint8_t backFacingPosition = 255;
    std::string anyId;
    uint8_t anyPosition = 255;

    for (int i = 0; i < cameraIds->numCameras; ++i) {
        const char* id = cameraIds->cameraIds[i];
        ACameraMetadata* metadata = nullptr;
        if (ACameraManager_getCameraCharacteristics(cameraMgr_, id, &metadata) != ACAMERA_OK) {
            continue;
        }

        bool isPassthrough = false;
        uint8_t position = 255;

        ACameraMetadata_const_entry sourceEntry{};
        if (ACameraMetadata_getConstEntry(metadata, kMetaCameraSourceTag, &sourceEntry) == ACAMERA_OK
            && sourceEntry.count > 0) {
            isPassthrough = (sourceEntry.data.u8[0] == 0);
        }

        ACameraMetadata_const_entry positionEntry{};
        if (ACameraMetadata_getConstEntry(metadata, kMetaCameraPositionTag, &positionEntry) == ACAMERA_OK
            && positionEntry.count > 0) {
            position = positionEntry.data.u8[0];
        }

        ACameraMetadata_const_entry lensFacing{};
        uint8_t facing = 255;
        if (ACameraMetadata_getConstEntry(metadata, ACAMERA_LENS_FACING, &lensFacing) == ACAMERA_OK
            && lensFacing.count > 0) {
            facing = lensFacing.data.u8[0];
        }

        ACameraMetadata_const_entry orientationEntry{};
        int32_t orientation = -1;
        if (ACameraMetadata_getConstEntry(metadata, ACAMERA_SENSOR_ORIENTATION, &orientationEntry) == ACAMERA_OK
            && orientationEntry.count > 0) {
            orientation = orientationEntry.data.i32[0];
        }

        LOGI("CameraManager: camera '%s' facing=%u orientation=%d passthrough=%d position=%u",
             id, facing, orientation, isPassthrough ? 1 : 0, position);

        if (isPassthrough) {
            if (anyPassthroughId.empty()) {
                anyPassthroughId = id;
                anyPassthroughPosition = position;
            }
            if (position == 0 && leftPassthroughId.empty()) {
                leftPassthroughId = id;
                leftPassthroughPosition = position;
            }
        }

        if (facing == ACAMERA_LENS_FACING_BACK && backFacingId.empty()) {
            backFacingId = id;
            backFacingPosition = position;
        }
        if (anyId.empty()) {
            anyId = id;
            anyPosition = position;
        }

        ACameraMetadata_free(metadata);
    }

    ACameraManager_deleteCameraIdList(cameraIds);
    std::string result;
    uint8_t resultPosition = 255;
    if (!leftPassthroughId.empty()) {
        result = leftPassthroughId;
        resultPosition = leftPassthroughPosition;
    } else if (!anyPassthroughId.empty()) {
        result = anyPassthroughId;
        resultPosition = anyPassthroughPosition;
    } else if (!backFacingId.empty()) {
        result = backFacingId;
        resultPosition = backFacingPosition;
    } else {
        result = anyId;
        resultPosition = anyPosition;
    }
    selectedCameraPosition_.store(resultPosition);
    LOGI("CameraManager: using camera '%s'", result.c_str());

    if (StatusOverlay::sInstance) {
        char buf[128];
        snprintf(buf, sizeof(buf), "cam: id=%s req=%dx%d", result.c_str(), kImageWidth, kImageHeight);
        StatusOverlay::sInstance->SetStatusLine(1, buf);

        snprintf(buf, sizeof(buf), "cam: yuv420 max=%d", kMaxImages);
        StatusOverlay::sInstance->SetStatusLine(2, buf);
    }

    return result;
}

bool CameraManager::CreateImageReader() {
    media_status_t status = AImageReader_new(kImageWidth, kImageHeight,
                                              AIMAGE_FORMAT_YUV_420_888,
                                              kMaxImages, &imageReader_);
    if (status != AMEDIA_OK || !imageReader_) {
        LOGE("CameraManager: AImageReader_new failed: %d", status);
        if (StatusOverlay::sInstance) {
            char buf[96];
            snprintf(buf, sizeof(buf), "cam: reader create failed (%d)", status);
            StatusOverlay::sInstance->SetStatusLine(2, buf);
            StatusOverlay::sInstance->AddLog("[ERR] Camera reader create failed");
        }
        return false;
    }

    AImageReader_ImageListener listener{this, OnImageAvailable};
    status = AImageReader_setImageListener(imageReader_, &listener);
    if (status != AMEDIA_OK) {
        LOGE("CameraManager: AImageReader_setImageListener failed: %d", status);
        return false;
    }
    return true;
}

bool CameraManager::OpenCamera(const std::string& cameraId) {
    ACameraDevice_StateCallbacks deviceCb{};
    deviceCb.context         = this;
    deviceCb.onDisconnected  = OnDeviceDisconnected;
    deviceCb.onError         = OnDeviceError;

    camera_status_t status = ACameraManager_openCamera(cameraMgr_, cameraId.c_str(),
                                                        &deviceCb, &cameraDevice_);
    if (status != ACAMERA_OK || !cameraDevice_) {
        LOGE("CameraManager: openCamera failed: %d (%s)", status, CameraStatusToString(status));
        if (StatusOverlay::sInstance) {
            char buf[128];
            snprintf(buf, sizeof(buf), "cam: open failed %s (%d)", CameraStatusToString(status), status);
            StatusOverlay::sInstance->SetStatusLine(2, buf);
            if (status == ACAMERA_ERROR_PERMISSION_DENIED) {
                StatusOverlay::sInstance->AddLog("[ERR] Camera permission denied");
            } else {
                StatusOverlay::sInstance->AddLog("[ERR] Camera open failed");
            }
        }
        if (status == ACAMERA_ERROR_PERMISSION_DENIED) {
            const bool hasCamera = HasPermission("android.permission.CAMERA");
            const bool hasHeadsetCamera = HasPermission("horizonos.permission.HEADSET_CAMERA");
            const auto now = std::chrono::steady_clock::now();
            if (lastPermissionRequest_.time_since_epoch().count() == 0 ||
                now - lastPermissionRequest_ >= kPermissionRequestCooldown) {
                RequestCameraPermissions(!hasCamera, !hasHeadsetCamera);
                lastPermissionRequest_ = now;
            }
            LOGW("CameraManager: open denied; permission state CAMERA=%d HEADSET_CAMERA=%d",
                 hasCamera ? 1 : 0,
                 hasHeadsetCamera ? 1 : 0);
        }
        return false;
    }

    if (StatusOverlay::sInstance) {
        char buf[96];
        snprintf(buf, sizeof(buf), "cam: open ok id=%s", cameraId.c_str());
        StatusOverlay::sInstance->SetStatusLine(2, buf);
    }
    return true;
}

bool CameraManager::CreateCaptureSession() {
    ANativeWindow* window = nullptr;
    if (AImageReader_getWindow(imageReader_, &window) != AMEDIA_OK || !window) {
        LOGE("CameraManager: failed to get ANativeWindow from AImageReader");
        return false;
    }

    camera_status_t status = ACameraOutputTarget_create(window, &outputTarget_);
    if (status != ACAMERA_OK || !outputTarget_) {
        LOGE("CameraManager: ACameraOutputTarget_create failed: %d (%s)",
             status, CameraStatusToString(status));
        return false;
    }
    status = ACaptureSessionOutputContainer_create(&outputContainer_);
    if (status != ACAMERA_OK || !outputContainer_) {
        LOGE("CameraManager: ACaptureSessionOutputContainer_create failed: %d (%s)",
             status, CameraStatusToString(status));
        return false;
    }
    status = ACaptureSessionOutput_create(window, &sessionOutput_);
    if (status != ACAMERA_OK || !sessionOutput_) {
        LOGE("CameraManager: ACaptureSessionOutput_create failed: %d (%s)",
             status, CameraStatusToString(status));
        return false;
    }
    status = ACaptureSessionOutputContainer_add(outputContainer_, sessionOutput_);
    if (status != ACAMERA_OK) {
        LOGE("CameraManager: ACaptureSessionOutputContainer_add failed: %d (%s)",
             status, CameraStatusToString(status));
        return false;
    }

    ACameraCaptureSession_stateCallbacks sessionCb{};
    sessionCb.context   = this;
    sessionCb.onClosed  = OnSessionClosed;
    sessionCb.onReady   = OnSessionReady;
    sessionCb.onActive  = OnSessionActive;

    status = ACameraDevice_createCaptureSession(
        cameraDevice_, outputContainer_, &sessionCb, &captureSession_);
    if (status != ACAMERA_OK || !captureSession_) {
        LOGE("CameraManager: createCaptureSession failed: %d (%s)",
             status, CameraStatusToString(status));
        if (StatusOverlay::sInstance) {
            char buf[96];
            snprintf(buf, sizeof(buf), "cam: session create failed (%d)", status);
            StatusOverlay::sInstance->SetStatusLine(2, buf);
            StatusOverlay::sInstance->AddLog("[ERR] Camera session create failed");
        }
        return false;
    }

    status = ACameraDevice_createCaptureRequest(cameraDevice_, TEMPLATE_PREVIEW, &captureRequest_);
    if (status != ACAMERA_OK || !captureRequest_) {
        LOGE("CameraManager: createCaptureRequest failed: %d (%s)",
             status, CameraStatusToString(status));
        return false;
    }
    status = ACaptureRequest_addTarget(captureRequest_, outputTarget_);
    if (status != ACAMERA_OK) {
        LOGE("CameraManager: addTarget failed: %d (%s)",
             status, CameraStatusToString(status));
        return false;
    }
    status = ACameraCaptureSession_setRepeatingRequest(captureSession_, nullptr, 1,
                                                       &captureRequest_, nullptr);
    if (status != ACAMERA_OK) {
        LOGE("CameraManager: setRepeatingRequest failed: %d (%s)",
             status, CameraStatusToString(status));
        return false;
    }
    if (StatusOverlay::sInstance) {
        StatusOverlay::sInstance->SetStatusLine(2, "cam: repeating request active");
    }
    return true;
}

// ── NDK Callbacks ────────────────────────────────────────────────────────────

void CameraManager::OnDeviceDisconnected(void* context, ACameraDevice*) {
    LOGW("CameraManager: device disconnected");
    auto* self = static_cast<CameraManager*>(context);
    self->running_.store(false);
    if (StatusOverlay::sInstance) {
        StatusOverlay::sInstance->SetStatusLine(2, "cam: device disconnected");
        StatusOverlay::sInstance->AddLog("[ERR] Camera disconnected");
    }
}

void CameraManager::OnDeviceError(void* context, ACameraDevice*, int error) {
    LOGE("CameraManager: device error %d", error);
    auto* self = static_cast<CameraManager*>(context);
    self->running_.store(false);
    if (StatusOverlay::sInstance) {
        char buf[96];
        snprintf(buf, sizeof(buf), "cam: device error=%d", error);
        StatusOverlay::sInstance->SetStatusLine(2, buf);
        StatusOverlay::sInstance->AddLog("[ERR] Camera device error");
    }
}

void CameraManager::OnSessionClosed(void*, ACameraCaptureSession*) {
    LOGI("CameraManager: session closed");
    if (StatusOverlay::sInstance) {
        StatusOverlay::sInstance->SetStatusLine(2, "cam: session closed");
    }
}

void CameraManager::OnSessionReady(void*, ACameraCaptureSession*) {
    LOGI("CameraManager: session ready");
    if (StatusOverlay::sInstance) {
        StatusOverlay::sInstance->SetStatusLine(2, "cam: session ready");
    }
}

void CameraManager::OnSessionActive(void*, ACameraCaptureSession*) {
    LOGI("CameraManager: session active");
    if (StatusOverlay::sInstance) {
        StatusOverlay::sInstance->SetStatusLine(2, "cam: session active");
        StatusOverlay::sInstance->AddLog("[OK] Camera session active");
    }
}

void CameraManager::OnImageAvailable(void* context, AImageReader* reader) {
    auto* self = static_cast<CameraManager*>(context);
    if (!self->running_.load()) return;

    AImage* image = nullptr;
    if (AImageReader_acquireLatestImage(reader, &image) != AMEDIA_OK || !image) {
        return;
    }

    // Extract Y-plane (luminance) data for QR decoding.
    uint8_t* yData = nullptr;
    int yLen = 0;
    if (AImage_getPlaneData(image, 0, &yData, &yLen) == AMEDIA_OK && yData) {
        int32_t width = 0, height = 0, rowStride = 0, pixelStride = 0;
        AImage_getWidth(image, &width);
        AImage_getHeight(image, &height);
        AImage_getPlaneRowStride(image, 0, &rowStride);
        AImage_getPlanePixelStride(image, 0, &pixelStride);

        static int frameCount = 0;
        if (frameCount++ % 120 == 0) {
            uint64_t lumSum = 0;
            int sampleCount = 0;
            for (int y = 0; y < height; y += 16) {
                const uint8_t* row = yData + y * rowStride;
                for (int x = 0; x < width; x += 16) {
                    lumSum += row[x];
                    ++sampleCount;
                }
            }
            int lumAvg = sampleCount > 0 ? static_cast<int>(lumSum / sampleCount) : 0;
            LOGI("CameraManager: frame #%d %dx%d rowStride=%d pixelStride=%d lumAvg=%d len=%d",
                 frameCount, width, height, rowStride, pixelStride, lumAvg, yLen);
            if (StatusOverlay::sInstance) {
                char buf[128];
                snprintf(buf, sizeof(buf), "frm: #%d %dx%d rs=%d ps=%d lum=%d",
                         frameCount, width, height, rowStride, pixelStride, lumAvg);
                StatusOverlay::sInstance->SetStatusLine(3, buf);
            }
        }

        std::lock_guard lock(self->callbackMutex_);
        if (self->frameCallback_) {
            self->frameCallback_(yData, width, height, rowStride);
        }
    }

    AImage_delete(image);
}
