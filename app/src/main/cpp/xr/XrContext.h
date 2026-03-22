#pragma once

// openxr_platform.h requires platform headers to be included before it
#include <jni.h>
#include <vulkan/vulkan.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <android_native_app_glue.h>

#include <array>
#include <span>
#include <vector>

// XrContext owns the OpenXR instance, session, Vulkan objects required for the
// graphics binding, and the XR_FB_passthrough handles.  All other XR subsystems
// receive a reference to this object.
class XrContext {
public:
    explicit XrContext(android_app* app);
    ~XrContext();

    // Phase 1 — call in order before entering the frame loop.
    void CreateInstance();
    void CreateVulkanObjects();   // minimal VkInstance/VkDevice for XrGraphicsBindingVulkanKHR
    void CreateSession();
    void InitializePassthrough();

    // Phase 2 — called each iteration of the main loop.
    // Returns false on XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED that ends the session.
    bool PollEvents(bool& exitRequested, bool& sessionActive);
    bool BeginFrame(XrFrameState& frameState);   // xrWaitFrame + xrBeginFrame
    bool EndFrame(const XrFrameState& frameState,
                  uint32_t layerCount,
                  const XrCompositionLayerBaseHeader* const* layers);

    // Enumerate stereo views for the predicted display time.
    bool LocateViews(XrTime predictedTime, std::array<XrView, 2>& views);

    // ── Accessors ─────────────────────────────────────────────────────────────
    XrInstance   GetInstance()       const { return instance_; }
    XrSession    GetSession()        const { return session_; }
    XrSpace      GetWorldSpace()     const { return worldSpace_; }
    XrSystemId   GetSystemId()       const { return systemId_; }
    VkDevice     GetVkDevice()       const { return vkDevice_; }
    VkPhysicalDevice GetVkPhysDevice() const { return vkPhysDevice_; }
    uint32_t     GetVkQueueFamily()  const { return vkQueueFamily_; }
    VkQueue      GetVkQueue()        const { return vkQueue_; }

    XrPassthroughFB      GetPassthroughFB()      const { return passthrough_; }
    XrPassthroughLayerFB GetPassthroughLayerFB() const { return passthroughLayer_; }

private:
    android_app*         app_              = nullptr;
    XrInstance           instance_         = XR_NULL_HANDLE;
    XrSession            session_          = XR_NULL_HANDLE;
    XrSystemId           systemId_         = XR_NULL_SYSTEM_ID;
    XrSpace              worldSpace_       = XR_NULL_HANDLE;
    XrSessionState       sessionState_     = XR_SESSION_STATE_UNKNOWN;
    XrPassthroughFB      passthrough_      = XR_NULL_HANDLE;
    XrPassthroughLayerFB passthroughLayer_ = XR_NULL_HANDLE;

    // Minimal Vulkan objects — only what XrGraphicsBindingVulkanKHR requires.
    VkInstance       vkInstance_   = VK_NULL_HANDLE;
    VkPhysicalDevice vkPhysDevice_ = VK_NULL_HANDLE;
    VkDevice         vkDevice_     = VK_NULL_HANDLE;
    uint32_t         vkQueueFamily_= 0;
    VkQueue          vkQueue_      = VK_NULL_HANDLE;

    // PFN for XR_FB_passthrough (loaded after CreateInstance)
    PFN_xrCreatePassthroughFB       pfnCreatePassthroughFB_      = nullptr;
    PFN_xrDestroyPassthroughFB      pfnDestroyPassthroughFB_     = nullptr;
    PFN_xrPassthroughStartFB        pfnPassthroughStartFB_       = nullptr;
    PFN_xrPassthroughPauseFB        pfnPassthroughPauseFB_       = nullptr;
    PFN_xrCreatePassthroughLayerFB  pfnCreatePassthroughLayerFB_ = nullptr;
    PFN_xrDestroyPassthroughLayerFB pfnDestroyPassthroughLayerFB_= nullptr;
    PFN_xrPassthroughLayerResumeFB  pfnPassthroughLayerResumeFB_ = nullptr;
    PFN_xrPassthroughLayerPauseFB   pfnPassthroughLayerPauseFB_  = nullptr;

    // PFN for XR_KHR_vulkan_enable2
    PFN_xrGetVulkanGraphicsRequirements2KHR pfnGetVulkanGraphicsRequirements2KHR_ = nullptr;
    PFN_xrCreateVulkanInstanceKHR           pfnCreateVulkanInstanceKHR_           = nullptr;
    PFN_xrGetVulkanGraphicsDevice2KHR       pfnGetVulkanGraphicsDevice2KHR_       = nullptr;
    PFN_xrCreateVulkanDeviceKHR             pfnCreateVulkanDeviceKHR_             = nullptr;

    void LoadExtensionFunctions();
    void HandleSessionStateChange(const XrEventDataSessionStateChanged& event,
                                  bool& exitRequested, bool& sessionActive);
};
