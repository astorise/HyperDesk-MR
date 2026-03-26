#include "StatusOverlay.h"
#include "XrContext.h"
#include "../util/Logger.h"
#include "../util/Check.h"

#include <algorithm>
#include <cstring>

// ── Simple 5×7 bitmap font ──────────────────────────────────────────────────
// Each glyph is 5 pixels wide, 7 pixels tall, stored as 7 uint8_t rows
// where bits 4..0 represent pixels left-to-right.
// Covers printable ASCII 0x20–0x7E (space through tilde).
namespace {

// clang-format off
constexpr uint8_t kFont5x7[][7] = {
    // 0x20 SPACE
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    // ! 0x21
    {0x04,0x04,0x04,0x04,0x04,0x00,0x04},
    // " 0x22
    {0x0A,0x0A,0x00,0x00,0x00,0x00,0x00},
    // # 0x23
    {0x0A,0x1F,0x0A,0x0A,0x1F,0x0A,0x00},
    // $ 0x24
    {0x04,0x0F,0x14,0x0E,0x05,0x1E,0x04},
    // % 0x25
    {0x18,0x19,0x02,0x04,0x08,0x13,0x03},
    // & 0x26
    {0x08,0x14,0x14,0x08,0x15,0x12,0x0D},
    // ' 0x27
    {0x04,0x04,0x00,0x00,0x00,0x00,0x00},
    // ( 0x28
    {0x02,0x04,0x08,0x08,0x08,0x04,0x02},
    // ) 0x29
    {0x08,0x04,0x02,0x02,0x02,0x04,0x08},
    // * 0x2A
    {0x00,0x04,0x15,0x0E,0x15,0x04,0x00},
    // + 0x2B
    {0x00,0x04,0x04,0x1F,0x04,0x04,0x00},
    // , 0x2C
    {0x00,0x00,0x00,0x00,0x00,0x04,0x08},
    // - 0x2D
    {0x00,0x00,0x00,0x1F,0x00,0x00,0x00},
    // . 0x2E
    {0x00,0x00,0x00,0x00,0x00,0x00,0x04},
    // / 0x2F
    {0x01,0x02,0x02,0x04,0x08,0x08,0x10},
    // 0
    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E},
    // 1
    {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
    // 2
    {0x0E,0x11,0x01,0x06,0x08,0x10,0x1F},
    // 3
    {0x0E,0x11,0x01,0x06,0x01,0x11,0x0E},
    // 4
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},
    // 5
    {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},
    // 6
    {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E},
    // 7
    {0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
    // 8
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},
    // 9
    {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C},
    // : 0x3A
    {0x00,0x00,0x04,0x00,0x04,0x00,0x00},
    // ; 0x3B
    {0x00,0x00,0x04,0x00,0x04,0x04,0x08},
    // < 0x3C
    {0x02,0x04,0x08,0x10,0x08,0x04,0x02},
    // = 0x3D
    {0x00,0x00,0x1F,0x00,0x1F,0x00,0x00},
    // > 0x3E
    {0x08,0x04,0x02,0x01,0x02,0x04,0x08},
    // ? 0x3F
    {0x0E,0x11,0x01,0x02,0x04,0x00,0x04},
    // @ 0x40
    {0x0E,0x11,0x17,0x15,0x17,0x10,0x0E},
    // A
    {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11},
    // B
    {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},
    // C
    {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},
    // D
    {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E},
    // E
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},
    // F
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
    // G
    {0x0E,0x11,0x10,0x17,0x11,0x11,0x0E},
    // H
    {0x11,0x11,0x11,0x1F,0x11,0x11,0x11},
    // I
    {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E},
    // J
    {0x07,0x02,0x02,0x02,0x02,0x12,0x0C},
    // K
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11},
    // L
    {0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
    // M
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11},
    // N
    {0x11,0x19,0x15,0x13,0x11,0x11,0x11},
    // O
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},
    // P
    {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},
    // Q
    {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D},
    // R
    {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
    // S
    {0x0E,0x11,0x10,0x0E,0x01,0x11,0x0E},
    // T
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x04},
    // U
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0E},
    // V
    {0x11,0x11,0x11,0x11,0x0A,0x0A,0x04},
    // W
    {0x11,0x11,0x11,0x15,0x15,0x1B,0x11},
    // X
    {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
    // Y
    {0x11,0x11,0x0A,0x04,0x04,0x04,0x04},
    // Z
    {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F},
    // [ 0x5B
    {0x0E,0x08,0x08,0x08,0x08,0x08,0x0E},
    // backslash 0x5C
    {0x10,0x08,0x08,0x04,0x02,0x02,0x01},
    // ] 0x5D
    {0x0E,0x02,0x02,0x02,0x02,0x02,0x0E},
    // ^ 0x5E
    {0x04,0x0A,0x11,0x00,0x00,0x00,0x00},
    // _ 0x5F
    {0x00,0x00,0x00,0x00,0x00,0x00,0x1F},
    // ` 0x60
    {0x08,0x04,0x00,0x00,0x00,0x00,0x00},
    // a
    {0x00,0x00,0x0E,0x01,0x0F,0x11,0x0F},
    // b
    {0x10,0x10,0x1E,0x11,0x11,0x11,0x1E},
    // c
    {0x00,0x00,0x0E,0x11,0x10,0x11,0x0E},
    // d
    {0x01,0x01,0x0F,0x11,0x11,0x11,0x0F},
    // e
    {0x00,0x00,0x0E,0x11,0x1F,0x10,0x0E},
    // f
    {0x06,0x08,0x1C,0x08,0x08,0x08,0x08},
    // g
    {0x00,0x00,0x0F,0x11,0x0F,0x01,0x0E},
    // h
    {0x10,0x10,0x1E,0x11,0x11,0x11,0x11},
    // i
    {0x04,0x00,0x0C,0x04,0x04,0x04,0x0E},
    // j
    {0x02,0x00,0x06,0x02,0x02,0x12,0x0C},
    // k
    {0x10,0x10,0x12,0x14,0x18,0x14,0x12},
    // l
    {0x0C,0x04,0x04,0x04,0x04,0x04,0x0E},
    // m
    {0x00,0x00,0x1A,0x15,0x15,0x11,0x11},
    // n
    {0x00,0x00,0x1E,0x11,0x11,0x11,0x11},
    // o
    {0x00,0x00,0x0E,0x11,0x11,0x11,0x0E},
    // p
    {0x00,0x00,0x1E,0x11,0x1E,0x10,0x10},
    // q
    {0x00,0x00,0x0F,0x11,0x0F,0x01,0x01},
    // r
    {0x00,0x00,0x16,0x19,0x10,0x10,0x10},
    // s
    {0x00,0x00,0x0F,0x10,0x0E,0x01,0x1E},
    // t
    {0x08,0x08,0x1C,0x08,0x08,0x09,0x06},
    // u
    {0x00,0x00,0x11,0x11,0x11,0x11,0x0F},
    // v
    {0x00,0x00,0x11,0x11,0x11,0x0A,0x04},
    // w
    {0x00,0x00,0x11,0x11,0x15,0x15,0x0A},
    // x
    {0x00,0x00,0x11,0x0A,0x04,0x0A,0x11},
    // y
    {0x00,0x00,0x11,0x11,0x0F,0x01,0x0E},
    // z
    {0x00,0x00,0x1F,0x02,0x04,0x08,0x1F},
    // { 0x7B
    {0x02,0x04,0x04,0x08,0x04,0x04,0x02},
    // | 0x7C
    {0x04,0x04,0x04,0x04,0x04,0x04,0x04},
    // } 0x7D
    {0x08,0x04,0x04,0x02,0x04,0x04,0x08},
    // ~ 0x7E
    {0x00,0x00,0x08,0x15,0x02,0x00,0x00},
};
// clang-format on

constexpr int kGlyphW       = 5;
constexpr int kGlyphH       = 7;
constexpr int kGlyphSpacing = 1;  // 1px between glyphs
constexpr int kScale        = 3;  // render each font pixel as 3×3 screen pixels

void DrawGlyph(uint8_t* rgba, uint32_t texW, uint32_t texH,
               int ox, int oy, char ch,
               uint8_t r, uint8_t g, uint8_t b) {
    int idx = static_cast<int>(ch) - 0x20;
    if (idx < 0 || idx >= static_cast<int>(std::size(kFont5x7))) return;

    const uint8_t* glyph = kFont5x7[idx];
    for (int gy = 0; gy < kGlyphH; ++gy) {
        uint8_t row = glyph[gy];
        for (int gx = 0; gx < kGlyphW; ++gx) {
            if (!(row & (1 << (kGlyphW - 1 - gx)))) continue;
            // Draw a kScale × kScale block.
            for (int sy = 0; sy < kScale; ++sy) {
                for (int sx = 0; sx < kScale; ++sx) {
                    int px = ox + gx * kScale + sx;
                    int py = oy + gy * kScale + sy;
                    if (px < 0 || px >= static_cast<int>(texW) ||
                        py < 0 || py >= static_cast<int>(texH)) continue;
                    uint8_t* p = rgba + (py * texW + px) * 4;
                    p[0] = r; p[1] = g; p[2] = b; p[3] = 255;
                }
            }
        }
    }
}

} // anonymous namespace

// ── Constructor / Destructor ────────────────────────────────────────────────

StatusOverlay::StatusOverlay(XrContext& ctx, uint32_t texWidth, uint32_t texHeight)
    : ctx_(ctx), texWidth_(texWidth), texHeight_(texHeight) {
    CreateSwapchain();
    CreateStagingBuffer();
    LOGI("StatusOverlay: created (%ux%u)", texWidth_, texHeight_);
}

StatusOverlay::~StatusOverlay() {
    VkDevice dev = ctx_.GetVkDevice();
    if (stagingBuffer_ != VK_NULL_HANDLE) {
        if (stagingMapped_) vkUnmapMemory(dev, stagingMemory_);
        vkDestroyBuffer(dev, stagingBuffer_, nullptr);
    }
    if (stagingMemory_ != VK_NULL_HANDLE) vkFreeMemory(dev, stagingMemory_, nullptr);
    if (swapchain_ != XR_NULL_HANDLE)     xrDestroySwapchain(swapchain_);
}

// ── SetMessage ──────────────────────────────────────────────────────────────

void StatusOverlay::SetMessage(const std::string& message) {
    std::lock_guard lock(messageMutex_);
    if (message == currentMessage_) return;
    currentMessage_ = message;
    messageDirty_   = true;
    visible_.store(!message.empty());
}

// ── GetCompositionLayer ─────────────────────────────────────────────────────

const XrCompositionLayerQuad* StatusOverlay::GetCompositionLayer(XrSpace worldSpace) {
    if (!visible_.load()) return nullptr;

    std::string msg;
    bool dirty = false;
    {
        std::lock_guard lock(messageMutex_);
        dirty = messageDirty_;
        if (dirty) {
            msg = currentMessage_;
            messageDirty_ = false;
        }
    }

    // Acquire swapchain image.
    uint32_t imageIndex = 0;
    XrSwapchainImageAcquireInfo acquireInfo{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    if (XR_FAILED(xrAcquireSwapchainImage(swapchain_, &acquireInfo, &imageIndex)))
        return nullptr;

    XrSwapchainImageWaitInfo waitInfo{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    waitInfo.timeout = XR_INFINITE_DURATION;
    if (XR_FAILED(xrWaitSwapchainImage(swapchain_, &waitInfo))) return nullptr;

    if (dirty) {
        RenderTextToStaging(msg);
        UploadStagingToSwapchainImage(swapchainImages_[imageIndex]);
    }

    // Populate composition layer — centred in front of the user at z = -1.5m.
    compositionLayer_                = XrCompositionLayerQuad{XR_TYPE_COMPOSITION_LAYER_QUAD};
    compositionLayer_.layerFlags     = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
    compositionLayer_.space          = worldSpace;
    compositionLayer_.eyeVisibility  = XR_EYE_VISIBILITY_BOTH;

    XrSwapchainSubImage sub{};
    sub.swapchain             = swapchain_;
    sub.imageRect.offset      = {0, 0};
    sub.imageRect.extent      = {static_cast<int32_t>(texWidth_),
                                 static_cast<int32_t>(texHeight_)};
    sub.imageArrayIndex       = 0;
    compositionLayer_.subImage = sub;

    // Place quad at eye height (~1.5m), 1.5m in front, facing the user.
    compositionLayer_.pose.orientation = {0.f, 0.f, 0.f, 1.f};
    compositionLayer_.pose.position   = {0.f, 1.2f, -1.5f};
    compositionLayer_.size            = {0.6f, 0.15f};

    XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    xrReleaseSwapchainImage(swapchain_, &releaseInfo);

    return &compositionLayer_;
}

// ── Private ─────────────────────────────────────────────────────────────────

void StatusOverlay::CreateSwapchain() {
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

    LOGD("StatusOverlay: swapchain created with %u images", imageCount);
}

void StatusOverlay::CreateStagingBuffer() {
    VkDevice dev = ctx_.GetVkDevice();
    VkDeviceSize bufferSize = texWidth_ * texHeight_ * 4; // RGBA

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

void StatusOverlay::RenderTextToStaging(const std::string& message) {
    auto* rgba = static_cast<uint8_t*>(stagingMapped_);
    size_t totalBytes = texWidth_ * texHeight_ * 4;

    // Clear to semi-transparent dark background (RGBA).
    for (size_t i = 0; i < totalBytes; i += 4) {
        rgba[i + 0] = 20;   // R
        rgba[i + 1] = 20;   // G
        rgba[i + 2] = 30;   // B
        rgba[i + 3] = 180;  // A
    }

    // Centre the text horizontally and vertically.
    int charPixelW = kGlyphW * kScale + kGlyphSpacing * kScale;
    int textW      = static_cast<int>(message.size()) * charPixelW;
    int textH      = kGlyphH * kScale;
    int startX     = std::max(0, (static_cast<int>(texWidth_)  - textW) / 2);
    int startY     = std::max(0, (static_cast<int>(texHeight_) - textH) / 2);

    for (size_t i = 0; i < message.size(); ++i) {
        DrawGlyph(rgba, texWidth_, texHeight_,
                  startX + static_cast<int>(i) * charPixelW, startY,
                  message[i],
                  255, 255, 255);
    }
}

void StatusOverlay::UploadStagingToSwapchainImage(VkImage image) {
    VkDevice dev   = ctx_.GetVkDevice();
    VkQueue  queue = ctx_.GetVkQueue();

    // Create a one-shot command buffer.
    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.queueFamilyIndex = ctx_.GetVkQueueFamily();
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    VkCommandPool pool = VK_NULL_HANDLE;
    VK_CHECK(vkCreateCommandPool(dev, &poolInfo, nullptr, &pool));

    VkCommandBufferAllocateInfo allocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocInfo.commandPool        = pool;
    allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VK_CHECK(vkAllocateCommandBuffers(dev, &allocInfo, &cmd));

    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo));

    // Transition image to TRANSFER_DST_OPTIMAL.
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.srcAccessMask       = 0;
    barrier.dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.image               = image;
    barrier.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);

    // Copy staging buffer → image.
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent      = {texWidth_, texHeight_, 1};
    vkCmdCopyBufferToImage(cmd, stagingBuffer_, image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    // Transition image to COLOR_ATTACHMENT_OPTIMAL for compositor sampling.
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);

    VK_CHECK(vkEndCommandBuffer(cmd));

    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers    = &cmd;
    VK_CHECK(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));
    vkQueueWaitIdle(queue);

    vkDestroyCommandPool(dev, pool, nullptr);
}
