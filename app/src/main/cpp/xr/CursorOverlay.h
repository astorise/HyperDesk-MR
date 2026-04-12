#pragma once

#include <jni.h>
#include <vulkan/vulkan.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <android/asset_manager.h>

#include <cstdint>
#include <mutex>
#include <vector>

class XrContext;

// CursorOverlay renders a cursor icon in 3D space to represent the
// mouse cursor position on the virtual monitor wall.  It owns its own
// XrSwapchain and XrCompositionLayerQuad.
class CursorOverlay {
public:
    CursorOverlay(XrContext& ctx, AAssetManager* assetManager);
    ~CursorOverlay();

    // Update cursor desktop-coordinate position.  Thread-safe.
    void SetPosition(int32_t desktopX, int32_t desktopY);

    // Returns the composition layer for this frame, or nullptr if hidden.
    const XrCompositionLayerQuad* GetCompositionLayer(
        XrSpace worldSpace,
        const XrPosef& cylinderCenter,
        float cylinderRadius,
        float centralAngle,
        float aspectRatio,
        bool splitRows);

private:
    // Cursor icon size in meters.
    static constexpr float kCursorSize = 0.03f;

    XrContext& ctx_;
    XrSwapchain swapchain_ = XR_NULL_HANDLE;
    XrCompositionLayerQuad compositionLayer_{};

    VkBuffer       stagingBuffer_  = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory_  = VK_NULL_HANDLE;
    void*          stagingMapped_  = nullptr;
    std::vector<VkImage> swapchainImages_;
    bool textureUploaded_ = false;

    uint32_t texWidth_  = 0;
    uint32_t texHeight_ = 0;

    std::mutex posMutex_;
    int32_t    desktopX_ = 2880;  // center of primary monitor (x in desktop space)
    int32_t    desktopY_ = 540;

    bool LoadPngFromAssets(AAssetManager* mgr, const char* path);
    void GenerateFallbackCursor();
    void CreateSwapchain();
    void CreateStagingBuffer();
    void UploadToAllSwapchainImages();
    void UploadToSwapchainImage(VkImage image);

    // Temporary storage for decoded pixels (cleared after staging upload).
    std::vector<uint8_t> decodedPixels_;
    size_t decodedStride_ = 0;
};
