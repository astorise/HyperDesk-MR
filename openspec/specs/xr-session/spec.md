## Purpose
Establish and manage the OpenXR session lifecycle, including Vulkan backend initialization, passthrough extension activation, cylinder-layer composition, and overlay rendering for cursor, toolbar, and debug HUD.

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
Each frame, the application SHALL call `xrEndFrame` with the clear color alpha set to 0.0f so that the passthrough environment is visible behind rendered content. The `environmentBlendMode` SHALL be set to `XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND` when available. The layer submission order SHALL be: (1) `XrCompositionLayerPassthroughFB`, (2) `StatusOverlay` quad, (3) up to 16 `XrCompositionLayerCylinderKHR` monitor layers, (4) `ImGuiToolbar` cylinder layer, (5) `CursorOverlay` quad layer. Each monitor layer uses cylinder geometry with a radius of 1.6 m and a central angle of 36° (one angular step).

#### Scenario: Clear color alpha enables passthrough visibility
- **WHEN** `EndFrame` is called with passthrough active
- **THEN** the clear color alpha is 0.0f and the real-world environment is visible behind virtual content

#### Scenario: Alpha blend mode is used when available
- **WHEN** the runtime supports `XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND`
- **THEN** that blend mode is selected for frame submission

#### Scenario: Frame is submitted with passthrough and cylinder layers
- **WHEN** a new frame is predicted by `xrWaitFrame`, `xrBeginFrame` is called, and passthrough is available
- **THEN** `xrEndFrame` is called with a layer array containing the passthrough layer followed by active `XrCompositionLayerCylinderKHR` entries without returning an error code

#### Scenario: Frame is submitted with only cylinder layers
- **WHEN** a new frame is predicted by `xrWaitFrame`, `xrBeginFrame` is called, and passthrough is unavailable
- **THEN** `xrEndFrame` is called with a layer array containing only the active cylinder and overlay entries without returning an error code

### Requirement: A Vulkan-backed OpenXR swapchain is created per VirtualMonitor
The `VirtualMonitor` class SHALL call `xrCreateSwapchain` with a Vulkan image format matching the `AImageReader` output during its OpenXR initialization phase. The call MUST be wrapped in an `XR_FAILED()` check that logs the specific `XrResult` code to logcat via `__android_log_print` with the tag `"HyperDesk-MR"` if the swapchain cannot be created.

#### Scenario: Swapchain creation succeeds and returns a valid handle
- **WHEN** `VirtualMonitor` initializes its OpenXR integration
- **THEN** `xrCreateSwapchain` succeeds and returns a valid `XrSwapchain` handle whose Vulkan format is compatible with the decoded image format produced by `AImageReader`

### Requirement: XrCompositionLayerCylinderKHR is fully populated before xrEndFrame
During the render loop the application SHALL acquire the latest image from `AImageReader`, bind it to the active OpenXR swapchain image, populate all fields of an `XrCompositionLayerCylinderKHR` including `space` set to the STAGE reference space, `radius` = 1.6 m, `centralAngle` = 36° (kAngularStepRadians), and `aspectRatio` = 16:9, and MUST assert that no null pointers are passed to `xrEndFrame`. The main render loop MUST call a `GetCompositionLayer()` method on each active `VirtualMonitor` to retrieve the layer pointer.

#### Scenario: xrEndFrame receives a fully-populated non-null composition layer
- **WHEN** a decoded video frame is available and the render loop calls `xrEndFrame`
- **THEN** an `XrCompositionLayerCylinderKHR` with a valid non-null swapchain reference and STAGE reference space is included in the layer array without `xrEndFrame` returning an error code

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

### Requirement: StatusOverlay displays connection state and debug HUD in MR
A `StatusOverlay` class SHALL manage its own `XrSwapchain` and `XrCompositionLayerQuad` to render status messages and live debug information. The overlay supports up to 8 persistent status lines (for real-time data such as cursor coordinates and scroll angle) plus a scrolling log buffer of the last 4 messages.

#### Scenario: Status overlay renders above passthrough but below monitors
- **WHEN** the `StatusOverlay` is active during the render loop
- **THEN** its `XrCompositionLayerQuad` is submitted in STAGE space, composited above the passthrough layer but below monitor layers

#### Scenario: Status overlay shows connection error
- **WHEN** an RDP connection fails and an error message is available
- **THEN** the `StatusOverlay` displays the human-readable error text via `AddLog()`

#### Scenario: Debug HUD shows live cursor and scroll data
- **WHEN** the cursor overlay is active and the render loop runs
- **THEN** `SetStatusLine(6, ...)` and `SetStatusLine(7, ...)` display live cursor coordinates, monitor index, local U, scroll angle, and cursor world yaw

#### Scenario: Status overlay disappears when no content is set
- **WHEN** all status lines are cleared and the log buffer is empty
- **THEN** the `StatusOverlay` is hidden and its composition layer is no longer submitted

### Requirement: StatusOverlay uses a texture-based text rendering approach
The `StatusOverlay` SHALL render text to a Vulkan-backed swapchain texture (1024×512) without depending on a UI engine. A built-in bitmap font renderer SHALL be used.

#### Scenario: Error text is rendered to a texture
- **WHEN** a status message needs to be displayed
- **THEN** the message text is rendered to the overlay's swapchain texture and the quad is updated

### Requirement: CursorOverlay renders a mouse cursor icon on the cylinder wall
A `CursorOverlay` class SHALL manage its own `XrSwapchain` and `XrCompositionLayerQuad` to render a mouse cursor icon on the virtual monitor wall. The cursor texture SHALL be loaded from `assets/cursor.png` via `AImageDecoder`. If PNG loading fails, a procedural fallback arrow cursor SHALL be generated. The cursor texture SHALL be uploaded to all swapchain images during initialization, not lazily during rendering.

#### Scenario: Cursor icon is loaded from APK assets
- **WHEN** `CursorOverlay` is constructed with a valid `AAssetManager`
- **THEN** `cursor.png` is loaded via `AImageDecoder` in RGBA_8888 format and the decoded pixels are uploaded to all swapchain images

#### Scenario: Fallback cursor is generated when PNG loading fails
- **WHEN** `AImageDecoder` fails to load `cursor.png`
- **THEN** a procedural white arrow cursor with black outline is generated at 24×24 pixels

#### Scenario: Cursor is submitted as a quad layer after monitor and toolbar layers
- **WHEN** the render loop calls `xrEndFrame`
- **THEN** the `CursorOverlay` quad layer is submitted after all monitor cylinder layers and the toolbar layer, composited on top

### Requirement: CursorOverlay positions the cursor hotspot correctly on the cylinder surface
The cursor quad SHALL be positioned so that the arrow tip (hotspot pixel) aligns with the 3D point on the cylinder surface corresponding to the current desktop coordinates. The cursor position SHALL be computed by mapping desktop coordinates to monitor index (`monitorIdx = desktopX / 1920`) and local UV (`localU = (desktopX mod 1920) / 1920`), then converting to a world yaw angle: `cursorAngle = -monitorYaw - scrollYaw + (localU - 0.5) * centralAngle`, where `monitorYaw = monitorIdx * kAngularStepRadians`. The cursor is placed at `(r * sin(α), y, -r * cos(α))` on the cylinder, pulled slightly toward the viewer to avoid z-fighting. The scroll offset from `MonitorLayout::GetScrollYaw()` is explicitly passed so the cursor tracks the scrolled wall.

#### Scenario: Desktop coordinates map to correct 3D position across all monitors
- **WHEN** the cursor desktop position changes
- **THEN** the cursor quad moves to the corresponding position on the correct monitor's cylinder surface segment, using sequential monitor indexing: `monitorIdx = desktopX / 1920`

#### Scenario: Cursor tracks the wall under scroll
- **WHEN** the carousel scroll offset changes
- **THEN** the cursor angle incorporates `-scrollYaw` so the cursor stays aligned with the scrolled monitor position

#### Scenario: Hotspot pixel aligns with the cursor point
- **WHEN** the cursor quad is positioned
- **THEN** the quad is offset so the arrow tip pixel (not the image center) sits at the target 3D position

### Requirement: ImGuiToolbar renders action buttons below the central monitor
An `ImGuiToolbar` class SHALL manage its own `XrSwapchain` and `XrCompositionLayerCylinderKHR` to render a Dear ImGui-based toolbar. The toolbar is anchored to the unscrolled primary anchor pose (independent of carousel scroll) and provides buttons for: add screen, remove screen, QR scan, volume up, volume down, split rows, and drag mode.

#### Scenario: Toolbar is submitted as a cylinder layer after monitors
- **WHEN** monitor 0 is active and the toolbar is ready
- **THEN** the toolbar's cylinder layer is submitted in the xrEndFrame layer array between monitors and cursor

#### Scenario: Toolbar cursor input is forwarded
- **WHEN** the mouse hovers over the toolbar region
- **THEN** `RdpInputForwarder::GetToolbarCursor()` returns UV coordinates and `ImGuiToolbar::SetMouseInput()` processes hover and click events
