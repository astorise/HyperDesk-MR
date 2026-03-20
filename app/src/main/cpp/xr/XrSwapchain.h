#pragma once

#include <openxr/openxr.h>
#include <vulkan/vulkan.h>
#include <android/hardware_buffer.h>

#include <cstdint>
#include <vector>

class XrContext;

// XrSwapchain manages one XrSwapchainKHR for a single monitor slot.
// It supports binding an AHardwareBuffer as external Vulkan memory
// (zero-copy path from AImageReader → VkImage → OpenXR compositor).
class XrSwapchain {
public:
    XrSwapchain(XrContext& ctx, uint32_t width, uint32_t height, uint32_t monitorIndex);
    ~XrSwapchain();

    // Per-frame acquire/wait/release cycle.
    bool AcquireImage(uint32_t& imageIndex);
    bool WaitImage();
    bool ReleaseImage();

    // Bind an AHardwareBuffer as the backing memory for swapchain image slot [imageIndex].
    // Called once per slot during setup (Task 7).
    void BindExternalHardwareBuffer(uint32_t imageIndex, AHardwareBuffer* ahb);

    // Returns the sub-image descriptor used in XrCompositionLayerQuad.
    XrSwapchainSubImage GetSubImage() const;

    XrSwapchainKHR GetHandle()  const { return swapchain_; }
    uint32_t       GetWidth()   const { return width_; }
    uint32_t       GetHeight()  const { return height_; }

private:
    XrContext&     ctx_;
    uint32_t       width_;
    uint32_t       height_;
    uint32_t       monitorIndex_;
    XrSwapchainKHR swapchain_ = XR_NULL_HANDLE;

    struct SlotMemory {
        VkImage        image      = VK_NULL_HANDLE;
        VkDeviceMemory memory     = VK_NULL_HANDLE;
    };
    std::vector<XrSwapchainImageVulkanKHR> images_;
    std::vector<SlotMemory>                slotMemory_;
};
