#include "XrSwapchain.h"
#include "XrContext.h"
#include "../util/Logger.h"
#include "../util/Check.h"

#include <vulkan/vulkan_android.h>

XrSwapchain::XrSwapchain(XrContext& ctx, uint32_t width, uint32_t height, uint32_t monitorIndex)
    : ctx_(ctx), width_(width), height_(height), monitorIndex_(monitorIndex) {

    XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    info.usageFlags  = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT |
                       XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    info.format      = VK_FORMAT_R8G8B8A8_UNORM;
    info.sampleCount = 1;
    info.width       = width_;
    info.height      = height_;
    info.faceCount   = 1;
    info.arraySize   = 1;
    info.mipCount    = 1;
    XR_CHECK(xrCreateSwapchain(ctx_.GetSession(), &info, &swapchain_));

    // Enumerate swapchain images.
    uint32_t imageCount = 0;
    XR_CHECK(xrEnumerateSwapchainImages(swapchain_, 0, &imageCount, nullptr));
    images_.resize(imageCount, {XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR});
    slotMemory_.resize(imageCount);
    XR_CHECK(xrEnumerateSwapchainImages(
        swapchain_, imageCount, &imageCount,
        reinterpret_cast<XrSwapchainImageBaseHeader*>(images_.data())));

    LOGD("XrSwapchain[%u] created: %u images %ux%u", monitorIndex_, imageCount, width_, height_);
}

XrSwapchain::~XrSwapchain() {
    // Free any externally-bound device memory.
    VkDevice dev = ctx_.GetVkDevice();
    for (auto& slot : slotMemory_) {
        if (slot.memory != VK_NULL_HANDLE) vkFreeMemory(dev, slot.memory, nullptr);
    }
    if (swapchain_ != XR_NULL_HANDLE) xrDestroySwapchain(swapchain_);
}

bool XrSwapchain::AcquireImage(uint32_t& imageIndex) {
    XrSwapchainImageAcquireInfo acquireInfo{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    XrResult result = xrAcquireSwapchainImage(swapchain_, &acquireInfo, &imageIndex);
    return XR_SUCCEEDED(result);
}

bool XrSwapchain::WaitImage() {
    XrSwapchainImageWaitInfo waitInfo{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    waitInfo.timeout = XR_INFINITE_DURATION;
    return XR_SUCCEEDED(xrWaitSwapchainImage(swapchain_, &waitInfo));
}

bool XrSwapchain::ReleaseImage() {
    XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    return XR_SUCCEEDED(xrReleaseSwapchainImage(swapchain_, &releaseInfo));
}

void XrSwapchain::BindExternalHardwareBuffer(uint32_t imageIndex, AHardwareBuffer* ahb) {
    VkDevice         dev   = ctx_.GetVkDevice();
    VkPhysicalDevice phys  = ctx_.GetVkPhysDevice();
    VkImage          image = images_[imageIndex].image;

    // Query AHardwareBuffer memory properties.
    VkAndroidHardwareBufferPropertiesANDROID ahbProps{
        VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID};
    VkAndroidHardwareBufferFormatPropertiesANDROID ahbFmtProps{
        VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_ANDROID};
    ahbProps.pNext = &ahbFmtProps;

    auto pfnGetAhbProps = reinterpret_cast<PFN_vkGetAndroidHardwareBufferPropertiesANDROID>(
        vkGetDeviceProcAddr(dev, "vkGetAndroidHardwareBufferPropertiesANDROID"));
    VK_CHECK(pfnGetAhbProps(dev, ahb, &ahbProps));

    // Find a memory type compatible with the AHardwareBuffer.
    VkPhysicalDeviceMemoryProperties memProps{};
    vkGetPhysicalDeviceMemoryProperties(phys, &memProps);

    uint32_t memTypeIndex = UINT32_MAX;
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((ahbProps.memoryTypeBits & (1u << i)) &&
            (memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            memTypeIndex = i;
            break;
        }
    }
    if (memTypeIndex == UINT32_MAX) {
        LOGE("XrSwapchain[%u]: no suitable memory type for AHardwareBuffer", monitorIndex_);
        return;
    }

    // Import the AHardwareBuffer as Vulkan external memory.
    VkImportAndroidHardwareBufferInfoANDROID importInfo{
        VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID};
    importInfo.buffer = ahb;

    VkMemoryDedicatedAllocateInfo dedicatedInfo{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};
    dedicatedInfo.pNext  = &importInfo;
    dedicatedInfo.image  = image;

    VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocInfo.pNext           = &dedicatedInfo;
    allocInfo.allocationSize  = ahbProps.allocationSize;
    allocInfo.memoryTypeIndex = memTypeIndex;

    VkDeviceMemory mem = VK_NULL_HANDLE;
    VK_CHECK(vkAllocateMemory(dev, &allocInfo, nullptr, &mem));
    VK_CHECK(vkBindImageMemory(dev, image, mem, 0));

    // Free previous memory if any (re-bind case).
    if (slotMemory_[imageIndex].memory != VK_NULL_HANDLE) {
        vkFreeMemory(dev, slotMemory_[imageIndex].memory, nullptr);
    }
    slotMemory_[imageIndex] = {image, mem};
    LOGD("XrSwapchain[%u] slot %u bound to AHardwareBuffer", monitorIndex_, imageIndex);
}

XrSwapchainSubImage XrSwapchain::GetSubImage() const {
    XrSwapchainSubImage sub{};
    sub.swapchain             = swapchain_;
    sub.imageRect.offset      = {0, 0};
    sub.imageRect.extent      = {static_cast<int32_t>(width_), static_cast<int32_t>(height_)};
    sub.imageArrayIndex       = 0;
    return sub;
}
