#pragma once

// openxr_platform.h requires platform headers first.
#include <jni.h>
#include <vulkan/vulkan.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <media/NdkMediaCodec.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <queue>
#include <vector>

class XrContext;
class CodecSurfaceBridge;
class MediaCodecDecoder;
class HdSwapchain;

// VirtualMonitor is the top-level abstraction for one virtual monitor slot.
// It owns:
//   - CodecSurfaceBridge   (AImageReader configured for zero-copy GPU path)
//   - MediaCodecDecoder    (H.264 AMediaCodec, async callback mode)
//   - HdSwapchain          (per-monitor XrSwapchain backed by the decoded frames)
//
// The render loop calls GetCompositionLayer() each frame; the RDP layer calls
// SubmitFrame() from its network thread to push compressed NAL units.
class VirtualMonitor {
public:
    VirtualMonitor(uint32_t monitorIndex, uint32_t width, uint32_t height);
    ~VirtualMonitor();

    // Create AImageReader + AMediaCodec for the H.264 decode path.
    // Safe to call lazily after InitXr(); subsequent calls are no-ops.
    bool InitCodec();

    // Create the OpenXR swapchain once the XrSession is ready.
    bool InitXr(XrContext& ctx);

    // Feed one compressed H.264 NAL unit.  Thread-safe; non-blocking.
    // The async OnInputAvailable callback will drain the internal queue.
    void SubmitFrame(const uint8_t* data, size_t size, int64_t presentationTimeUs);

    // Acquire the latest decoded frame, advance the swapchain, and populate the
    // composition layer descriptor.  Returns nullptr if the decoder is stopped,
    // the swapchain cannot be acquired, or InitXr() has not been called.
    // worldSpace, pose, and size are passed in from the render loop / MonitorLayout.
    const XrCompositionLayerQuad* GetCompositionLayer(XrSpace     worldSpace,
                                                       XrPosef     pose,
                                                       XrExtent2Df size);

    // Frustum-culler hooks — forward to the underlying decoder.
    void PauseDecoder();
    void ResumeDecoder();
    bool IsRunning() const;

    // Returns the underlying decoder so the render loop can pass it to FrustumCuller::UpdateAll.
    MediaCodecDecoder* GetDecoder() const { return decoder_.get(); }

private:
    uint32_t monitorIndex_;
    uint32_t width_;
    uint32_t height_;

    std::unique_ptr<CodecSurfaceBridge> bridge_;
    std::unique_ptr<MediaCodecDecoder>  decoder_;
    std::unique_ptr<HdSwapchain>        swapchain_;

    bool                    slotBound_ = false;
    XrCompositionLayerQuad  compositionLayer_{};

    // ── Async codec state ─────────────────────────────────────────────────────
    // Invariant: at most one side has data.
    //   pendingFrames_ non-empty  → codec buffer not yet available
    //   pendingInputIdx_ >= 0     → codec buffer waiting, no frame queued yet
    struct PendingFrame {
        std::vector<uint8_t> data;
        int64_t              pts = 0;
    };

    std::mutex               asyncMutex_;
    std::queue<PendingFrame> pendingFrames_;
    int32_t                  pendingInputIdx_ = -1;

    // Feed a frame into the codec at the given buffer index.
    // May be called from OnInputAvailable (codec thread) or SubmitFrame (RDP thread).
    void FeedToCodec(AMediaCodec* codec, int32_t index, const PendingFrame& frame);

    // ── AMediaCodec async callbacks (static, codec callback thread) ───────────
    static void OnInputAvailable(AMediaCodec* codec, void* userdata, int32_t index);
    static void OnOutputAvailable(AMediaCodec* codec, void* userdata, int32_t index,
                                  AMediaCodecBufferInfo* bufferInfo);
    static void OnFormatChanged(AMediaCodec* codec, void* userdata, AMediaFormat* format);
    static void OnError(AMediaCodec* codec, void* userdata, media_status_t error,
                        int32_t actionCode, const char* detail);
};
