#pragma once

#include <jni.h>
#include <vulkan/vulkan.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <atomic>
#include <array>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

class XrContext;

// StatusOverlay renders a multi-line debug console quad in MR space.
// It owns its own XrSwapchain and XrCompositionLayerQuad, independent
// of the VirtualMonitor pipeline.  Text is rendered as a simple
// CPU-generated RGBA texture written directly to the swapchain's
// Vulkan image via vkMapMemory (the swapchain is created with
// XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT for CPU-writable memory).
class StatusOverlay {
public:
    StatusOverlay(XrContext& ctx, uint32_t texWidth, uint32_t texHeight);
    ~StatusOverlay();

    // Set the message shown on the overlay.  Thread-safe.
    // Empty string hides the overlay.
    void SetMessage(const std::string& message);

    // Append a debug log line.  Thread-safe.
    // The overlay keeps the last kMaxLogLines and stays visible.
    void AddLog(const std::string& line);

    // Replace one persistent status line rendered above the log buffer.
    void SetStatusLine(size_t index, const std::string& line);
    void ClearStatusLine(size_t index);

    // Returns the composition layer for this frame, or nullptr if hidden.
    // Called from the render loop thread.
    const XrCompositionLayerQuad* GetCompositionLayer(XrSpace worldSpace);

    bool IsVisible() const { return visible_.load(); }

    // Global instance for debug logging from anywhere.
    static StatusOverlay* sInstance;

private:
    static constexpr size_t kMaxStatusLines = 6;
    static constexpr int    kMaxLogLines    = 4;

    XrContext&   ctx_;
    uint32_t     texWidth_;
    uint32_t     texHeight_;

    XrSwapchain  swapchain_ = XR_NULL_HANDLE;
    XrCompositionLayerQuad compositionLayer_{};

    // CPU-writable staging buffer for the texture.
    VkBuffer       stagingBuffer_  = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory_  = VK_NULL_HANDLE;
    void*          stagingMapped_  = nullptr;

    // Swapchain image info.
    std::vector<VkImage> swapchainImages_;

    std::mutex              messageMutex_;
    std::array<std::string, kMaxStatusLines> statusLines_{};
    std::deque<std::string> logLines_;
    bool                    messageDirty_ = false;
    std::atomic<bool>       visible_{false};

    void CreateSwapchain();
    void CreateStagingBuffer();
    void RenderTextToStaging();
    void UploadStagingToSwapchainImage(VkImage image);
};
