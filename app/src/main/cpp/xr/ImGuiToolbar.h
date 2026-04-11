#pragma once

#include <android/asset_manager.h>
#include <jni.h>
#include <vulkan/vulkan.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

struct ImGuiContext;

class XrContext;

// ImGuiToolbar renders a row of action buttons under the central monitor.
// Uses Dear ImGui with the Vulkan backend, drawing into an OpenXR swapchain
// presented as a quad layer in MR space.
class ImGuiToolbar {
public:
    enum Button : int {
        BtnAdd = 0,
        BtnRemove,
        BtnDrag,
        BtnQrCode,
        BtnSplitScreen,
        BtnVolumeDown,
        BtnVolumeUp,
        kButtonCount
    };

    ImGuiToolbar(XrContext& ctx, AAssetManager* assetManager);
    ~ImGuiToolbar();

    // Inject mouse input.  u/v are normalized [0..1] toolbar UV coords.
    // inside=false means the cursor is outside the toolbar (no hover).
    void SetMouseInput(float u, float v, bool inside, bool leftDown);

    // Render the toolbar and return its composition layer.
    // Returns nullptr if init failed.  cylinderCenter is monitor 0's pose.
    const XrCompositionLayerQuad* GetCompositionLayer(
        XrSpace worldSpace,
        const XrPosef& cylinderCenter,
        float cylinderRadius,
        float centralAngle,
        float aspectRatio);

    // Returns the index of the most recently clicked button, or -1 if none.
    // Clears the latched value on read.
    int PollClickedButton();

    // Toolbar quad dimensions in pixels (used for cursor mapping).
    static constexpr uint32_t kTexWidth  = 1024;
    static constexpr uint32_t kTexHeight = 128;

    bool IsReady() const { return ready_; }

private:
    struct ButtonTexture {
        VkImage         image          = VK_NULL_HANDLE;
        VkDeviceMemory  memory         = VK_NULL_HANDLE;
        VkImageView     view           = VK_NULL_HANDLE;
        VkDescriptorSet descriptorSet  = VK_NULL_HANDLE;  // ImGui texture id
        uint32_t        width          = 0;
        uint32_t        height         = 0;
    };

    XrContext&    ctx_;
    ImGuiContext* imguiCtx_ = nullptr;
    bool          ready_    = false;

    // OpenXR swapchain.
    XrSwapchain                  swapchain_ = XR_NULL_HANDLE;
    std::vector<VkImage>         swapchainImages_;
    std::vector<VkImageView>     swapchainViews_;
    std::vector<VkFramebuffer>   swapchainFramebuffers_;

    // Vulkan resources owned by this toolbar.
    VkRenderPass     renderPass_     = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkCommandPool    commandPool_    = VK_NULL_HANDLE;
    VkCommandBuffer  commandBuffer_  = VK_NULL_HANDLE;
    VkSampler        sampler_        = VK_NULL_HANDLE;

    std::array<ButtonTexture, kButtonCount> buttons_;

    XrCompositionLayerQuad compositionLayer_{};

    // Mouse input state (writer = render thread / input thread).
    std::mutex inputMutex_;
    float      mouseU_      = -1.0f;
    float      mouseV_      = -1.0f;
    bool       mouseInside_ = false;
    bool       leftDown_    = false;

    // Click tracking — latched until polled by the app.
    std::atomic<int> lastClicked_{-1};

    // Helpers.
    bool LoadButtonTexture(int idx, AAssetManager* mgr, const char* path);
    void CreateRenderPass();
    void CreateDescriptorPool();
    void CreateSampler();
    void CreateSwapchain();
    void CreateCommandObjects();
    bool InitImGui();
    void RenderToImage(uint32_t imageIndex);

    void TransitionImageLayout(VkCommandBuffer cmd,
                               VkImage image,
                               VkImageLayout oldLayout,
                               VkImageLayout newLayout);
    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags props);
};
