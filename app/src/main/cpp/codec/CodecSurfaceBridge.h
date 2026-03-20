#pragma once

#include <media/NdkImageReader.h>
#include <android/native_window.h>
#include <android/hardware_buffer.h>

#include <atomic>
#include <cstdint>

// CodecSurfaceBridge creates an AImageReader whose ANativeWindow is used as the
// output surface of an AMediaCodec decoder.  Decoded frames arrive as
// AHardwareBuffers that can be imported directly into Vulkan as external memory
// (zero-copy path — see XrSwapchain::BindExternalHardwareBuffer).
class CodecSurfaceBridge {
public:
    CodecSurfaceBridge(uint32_t width, uint32_t height, uint32_t monitorIndex);
    ~CodecSurfaceBridge();

    // The ANativeWindow to pass to MediaCodecDecoder (and thus AMediaCodec_configure)
    // as the decoder output surface.
    ANativeWindow* GetCodecOutputSurface() const { return readerWindow_; }

    // Attempt to acquire the most recently decoded frame.
    // Returns true if a new AHardwareBuffer is available.
    // The caller MUST call ReleaseCurrentBuffer() when done to avoid AImageReader starvation.
    bool AcquireLatestFrame(AHardwareBuffer** outBuffer);
    void ReleaseCurrentBuffer();

    bool HasNewFrame() const { return newFrameAvailable_.load(std::memory_order_acquire); }

private:
    AImageReader*         imageReader_        = nullptr;
    ANativeWindow*        readerWindow_       = nullptr;
    AImage*               currentImage_       = nullptr;
    uint32_t              monitorIndex_;
    std::atomic<bool>     newFrameAvailable_{false};

    // Called by AImageReader on the codec output thread when a decoded frame is ready.
    static void OnImageAvailable(void* context, AImageReader* reader);
};
