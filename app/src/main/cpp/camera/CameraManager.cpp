#include "CameraManager.h"
#include "../util/Logger.h"

#include <media/NdkImage.h>

static constexpr int32_t kImageWidth  = 640;
static constexpr int32_t kImageHeight = 480;
static constexpr int32_t kMaxImages   = 2;

CameraManager::CameraManager(ANativeActivity* /*activity*/) {}

CameraManager::~CameraManager() {
    Stop();
}

void CameraManager::SetFrameCallback(FrameCallback cb) {
    std::lock_guard lock(callbackMutex_);
    frameCallback_ = std::move(cb);
}

bool CameraManager::Start() {
    if (running_.load()) return true;

    cameraMgr_ = ACameraManager_create();
    if (!cameraMgr_) {
        LOGE("CameraManager: failed to create ACameraManager");
        return false;
    }

    std::string cameraId = FindCameraId();
    if (cameraId.empty()) {
        LOGE("CameraManager: no camera found");
        return false;
    }

    if (!CreateImageReader()) return false;
    if (!OpenCamera(cameraId)) return false;
    if (!CreateCaptureSession()) return false;

    running_.store(true);
    LOGI("CameraManager: started");
    return true;
}

void CameraManager::Stop() {
    if (!running_.load()) return;
    running_.store(false);

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
    LOGI("CameraManager: stopped");
}

std::string CameraManager::FindCameraId() {
    ACameraIdList* cameraIds = nullptr;
    if (ACameraManager_getCameraIdList(cameraMgr_, &cameraIds) != ACAMERA_OK || !cameraIds) {
        LOGE("CameraManager: failed to enumerate cameras");
        return {};
    }

    std::string result;
    for (int i = 0; i < cameraIds->numCameras; ++i) {
        ACameraMetadata* metadata = nullptr;
        if (ACameraManager_getCameraCharacteristics(cameraMgr_, cameraIds->cameraIds[i],
                                                     &metadata) != ACAMERA_OK) {
            continue;
        }

        ACameraMetadata_const_entry lensFacing{};
        if (ACameraMetadata_getConstEntry(metadata, ACAMERA_LENS_FACING, &lensFacing) == ACAMERA_OK
            && lensFacing.count > 0) {
            // Prefer back-facing camera for QR scanning
            if (lensFacing.data.u8[0] == ACAMERA_LENS_FACING_BACK) {
                result = cameraIds->cameraIds[i];
                ACameraMetadata_free(metadata);
                break;
            }
            // Fall back to any camera
            if (result.empty()) {
                result = cameraIds->cameraIds[i];
            }
        }
        ACameraMetadata_free(metadata);
    }

    ACameraManager_deleteCameraIdList(cameraIds);
    LOGI("CameraManager: using camera '%s'", result.c_str());
    return result;
}

bool CameraManager::CreateImageReader() {
    media_status_t status = AImageReader_new(kImageWidth, kImageHeight,
                                              AIMAGE_FORMAT_YUV_420_888,
                                              kMaxImages, &imageReader_);
    if (status != AMEDIA_OK || !imageReader_) {
        LOGE("CameraManager: AImageReader_new failed: %d", status);
        return false;
    }

    AImageReader_ImageListener listener{this, OnImageAvailable};
    AImageReader_setImageListener(imageReader_, &listener);
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
        LOGE("CameraManager: openCamera failed: %d", status);
        return false;
    }
    return true;
}

bool CameraManager::CreateCaptureSession() {
    ANativeWindow* window = nullptr;
    AImageReader_getWindow(imageReader_, &window);
    if (!window) {
        LOGE("CameraManager: failed to get ANativeWindow from AImageReader");
        return false;
    }

    ACameraOutputTarget_create(window, &outputTarget_);
    ACaptureSessionOutputContainer_create(&outputContainer_);
    ACaptureSessionOutput_create(window, &sessionOutput_);
    ACaptureSessionOutputContainer_add(outputContainer_, sessionOutput_);

    ACameraCaptureSession_stateCallbacks sessionCb{};
    sessionCb.context   = this;
    sessionCb.onClosed  = OnSessionClosed;
    sessionCb.onReady   = OnSessionReady;
    sessionCb.onActive  = OnSessionActive;

    camera_status_t status = ACameraDevice_createCaptureSession(
        cameraDevice_, outputContainer_, &sessionCb, &captureSession_);
    if (status != ACAMERA_OK) {
        LOGE("CameraManager: createCaptureSession failed: %d", status);
        return false;
    }

    ACameraDevice_createCaptureRequest(cameraDevice_, TEMPLATE_PREVIEW, &captureRequest_);
    ACaptureRequest_addTarget(captureRequest_, outputTarget_);
    ACameraCaptureSession_setRepeatingRequest(captureSession_, nullptr, 1,
                                               &captureRequest_, nullptr);
    return true;
}

// ── NDK Callbacks ────────────────────────────────────────────────────────────

void CameraManager::OnDeviceDisconnected(void* context, ACameraDevice*) {
    LOGW("CameraManager: device disconnected");
    auto* self = static_cast<CameraManager*>(context);
    self->running_.store(false);
}

void CameraManager::OnDeviceError(void* context, ACameraDevice*, int error) {
    LOGE("CameraManager: device error %d", error);
    auto* self = static_cast<CameraManager*>(context);
    self->running_.store(false);
}

void CameraManager::OnSessionClosed(void*, ACameraCaptureSession*) {
    LOGI("CameraManager: session closed");
}

void CameraManager::OnSessionReady(void*, ACameraCaptureSession*) {
    LOGI("CameraManager: session ready");
}

void CameraManager::OnSessionActive(void*, ACameraCaptureSession*) {
    LOGI("CameraManager: session active");
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
        int32_t width = 0, height = 0, rowStride = 0;
        AImage_getWidth(image, &width);
        AImage_getHeight(image, &height);
        AImage_getPlaneRowStride(image, 0, &rowStride);

        std::lock_guard lock(self->callbackMutex_);
        if (self->frameCallback_) {
            self->frameCallback_(yData, width, height, rowStride);
        }
    }

    AImage_delete(image);
}
