#include "VirtualMonitor.h"
#include "../codec/CodecSurfaceBridge.h"
#include "../codec/MediaCodecDecoder.h"
#include "../xr/XrSwapchain.h"
#include "../xr/XrContext.h"
#include "../util/DebugUtils.h"

#include <algorithm>
#include <cstring>

// ── Constructor / Destructor ───────────────────────────────────────────────────

VirtualMonitor::VirtualMonitor(uint32_t monitorIndex, uint32_t width, uint32_t height)
    : monitorIndex_(monitorIndex), width_(width), height_(height) {}

VirtualMonitor::~VirtualMonitor() = default;

// ── Phase 1: codec initialisation ─────────────────────────────────────────────

// Task 4: Create AImageReader configured for GPU sampling.
// Task 5: Bind its ANativeWindow as the AMediaCodec output surface.
// Task 3: Initialize AMediaCodec for "video/avc" inside VirtualMonitor.
// Task 6: Enable async callbacks before Configure().
bool VirtualMonitor::InitCodec() {
    if (decoder_ && bridge_) {
        return true;
    }

    // Task 4 — AImageReader with GPU-sampling usage (inside CodecSurfaceBridge).
    bridge_ = std::make_unique<CodecSurfaceBridge>(width_, height_, monitorIndex_);

    ANativeWindow* window = bridge_->GetCodecOutputSurface();
    if (!window) {
        LOGE("VirtualMonitor[%u]: AImageReader ANativeWindow is null", monitorIndex_);
        return false;
    }

    // Task 5 — the ANativeWindow is passed to MediaCodecDecoder (AMediaCodec_configure)
    //          as the decoder output surface.
    // Task 3 — MediaCodecDecoder creates and configures AMediaCodec for "video/avc".
    decoder_ = std::make_unique<MediaCodecDecoder>(window, monitorIndex_);

    // Task 6 — register async callbacks BEFORE Configure() (NDK requirement).
    AMediaCodecOnAsyncNotifyCallback asyncCb{};
    asyncCb.onAsyncInputAvailable  = OnInputAvailable;
    asyncCb.onAsyncOutputAvailable = OnOutputAvailable;
    asyncCb.onAsyncFormatChanged   = OnFormatChanged;
    asyncCb.onAsyncError           = OnError;
    decoder_->EnableAsyncCallbacks(asyncCb, this);

    if (!decoder_->Configure(width_, height_)) return false;
    if (!decoder_->Start())                    return false;

    LOGI("VirtualMonitor[%u]: codec initialised (%ux%u, async)", monitorIndex_, width_, height_);
    return true;
}

// ── Phase 2: OpenXR swapchain initialisation ──────────────────────────────────

// Task 7: Create Vulkan-backed OpenXR swapchain (xrCreateSwapchain inside HdSwapchain).
bool VirtualMonitor::InitXr(XrContext& ctx) {
    swapchain_ = std::make_unique<HdSwapchain>(ctx, width_, height_, monitorIndex_);
    LOGI("VirtualMonitor[%u]: XR swapchain created", monitorIndex_);
    return true;
}

// ── SubmitFrame ───────────────────────────────────────────────────────────────

// Feed a compressed NAL unit.  If the codec has a pending empty input buffer
// (pendingInputIdx_ >= 0), feed directly; otherwise queue for OnInputAvailable.
void VirtualMonitor::SubmitFrame(const uint8_t* data, size_t size, int64_t pts) {
    PendingFrame frame;
    frame.data.assign(data, data + size);
    frame.pts = pts;

    int32_t idx = -1;
    {
        std::lock_guard<std::mutex> lock(asyncMutex_);
        if (pendingInputIdx_ >= 0) {
            idx               = pendingInputIdx_;
            pendingInputIdx_  = -1;
        } else {
            pendingFrames_.push(std::move(frame));
        }
    }

    if (idx >= 0) {
        FeedToCodec(decoder_->GetCodec(), idx, frame);
    }
}

void VirtualMonitor::SubmitSoftwareFrame(const uint8_t* bgra, uint32_t srcWidth,
                                         uint32_t srcHeight, uint32_t srcStride) {
    if (!bgra || srcWidth == 0 || srcHeight == 0 || srcStride < srcWidth * 4u) {
        return;
    }

    std::vector<uint8_t> scaled(static_cast<size_t>(width_) * height_ * 4u);
    for (uint32_t y = 0; y < height_; ++y) {
        const uint32_t srcY = std::min(srcHeight - 1, (y * srcHeight) / height_);
        const uint8_t* srcRow = bgra + static_cast<size_t>(srcY) * srcStride;
        uint8_t* dstRow = scaled.data() + static_cast<size_t>(y) * width_ * 4u;

        for (uint32_t x = 0; x < width_; ++x) {
            const uint32_t srcX = std::min(srcWidth - 1, (x * srcWidth) / width_);
            const uint8_t* srcPixel = srcRow + static_cast<size_t>(srcX) * 4u;
            uint8_t* dstPixel = dstRow + static_cast<size_t>(x) * 4u;

            dstPixel[0] = srcPixel[2];
            dstPixel[1] = srcPixel[1];
            dstPixel[2] = srcPixel[0];
            dstPixel[3] = 255;
        }
    }

    std::lock_guard<std::mutex> lock(softwareFrameMutex_);
    softwareFrameRgba_.swap(scaled);
    softwareFrameReady_ = true;
}

// ── GetCompositionLayer (Tasks 8 + 10) ────────────────────────────────────────

// Task 8: Acquire latest image from AImageReader; bind to swapchain.
// Task 10: Populate and return a non-null XrCompositionLayerQuad*.
const XrCompositionLayerCylinderKHR* VirtualMonitor::GetCompositionLayer(
        XrSpace worldSpace, XrPosef pose,
        float radius, float centralAngle, float aspectRatio) {
    if (!swapchain_) return nullptr;

    // Task 8 — try to acquire the most recently decoded frame.
    const bool canUseHardwareDecoder = decoder_ && decoder_->IsRunning();
    AHardwareBuffer* ahb = nullptr;
    const bool hasHardwareFrame =
        canUseHardwareDecoder && bridge_ && bridge_->AcquireLatestFrame(&ahb);

    // Acquire and wait for a swapchain image slot.
    uint32_t imageIndex = 0;
    if (!swapchain_->AcquireImage(imageIndex)) return nullptr;
    if (!swapchain_->WaitImage())               return nullptr;

    // Rebind the current swapchain slot to the latest decoder buffer.
    // OpenXR rotates swapchain images, so binding a single slot once leaves
    // the remaining images transparent/invisible.
    if (hasHardwareFrame) {
        swapchain_->BindExternalHardwareBuffer(imageIndex, ahb);
        if (!firstBoundFrameLogged_) {
            LOGI("VirtualMonitor[%u]: first GPU frame bound to swapchain slot %u",
                 monitorIndex_, imageIndex);
            firstBoundFrameLogged_ = true;
        }
    }

    bool hasRenderableImage = swapchain_->IsImageBound(imageIndex);
    if (hasHardwareFrame) {
        bridge_->ReleaseCurrentBuffer();
        hasRenderableImage = true;
    }

    if (!hasRenderableImage) {
        std::vector<uint8_t> softwareFrame;
        {
            std::lock_guard<std::mutex> lock(softwareFrameMutex_);
            if (softwareFrameReady_) {
                softwareFrame = softwareFrameRgba_;
            }
        }

        if (!softwareFrame.empty() &&
            swapchain_->UploadRgbaFrame(imageIndex, softwareFrame.data(), softwareFrame.size())) {
            hasRenderableImage = true;
            if (!firstSoftwareFrameLogged_) {
                LOGI("VirtualMonitor[%u]: first software frame uploaded to swapchain slot %u",
                     monitorIndex_, imageIndex);
                firstSoftwareFrameLogged_ = true;
            }
        }
    }

    if (!hasRenderableImage) {
        swapchain_->ReleaseImage();
        return nullptr;
    }

    // Task 10 — populate XrCompositionLayerCylinderKHR with non-null swapchain reference.
    compositionLayer_                = XrCompositionLayerCylinderKHR{XR_TYPE_COMPOSITION_LAYER_CYLINDER_KHR};
    compositionLayer_.layerFlags     = 0;
    compositionLayer_.space          = worldSpace;
    compositionLayer_.eyeVisibility  = XR_EYE_VISIBILITY_BOTH;
    compositionLayer_.subImage       = swapchain_->GetSubImage();
    compositionLayer_.pose           = pose;
    compositionLayer_.radius         = radius;
    compositionLayer_.centralAngle   = centralAngle;
    compositionLayer_.aspectRatio    = aspectRatio;

    swapchain_->ReleaseImage();

    return &compositionLayer_;
}

// ── Frustum culler hooks ───────────────────────────────────────────────────────

void VirtualMonitor::PauseDecoder()   { if (decoder_) decoder_->Pause(); }
void VirtualMonitor::ResumeDecoder()  { if (decoder_) decoder_->Resume(); }
bool VirtualMonitor::IsRunning() const { return decoder_ && decoder_->IsRunning(); }

// ── FeedToCodec ───────────────────────────────────────────────────────────────

void VirtualMonitor::FeedToCodec(AMediaCodec* codec, int32_t index,
                                  const PendingFrame& frame) {
    size_t   bufSize = 0;
    uint8_t* buf     = AMediaCodec_getInputBuffer(
        codec, static_cast<size_t>(index), &bufSize);

    if (!buf || bufSize < frame.data.size()) {
        LOGE("VirtualMonitor[%u]: OnInputAvailable: buffer invalid (ptr=%p, "
             "bufSize=%zu, frameSize=%zu)",
             monitorIndex_, static_cast<void*>(buf), bufSize, frame.data.size());
        return;
    }

    std::memcpy(buf, frame.data.data(), frame.data.size());
    media_status_t status = AMediaCodec_queueInputBuffer(
        codec, static_cast<size_t>(index),
        /*offset=*/0, frame.data.size(), frame.pts, /*flags=*/0);
    if (status != AMEDIA_OK) {
        LOGE("VirtualMonitor[%u]: AMediaCodec_queueInputBuffer failed: %d",
             monitorIndex_, status);
    }
}

// ── Async callbacks (codec callback thread) ───────────────────────────────────

// Task 6 — OnInputAvailable: pop a pending frame from the queue and feed it.
// If the queue is empty, store the buffer index for SubmitFrame() to use.
void VirtualMonitor::OnInputAvailable(AMediaCodec* codec, void* userdata, int32_t index) {
    auto* vm = static_cast<VirtualMonitor*>(userdata);

    PendingFrame frame;
    bool         hasFrame = false;
    {
        std::lock_guard<std::mutex> lock(vm->asyncMutex_);
        if (!vm->pendingFrames_.empty()) {
            frame    = std::move(vm->pendingFrames_.front());
            vm->pendingFrames_.pop();
            hasFrame = true;
        } else {
            vm->pendingInputIdx_ = index;
        }
    }

    if (hasFrame) {
        vm->FeedToCodec(codec, index, frame);
    }
}

// Task 6 — OnOutputAvailable: release decoded buffer to the output surface
// (which feeds the AImageReader / CodecSurfaceBridge zero-copy path).
void VirtualMonitor::OnOutputAvailable(AMediaCodec* codec, void* userdata,
                                        int32_t index,
                                        AMediaCodecBufferInfo* /*bufferInfo*/) {
    auto* vm = static_cast<VirtualMonitor*>(userdata);
    media_status_t status = AMediaCodec_releaseOutputBuffer(
        codec, static_cast<size_t>(index), /*render=*/true);
    if (status != AMEDIA_OK) {
        LOGE("VirtualMonitor[%u]: OnOutputAvailable: releaseOutputBuffer failed: %d",
             vm->monitorIndex_, status);
    }
}

void VirtualMonitor::OnFormatChanged(AMediaCodec* /*codec*/, void* userdata,
                                      AMediaFormat* /*format*/) {
    auto* vm = static_cast<VirtualMonitor*>(userdata);
    LOGI("VirtualMonitor[%u]: codec output format changed", vm->monitorIndex_);
}

void VirtualMonitor::OnError(AMediaCodec* /*codec*/, void* userdata,
                              media_status_t error, int32_t actionCode,
                              const char* /*detail*/) {
    auto* vm = static_cast<VirtualMonitor*>(userdata);
    LOGE("VirtualMonitor[%u]: async codec error: status=%d actionCode=%d",
         vm->monitorIndex_, error, actionCode);
}
