#pragma once

#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <android/native_window.h>

#include <cstdint>
#include <cstddef>

// MediaCodecDecoder wraps one AMediaCodec instance configured for hardware-
// accelerated H.264/AVC decode in low-latency mode.
//
// The output goes directly to an ANativeWindow provided at construction time
// (which comes from a CodecSurfaceBridge's AImageReader).
class MediaCodecDecoder {
public:
    // outputSurface : ANativeWindow* from CodecSurfaceBridge::GetCodecOutputSurface()
    // monitorIndex  : 0-15, used for logging
    MediaCodecDecoder(ANativeWindow* outputSurface, uint32_t monitorIndex);
    ~MediaCodecDecoder();

    // Configure the decoder for the given resolution.  Must be called before Start().
    bool Configure(uint32_t width, uint32_t height);

    bool Start();
    bool Stop();

    // Submit one compressed access unit to the decoder input queue.
    // Non-blocking: returns false if no input buffer is currently available.
    bool SubmitFrame(const uint8_t* data, size_t size, int64_t presentationTimeUs);

    // Pause/resume codec to save GPU cycles (called by FrustumCuller).
    void Pause();
    void Resume();

    bool IsRunning()    const { return running_; }
    bool IsConfigured() const { return configured_; }

private:
    ANativeWindow* outputSurface_  = nullptr;
    uint32_t       monitorIndex_;
    AMediaCodec*   codec_          = nullptr;
    bool           running_        = false;
    bool           configured_     = false;

    // Apply vendor-specific and standard low-latency keys to the format.
    void ApplyLowLatencyKeys(AMediaFormat* format);
};
