#include "XrContext.h"
#include "../util/Logger.h"
#include "../util/Check.h"

#include <openxr/openxr_platform.h>
#include <vulkan/vulkan_android.h>
#include <vulkan/vulkan_core.h>

#include <cstring>
#include <stdexcept>
#include <vector>

// ── Helper: load an XR extension function pointer ────────────────────────────
#define XR_LOAD_FN(instance, name, pfn)                                     \
    XR_CHECK(xrGetInstanceProcAddr((instance),                              \
        #name, reinterpret_cast<PFN_xrVoidFunction*>(&(pfn))))

XrContext::XrContext(android_app* app) : app_(app) {}

XrContext::~XrContext() {
    if (worldSpace_       != XR_NULL_HANDLE) xrDestroySpace(worldSpace_);
    if (passthroughLayer_ != XR_NULL_HANDLE) pfnDestroyPassthroughLayerFB_(passthroughLayer_);
    if (passthrough_      != XR_NULL_HANDLE) pfnDestroyPassthroughFB_(passthrough_);
    if (session_          != XR_NULL_HANDLE) xrDestroySession(session_);
    if (vkDevice_         != VK_NULL_HANDLE) vkDestroyDevice(vkDevice_, nullptr);
    if (vkInstance_       != VK_NULL_HANDLE) vkDestroyInstance(vkInstance_, nullptr);
    if (instance_         != XR_NULL_HANDLE) xrDestroyInstance(instance_);
}

// ── CreateInstance ────────────────────────────────────────────────────────────

void XrContext::CreateInstance() {
    // Initialise the Android OpenXR loader.
    PFN_xrInitializeLoaderKHR initLoader = nullptr;
    XR_CHECK(xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrInitializeLoaderKHR",
        reinterpret_cast<PFN_xrVoidFunction*>(&initLoader)));

    XrLoaderInitInfoAndroidKHR loaderInfo{XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR};
    loaderInfo.applicationVM       = app_->activity->vm;
    loaderInfo.applicationContext  = app_->activity->clazz;
    XR_CHECK(initLoader(reinterpret_cast<const XrLoaderInitInfoBaseHeaderKHR*>(&loaderInfo)));

    // Check which optional extensions are available.
    uint32_t extCount = 0;
    xrEnumerateInstanceExtensionProperties(nullptr, 0, &extCount, nullptr);
    std::vector<XrExtensionProperties> availableExts(extCount,
        {XR_TYPE_EXTENSION_PROPERTIES});
    xrEnumerateInstanceExtensionProperties(nullptr, extCount, &extCount,
        availableExts.data());
    for (const auto& ext : availableExts) {
        if (std::strcmp(ext.extensionName, XR_FB_PASSTHROUGH_EXTENSION_NAME) == 0) {
            passthroughAvailable_ = true;
            break;
        }
    }
    LOGI("XR_FB_passthrough: %s", passthroughAvailable_ ? "available" : "not available");

    std::vector<const char*> enabledExtensions = {
        XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME,
        XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME,
    };
    if (passthroughAvailable_) {
        enabledExtensions.push_back(XR_FB_PASSTHROUGH_EXTENSION_NAME);
    }

    XrInstanceCreateInfoAndroidKHR androidInfo{XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};
    androidInfo.applicationVM       = app_->activity->vm;
    androidInfo.applicationActivity = app_->activity->clazz;

    XrApplicationInfo appInfo{};
    std::strncpy(appInfo.applicationName, "HyperDesk-MR", XR_MAX_APPLICATION_NAME_SIZE - 1);
    std::strncpy(appInfo.engineName,      "None",         XR_MAX_ENGINE_NAME_SIZE - 1);
    appInfo.applicationVersion = 1;
    appInfo.engineVersion      = 0;
    appInfo.apiVersion         = XR_CURRENT_API_VERSION;

    XrInstanceCreateInfo createInfo{XR_TYPE_INSTANCE_CREATE_INFO};
    createInfo.next                    = &androidInfo;
    createInfo.applicationInfo         = appInfo;
    createInfo.enabledExtensionCount   = static_cast<uint32_t>(enabledExtensions.size());
    createInfo.enabledExtensionNames   = enabledExtensions.data();

    XR_CHECK(xrCreateInstance(&createInfo, &instance_));
    LOGI("XrInstance created");

    LoadExtensionFunctions();

    XrSystemGetInfo sysInfo{XR_TYPE_SYSTEM_GET_INFO};
    sysInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XR_CHECK(xrGetSystem(instance_, &sysInfo, &systemId_));
    LOGI("XrSystem obtained");
}

// ── LoadExtensionFunctions ────────────────────────────────────────────────────

void XrContext::LoadExtensionFunctions() {
    XR_LOAD_FN(instance_, xrGetVulkanGraphicsRequirements2KHR, pfnGetVulkanGraphicsRequirements2KHR_);
    XR_LOAD_FN(instance_, xrCreateVulkanInstanceKHR,           pfnCreateVulkanInstanceKHR_);
    XR_LOAD_FN(instance_, xrGetVulkanGraphicsDevice2KHR,       pfnGetVulkanGraphicsDevice2KHR_);
    XR_LOAD_FN(instance_, xrCreateVulkanDeviceKHR,             pfnCreateVulkanDeviceKHR_);
    if (passthroughAvailable_) {
        XR_LOAD_FN(instance_, xrCreatePassthroughFB,               pfnCreatePassthroughFB_);
        XR_LOAD_FN(instance_, xrDestroyPassthroughFB,              pfnDestroyPassthroughFB_);
        XR_LOAD_FN(instance_, xrPassthroughStartFB,                pfnPassthroughStartFB_);
        XR_LOAD_FN(instance_, xrPassthroughPauseFB,                pfnPassthroughPauseFB_);
        XR_LOAD_FN(instance_, xrCreatePassthroughLayerFB,          pfnCreatePassthroughLayerFB_);
        XR_LOAD_FN(instance_, xrDestroyPassthroughLayerFB,         pfnDestroyPassthroughLayerFB_);
        XR_LOAD_FN(instance_, xrPassthroughLayerResumeFB,          pfnPassthroughLayerResumeFB_);
        XR_LOAD_FN(instance_, xrPassthroughLayerPauseFB,           pfnPassthroughLayerPauseFB_);
    }
}

// ── CreateVulkanObjects ───────────────────────────────────────────────────────

void XrContext::CreateVulkanObjects() {
    // Verify Vulkan version requirements.
    XrGraphicsRequirementsVulkan2KHR reqs{XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN2_KHR};
    XR_CHECK(pfnGetVulkanGraphicsRequirements2KHR_(instance_, systemId_, &reqs));

    // Create VkInstance via xrCreateVulkanInstanceKHR so the runtime can inject
    // any required instance extensions.
    const char* vkInstExtensions[] = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_ANDROID_SURFACE_EXTENSION_NAME,
        VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME,
    };
    const char* vkLayers[] = {};

    VkApplicationInfo vkAppInfo{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    vkAppInfo.pApplicationName   = "HyperDesk-MR";
    vkAppInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    vkAppInfo.apiVersion         = VK_API_VERSION_1_1;

    VkInstanceCreateInfo vkInstInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    vkInstInfo.pApplicationInfo        = &vkAppInfo;
    vkInstInfo.enabledExtensionCount   = static_cast<uint32_t>(std::size(vkInstExtensions));
    vkInstInfo.ppEnabledExtensionNames = vkInstExtensions;
    vkInstInfo.enabledLayerCount       = 0;
    vkInstInfo.ppEnabledLayerNames     = vkLayers;

    XrVulkanInstanceCreateInfoKHR xrVkInstInfo{XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR};
    xrVkInstInfo.systemId                    = systemId_;
    xrVkInstInfo.pfnGetInstanceProcAddr      = vkGetInstanceProcAddr;
    xrVkInstInfo.vulkanCreateInfo            = &vkInstInfo;
    xrVkInstInfo.vulkanAllocator             = nullptr;

    VkResult vkResult = VK_SUCCESS;
    XR_CHECK(pfnCreateVulkanInstanceKHR_(instance_, &xrVkInstInfo, &vkInstance_, &vkResult));
    VK_CHECK(vkResult);
    LOGI("VkInstance created");

    // Select physical device.
    XrVulkanGraphicsDeviceGetInfoKHR devGetInfo{XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR};
    devGetInfo.systemId        = systemId_;
    devGetInfo.vulkanInstance  = vkInstance_;
    XR_CHECK(pfnGetVulkanGraphicsDevice2KHR_(instance_, &devGetInfo, &vkPhysDevice_));

    // Find a graphics-capable queue family.
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(vkPhysDevice_, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(vkPhysDevice_, &queueFamilyCount, queueFamilies.data());
    for (uint32_t i = 0; i < queueFamilyCount; ++i) {
        if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            vkQueueFamily_ = i;
            break;
        }
    }

    const float queuePriority = 0.0f;
    VkDeviceQueueCreateInfo queueCreateInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queueCreateInfo.queueFamilyIndex = vkQueueFamily_;
    queueCreateInfo.queueCount       = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    // Enable VK_ANDROID_external_memory_android_hardware_buffer for zero-copy path.
    const char* vkDevExtensions[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,
        VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME,
        VK_ANDROID_EXTERNAL_MEMORY_ANDROID_HARDWARE_BUFFER_EXTENSION_NAME,
    };

    VkDeviceCreateInfo vkDevInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    vkDevInfo.queueCreateInfoCount    = 1;
    vkDevInfo.pQueueCreateInfos       = &queueCreateInfo;
    vkDevInfo.enabledExtensionCount   = static_cast<uint32_t>(std::size(vkDevExtensions));
    vkDevInfo.ppEnabledExtensionNames = vkDevExtensions;

    XrVulkanDeviceCreateInfoKHR xrVkDevInfo{XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR};
    xrVkDevInfo.systemId               = systemId_;
    xrVkDevInfo.pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
    xrVkDevInfo.vulkanPhysicalDevice   = vkPhysDevice_;
    xrVkDevInfo.vulkanCreateInfo       = &vkDevInfo;
    xrVkDevInfo.vulkanAllocator        = nullptr;

    XR_CHECK(pfnCreateVulkanDeviceKHR_(instance_, &xrVkDevInfo, &vkDevice_, &vkResult));
    VK_CHECK(vkResult);

    vkGetDeviceQueue(vkDevice_, vkQueueFamily_, 0, &vkQueue_);
    LOGI("VkDevice created");
}

// ── CreateSession ─────────────────────────────────────────────────────────────

void XrContext::CreateSession() {
    XrGraphicsBindingVulkan2KHR binding{XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR};
    binding.instance       = vkInstance_;
    binding.physicalDevice = vkPhysDevice_;
    binding.device         = vkDevice_;
    binding.queueFamilyIndex = vkQueueFamily_;
    binding.queueIndex     = 0;

    XrSessionCreateInfo sessionInfo{XR_TYPE_SESSION_CREATE_INFO};
    sessionInfo.next     = &binding;
    sessionInfo.systemId = systemId_;
    XR_CHECK(xrCreateSession(instance_, &sessionInfo, &session_));
    LOGI("XrSession created");
    // xrBeginSession and xrCreateReferenceSpace are deferred to
    // HandleSessionStateChange(XR_SESSION_STATE_READY) as required by the spec.
}

// ── InitializePassthrough ─────────────────────────────────────────────────────

void XrContext::InitializePassthrough() {
    if (!passthroughAvailable_) {
        LOGI("Passthrough not available — skipped");
        return;
    }
    XrPassthroughCreateInfoFB ptInfo{XR_TYPE_PASSTHROUGH_CREATE_INFO_FB};
    ptInfo.flags = XR_PASSTHROUGH_IS_RUNNING_AT_CREATION_BIT_FB;
    XR_CHECK(pfnCreatePassthroughFB_(session_, &ptInfo, &passthrough_));

    XrPassthroughLayerCreateInfoFB layerInfo{XR_TYPE_PASSTHROUGH_LAYER_CREATE_INFO_FB};
    layerInfo.passthrough = passthrough_;
    layerInfo.purpose     = XR_PASSTHROUGH_LAYER_PURPOSE_RECONSTRUCTION_FB;
    layerInfo.flags       = XR_PASSTHROUGH_IS_RUNNING_AT_CREATION_BIT_FB;
    XR_CHECK(pfnCreatePassthroughLayerFB_(session_, &layerInfo, &passthroughLayer_));
    LOGI("Passthrough initialised");
}

// ── PollEvents ────────────────────────────────────────────────────────────────

bool XrContext::PollEvents(bool& exitRequested, bool& sessionActive) {
    XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
    while (xrPollEvent(instance_, &event) == XR_SUCCESS) {
        switch (event.type) {
            case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
                const auto& stateChange =
                    *reinterpret_cast<const XrEventDataSessionStateChanged*>(&event);
                HandleSessionStateChange(stateChange, exitRequested, sessionActive);
                break;
            }
            case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
                LOGI("Instance loss pending — exiting");
                exitRequested = true;
                return false;
            default:
                break;
        }
        event = {XR_TYPE_EVENT_DATA_BUFFER};
    }
    return true;
}

void XrContext::HandleSessionStateChange(const XrEventDataSessionStateChanged& event,
                                         bool& exitRequested, bool& sessionActive) {
    sessionState_ = event.state;
    LOGI("Session state → %d", static_cast<int>(sessionState_));
    switch (sessionState_) {
        case XR_SESSION_STATE_READY: {
            XrSessionBeginInfo beginInfo{XR_TYPE_SESSION_BEGIN_INFO};
            beginInfo.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
            XR_CHECK(xrBeginSession(session_, &beginInfo));
            LOGI("XrSession begun");
            XrReferenceSpaceCreateInfo spaceInfo{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
            spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
            spaceInfo.poseInReferenceSpace = {{0.f, 0.f, 0.f, 1.f}, {0.f, 0.f, 0.f}};
            XR_CHECK(xrCreateReferenceSpace(session_, &spaceInfo, &worldSpace_));
            break;
        }
        case XR_SESSION_STATE_FOCUSED:
            sessionActive = true;
            break;
        case XR_SESSION_STATE_STOPPING:
            sessionActive = false;
            xrEndSession(session_);
            break;
        case XR_SESSION_STATE_EXITING:
        case XR_SESSION_STATE_LOSS_PENDING:
            exitRequested = true;
            sessionActive = false;
            break;
        default:
            break;
    }
}

// ── BeginFrame / EndFrame ─────────────────────────────────────────────────────

bool XrContext::BeginFrame(XrFrameState& frameState) {
    frameState = {XR_TYPE_FRAME_STATE};
    XrFrameWaitInfo waitInfo{XR_TYPE_FRAME_WAIT_INFO};
    XR_CHECK(xrWaitFrame(session_, &waitInfo, &frameState));

    XrFrameBeginInfo beginInfo{XR_TYPE_FRAME_BEGIN_INFO};
    XR_CHECK(xrBeginFrame(session_, &beginInfo));
    return frameState.shouldRender == XR_TRUE;
}

bool XrContext::EndFrame(const XrFrameState& frameState,
                         uint32_t layerCount,
                         const XrCompositionLayerBaseHeader* const* layers) {
    XrFrameEndInfo endInfo{XR_TYPE_FRAME_END_INFO};
    endInfo.displayTime          = frameState.predictedDisplayTime;
    endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    endInfo.layerCount           = layerCount;
    endInfo.layers               = layers;
    XR_CHECK(xrEndFrame(session_, &endInfo));
    return true;
}

// ── LocateViews ───────────────────────────────────────────────────────────────

bool XrContext::LocateViews(XrTime predictedTime, std::array<XrView, 2>& views) {
    views[0] = {XR_TYPE_VIEW};
    views[1] = {XR_TYPE_VIEW};

    XrViewLocateInfo locateInfo{XR_TYPE_VIEW_LOCATE_INFO};
    locateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    locateInfo.displayTime           = predictedTime;
    locateInfo.space                 = worldSpace_;

    XrViewState viewState{XR_TYPE_VIEW_STATE};
    uint32_t viewCount = 2;
    XR_CHECK(xrLocateViews(session_, &locateInfo, &viewState, viewCount, &viewCount, views.data()));
    return (viewState.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) != 0;
}
