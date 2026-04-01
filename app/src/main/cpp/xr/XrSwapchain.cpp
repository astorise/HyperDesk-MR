#include "XrSwapchain.h"
#include "XrContext.h"
#include "../util/Logger.h"
#include "../util/Check.h"

#include <cstring>
#include <vulkan/vulkan_android.h>

HdSwapchain::HdSwapchain(XrContext& ctx, uint32_t width, uint32_t height, uint32_t monitorIndex)
    : ctx_(ctx), width_(width), height_(height), monitorIndex_(monitorIndex) {

    XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    info.usageFlags  = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT |
                       XR_SWAPCHAIN_USAGE_SAMPLED_BIT |
                       XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
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

    CreateStagingBuffer();
    LOGD("XrSwapchain[%u] created: %u images %ux%u", monitorIndex_, imageCount, width_, height_);
}

HdSwapchain::~HdSwapchain() {
    // Free any externally-bound device memory.
    VkDevice dev = ctx_.GetVkDevice();
    if (stagingBuffer_ != VK_NULL_HANDLE) {
        if (stagingMapped_) vkUnmapMemory(dev, stagingMemory_);
        vkDestroyBuffer(dev, stagingBuffer_, nullptr);
    }
    if (stagingMemory_ != VK_NULL_HANDLE) {
        vkFreeMemory(dev, stagingMemory_, nullptr);
    }
    for (auto& slot : slotMemory_) {
        if (slot.memory != VK_NULL_HANDLE) vkFreeMemory(dev, slot.memory, nullptr);
    }
    if (swapchain_ != XR_NULL_HANDLE) xrDestroySwapchain(swapchain_);
}

bool HdSwapchain::AcquireImage(uint32_t& imageIndex) {
    XrSwapchainImageAcquireInfo acquireInfo{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    XrResult result = xrAcquireSwapchainImage(swapchain_, &acquireInfo, &imageIndex);
    return XR_SUCCEEDED(result);
}

bool HdSwapchain::WaitImage() {
    XrSwapchainImageWaitInfo waitInfo{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    waitInfo.timeout = XR_INFINITE_DURATION;
    return XR_SUCCEEDED(xrWaitSwapchainImage(swapchain_, &waitInfo));
}

bool HdSwapchain::ReleaseImage() {
    XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    return XR_SUCCEEDED(xrReleaseSwapchainImage(swapchain_, &releaseInfo));
}

bool HdSwapchain::IsImageBound(uint32_t imageIndex) const {
    return imageIndex < slotMemory_.size() &&
           slotMemory_[imageIndex].memory != VK_NULL_HANDLE;
}

void HdSwapchain::BindExternalHardwareBuffer(uint32_t imageIndex, AHardwareBuffer* ahb) {
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

bool HdSwapchain::UploadRgbaFrame(uint32_t imageIndex, const uint8_t* rgba, size_t sizeBytes) {
    if (!rgba || imageIndex >= images_.size() || !stagingMapped_) return false;

    const size_t expectedSize = static_cast<size_t>(width_) * height_ * 4u;
    if (sizeBytes < expectedSize) {
        LOGE("XrSwapchain[%u]: software frame too small (%zu < %zu)",
             monitorIndex_, sizeBytes, expectedSize);
        return false;
    }

    std::memcpy(stagingMapped_, rgba, expectedSize);
    return UploadStagingToSwapchainImage(images_[imageIndex].image);
}

void HdSwapchain::CreateStagingBuffer() {
    VkDevice dev = ctx_.GetVkDevice();
    const VkDeviceSize bufferSize = static_cast<VkDeviceSize>(width_) * height_ * 4u;

    VkBufferCreateInfo bufInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufInfo.size = bufferSize;
    bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VK_CHECK(vkCreateBuffer(dev, &bufInfo, nullptr, &stagingBuffer_));

    VkMemoryRequirements memReqs{};
    vkGetBufferMemoryRequirements(dev, stagingBuffer_, &memReqs);

    VkPhysicalDeviceMemoryProperties memProps{};
    vkGetPhysicalDeviceMemoryProperties(ctx_.GetVkPhysDevice(), &memProps);

    uint32_t memIdx = UINT32_MAX;
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        const VkMemoryPropertyFlags required =
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        if ((memReqs.memoryTypeBits & (1u << i)) &&
            (memProps.memoryTypes[i].propertyFlags & required) == required) {
            memIdx = i;
            break;
        }
    }

    if (memIdx == UINT32_MAX) {
        LOGE("XrSwapchain[%u]: no HOST_VISIBLE staging memory type", monitorIndex_);
        return;
    }

    VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = memIdx;
    VK_CHECK(vkAllocateMemory(dev, &allocInfo, nullptr, &stagingMemory_));
    VK_CHECK(vkBindBufferMemory(dev, stagingBuffer_, stagingMemory_, 0));
    VK_CHECK(vkMapMemory(dev, stagingMemory_, 0, bufferSize, 0, &stagingMapped_));
}

bool HdSwapchain::UploadStagingToSwapchainImage(VkImage image) {
    VkDevice dev = ctx_.GetVkDevice();
    VkQueue queue = ctx_.GetVkQueue();

    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.queueFamilyIndex = ctx_.GetVkQueueFamily();
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    VkCommandPool pool = VK_NULL_HANDLE;
    VK_CHECK(vkCreateCommandPool(dev, &poolInfo, nullptr, &pool));

    VkCommandBufferAllocateInfo allocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocInfo.commandPool = pool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VK_CHECK(vkAllocateCommandBuffers(dev, &allocInfo, &cmd));

    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo));

    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.image = image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {width_, height_, 1};
    vkCmdCopyBufferToImage(cmd, stagingBuffer_, image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    VK_CHECK(vkEndCommandBuffer(cmd));

    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    VK_CHECK(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));
    vkQueueWaitIdle(queue);

    vkDestroyCommandPool(dev, pool, nullptr);
    return true;
}

XrSwapchainSubImage HdSwapchain::GetSubImage() const {
    XrSwapchainSubImage sub{};
    sub.swapchain             = swapchain_;
    sub.imageRect.offset      = {0, 0};
    sub.imageRect.extent      = {static_cast<int32_t>(width_), static_cast<int32_t>(height_)};
    sub.imageArrayIndex       = 0;
    return sub;
}
