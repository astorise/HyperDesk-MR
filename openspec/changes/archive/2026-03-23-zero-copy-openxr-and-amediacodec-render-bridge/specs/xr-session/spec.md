## ADDED Requirements

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
