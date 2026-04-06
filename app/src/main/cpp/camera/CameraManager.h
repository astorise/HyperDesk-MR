#pragma once

#include <android/native_activity.h>
#include <camera/NdkCameraDevice.h>
#include <camera/NdkCameraManager.h>
#include <camera/NdkCameraCaptureSession.h>
#include <camera/NdkCaptureRequest.h>
#include <media/NdkImageReader.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>

// CameraManager acquires grayscale camera frames from the Quest 3 sensors
// and delivers them to a callback for QR code processing.
class CameraManager {
public:
    // Callback receives a pointer to luminance (Y-plane) data, width, height,
    // and row stride.
    using FrameCallback = std::function<void(const uint8_t* data, int32_t width,
                                             int32_t height, int32_t rowStride)>;

    explicit CameraManager(ANativeActivity* activity);
    ~CameraManager();

    // Set the callback that receives each camera frame.
    void SetFrameCallback(FrameCallback cb);

    // Start acquiring camera frames.  Returns false if camera open fails.
    bool Start();

    // Stop acquisition and release all camera resources.
    void Stop();

    bool IsRunning() const { return running_.load(); }
    uint8_t GetSelectedCameraPosition() const { return selectedCameraPosition_.load(); }

private:
    ACameraManager*    cameraMgr_ = nullptr;
    ACameraDevice*     cameraDevice_ = nullptr;
    AImageReader*      imageReader_ = nullptr;
    ACaptureSessionOutput*         sessionOutput_ = nullptr;
    ACaptureSessionOutputContainer* outputContainer_ = nullptr;
    ACameraCaptureSession*         captureSession_ = nullptr;
    ACaptureRequest*               captureRequest_ = nullptr;
    ACameraOutputTarget*           outputTarget_ = nullptr;

    std::atomic<bool>  running_{false};
    std::atomic<uint8_t> selectedCameraPosition_{255};
    FrameCallback      frameCallback_;
    std::mutex         callbackMutex_;

    std::string FindCameraId();
    bool CreateImageReader();
    bool OpenCamera(const std::string& cameraId);
    bool CreateCaptureSession();

    // NDK camera callbacks
    static void OnDeviceDisconnected(void* context, ACameraDevice* device);
    static void OnDeviceError(void* context, ACameraDevice* device, int error);
    static void OnSessionClosed(void* context, ACameraCaptureSession* session);
    static void OnSessionReady(void* context, ACameraCaptureSession* session);
    static void OnSessionActive(void* context, ACameraCaptureSession* session);
    static void OnImageAvailable(void* context, AImageReader* reader);
};
