#include "QrScanner.h"
#include "../util/Logger.h"

#include <ReadBarcode.h>
#include <ImageView.h>

#include <cstdlib>
#include <cstring>

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

// ── QrScanner ────────────────────────────────────────────────────────────────

QrScanner::QrScanner(ANativeActivity* activity)
    : camera_(std::make_unique<CameraManager>(activity)) {}

QrScanner::~QrScanner() {
    Stop();
}

void QrScanner::SetConnectCallback(ConnectCallback cb) {
    std::lock_guard lock(cbMutex_);
    connectCallback_ = std::move(cb);
}

bool QrScanner::Start() {
    hasResult_.store(false);

    camera_->SetFrameCallback([this](const uint8_t* data, int32_t w, int32_t h, int32_t stride) {
        OnFrame(data, w, h, stride);
    });

    if (!camera_->Start()) {
        LOGE("QrScanner: camera start failed");
        return false;
    }

    LOGI("Scanner initialized");
    return true;
}

void QrScanner::Stop() {
    camera_->Stop();
}

bool QrScanner::IsRunning() const {
    return camera_->IsRunning() && !hasResult_.load();
}

void QrScanner::OnFrame(const uint8_t* data, int32_t width, int32_t height, int32_t rowStride) {
    if (hasResult_.load()) return;

    std::string decoded = DecodeQr(data, width, height, rowStride);
    if (decoded.empty()) return;

    LOGI("QR Found: %s", decoded.c_str());

    RdpConnectionManager::ConnectionParams params;
    if (!ParseConnectionParams(decoded, params)) {
        LOGW("QrScanner: invalid JSON in QR code");
        return;
    }

    hasResult_.store(true);

    std::lock_guard lock(cbMutex_);
    if (connectCallback_) {
        connectCallback_(params);
    }
}

std::string QrScanner::DecodeQr(const uint8_t* data, int32_t width, int32_t height,
                                 int32_t rowStride) {
    ZXing::ImageView image(data, width, height, ZXing::ImageFormat::Lum, rowStride);

    ZXing::ReaderOptions opts;
    opts.setFormats(ZXing::BarcodeFormat::QRCode);
    opts.setTryHarder(true);
    opts.setTryRotate(true);

    auto result = ZXing::ReadBarcode(image, opts);
    if (result.isValid()) {
        return result.text();
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
