#include "CursorOverlay.h"
#include "XrContext.h"
#include "../scene/MonitorLayout.h"
#include "../util/Logger.h"
#include "../util/Check.h"

#include <android/imagedecoder.h>

#include <algorithm>
#include <cmath>
#include <cstring>

CursorOverlay::CursorOverlay(XrContext& ctx, AAssetManager* assetManager)
    : ctx_(ctx) {
    if (!LoadPngFromAssets(assetManager, "cursor.png")) {
        LOGW("CursorOverlay: PNG load failed, using fallback cursor");
        GenerateFallbackCursor();
    }
    CreateSwapchain();
    CreateStagingBuffer();
    UploadToAllSwapchainImages();
    LOGI("CursorOverlay: init complete (%ux%u, %zu images)",
         texWidth_, texHeight_, swapchainImages_.size());
}

CursorOverlay::~CursorOverlay() {
    VkDevice dev = ctx_.GetVkDevice();
    if (stagingBuffer_)  vkDestroyBuffer(dev, stagingBuffer_, nullptr);
    if (stagingMemory_)  vkFreeMemory(dev, stagingMemory_, nullptr);
    if (swapchain_ != XR_NULL_HANDLE) xrDestroySwapchain(swapchain_);
}

bool CursorOverlay::LoadPngFromAssets(AAssetManager* mgr, const char* path) {
    AAsset* asset = AAssetManager_open(mgr, path, AASSET_MODE_BUFFER);
    if (!asset) {
        LOGE("CursorOverlay: asset '%s' not found", path);
        return false;
    }

    AImageDecoder* decoder = nullptr;
    int result = AImageDecoder_createFromAAsset(asset, &decoder);
    if (result != ANDROID_IMAGE_DECODER_SUCCESS || !decoder) {
        LOGE("CursorOverlay: AImageDecoder_createFromAAsset failed: %d", result);
        AAsset_close(asset);
        return false;
    }

    // Force RGBA_8888 output.
    AImageDecoder_setAndroidBitmapFormat(decoder, ANDROID_BITMAP_FORMAT_RGBA_8888);

    const AImageDecoderHeaderInfo* info = AImageDecoder_getHeaderInfo(decoder);
    texWidth_  = AImageDecoderHeaderInfo_getWidth(info);
    texHeight_ = AImageDecoderHeaderInfo_getHeight(info);
    const size_t stride = AImageDecoder_getMinimumStride(decoder);

    LOGI("CursorOverlay: loaded %s (%ux%u, stride=%zu)", path, texWidth_, texHeight_, stride);

    // Decode into a temporary buffer — will be copied to staging later.
    std::vector<uint8_t> pixels(stride * texHeight_);
    result = AImageDecoder_decodeImage(decoder, pixels.data(), stride, pixels.size());
    AImageDecoder_delete(decoder);
    AAsset_close(asset);

    if (result != ANDROID_IMAGE_DECODER_SUCCESS) {
        LOGE("CursorOverlay: AImageDecoder_decodeImage failed: %d", result);
        return false;
    }

    decodedPixels_ = std::move(pixels);
    decodedStride_ = stride;
    return true;
}

void CursorOverlay::GenerateFallbackCursor() {
    // 24x24 white arrow cursor with alpha.
    texWidth_  = 24;
    texHeight_ = 24;
    decodedStride_ = texWidth_ * 4;
    decodedPixels_.resize(decodedStride_ * texHeight_, 0);

    // Draw a simple arrow shape (filled white triangle with black outline).
    // Arrow points top-left, body goes down-right.
    auto setPixel = [&](int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
        if (x >= 0 && x < (int)texWidth_ && y >= 0 && y < (int)texHeight_) {
            size_t off = y * decodedStride_ + x * 4;
            decodedPixels_[off + 0] = r;
            decodedPixels_[off + 1] = g;
            decodedPixels_[off + 2] = b;
            decodedPixels_[off + 3] = a;
        }
    };

    // Classic arrow cursor: triangle widening from top-left.
    // arrowRows[y] = number of filled pixels on row y.
    static constexpr int kRows = 20;
    static constexpr int arrowWidth[kRows] = {
        1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 5, 4, 3, 2, 1, 0, 0, 0
    };
    for (int y = 0; y < kRows; ++y) {
        int w = arrowWidth[y];
        for (int x = 0; x < w; ++x) {
            if (x == 0 || x == w - 1 || y == 0)
                setPixel(x, y, 0, 0, 0, 255);      // black outline
            else
                setPixel(x, y, 255, 255, 255, 255); // white fill
        }
    }
    LOGI("CursorOverlay: generated fallback cursor %ux%u", texWidth_, texHeight_);
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
        float aspectRatio,
        bool splitRows,
        float scrollYaw) {

    if (texWidth_ == 0 || texHeight_ == 0) return nullptr;

    int32_t dx, dy;
    {
        std::lock_guard lock(posMutex_);
        dx = desktopX_;
        dy = desktopY_;
    }

    // Sequential desktop layout: monitor i @ x=[i*1920, (i+1)*1920).
    const int32_t clampedX =
        std::max<int32_t>(0, std::min<int32_t>(dx, static_cast<int32_t>(1920 * MonitorLayout::kMaxMonitors - 1)));
    const uint32_t monitorIdx = static_cast<uint32_t>(
        std::min<int32_t>(clampedX / 1920, static_cast<int32_t>(MonitorLayout::kMaxMonitors - 1)));
    const float localU = static_cast<float>(clampedX - static_cast<int32_t>(monitorIdx) * 1920) / 1920.0f;
    float localV = static_cast<float>(dy) / 1080.0f;

    // Yaw angles matching MonitorLayout::MonitorYaw (sequential to the right),
    // plus the carousel scroll offset so the cursor tracks the scrolled wall.
    constexpr float kArcStep = MonitorLayout::kAngularStepRadians;
    float monitorYaw;
    if (splitRows) {
        monitorYaw = -static_cast<float>(monitorIdx / 2u) * kArcStep;
    } else {
        monitorYaw = -static_cast<float>(monitorIdx) * kArcStep;
    }

    // Monitor i's world orientation = anchorOrient * YawQuat(-i*step + scrollYaw).
    // Rotating local -Z by YawQuat(θ) yields (-sin θ, 0, -cos θ), so monitor i's
    // visible center has world yaw = -(-i*step + scrollYaw) = i*step - scrollYaw.
    // The cursor quad uses cx = r*sin(α), cz = -r*cos(α) — this places it at
    // world yaw +α — so we need α = i*step - scrollYaw + (localU - 0.5)*step.
    float cursorAngle = -monitorYaw - scrollYaw + (localU - 0.5f) * centralAngle;

    // 3D position on cylinder surface, pulled 2cm toward viewer.
    constexpr float kZOffset = 0.02f;
    float r = cylinderRadius - kZOffset;
    float cx = r * std::sin(cursorAngle);
    float cz = -r * std::cos(cursorAngle);

    float cylinderHeight = cylinderRadius * centralAngle / aspectRatio;
    float cy = (0.5f - localV) * cylinderHeight;
    if (splitRows) {
        constexpr float kSplitRowOffsetY = 0.60f;
        const bool topRow = (monitorIdx % 2u) == 0u;
        cy += topRow ? +kSplitRowOffsetY : -kSplitRowOffsetY;
    }

    // Rotate by cylinder center orientation.
    const auto& q = cylinderCenter.orientation;
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

    // Build quad layer.
    compositionLayer_                = XrCompositionLayerQuad{XR_TYPE_COMPOSITION_LAYER_QUAD};
    compositionLayer_.layerFlags     = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
    compositionLayer_.space          = worldSpace;
    compositionLayer_.eyeVisibility  = XR_EYE_VISIBILITY_BOTH;

    XrSwapchainSubImage sub{};
    sub.swapchain         = swapchain_;
    sub.imageRect.offset  = {0, 0};
    sub.imageRect.extent  = {static_cast<int32_t>(texWidth_), static_cast<int32_t>(texHeight_)};
    sub.imageArrayIndex   = 0;
    compositionLayer_.subImage = sub;

    // Orient to face the cylinder center.
    float faceYaw = std::atan2(-wx, -wz);
    float halfYaw = faceYaw * 0.5f;
    compositionLayer_.pose.orientation = {0.0f, std::sin(halfYaw), 0.0f, std::cos(halfYaw)};

    // Hotspot is at pixel (18, 5) on the 24x24 icon, i.e. offset (+6, -7)
    // from the image center.  Shift the quad so that pixel aligns with the
    // cursor 3D position.  In 3D local-quad space the hotspot offset is:
    //   localX = +6/24 * kCursorSize  (right of center)
    //   localY = +7/24 * kCursorSize  (above center — image Y is flipped)
    // Move the quad in the opposite direction to compensate.
    constexpr float kHotspotLocalX = (6.0f / 24.0f) * kCursorSize;
    constexpr float kHotspotLocalY = -(7.0f / 24.0f) * kCursorSize;
    float cosYaw = std::cos(faceYaw);
    float sinYaw = std::sin(faceYaw);
    // Local right in world = (cosYaw, 0, sinYaw)
    compositionLayer_.pose.position    = {
        cylinderCenter.position.x + wx + kHotspotLocalX * cosYaw,
        cylinderCenter.position.y + wy + kHotspotLocalY,
        cylinderCenter.position.z + wz + kHotspotLocalX * sinYaw
    };

    compositionLayer_.size = {kCursorSize, kCursorSize};

    XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    xrReleaseSwapchainImage(swapchain_, &releaseInfo);

    static int logCounter = 0;
    if (++logCounter % 72 == 0) {
        LOGI("CursorOverlay: desktop=(%d,%d) mon=%u localU=%.3f localV=%.3f scroll=%.1f° monYaw=%.1f° cursorAngle=%.1f° anchor=(%.2f,%.2f,%.2f) quadPos=(%.2f,%.2f,%.2f)",
             dx, dy, monitorIdx, localU, localV,
             scrollYaw * 180.0f / 3.14159265f,
             monitorYaw * 180.0f / 3.14159265f,
             cursorAngle * 180.0f / 3.14159265f,
             cylinderCenter.position.x, cylinderCenter.position.y, cylinderCenter.position.z,
             compositionLayer_.pose.position.x,
             compositionLayer_.pose.position.y,
             compositionLayer_.pose.position.z);
    }

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
    info.width       = texWidth_;
    info.height      = texHeight_;
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

    LOGD("CursorOverlay: swapchain %ux%u with %u images", texWidth_, texHeight_, imageCount);
}

void CursorOverlay::CreateStagingBuffer() {
    VkDevice dev = ctx_.GetVkDevice();
    VkDeviceSize bufferSize = texWidth_ * texHeight_ * 4;

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

    // Copy decoded PNG pixels to staging buffer.
    if (!decodedPixels_.empty()) {
        auto* dst = static_cast<uint8_t*>(stagingMapped_);
        const uint32_t dstStride = texWidth_ * 4;
        for (uint32_t row = 0; row < texHeight_; ++row) {
            memcpy(dst + row * dstStride,
                   decodedPixels_.data() + row * decodedStride_,
                   dstStride);
        }
        // Free decoded pixels — no longer needed.
        decodedPixels_.clear();
        decodedPixels_.shrink_to_fit();
    }
}

void CursorOverlay::UploadToAllSwapchainImages() {
    for (size_t i = 0; i < swapchainImages_.size(); ++i) {
        uint32_t imageIndex = 0;
        XrSwapchainImageAcquireInfo acquireInfo{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
        XR_CHECK(xrAcquireSwapchainImage(swapchain_, &acquireInfo, &imageIndex));

        XrSwapchainImageWaitInfo waitInfo{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
        waitInfo.timeout = XR_INFINITE_DURATION;
        XR_CHECK(xrWaitSwapchainImage(swapchain_, &waitInfo));

        UploadToSwapchainImage(swapchainImages_[imageIndex]);

        XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        XR_CHECK(xrReleaseSwapchainImage(swapchain_, &releaseInfo));

        LOGD("CursorOverlay: uploaded texture to swapchain image %u", imageIndex);
    }
    textureUploaded_ = true;
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
    region.imageExtent      = {texWidth_, texHeight_, 1};
    vkCmdCopyBufferToImage(cmd, stagingBuffer_, image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

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
