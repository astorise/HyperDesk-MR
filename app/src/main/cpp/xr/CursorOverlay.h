#pragma once

#include <jni.h>
#include <vulkan/vulkan.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <cstdint>
#include <mutex>
#include <vector>

class XrContext;

// CursorOverlay renders a small white dot in 3D space to represent the
// mouse cursor position on the virtual monitor wall.  It owns its own
// XrSwapchain and XrCompositionLayerQuad.
class CursorOverlay {
public:
    CursorOverlay(XrContext& ctx);
    ~CursorOverlay();

    // Update cursor desktop-coordinate position.  Thread-safe.
    void SetPosition(int32_t desktopX, int32_t desktopY);

    // Returns the composition layer for this frame, or nullptr if hidden.
    // cylinderCenter is the shared pose used for all cylinder monitor layers.
    // cylinderRadius and centralAngle must match XrCompositor values.
    const XrCompositionLayerQuad* GetCompositionLayer(
        XrSpace worldSpace,
        const XrPosef& cylinderCenter,
        float cylinderRadius,
        float centralAngle,
        float aspectRatio);

private:
    static constexpr uint32_t kTexSize = 32;
    // Cursor dot size in meters.
    static constexpr float kCursorSize = 0.012f;

    XrContext& ctx_;
    XrSwapchain swapchain_ = XR_NULL_HANDLE;
    XrCompositionLayerQuad compositionLayer_{};

    VkBuffer       stagingBuffer_  = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory_  = VK_NULL_HANDLE;
    void*          stagingMapped_  = nullptr;
    std::vector<VkImage> swapchainImages_;
    bool textureUploaded_ = false;

    std::mutex posMutex_;
    int32_t    desktopX_ = 2880;  // center of 5760
    int32_t    desktopY_ = 540;

    void CreateSwapchain();
    void CreateStagingBuffer();
    void RenderCursorTexture();
    void UploadToSwapchainImage(VkImage image);
};
