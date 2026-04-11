#include "ImGuiToolbar.h"
#include "XrContext.h"
#include "../util/Logger.h"
#include "../util/Check.h"

#include <imgui.h>
#include <imgui_impl_vulkan.h>

#include <android/imagedecoder.h>

#include <cfloat>
#include <cmath>
#include <cstring>

namespace {
constexpr const char* kButtonAssetPaths[ImGuiToolbar::kButtonCount] = {
    "add.png",
    "remove.png",
    "drag.png",
    "qr_code.png",
    "splitscreen.png",
    "volume_down.png",
    "volume_up.png"
};

constexpr float kToolbarSizeMeters   = 0.50f;   // toolbar quad width  in metres
constexpr float kToolbarHeightMeters = 0.0625f; // toolbar quad height in metres (8:1 aspect)
constexpr float kToolbarYOffset      = -0.34f;  // metres below cylinder center
constexpr float kToolbarZPull        = 0.01f;   // pull toward viewer (less than cursor)
}  // namespace

// ── Construction ─────────────────────────────────────────────────────────────

ImGuiToolbar::ImGuiToolbar(XrContext& ctx, AAssetManager* assetManager)
    : ctx_(ctx) {
    LOGI("ImGuiToolbar: initializing");

    CreateRenderPass();
    CreateDescriptorPool();
    CreateSampler();
    CreateSwapchain();
    CreateCommandObjects();

    if (!InitImGui()) {
        LOGE("ImGuiToolbar: ImGui init failed");
        return;
    }

    // Load all button textures from assets.
    for (int i = 0; i < kButtonCount; ++i) {
        if (!LoadButtonTexture(i, assetManager, kButtonAssetPaths[i])) {
            LOGE("ImGuiToolbar: failed to load %s", kButtonAssetPaths[i]);
        }
    }

    ready_ = true;
    LOGI("ImGuiToolbar: ready (%ux%u, %zu swapchain images)",
         kTexWidth, kTexHeight, swapchainImages_.size());
}

ImGuiToolbar::~ImGuiToolbar() {
    VkDevice dev = ctx_.GetVkDevice();
    if (dev != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(dev);
    }

    if (imguiCtx_) {
        ImGui_ImplVulkan_Shutdown();
        ImGui::DestroyContext(imguiCtx_);
        imguiCtx_ = nullptr;
    }

    for (auto& b : buttons_) {
        if (b.view)   vkDestroyImageView(dev, b.view, nullptr);
        if (b.image)  vkDestroyImage(dev, b.image, nullptr);
        if (b.memory) vkFreeMemory(dev, b.memory, nullptr);
    }

    for (auto fb : swapchainFramebuffers_) if (fb) vkDestroyFramebuffer(dev, fb, nullptr);
    for (auto v  : swapchainViews_)        if (v)  vkDestroyImageView(dev, v, nullptr);

    if (sampler_)        vkDestroySampler(dev, sampler_, nullptr);
    if (renderPass_)     vkDestroyRenderPass(dev, renderPass_, nullptr);
    if (descriptorPool_) vkDestroyDescriptorPool(dev, descriptorPool_, nullptr);
    if (commandPool_)    vkDestroyCommandPool(dev, commandPool_, nullptr);
    if (swapchain_ != XR_NULL_HANDLE) xrDestroySwapchain(swapchain_);
}

// ── Public API ───────────────────────────────────────────────────────────────

void ImGuiToolbar::SetMouseInput(float u, float v, bool inside, bool leftDown) {
    std::lock_guard lock(inputMutex_);
    mouseU_      = u;
    mouseV_      = v;
    mouseInside_ = inside;
    leftDown_    = leftDown;
}

int ImGuiToolbar::PollClickedButton() {
    return lastClicked_.exchange(-1);
}

const XrCompositionLayerQuad* ImGuiToolbar::GetCompositionLayer(
        XrSpace worldSpace,
        const XrPosef& cylinderCenter,
        float cylinderRadius,
        float /*centralAngle*/,
        float /*aspectRatio*/) {
    if (!ready_) return nullptr;

    // Acquire swapchain image.
    uint32_t imageIndex = 0;
    XrSwapchainImageAcquireInfo acquireInfo{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    if (XR_FAILED(xrAcquireSwapchainImage(swapchain_, &acquireInfo, &imageIndex)))
        return nullptr;

    XrSwapchainImageWaitInfo waitInfo{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    waitInfo.timeout = XR_INFINITE_DURATION;
    if (XR_FAILED(xrWaitSwapchainImage(swapchain_, &waitInfo))) {
        XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        xrReleaseSwapchainImage(swapchain_, &releaseInfo);
        return nullptr;
    }

    RenderToImage(imageIndex);

    XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    xrReleaseSwapchainImage(swapchain_, &releaseInfo);

    // Build the composition layer in world space.
    // Position: directly under monitor 0, on the cylinder surface, pulled toward viewer.
    float r  = cylinderRadius - kToolbarZPull;
    float wx = 0.0f;          // local X = 0 (centered)
    float wy = kToolbarYOffset;
    float wz = -r;            // local Z = -r (in front)

    // Rotate by cylinderCenter.orientation (quaternion).
    const auto& q = cylinderCenter.orientation;
    float ux = q.x, uy = q.y, uz = q.z, uw = q.w;
    float crossX = uy * wz - uz * wy;
    float crossY = uz * wx - ux * wz;
    float crossZ = ux * wy - uy * wx;
    float crossCrossX = uy * crossZ - uz * crossY;
    float crossCrossY = uz * crossX - ux * crossZ;
    float crossCrossZ = ux * crossY - uy * crossX;
    float worldX = wx + 2.0f * (uw * crossX + crossCrossX);
    float worldY = wy + 2.0f * (uw * crossY + crossCrossY);
    float worldZ = wz + 2.0f * (uw * crossZ + crossCrossZ);

    compositionLayer_                = XrCompositionLayerQuad{XR_TYPE_COMPOSITION_LAYER_QUAD};
    compositionLayer_.layerFlags     = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
    compositionLayer_.space          = worldSpace;
    compositionLayer_.eyeVisibility  = XR_EYE_VISIBILITY_BOTH;

    XrSwapchainSubImage sub{};
    sub.swapchain        = swapchain_;
    sub.imageRect.offset = {0, 0};
    sub.imageRect.extent = {static_cast<int32_t>(kTexWidth), static_cast<int32_t>(kTexHeight)};
    sub.imageArrayIndex  = 0;
    compositionLayer_.subImage = sub;

    compositionLayer_.pose.position = {
        cylinderCenter.position.x + worldX,
        cylinderCenter.position.y + worldY,
        cylinderCenter.position.z + worldZ
    };
    // Face the viewer (rotate to match cylinder front direction).
    compositionLayer_.pose.orientation = cylinderCenter.orientation;
    compositionLayer_.size = {kToolbarSizeMeters, kToolbarHeightMeters};

    return &compositionLayer_;
}

// ── Vulkan resource creation ─────────────────────────────────────────────────

void ImGuiToolbar::CreateRenderPass() {
    VkAttachmentDescription colorAttach{};
    colorAttach.format         = VK_FORMAT_R8G8B8A8_UNORM;
    colorAttach.samples        = VK_SAMPLE_COUNT_1_BIT;
    colorAttach.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttach.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttach.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttach.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttach.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttach.finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments    = &colorRef;

    VkSubpassDependency dep{};
    dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass    = 0;
    dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = 0;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    info.attachmentCount = 1;
    info.pAttachments    = &colorAttach;
    info.subpassCount    = 1;
    info.pSubpasses      = &subpass;
    info.dependencyCount = 1;
    info.pDependencies   = &dep;
    VK_CHECK(vkCreateRenderPass(ctx_.GetVkDevice(), &info, nullptr, &renderPass_));
}

void ImGuiToolbar::CreateDescriptorPool() {
    VkDescriptorPoolSize poolSizes[] = {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 32 },
    };
    VkDescriptorPoolCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    info.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    info.maxSets       = 32;
    info.poolSizeCount = 1;
    info.pPoolSizes    = poolSizes;
    VK_CHECK(vkCreateDescriptorPool(ctx_.GetVkDevice(), &info, nullptr, &descriptorPool_));
}

void ImGuiToolbar::CreateSampler() {
    VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    info.magFilter    = VK_FILTER_LINEAR;
    info.minFilter    = VK_FILTER_LINEAR;
    info.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.maxLod       = 1.0f;
    info.borderColor  = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    VK_CHECK(vkCreateSampler(ctx_.GetVkDevice(), &info, nullptr, &sampler_));
}

void ImGuiToolbar::CreateSwapchain() {
    XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    info.usageFlags  = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT |
                       XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    info.format      = VK_FORMAT_R8G8B8A8_UNORM;
    info.sampleCount = 1;
    info.width       = kTexWidth;
    info.height      = kTexHeight;
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

    VkDevice dev = ctx_.GetVkDevice();
    swapchainImages_.resize(imageCount);
    swapchainViews_.resize(imageCount);
    swapchainFramebuffers_.resize(imageCount);

    for (uint32_t i = 0; i < imageCount; ++i) {
        swapchainImages_[i] = images[i].image;

        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image    = swapchainImages_[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format   = VK_FORMAT_R8G8B8A8_UNORM;
        viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VK_CHECK(vkCreateImageView(dev, &viewInfo, nullptr, &swapchainViews_[i]));

        VkFramebufferCreateInfo fbInfo{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fbInfo.renderPass      = renderPass_;
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments    = &swapchainViews_[i];
        fbInfo.width           = kTexWidth;
        fbInfo.height          = kTexHeight;
        fbInfo.layers          = 1;
        VK_CHECK(vkCreateFramebuffer(dev, &fbInfo, nullptr, &swapchainFramebuffers_[i]));
    }
}

void ImGuiToolbar::CreateCommandObjects() {
    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.queueFamilyIndex = ctx_.GetVkQueueFamily();
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VK_CHECK(vkCreateCommandPool(ctx_.GetVkDevice(), &poolInfo, nullptr, &commandPool_));

    VkCommandBufferAllocateInfo allocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocInfo.commandPool        = commandPool_;
    allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    VK_CHECK(vkAllocateCommandBuffers(ctx_.GetVkDevice(), &allocInfo, &commandBuffer_));
}

// ── ImGui setup ──────────────────────────────────────────────────────────────

bool ImGuiToolbar::InitImGui() {
    const VkInstance vkInstance = ctx_.GetVkInstance();
    const VkPhysicalDevice vkPhysicalDevice = ctx_.GetVkPhysDevice();
    const VkDevice vkDevice = ctx_.GetVkDevice();
    const VkQueue vkQueue = ctx_.GetVkQueue();
    if (vkInstance == VK_NULL_HANDLE ||
        vkPhysicalDevice == VK_NULL_HANDLE ||
        vkDevice == VK_NULL_HANDLE ||
        vkQueue == VK_NULL_HANDLE) {
        LOGE("ImGuiToolbar: Vulkan not ready for ImGui init (instance=%p phys=%p dev=%p queue=%p)",
             reinterpret_cast<void*>(vkInstance),
             reinterpret_cast<void*>(vkPhysicalDevice),
             reinterpret_cast<void*>(vkDevice),
             reinterpret_cast<void*>(vkQueue));
        return false;
    }

    IMGUI_CHECKVERSION();
    imguiCtx_ = ImGui::CreateContext();
    ImGui::SetCurrentContext(imguiCtx_);

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename  = nullptr;  // no settings file
    io.LogFilename  = nullptr;
    io.DisplaySize  = ImVec2(static_cast<float>(kTexWidth),
                             static_cast<float>(kTexHeight));
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    io.MouseDrawCursor = false;

    // Build font atlas (default font, small).
    io.Fonts->AddFontDefault();
    unsigned char* fontPixels;
    int fontW, fontH;
    io.Fonts->GetTexDataAsRGBA32(&fontPixels, &fontW, &fontH);

    // Style — dark theme with rounded buttons.
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 12.0f;
    style.FrameRounding  = 8.0f;
    style.WindowPadding  = ImVec2(8, 8);
    style.ItemSpacing    = ImVec2(8, 8);
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.10f, 0.12f, 0.85f);
    style.Colors[ImGuiCol_Button]   = ImVec4(0.20f, 0.20f, 0.24f, 1.00f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.30f, 0.50f, 0.80f, 1.00f);
    style.Colors[ImGuiCol_ButtonActive]  = ImVec4(0.20f, 0.40f, 0.70f, 1.00f);

    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.Instance        = vkInstance;
    initInfo.PhysicalDevice  = vkPhysicalDevice;
    initInfo.Device          = vkDevice;
    initInfo.QueueFamily     = ctx_.GetVkQueueFamily();
    initInfo.Queue           = vkQueue;
    initInfo.DescriptorPool  = descriptorPool_;
    initInfo.RenderPass      = renderPass_;
    initInfo.MinImageCount   = static_cast<uint32_t>(swapchainImages_.size());
    initInfo.ImageCount      = static_cast<uint32_t>(swapchainImages_.size());
    initInfo.MSAASamples     = VK_SAMPLE_COUNT_1_BIT;
    initInfo.Subpass         = 0;
    initInfo.PipelineCache   = VK_NULL_HANDLE;
    initInfo.Allocator       = nullptr;
    initInfo.CheckVkResultFn = nullptr;

    if (!ImGui_ImplVulkan_Init(&initInfo)) {
        LOGE("ImGuiToolbar: ImGui_ImplVulkan_Init failed");
        return false;
    }

    // Upload font atlas.
    if (!ImGui_ImplVulkan_CreateFontsTexture()) {
        LOGE("ImGuiToolbar: failed to create font texture");
        return false;
    }

    return true;
}

// ── Texture loading (PNG → VkImage) ──────────────────────────────────────────

bool ImGuiToolbar::LoadButtonTexture(int idx, AAssetManager* mgr, const char* path) {
    AAsset* asset = AAssetManager_open(mgr, path, AASSET_MODE_BUFFER);
    if (!asset) {
        LOGE("ImGuiToolbar: asset '%s' not found", path);
        return false;
    }

    AImageDecoder* decoder = nullptr;
    int result = AImageDecoder_createFromAAsset(asset, &decoder);
    if (result != ANDROID_IMAGE_DECODER_SUCCESS || !decoder) {
        LOGE("ImGuiToolbar: AImageDecoder_createFromAAsset failed for %s: %d", path, result);
        AAsset_close(asset);
        return false;
    }
    AImageDecoder_setAndroidBitmapFormat(decoder, ANDROID_BITMAP_FORMAT_RGBA_8888);

    const AImageDecoderHeaderInfo* info = AImageDecoder_getHeaderInfo(decoder);
    uint32_t w = AImageDecoderHeaderInfo_getWidth(info);
    uint32_t h = AImageDecoderHeaderInfo_getHeight(info);
    size_t stride = AImageDecoder_getMinimumStride(decoder);
    std::vector<uint8_t> pixels(stride * h);
    result = AImageDecoder_decodeImage(decoder, pixels.data(), stride, pixels.size());
    AImageDecoder_delete(decoder);
    AAsset_close(asset);
    if (result != ANDROID_IMAGE_DECODER_SUCCESS) {
        LOGE("ImGuiToolbar: decode failed for %s: %d", path, result);
        return false;
    }

    VkDevice dev = ctx_.GetVkDevice();
    auto& bt = buttons_[idx];
    bt.width  = w;
    bt.height = h;

    // Create VkImage.
    VkImageCreateInfo imgInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imgInfo.imageType   = VK_IMAGE_TYPE_2D;
    imgInfo.format      = VK_FORMAT_R8G8B8A8_UNORM;
    imgInfo.extent      = {w, h, 1};
    imgInfo.mipLevels   = 1;
    imgInfo.arrayLayers = 1;
    imgInfo.samples     = VK_SAMPLE_COUNT_1_BIT;
    imgInfo.tiling      = VK_IMAGE_TILING_OPTIMAL;
    imgInfo.usage       = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imgInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VK_CHECK(vkCreateImage(dev, &imgInfo, nullptr, &bt.image));

    VkMemoryRequirements memReqs{};
    vkGetImageMemoryRequirements(dev, bt.image, &memReqs);

    VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocInfo.allocationSize  = memReqs.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memReqs.memoryTypeBits,
                                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK_CHECK(vkAllocateMemory(dev, &allocInfo, nullptr, &bt.memory));
    VK_CHECK(vkBindImageMemory(dev, bt.image, bt.memory, 0));

    // Create staging buffer.
    VkBufferCreateInfo bufInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufInfo.size  = w * h * 4;
    bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VkBuffer staging = VK_NULL_HANDLE;
    VK_CHECK(vkCreateBuffer(dev, &bufInfo, nullptr, &staging));

    VkMemoryRequirements bufReqs{};
    vkGetBufferMemoryRequirements(dev, staging, &bufReqs);
    VkMemoryAllocateInfo bufAlloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    bufAlloc.allocationSize  = bufReqs.size;
    bufAlloc.memoryTypeIndex = FindMemoryType(
        bufReqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkDeviceMemory stagingMem = VK_NULL_HANDLE;
    VK_CHECK(vkAllocateMemory(dev, &bufAlloc, nullptr, &stagingMem));
    VK_CHECK(vkBindBufferMemory(dev, staging, stagingMem, 0));

    void* mapped = nullptr;
    VK_CHECK(vkMapMemory(dev, stagingMem, 0, bufInfo.size, 0, &mapped));
    auto* dst = static_cast<uint8_t*>(mapped);
    const uint32_t dstStride = w * 4;
    for (uint32_t row = 0; row < h; ++row) {
        memcpy(dst + row * dstStride, pixels.data() + row * stride, dstStride);
    }
    vkUnmapMemory(dev, stagingMem);

    // One-shot command buffer to upload + transition.
    VkCommandBufferAllocateInfo cmdAlloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cmdAlloc.commandPool        = commandPool_;
    cmdAlloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAlloc.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VK_CHECK(vkAllocateCommandBuffers(dev, &cmdAlloc, &cmd));

    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo));

    TransitionImageLayout(cmd, bt.image,
                          VK_IMAGE_LAYOUT_UNDEFINED,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent      = {w, h, 1};
    vkCmdCopyBufferToImage(cmd, staging, bt.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    TransitionImageLayout(cmd, bt.image,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    VK_CHECK(vkEndCommandBuffer(cmd));

    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers    = &cmd;
    VK_CHECK(vkQueueSubmit(ctx_.GetVkQueue(), 1, &submit, VK_NULL_HANDLE));
    VK_CHECK(vkQueueWaitIdle(ctx_.GetVkQueue()));

    vkFreeCommandBuffers(dev, commandPool_, 1, &cmd);
    vkDestroyBuffer(dev, staging, nullptr);
    vkFreeMemory(dev, stagingMem, nullptr);

    // Create image view.
    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image    = bt.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format   = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VK_CHECK(vkCreateImageView(dev, &viewInfo, nullptr, &bt.view));

    // Register with ImGui as a texture.
    bt.descriptorSet = ImGui_ImplVulkan_AddTexture(
        sampler_, bt.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    LOGI("ImGuiToolbar: loaded %s (%ux%u)", path, w, h);
    return true;
}

void ImGuiToolbar::TransitionImageLayout(VkCommandBuffer cmd,
                                         VkImage image,
                                         VkImageLayout oldLayout,
                                         VkImageLayout newLayout) {
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.oldLayout           = oldLayout;
    barrier.newLayout           = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image               = image;
    barrier.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
        newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
               newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }

    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

uint32_t ImGuiToolbar::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties memProps{};
    vkGetPhysicalDeviceMemoryProperties(ctx_.GetVkPhysDevice(), &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((typeFilter & (1u << i)) &&
            (memProps.memoryTypes[i].propertyFlags & props) == props) {
            return i;
        }
    }
    return 0;
}

// ── Per-frame rendering ──────────────────────────────────────────────────────

void ImGuiToolbar::RenderToImage(uint32_t imageIndex) {
    ImGui::SetCurrentContext(imguiCtx_);
    ImGuiIO& io = ImGui::GetIO();

    // Inject mouse state from cursor.
    {
        std::lock_guard lock(inputMutex_);
        if (mouseInside_) {
            io.MousePos = ImVec2(mouseU_ * io.DisplaySize.x,
                                 mouseV_ * io.DisplaySize.y);
        } else {
            io.MousePos = ImVec2(-FLT_MAX, -FLT_MAX);
        }
        io.MouseDown[0] = leftDown_;
    }

    io.DeltaTime = 1.0f / 72.0f;

    ImGui::NewFrame();

    // Full-screen window with no title/decorations.
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("##toolbar", nullptr,
                 ImGuiWindowFlags_NoTitleBar |
                 ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoScrollbar |
                 ImGuiWindowFlags_NoSavedSettings);

    // Layout 7 buttons in a horizontal row, centered vertically.
    const float btnSize = 96.0f;
    const ImVec2 imgSize(btnSize, btnSize);
    for (int i = 0; i < kButtonCount; ++i) {
        ImGui::PushID(i);
        const auto& bt = buttons_[i];
        bool clicked = false;
        if (bt.descriptorSet != VK_NULL_HANDLE) {
            ImTextureID tex = reinterpret_cast<ImTextureID>(bt.descriptorSet);
            clicked = ImGui::ImageButton("##btn", tex, imgSize);
        } else {
            clicked = ImGui::Button("?", ImVec2(btnSize + 16, btnSize + 16));
        }
        if (clicked) {
            lastClicked_.store(i);
            LOGI("ImGuiToolbar: button %d clicked", i);
        }
        ImGui::PopID();
        if (i + 1 < kButtonCount) ImGui::SameLine();
    }

    ImGui::End();
    ImGui::Render();

    ImDrawData* drawData = ImGui::GetDrawData();

    // Record command buffer.
    VK_CHECK(vkResetCommandBuffer(commandBuffer_, 0));
    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(commandBuffer_, &beginInfo));

    VkClearValue clear{};
    clear.color = {{0.0f, 0.0f, 0.0f, 0.0f}};

    VkRenderPassBeginInfo rpInfo{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rpInfo.renderPass        = renderPass_;
    rpInfo.framebuffer       = swapchainFramebuffers_[imageIndex];
    rpInfo.renderArea.offset = {0, 0};
    rpInfo.renderArea.extent = {kTexWidth, kTexHeight};
    rpInfo.clearValueCount   = 1;
    rpInfo.pClearValues      = &clear;

    vkCmdBeginRenderPass(commandBuffer_, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);
    if (drawData) {
        ImGui_ImplVulkan_RenderDrawData(drawData, commandBuffer_);
    }
    vkCmdEndRenderPass(commandBuffer_);

    VK_CHECK(vkEndCommandBuffer(commandBuffer_));

    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers    = &commandBuffer_;
    VK_CHECK(vkQueueSubmit(ctx_.GetVkQueue(), 1, &submit, VK_NULL_HANDLE));
    VK_CHECK(vkQueueWaitIdle(ctx_.GetVkQueue()));
}
