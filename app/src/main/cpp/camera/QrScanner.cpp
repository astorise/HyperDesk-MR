#include "QrScanner.h"
#include "../xr/StatusOverlay.h"
#include "../util/Logger.h"

#include <ReadBarcode.h>
#include <ImageView.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

// ── Minimal JSON parser for {"h":"...", "u":"...", "p":"...", "d":"...", "port":N}
// We avoid pulling in a JSON library for this single, simple format.
static std::string ExtractJsonString(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return {};

    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return {};

    pos = json.find('"', pos + 1);
    if (pos == std::string::npos) return {};

    // Walk the string handling JSON escape sequences.
    std::string result;
    for (size_t i = pos + 1; i < json.size(); ++i) {
        if (json[i] == '"') break;           // unescaped quote → end of string
        if (json[i] == '\\' && i + 1 < json.size()) {
            ++i;  // consume the escaped character
            switch (json[i]) {
                case '"':  result += '"';  break;
                case '\\': result += '\\'; break;
                case '/':  result += '/';  break;
                case 'n':  result += '\n'; break;
                case 't':  result += '\t'; break;
                default:   result += json[i]; break;
            }
        } else {
            result += json[i];
        }
    }
    return result;
}

static int ExtractJsonInt(const std::string& json, const std::string& key, int defaultVal) {
    std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return defaultVal;

    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return defaultVal;

    // Skip whitespace after colon
    pos++;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;

    char* end = nullptr;
    long val = std::strtol(json.c_str() + pos, &end, 10);
    if (end == json.c_str() + pos) return defaultVal;
    return static_cast<int>(val);
}

static std::string ReadQrView(const uint8_t* data, int32_t width, int32_t height,
                              int32_t rowStride, const ZXing::ReaderOptions& opts) {
    ZXing::ImageView image(data, width, height, ZXing::ImageFormat::Lum, rowStride);
    auto result = ZXing::ReadBarcode(image, opts);
    return result.isValid() ? result.text() : std::string{};
}

static std::vector<uint8_t> CopyCrop(const uint8_t* data, int32_t rowStride, int32_t x0,
                                     int32_t y0, int32_t cropWidth, int32_t cropHeight) {
    std::vector<uint8_t> crop(cropWidth * cropHeight);
    for (int32_t y = 0; y < cropHeight; ++y) {
        std::memcpy(crop.data() + y * cropWidth, data + (y0 + y) * rowStride + x0, cropWidth);
    }
    return crop;
}

// ── QrScanner ────────────────────────────────────────────────────────────────

QrScanner::QrScanner(ANativeActivity* activity)
    : camera_(std::make_unique<CameraManager>(activity)) {
    if (activity) {
        if (activity->externalDataPath) {
            debugSnapshotPath_ = std::string(activity->externalDataPath) + "/qr-debug-latest.pgm";
        } else if (activity->internalDataPath) {
            debugSnapshotPath_ = std::string(activity->internalDataPath) + "/qr-debug-latest.pgm";
        }
    }
}

QrScanner::~QrScanner() {
    Stop();
}

void QrScanner::SetConnectCallback(ConnectCallback cb) {
    std::lock_guard lock(cbMutex_);
    connectCallback_ = std::move(cb);
}

bool QrScanner::Start() {
    hasResult_.store(false);
    frameCount_    = 0;
    decodeMisses_  = 0;
    snapshotSaved_ = false;

    camera_->SetFrameCallback([this](const uint8_t* data, int32_t w, int32_t h, int32_t stride) {
        OnFrame(data, w, h, stride);
    });

    if (StatusOverlay::sInstance) {
        StatusOverlay::sInstance->SetStatusLine(0, "scan: starting");
        StatusOverlay::sInstance->SetStatusLine(4, "qr: waiting for frames");
        if (!debugSnapshotPath_.empty()) {
            StatusOverlay::sInstance->SetStatusLine(5, "snap: pending qr-debug-latest.pgm");
        } else {
            StatusOverlay::sInstance->SetStatusLine(5, "snap: path unavailable");
        }
    }

    if (!camera_->Start()) {
        LOGE("QrScanner: camera start failed");
        if (StatusOverlay::sInstance) {
            StatusOverlay::sInstance->SetStatusLine(0, "scan: camera start failed");
            StatusOverlay::sInstance->AddLog("[ERR] QR scanner camera start failed");
        }
        return false;
    }

    LOGI("Scanner initialized");
    if (StatusOverlay::sInstance) {
        StatusOverlay::sInstance->SetStatusLine(0, "scan: running");
        StatusOverlay::sInstance->AddLog("[OK] QR scanner running");
    }
    return true;
}

void QrScanner::Stop() {
    camera_->Stop();
    if (StatusOverlay::sInstance) {
        StatusOverlay::sInstance->SetStatusLine(0, hasResult_.load() ? "scan: completed" : "scan: stopped");
    }
}

bool QrScanner::IsRunning() const {
    return camera_->IsRunning() && !hasResult_.load();
}

void QrScanner::OnFrame(const uint8_t* data, int32_t width, int32_t height, int32_t rowStride) {
    {
        std::lock_guard lock(frameMutex_);
        lastWidth_     = width;
        lastHeight_    = height;
        lastRowStride_ = rowStride;
        lastFrame_.assign(data, data + rowStride * height);
    }

    ++frameCount_;
    if (hasResult_.load()) return;

    bool aggressiveDecode = (decodeMisses_ % 15 == 0);
    std::string decoded = DecodeQr(data, width, height, rowStride, aggressiveDecode);
    if (decoded.empty()) {
        ++decodeMisses_;

        if (StatusOverlay::sInstance && frameCount_ % 120 == 0) {
            char buf[128];
            snprintf(buf, sizeof(buf), "qr: frames=%llu miss=%llu last=empty",
                     static_cast<unsigned long long>(frameCount_),
                     static_cast<unsigned long long>(decodeMisses_));
            StatusOverlay::sInstance->SetStatusLine(4, buf);
        }

        if (!snapshotSaved_ && !debugSnapshotPath_.empty() && frameCount_ >= 240 &&
            SaveSnapshot(debugSnapshotPath_)) {
            snapshotSaved_ = true;
            if (StatusOverlay::sInstance) {
                StatusOverlay::sInstance->SetStatusLine(5, "snap: saved qr-debug-latest.pgm");
                StatusOverlay::sInstance->AddLog("[WARN] QR snapshot saved for debug");
            }
        }
        return;
    }

    LOGI("QR Found: %s", decoded.c_str());
    if (StatusOverlay::sInstance) {
        char buf[128];
        snprintf(buf, sizeof(buf), "qr: found len=%u after %llu frames",
                 static_cast<unsigned>(decoded.size()),
                 static_cast<unsigned long long>(frameCount_));
        StatusOverlay::sInstance->SetStatusLine(4, buf);
        StatusOverlay::sInstance->AddLog("[OK] QR payload decoded");
    }

    RdpConnectionManager::ConnectionParams params;
    if (!ParseConnectionParams(decoded, params)) {
        LOGW("QrScanner: invalid JSON in QR code");
        if (StatusOverlay::sInstance) {
            StatusOverlay::sInstance->SetStatusLine(0, "scan: decoded invalid JSON");
            StatusOverlay::sInstance->AddLog("[ERR] QR JSON parse failed");
        }
        return;
    }

    hasResult_.store(true);
    if (StatusOverlay::sInstance) {
        StatusOverlay::sInstance->SetStatusLine(0, "scan: decoded valid QR");
    }

    std::lock_guard lock(cbMutex_);
    if (connectCallback_) {
        connectCallback_(params);
    }
}

bool QrScanner::SaveSnapshot(const std::string& path) {
    std::lock_guard lock(frameMutex_);
    if (lastFrame_.empty()) return false;

    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;

    fprintf(f, "P5\n%d %d\n255\n", lastWidth_, lastHeight_);
    for (int32_t y = 0; y < lastHeight_; ++y) {
        fwrite(lastFrame_.data() + y * lastRowStride_, 1, lastWidth_, f);
    }
    fclose(f);

    LOGI("QrScanner: snapshot saved to %s (%dx%d)", path.c_str(), lastWidth_, lastHeight_);
    return true;
}

std::string QrScanner::DecodeQr(const uint8_t* data, int32_t width, int32_t height,
                                int32_t rowStride, bool aggressive) {
    ZXing::ReaderOptions fastOpts;
    fastOpts.setFormats(ZXing::BarcodeFormat::QRCode);
    fastOpts.setTryHarder(false);

    if (auto decoded = ReadQrView(data, width, height, rowStride, fastOpts); !decoded.empty()) {
        return decoded;
    }

    if (!aggressive) return {};

    ZXing::ReaderOptions aggressiveOpts;
    aggressiveOpts.setFormats(ZXing::BarcodeFormat::QRCode);
    aggressiveOpts.setTryHarder(true);

    if (auto decoded = ReadQrView(data, width, height, rowStride, aggressiveOpts); !decoded.empty()) {
        return decoded;
    }

    struct CropSpec {
        const char* name;
        float x;
        float y;
        float w;
        float h;
    };

    static constexpr CropSpec kCropSpecs[] = {
        {"center-80", 0.10f, 0.10f, 0.80f, 0.80f},
        {"center-66", 0.17f, 0.17f, 0.66f, 0.66f},
        {"upper-70",  0.15f, 0.05f, 0.70f, 0.70f},
        {"left-70",   0.05f, 0.15f, 0.70f, 0.70f},
        {"right-70",  0.25f, 0.15f, 0.70f, 0.70f},
    };

    for (const auto& spec : kCropSpecs) {
        int32_t cropWidth = static_cast<int32_t>(width * spec.w);
        int32_t cropHeight = static_cast<int32_t>(height * spec.h);
        int32_t x0 = static_cast<int32_t>(width * spec.x);
        int32_t y0 = static_cast<int32_t>(height * spec.y);

        if (cropWidth <= 0 || cropHeight <= 0 || x0 < 0 || y0 < 0 ||
            x0 + cropWidth > width || y0 + cropHeight > height) {
            continue;
        }

        auto crop = CopyCrop(data, rowStride, x0, y0, cropWidth, cropHeight);
        if (auto decoded = ReadQrView(crop.data(), cropWidth, cropHeight, cropWidth, aggressiveOpts);
            !decoded.empty()) {
            LOGI("QrScanner: decoded QR from %s crop (%dx%d @ %d,%d)",
                 spec.name, cropWidth, cropHeight, x0, y0);
            return decoded;
        }
    }

    return {};
}

bool QrScanner::ParseConnectionParams(const std::string& json,
                                       RdpConnectionManager::ConnectionParams& out) {
    out.hostname = ExtractJsonString(json, "h");
    out.username = ExtractJsonString(json, "u");
    out.password = ExtractJsonString(json, "p");
    out.domain   = ExtractJsonString(json, "d");
    out.port     = static_cast<uint16_t>(ExtractJsonInt(json, "port", 3389));

    // If username contains DOMAIN\user, split into domain + username.
    auto sep = out.username.find('\\');
    if (sep != std::string::npos && out.domain.empty()) {
        out.domain   = out.username.substr(0, sep);
        out.username = out.username.substr(sep + 1);
        LOGI("QrScanner: split domain='%s' username='%s'", out.domain.c_str(), out.username.c_str());
    }

    if (out.hostname.empty()) {
        LOGE("QrScanner: missing 'h' (hostname) in QR JSON");
        return false;
    }
    if (out.username.empty()) {
        LOGE("QrScanner: missing 'u' (username) in QR JSON");
        return false;
    }
    return true;
}
