## ADDED Requirements

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
