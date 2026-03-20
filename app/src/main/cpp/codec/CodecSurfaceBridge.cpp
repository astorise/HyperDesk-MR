#include "CodecSurfaceBridge.h"
#include "../util/Logger.h"
#include "../util/Check.h"

// AIMAGE_FORMAT_PRIVATE keeps frames GPU-only (required for zero-copy).
// The decoder must be configured with COLOR_FormatSurface for this to work.
static constexpr int32_t  kImageFormat = AIMAGE_FORMAT_PRIVATE;
static constexpr int32_t  kMaxImages   = 3;   // triple-buffering

// Required usage flags for Vulkan AHardwareBuffer import.
static constexpr uint64_t kUsage =
    AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE |
    AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT;

CodecSurfaceBridge::CodecSurfaceBridge(uint32_t width, uint32_t height, uint32_t monitorIndex)
    : monitorIndex_(monitorIndex) {

    // AImageReader_newWithUsage ensures AHardwareBuffers are GPU-accessible.
    media_status_t status = AImageReader_newWithUsage(
        static_cast<int32_t>(width),
        static_cast<int32_t>(height),
        kImageFormat,
        kUsage,
        kMaxImages,
        &imageReader_);

    MEDIA_CHECK(status);

    // Set the frame-available listener — runs on the codec output thread.
    AImageReader_ImageListener listener{};
    listener.context      = this;
    listener.onImageAvailable = OnImageAvailable;
    MEDIA_CHECK(AImageReader_setImageListener(imageReader_, &listener));

    // Obtain the ANativeWindow that the codec will write decoded frames into.
    MEDIA_CHECK(AImageReader_getWindow(imageReader_, &readerWindow_));

    LOGI("CodecSurfaceBridge[%u]: AImageReader created %ux%u (maxImages=%d)",
         monitorIndex_, width, height, kMaxImages);
}

CodecSurfaceBridge::~CodecSurfaceBridge() {
    ReleaseCurrentBuffer();
    if (imageReader_) {
        AImageReader_delete(imageReader_);
        imageReader_   = nullptr;
        readerWindow_  = nullptr;
    }
}

bool CodecSurfaceBridge::AcquireLatestFrame(AHardwareBuffer** outBuffer) {
    if (!newFrameAvailable_.load(std::memory_order_acquire)) return false;
    newFrameAvailable_.store(false, std::memory_order_release);

    // Release any previously held image before acquiring a new one.
    ReleaseCurrentBuffer();

    media_status_t status = AImageReader_acquireLatestImage(imageReader_, &currentImage_);
    if (status != AMEDIA_OK || !currentImage_) {
        currentImage_ = nullptr;
        return false;
    }

    AHardwareBuffer* ahb = nullptr;
    status = AImage_getHardwareBuffer(currentImage_, &ahb);
    if (status != AMEDIA_OK || !ahb) {
        AImage_delete(currentImage_);
        currentImage_ = nullptr;
        return false;
    }

    *outBuffer = ahb;
    return true;
}

void CodecSurfaceBridge::ReleaseCurrentBuffer() {
    if (currentImage_) {
        AImage_delete(currentImage_);
        currentImage_ = nullptr;
    }
}

// ── AImageReader callback (codec output thread) ───────────────────────────────

void CodecSurfaceBridge::OnImageAvailable(void* context, AImageReader* /*reader*/) {
    auto* self = static_cast<CodecSurfaceBridge*>(context);
    self->newFrameAvailable_.store(true, std::memory_order_release);
}
