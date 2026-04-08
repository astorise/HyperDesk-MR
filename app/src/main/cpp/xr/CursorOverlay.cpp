#include "CursorOverlay.h"
#include "XrContext.h"
#include "../util/Logger.h"
#include "../util/Check.h"

#include <cmath>
#include <cstring>

CursorOverlay::CursorOverlay(XrContext& ctx) : ctx_(ctx) {
    CreateSwapchain();
    CreateStagingBuffer();
    RenderCursorTexture();
}

CursorOverlay::~CursorOverlay() {
    VkDevice dev = ctx_.GetVkDevice();
    if (stagingBuffer_)  vkDestroyBuffer(dev, stagingBuffer_, nullptr);
    if (stagingMemory_)  vkFreeMemory(dev, stagingMemory_, nullptr);
    if (swapchain_ != XR_NULL_HANDLE) xrDestroySwapchain(swapchain_);
}

void CursorOverlay::SetPosition(int32_t desktopX, int32_t desktopY) {
    std::lock_guard lock(posMutex_);
    desktopX_ = desktopX;
    desktopY_ = desktopY;
}

const XrCompositionLayerQuad* CursorOverlay::GetCompositionLayer(
        XrSpace worldSpace,
        const XrPosef& cylinderCenter,
        float cylinderRadius,
        float centralAngle,
        float aspectRatio) {

    int32_t dx, dy;
    {
        std::lock_guard lock(posMutex_);
        dx = desktopX_;
        dy = desktopY_;
    }

    // Desktop layout: monitor 1 @ x=[0,1920), monitor 0 @ x=[1920,3840), monitor 2 @ x=[3840,5760).
    // Map to monitor index and local U coordinate [0,1].
    int monitorIdx;
    float localU;
    if (dx < 1920) {
        monitorIdx = 1;  // left
        localU = static_cast<float>(dx) / 1920.0f;
    } else if (dx < 3840) {
        monitorIdx = 0;  // center
        localU = static_cast<float>(dx - 1920) / 1920.0f;
    } else {
        monitorIdx = 2;  // right
        localU = static_cast<float>(dx - 3840) / 1920.0f;
    }
    float localV = static_cast<float>(dy) / 1080.0f;

    // Yaw angles for each monitor (matching MonitorLayout::MonitorYaw).
    constexpr float kDecagonStep = 2.0f * 3.14159265f / 10.0f;  // 36°
    float monitorYaw;
    switch (monitorIdx) {
        case 1:  monitorYaw =  kDecagonStep; break;  // left
        case 2:  monitorYaw = -kDecagonStep; break;  // right
        default: monitorYaw =  0.0f;         break;  // center
    }

    // Cursor angle on the cylinder: monitor center yaw + offset within the panel.
    // U=0 is left edge of the panel, U=1 is right edge.
    // The cylinder's central angle is kDecagonStep (36°), so U maps to [-half, +half].
    float cursorAngle = monitorYaw + (localU - 0.5f) * centralAngle;

    // 3D position on the cylinder surface, in the cylinder center's local frame.
    // Cylinder wraps around -Z axis, so angle 0 is straight ahead.
    float cx = cylinderRadius * std::sin(cursorAngle);
    float cz = -cylinderRadius * std::cos(cursorAngle);

    // Vertical position: map V to physical height.
    // Cylinder height = cylinderRadius * centralAngle / aspectRatio.
    float cylinderHeight = cylinderRadius * centralAngle / aspectRatio;
    float cy = (0.5f - localV) * cylinderHeight;

    // Transform from cylinder-local to world space using the cylinder center pose.
    // Rotate the local offset by the cylinder center's orientation.
    const auto& q = cylinderCenter.orientation;
    // Quaternion rotation of (cx, cy, cz).
    float ux = q.x, uy = q.y, uz = q.z, uw = q.w;
    float crossX = uy * cz - uz * cy;
    float crossY = uz * cx - ux * cz;
    float crossZ = ux * cy - uy * cx;
    float crossCrossX = uy * crossZ - uz * crossY;
    float crossCrossY = uz * crossX - ux * crossZ;
    float crossCrossZ = ux * crossY - uy * crossX;
    float wx = cx + 2.0f * (uw * crossX + crossCrossX);
    float wy = cy + 2.0f * (uw * crossY + crossCrossY);
    float wz = cz + 2.0f * (uw * crossZ + crossCrossZ);

    // Acquire swapchain image.
    uint32_t imageIndex = 0;
    XrSwapchainImageAcquireInfo acquireInfo{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    if (XR_FAILED(xrAcquireSwapchainImage(swapchain_, &acquireInfo, &imageIndex)))
        return nullptr;

    XrSwapchainImageWaitInfo waitInfo{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    waitInfo.timeout = XR_INFINITE_DURATION;
    if (XR_FAILED(xrWaitSwapchainImage(swapchain_, &waitInfo))) return nullptr;

    if (!textureUploaded_) {
        UploadToSwapchainImage(swapchainImages_[imageIndex]);
        textureUploaded_ = true;
    }

    // Build quad layer.
    compositionLayer_                = XrCompositionLayerQuad{XR_TYPE_COMPOSITION_LAYER_QUAD};
    compositionLayer_.layerFlags     = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
    compositionLayer_.space          = worldSpace;
    compositionLayer_.eyeVisibility  = XR_EYE_VISIBILITY_BOTH;

    XrSwapchainSubImage sub{};
    sub.swapchain         = swapchain_;
    sub.imageRect.offset  = {0, 0};
    sub.imageRect.extent  = {static_cast<int32_t>(kTexSize), static_cast<int32_t>(kTexSize)};
    sub.imageArrayIndex   = 0;
    compositionLayer_.subImage = sub;

    compositionLayer_.pose.position    = {
        cylinderCenter.position.x + wx,
        cylinderCenter.position.y + wy,
        cylinderCenter.position.z + wz
    };

    // Orient the cursor quad to face the cylinder center (billboard toward viewer).
    // The face normal should point from the surface back toward the cylinder center.
    float faceYaw = std::atan2(-wx, -wz);
    float halfYaw = faceYaw * 0.5f;
    compositionLayer_.pose.orientation = {0.0f, std::sin(halfYaw), 0.0f, std::cos(halfYaw)};

    compositionLayer_.size = {kCursorSize, kCursorSize};

    XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    xrReleaseSwapchainImage(swapchain_, &releaseInfo);

    return &compositionLayer_;
}

// ── Private ────────────────────────────────────────────────────────────────

void CursorOverlay::CreateSwapchain() {
    XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    info.usageFlags  = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT |
                       XR_SWAPCHAIN_USAGE_SAMPLED_BIT |
                       XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
    info.format      = VK_FORMAT_R8G8B8A8_UNORM;
    info.sampleCount = 1;
    info.width       = kTexSize;
    info.height      = kTexSize;
    info.faceCount   = 1;
    info.arraySize   = 1;
    info.mipCount    = 1;
    XR_CHECK(xrCreateSwapchain(ctx_.GetSession(), &info, &swapchain_));

    uint32_t imageCount = 0;
    XR_CHECK(xrEnumerateSwapchainImages(swapchain_, 0, &imageCount, nullptr));
    std::vector<XrSwapchainImageVulkanKHR> images(imageCount, {XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR});
    XR_CHECK(xrEnumerateSwapchainImages(
        swapchain_, imageCount, &imageCount,
        reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data())));

    swapchainImages_.resize(imageCount);
    for (uint32_t i = 0; i < imageCount; ++i)
        swapchainImages_[i] = images[i].image;

    LOGD("CursorOverlay: swapchain created with %u images", imageCount);
}

void CursorOverlay::CreateStagingBuffer() {
    VkDevice dev = ctx_.GetVkDevice();
    VkDeviceSize bufferSize = kTexSize * kTexSize * 4;

    VkBufferCreateInfo bufInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufInfo.size  = bufferSize;
    bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VK_CHECK(vkCreateBuffer(dev, &bufInfo, nullptr, &stagingBuffer_));

    VkMemoryRequirements memReqs{};
    vkGetBufferMemoryRequirements(dev, stagingBuffer_, &memReqs);

    VkPhysicalDeviceMemoryProperties memProps{};
    vkGetPhysicalDeviceMemoryProperties(ctx_.GetVkPhysDevice(), &memProps);

    uint32_t memIdx = UINT32_MAX;
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((memReqs.memoryTypeBits & (1u << i)) &&
            (memProps.memoryTypes[i].propertyFlags &
             (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
             (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
            memIdx = i;
            break;
        }
    }

    VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocInfo.allocationSize  = memReqs.size;
    allocInfo.memoryTypeIndex = memIdx;
    VK_CHECK(vkAllocateMemory(dev, &allocInfo, nullptr, &stagingMemory_));
    VK_CHECK(vkBindBufferMemory(dev, stagingBuffer_, stagingMemory_, 0));
    VK_CHECK(vkMapMemory(dev, stagingMemory_, 0, bufferSize, 0, &stagingMapped_));
}

void CursorOverlay::RenderCursorTexture() {
    auto* rgba = static_cast<uint8_t*>(stagingMapped_);
    const float center = kTexSize * 0.5f;
    const float outerR = kTexSize * 0.5f;
    const float innerR = outerR - 2.0f;

    for (uint32_t y = 0; y < kTexSize; ++y) {
        for (uint32_t x = 0; x < kTexSize; ++x) {
            float dx = static_cast<float>(x) + 0.5f - center;
            float dy = static_cast<float>(y) + 0.5f - center;
            float dist = std::sqrt(dx * dx + dy * dy);
            uint8_t* px = &rgba[(y * kTexSize + x) * 4];

            if (dist <= innerR) {
                // White filled center.
                px[0] = 255; px[1] = 255; px[2] = 255; px[3] = 240;
            } else if (dist <= outerR) {
                // Anti-aliased edge.
                float alpha = 1.0f - (dist - innerR) / (outerR - innerR);
                px[0] = 255; px[1] = 255; px[2] = 255;
                px[3] = static_cast<uint8_t>(alpha * 240.0f);
            } else {
                px[0] = 0; px[1] = 0; px[2] = 0; px[3] = 0;
            }
        }
    }
}

void CursorOverlay::UploadToSwapchainImage(VkImage image) {
    VkDevice dev   = ctx_.GetVkDevice();
    VkQueue  queue = ctx_.GetVkQueue();

    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.queueFamilyIndex = ctx_.GetVkQueueFamily();
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    VkCommandPool pool = VK_NULL_HANDLE;
    VK_CHECK(vkCreateCommandPool(dev, &poolInfo, nullptr, &pool));

    VkCommandBufferAllocateInfo cmdInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cmdInfo.commandPool        = pool;
    cmdInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdInfo.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VK_CHECK(vkAllocateCommandBuffers(dev, &cmdInfo, &cmd));

    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo));

    // Transition image to TRANSFER_DST.
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image               = image;
    barrier.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    barrier.srcAccessMask       = 0;
    barrier.dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &barrier);

    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent      = {kTexSize, kTexSize, 1};
    vkCmdCopyBufferToImage(cmd, stagingBuffer_, image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    // Transition back to COLOR_ATTACHMENT_OPTIMAL for sampling.
    barrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &barrier);

    VK_CHECK(vkEndCommandBuffer(cmd));

    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers    = &cmd;
    VK_CHECK(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));
    VK_CHECK(vkQueueWaitIdle(queue));

    vkDestroyCommandPool(dev, pool, nullptr);
}
