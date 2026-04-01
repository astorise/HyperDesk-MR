#pragma once

// openxr_platform.h needs Vulkan and JNI headers before it
#include <jni.h>
#include <vulkan/vulkan.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <android/hardware_buffer.h>

#include <cstddef>
#include <cstdint>
#include <vector>

class XrContext;

// HdSwapchain manages one XrSwapchain (OpenXR handle) for a single monitor slot.
// It supports binding an AHardwareBuffer as external Vulkan memory
// (zero-copy path from AImageReader → VkImage → OpenXR compositor).
class HdSwapchain {
public:
    HdSwapchain(XrContext& ctx, uint32_t width, uint32_t height, uint32_t monitorIndex);
    ~HdSwapchain();

    // Per-frame acquire/wait/release cycle.
    bool AcquireImage(uint32_t& imageIndex);
    bool WaitImage();
    bool ReleaseImage();
    bool IsImageBound(uint32_t imageIndex) const;

    // Bind an AHardwareBuffer as the backing memory for swapchain image slot [imageIndex].
    // Called once per slot during setup (Task 7).
    void BindExternalHardwareBuffer(uint32_t imageIndex, AHardwareBuffer* ahb);

    // Software fallback path for GDI-decoded desktop frames.
    bool UploadRgbaFrame(uint32_t imageIndex, const uint8_t* rgba, size_t sizeBytes);

    // Returns the sub-image descriptor used in XrCompositionLayerQuad.
    XrSwapchainSubImage GetSubImage() const;

    XrSwapchain    GetHandle()  const { return swapchain_; }
    uint32_t       GetWidth()   const { return width_; }
    uint32_t       GetHeight()  const { return height_; }

private:
    XrContext&     ctx_;
    uint32_t       width_;
    uint32_t       height_;
    uint32_t       monitorIndex_;
    XrSwapchain    swapchain_ = XR_NULL_HANDLE;

    struct SlotMemory {
        VkImage        image      = VK_NULL_HANDLE;
        VkDeviceMemory memory     = VK_NULL_HANDLE;
    };
    std::vector<XrSwapchainImageVulkanKHR> images_;
    std::vector<SlotMemory>                slotMemory_;

    VkBuffer       stagingBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory_ = VK_NULL_HANDLE;
    void*          stagingMapped_ = nullptr;

    void CreateStagingBuffer();
    bool UploadStagingToSwapchainImage(VkImage image);
};
