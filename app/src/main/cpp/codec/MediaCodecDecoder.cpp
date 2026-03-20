#include "MediaCodecDecoder.h"
#include "../util/Logger.h"
#include "../util/Check.h"

#include <cstring>

// COLOR_FormatSurface — routes decoded frames directly to the output ANativeWindow
// without a CPU-readable colour conversion step.
static constexpr int32_t kColorFormatSurface = 0x7F000789;

// H.264 MIME type.
static constexpr const char* kMimeTypeH264 = "video/avc";

MediaCodecDecoder::MediaCodecDecoder(ANativeWindow* outputSurface, uint32_t monitorIndex)
    : outputSurface_(outputSurface), monitorIndex_(monitorIndex) {}

MediaCodecDecoder::~MediaCodecDecoder() {
    if (running_) Stop();
    if (codec_) {
        AMediaCodec_delete(codec_);
        codec_ = nullptr;
    }
}

bool MediaCodecDecoder::Configure(uint32_t width, uint32_t height) {
    codec_ = AMediaCodec_createDecoderByType(kMimeTypeH264);
    if (!codec_) {
        LOGE("Decoder[%u]: AMediaCodec_createDecoderByType failed", monitorIndex_);
        return false;
    }

    AMediaFormat* format = AMediaFormat_new();
    AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME,         kMimeTypeH264);
    AMediaFormat_setInt32 (format, AMEDIAFORMAT_KEY_WIDTH,        static_cast<int32_t>(width));
    AMediaFormat_setInt32 (format, AMEDIAFORMAT_KEY_HEIGHT,       static_cast<int32_t>(height));
    AMediaFormat_setInt32 (format, AMEDIAFORMAT_KEY_COLOR_FORMAT, kColorFormatSurface);
    ApplyLowLatencyKeys(format);

    media_status_t status = AMediaCodec_configure(
        codec_, format, outputSurface_,
        /*crypto=*/nullptr,
        /*flags=*/0);   // 0 = decoder (not encoder)

    AMediaFormat_delete(format);

    if (status != AMEDIA_OK) {
        LOGE("Decoder[%u]: AMediaCodec_configure failed: %d", monitorIndex_, status);
        return false;
    }

    configured_ = true;
    LOGI("Decoder[%u]: configured %ux%u H.264", monitorIndex_, width, height);
    return true;
}

void MediaCodecDecoder::ApplyLowLatencyKeys(AMediaFormat* format) {
    // Android 12+ standard key.
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_LOW_LATENCY, 1);
    // Android 10+ latency hint (0 = as low as possible).
    AMediaFormat_setInt32(format, "latency", 0);
    // Qualcomm Snapdragon XR2 vendor extension for sub-frame latency.
    AMediaFormat_setInt32(format, "vendor.qti-ext-dec-low-latency.enable", 1);
}

bool MediaCodecDecoder::Start() {
    if (!configured_ || running_) return false;
    media_status_t status = AMediaCodec_start(codec_);
    if (status != AMEDIA_OK) {
        LOGE("Decoder[%u]: AMediaCodec_start failed: %d", monitorIndex_, status);
        return false;
    }
    running_ = true;
    LOGD("Decoder[%u]: started", monitorIndex_);
    return true;
}

bool MediaCodecDecoder::Stop() {
    if (!running_) return false;
    media_status_t status = AMediaCodec_stop(codec_);
    running_ = false;
    if (status != AMEDIA_OK) {
        LOGE("Decoder[%u]: AMediaCodec_stop failed: %d", monitorIndex_, status);
        return false;
    }
    LOGD("Decoder[%u]: stopped", monitorIndex_);
    return true;
}

bool MediaCodecDecoder::SubmitFrame(const uint8_t* data, size_t size,
                                    int64_t presentationTimeUs) {
    if (!running_) return false;

    // Dequeue an input buffer (non-blocking: timeout = 0).
    ssize_t bufIdx = AMediaCodec_dequeueInputBuffer(codec_, /*timeoutUs=*/0);
    if (bufIdx < 0) {
        // No buffer available right now — caller should drop or retry.
        return false;
    }

    size_t bufSize = 0;
    uint8_t* buf = AMediaCodec_getInputBuffer(codec_, static_cast<size_t>(bufIdx), &bufSize);
    if (!buf || bufSize < size) {
        LOGE("Decoder[%u]: input buffer too small (%zu < %zu)", monitorIndex_, bufSize, size);
        // Return the buffer unfilled with the EOS flag to avoid deadlock.
        AMediaCodec_queueInputBuffer(codec_, static_cast<size_t>(bufIdx),
                                     0, 0, presentationTimeUs,
                                     AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
        return false;
    }

    std::memcpy(buf, data, size);
    media_status_t status = AMediaCodec_queueInputBuffer(
        codec_, static_cast<size_t>(bufIdx),
        /*offset=*/0, size, presentationTimeUs, /*flags=*/0);
    return status == AMEDIA_OK;
}

void MediaCodecDecoder::Pause() {
    if (running_) {
        AMediaCodec_stop(codec_);
        running_ = false;
        LOGD("Decoder[%u]: paused (codec stopped)", monitorIndex_);
    }
}

void MediaCodecDecoder::Resume() {
    if (configured_ && !running_) {
        AMediaCodec_start(codec_);
        running_ = true;
        LOGD("Decoder[%u]: resumed", monitorIndex_);
    }
}
