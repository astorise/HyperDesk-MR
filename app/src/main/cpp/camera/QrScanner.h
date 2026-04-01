#pragma once

#include "../rdp/RdpConnectionManager.h"
#include "CameraManager.h"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// QrScanner wraps CameraManager and ZXing-cpp to scan QR codes and parse
// RDP connection parameters from them.  Expected JSON format:
//   {"h":"host", "u":"user", "p":"pass", "d":"domain", "port":3389}
class QrScanner {
public:
    using ConnectCallback = std::function<void(const RdpConnectionManager::ConnectionParams&)>;

    explicit QrScanner(ANativeActivity* activity);
    ~QrScanner();

    // Set the callback invoked when a valid QR code with RDP params is decoded.
    void SetConnectCallback(ConnectCallback cb);

    // Start scanning.  Returns false if the camera cannot be opened.
    bool Start();

    // Stop scanning and release camera resources.
    void Stop();

    bool IsRunning() const;
    bool HasResult() const { return hasResult_.load(); }

private:
    std::unique_ptr<CameraManager> camera_;
    ConnectCallback                connectCallback_;
    std::mutex                     cbMutex_;
    std::atomic<bool>              hasResult_{false};

    // Last frame storage for debug snapshots.
    std::mutex           frameMutex_;
    std::vector<uint8_t> lastFrame_;
    int32_t              lastWidth_     = 0;
    int32_t              lastHeight_    = 0;
    int32_t              lastRowStride_ = 0;

    std::string debugSnapshotPath_;
    uint64_t    frameCount_      = 0;
    uint64_t    decodeMisses_    = 0;
    bool        snapshotSaved_   = false;

    // Called from CameraManager's image callback.
    void OnFrame(const uint8_t* data, int32_t width, int32_t height, int32_t rowStride);

    bool SaveSnapshot(const std::string& path);

    // Attempt to decode a QR string from luminance image data.
    static std::string DecodeQr(const uint8_t* data, int32_t width, int32_t height,
                                int32_t rowStride, bool aggressive);

    // Parse JSON connection params.  Returns false if parsing fails.
    static bool ParseConnectionParams(const std::string& json,
                                      RdpConnectionManager::ConnectionParams& out);
};
