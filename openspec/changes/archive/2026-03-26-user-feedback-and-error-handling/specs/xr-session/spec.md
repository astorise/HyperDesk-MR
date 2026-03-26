## ADDED Requirements

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
