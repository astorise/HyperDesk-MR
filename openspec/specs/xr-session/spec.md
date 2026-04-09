## Purpose
Establish and manage the OpenXR session lifecycle, including Vulkan backend initialization, passthrough extension activation, and per-frame composition layer submission.

## Requirements

### Requirement: OpenXR session initializes with Vulkan backend
The application SHALL create a valid `XrInstance` and `XrSession` using the Vulkan graphics API as the backend, requesting all required extensions at instance creation time.

#### Scenario: Application starts and creates a valid XrSession
- **WHEN** the application launches on a Meta Quest 3 device
- **THEN** an `XrInstance` is created with Vulkan graphics binding and a valid `XrSession` handle is obtained without error

### Requirement: Passthrough extension is activated at session creation
The application SHALL request and enable the `XR_FB_passthrough` extension only when the runtime reports it as available via `xrEnumerateInstanceExtensionProperties`. When the extension is unavailable, the application SHALL proceed without passthrough and no error SHALL be raised.

#### Scenario: XR_FB_passthrough extension is enabled when available
- **WHEN** the XrSession is created and `XR_FB_passthrough` was found in the enumerated extensions
- **THEN** `XR_FB_passthrough` is listed in the enabled extensions and `xrCreatePassthroughFB` succeeds, returning a valid passthrough handle

#### Scenario: Session creation succeeds without passthrough
- **WHEN** the XrSession is created and `XR_FB_passthrough` was not found in the enumerated extensions
- **THEN** the session is created successfully without the passthrough extension and the application logs that passthrough is unavailable

#### Scenario: Passthrough layer is running after session becomes ready
- **WHEN** the session enters the `XR_SESSION_STATE_READY` state and passthrough is available
- **THEN** `xrPassthroughStartFB` and `xrPassthroughLayerResumeFB` are called and the passthrough layer is active

### Requirement: Render loop submits composition layers via xrEndFrame
Each frame, the application SHALL call `xrEndFrame` with the clear color alpha set to 0.0f so that the passthrough environment is visible behind rendered content. The `environmentBlendMode` SHALL be set to `XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND` when available. When passthrough is available, the `XrCompositionLayerPassthroughFB` SHALL be the first layer, followed by up to 16 `XrCompositionLayerQuad` layers. When passthrough is unavailable, only the quad layers SHALL be submitted.

#### Scenario: Clear color alpha enables passthrough visibility
- **WHEN** `EndFrame` is called with passthrough active
- **THEN** the clear color alpha is 0.0f and the real-world environment is visible behind virtual content

#### Scenario: Alpha blend mode is used when available
- **WHEN** the runtime supports `XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND`
- **THEN** that blend mode is selected for frame submission

#### Scenario: Frame is submitted with passthrough and quad layers
- **WHEN** a new frame is predicted by `xrWaitFrame`, `xrBeginFrame` is called, and passthrough is available
- **THEN** `xrEndFrame` is called with a layer array containing the passthrough layer followed by all active `XrCompositionLayerQuad` entries without returning an error code

#### Scenario: Frame is submitted with only quad layers
- **WHEN** a new frame is predicted by `xrWaitFrame`, `xrBeginFrame` is called, and passthrough is unavailable
- **THEN** `xrEndFrame` is called with a layer array containing only the active `XrCompositionLayerQuad` entries without returning an error code

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

### Requirement: xrBeginSession is deferred to SESSION_STATE_READY event
The application SHALL NOT call `xrBeginSession` in `CreateSession()`. Instead, `xrBeginSession` SHALL be called in `HandleSessionStateChange` when the `XR_SESSION_STATE_READY` event is received.

#### Scenario: CreateSession does not call xrBeginSession
- **WHEN** `CreateSession()` completes
- **THEN** `xrBeginSession` has not been called and the session is in the `IDLE` state

#### Scenario: xrBeginSession is called on READY event
- **WHEN** the runtime delivers an `XrEventDataSessionStateChanged` event with state `XR_SESSION_STATE_READY`
- **THEN** `xrBeginSession` is called with `primaryViewConfigurationType` set to `XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO` and succeeds

#### Scenario: xrBeginSession is not called before READY
- **WHEN** the session is in any state other than `READY`
- **THEN** `xrBeginSession` is not called, preventing `XR_ERROR_SESSION_NOT_READY (-28)`

### Requirement: Reference space creation is deferred to SESSION_STATE_READY event
The application SHALL NOT call `xrCreateReferenceSpace` in `CreateSession()`. Instead, the stage reference space SHALL be created in `HandleSessionStateChange` immediately after a successful `xrBeginSession`.

#### Scenario: Reference space is created after session begins
- **WHEN** `xrBeginSession` succeeds in the `SESSION_STATE_READY` handler
- **THEN** `xrCreateReferenceSpace` is called with `XR_REFERENCE_SPACE_TYPE_STAGE` and an identity pose, returning a valid `XrSpace` handle

#### Scenario: CreateSession does not create a reference space
- **WHEN** `CreateSession()` completes
- **THEN** no reference space has been created and `worldSpace_` is `XR_NULL_HANDLE`

### Requirement: StatusOverlay displays connection state in MR
A `StatusOverlay` class SHALL manage its own `XrSwapchain` and `XrCompositionLayerQuad` to render status messages (e.g. "Connecting...", "Host Not Found", "Access Denied") in front of the user.

#### Scenario: Status overlay renders above passthrough but below monitors
- **WHEN** the `StatusOverlay` is active during the render loop
- **THEN** its `XrCompositionLayerQuad` is submitted in front of the user (e.g. at z = -1.5m) and composited above the passthrough layer but below monitor layers

#### Scenario: Status overlay shows connection error
- **WHEN** an RDP connection fails and an error message is available
- **THEN** the `StatusOverlay` displays the human-readable error text

#### Scenario: Status overlay disappears on successful connection
- **WHEN** all monitors are successfully connected and active
- **THEN** the `StatusOverlay` is hidden and its composition layer is no longer submitted

### Requirement: StatusOverlay uses a texture-based text rendering approach
The `StatusOverlay` SHALL render text to a Vulkan-backed swapchain texture without depending on a UI engine. Pre-rendered atlas textures or a lightweight library (e.g. STB_truetype) SHALL be used.

#### Scenario: Error text is rendered to a texture
- **WHEN** a status message needs to be displayed
- **THEN** the message text is rendered to the overlay's swapchain texture and the quad is updated

### Requirement: CursorOverlay renders a mouse cursor icon in 3D space
A `CursorOverlay` class SHALL manage its own `XrSwapchain` and `XrCompositionLayerQuad` to render a mouse cursor icon on the virtual monitor wall. The cursor texture SHALL be loaded from `assets/cursor.png` via `AImageDecoder`. If PNG loading fails, a procedural fallback arrow cursor SHALL be generated. The cursor texture SHALL be uploaded to all swapchain images during initialization, not lazily during rendering.

#### Scenario: Cursor icon is loaded from APK assets
- **WHEN** `CursorOverlay` is constructed with a valid `AAssetManager`
- **THEN** `cursor.png` is loaded via `AImageDecoder` in RGBA_8888 format and the decoded pixels are uploaded to all swapchain images

#### Scenario: Fallback cursor is generated when PNG loading fails
- **WHEN** `AImageDecoder` fails to load `cursor.png`
- **THEN** a procedural white arrow cursor with black outline is generated at 24x24 pixels

#### Scenario: Cursor is submitted as a quad layer after monitor layers
- **WHEN** the render loop calls `xrEndFrame`
- **THEN** the `CursorOverlay` quad layer is submitted after all monitor cylinder layers, composited on top

### Requirement: CursorOverlay positions the cursor hotspot correctly on the cylinder surface
The cursor quad SHALL be positioned so that the arrow tip (hotspot pixel) aligns with the 3D point on the cylinder surface corresponding to the current desktop coordinates. The hotspot offset SHALL compensate for the difference between the image center and the arrow tip pixel. The cursor position SHALL be computed by mapping desktop coordinates to monitor index and local UV, then converting to a yaw angle and 3D position on the cylinder surface, pulled slightly toward the viewer to avoid z-fighting.

#### Scenario: Desktop coordinates map to correct 3D position across all monitors
- **WHEN** the cursor desktop position changes
- **THEN** the cursor quad moves to the corresponding position on the correct monitor's cylinder surface segment, using the desktop-to-monitor mapping: x=[0,1920) → monitor 2 (right, yaw=-36°), x=[1920,3840) → monitor 0 (center, yaw=0°), x=[3840,5760) → monitor 1 (left, yaw=+36°)

#### Scenario: Hotspot pixel aligns with the cursor point
- **WHEN** the cursor quad is positioned
- **THEN** the quad is offset so the arrow tip pixel (not the image center) sits at the target 3D position
