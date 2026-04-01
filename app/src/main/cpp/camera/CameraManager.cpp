#include "CameraManager.h"
#include "../xr/StatusOverlay.h"
#include "../util/Logger.h"

#include <media/NdkImage.h>

#include <cstdio>

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
        if (StatusOverlay::sInstance) {
            StatusOverlay::sInstance->SetStatusLine(1, "cam: manager create failed");
            StatusOverlay::sInstance->AddLog("[ERR] Camera manager create failed");
        }
        return false;
    }

    std::string cameraId = FindCameraId();
    if (cameraId.empty()) {
        LOGE("CameraManager: no camera found");
        if (StatusOverlay::sInstance) {
            StatusOverlay::sInstance->SetStatusLine(1, "cam: no camera found");
            StatusOverlay::sInstance->AddLog("[ERR] No camera found");
        }
        return false;
    }

    if (!CreateImageReader()) return false;
    if (!OpenCamera(cameraId)) return false;
    if (!CreateCaptureSession()) return false;

    running_.store(true);
    LOGI("CameraManager: started");
    if (StatusOverlay::sInstance) {
        StatusOverlay::sInstance->SetStatusLine(2, "cam: started, waiting session");
    }
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
    if (StatusOverlay::sInstance) {
        StatusOverlay::sInstance->SetStatusLine(2, "cam: stopped");
    }
}

std::string CameraManager::FindCameraId() {
    ACameraIdList* cameraIds = nullptr;
    if (ACameraManager_getCameraIdList(cameraMgr_, &cameraIds) != ACAMERA_OK || !cameraIds) {
        LOGE("CameraManager: failed to enumerate cameras");
        return {};
    }

    LOGI("CameraManager: %d cameras available", cameraIds->numCameras);
    std::string result;

    for (int i = 0; i < cameraIds->numCameras; ++i) {
        const char* id = cameraIds->cameraIds[i];
        ACameraMetadata* metadata = nullptr;
        if (ACameraManager_getCameraCharacteristics(cameraMgr_, id, &metadata) != ACAMERA_OK) {
            continue;
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

        LOGI("CameraManager: camera '%s' facing=%u orientation=%d", id, facing, orientation);

        if (facing == ACAMERA_LENS_FACING_BACK && result.empty()) {
            result = id;
        }
        if (result.empty()) {
            result = id;
        }

        ACameraMetadata_free(metadata);
    }

    ACameraManager_deleteCameraIdList(cameraIds);
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
        if (StatusOverlay::sInstance) {
            char buf[96];
            snprintf(buf, sizeof(buf), "cam: open failed (%d) id=%s", status, cameraId.c_str());
            StatusOverlay::sInstance->SetStatusLine(2, buf);
            StatusOverlay::sInstance->AddLog("[ERR] Camera open failed");
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
        if (StatusOverlay::sInstance) {
            char buf[96];
            snprintf(buf, sizeof(buf), "cam: session create failed (%d)", status);
            StatusOverlay::sInstance->SetStatusLine(2, buf);
            StatusOverlay::sInstance->AddLog("[ERR] Camera session create failed");
        }
        return false;
    }

    ACameraDevice_createCaptureRequest(cameraDevice_, TEMPLATE_PREVIEW, &captureRequest_);
    ACaptureRequest_addTarget(captureRequest_, outputTarget_);
    ACameraCaptureSession_setRepeatingRequest(captureSession_, nullptr, 1,
                                               &captureRequest_, nullptr);
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
