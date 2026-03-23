## Purpose
Establish and manage the OpenXR session lifecycle, including Vulkan backend initialization, passthrough extension activation, and per-frame composition layer submission.

## Requirements

### Requirement: OpenXR session initializes with Vulkan backend
The application SHALL create a valid `XrInstance` and `XrSession` using the Vulkan graphics API as the backend, requesting all required extensions at instance creation time.

#### Scenario: Application starts and creates a valid XrSession
- **WHEN** the application launches on a Meta Quest 3 device
- **THEN** an `XrInstance` is created with Vulkan graphics binding and a valid `XrSession` handle is obtained without error

### Requirement: Passthrough extension is activated at session creation
The application SHALL request and enable the `XR_FB_passthrough` extension, creating an `XrPassthroughFB` and an `XrPassthroughLayerFB` configured for full-screen environment blending.

#### Scenario: XR_FB_passthrough extension is enabled
- **WHEN** the XrSession is created
- **THEN** `XR_FB_passthrough` is listed in the enabled extensions and `xrCreatePassthroughFB` succeeds, returning a valid passthrough handle

#### Scenario: Passthrough layer is running before the first frame
- **WHEN** the session enters the `XR_SESSION_STATE_READY` state
- **THEN** `xrPassthroughStartFB` and `xrPassthroughLayerResumeFB` are called and the passthrough layer is active

### Requirement: Render loop submits composition layers via xrEndFrame
Each frame, the application SHALL call `xrEndFrame` with an array containing the `XrCompositionLayerPassthroughFB` as the first layer, followed by up to 16 `XrCompositionLayerQuad` layers representing the active virtual monitors.

#### Scenario: Frame is submitted with passthrough and quad layers
- **WHEN** a new frame is predicted by `xrWaitFrame` and `xrBeginFrame` is called
- **THEN** `xrEndFrame` is called with a layer array containing the passthrough layer and all active `XrCompositionLayerQuad` entries without returning an error code

### Requirement: A Vulkan-backed OpenXR swapchain is created per VirtualMonitor
The `VirtualMonitor` class SHALL call `xrCreateSwapchain` with a Vulkan image format matching the `AImageReader` output during its OpenXR initialization phase. The call MUST be wrapped in an `XR_FAILED()` check that logs the specific `XrResult` code to logcat via `__android_log_print` with the tag `"HyperDeskMR"` if the swapchain cannot be created.

#### Scenario: Swapchain creation succeeds and returns a valid handle
- **WHEN** `VirtualMonitor` initializes its OpenXR integration
- **THEN** `xrCreateSwapchain` succeeds and returns a valid `XrSwapchain` handle whose Vulkan format is compatible with the decoded image format produced by `AImageReader`

### Requirement: XrCompositionLayerQuad is fully populated before xrEndFrame
During the render loop the application SHALL acquire the latest image from `AImageReader`, bind it to the active OpenXR swapchain image, populate all fields of an `XrCompositionLayerQuad` including `space` set to `XR_REFERENCE_SPACE_TYPE_LOCAL` and `subImage.swapchain` linked to the per-monitor swapchain, and MUST assert that no null pointers are passed to `xrEndFrame`. The main render loop MUST call a `GetCompositionLayer()` method on each active `VirtualMonitor` to retrieve the layer pointer.

#### Scenario: xrEndFrame receives a fully-populated non-null composition layer
- **WHEN** a decoded video frame is available and the render loop calls `xrEndFrame`
- **THEN** an `XrCompositionLayerQuad` with a valid non-null swapchain reference and local reference space is included in the layer array without `xrEndFrame` returning an error code
