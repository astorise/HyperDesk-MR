## Purpose
Establish an RDP connection to a Windows host using FreeRDP and negotiate a 16-monitor display layout over the Display Control virtual channel.
## Requirements
### Requirement: Client connects to a Windows host via RDP using FreeRDP
The application SHALL establish an RDP connection to a Windows host using the FreeRDP 3.x library compiled as a static library for Android, running its event loop on a dedicated network thread. Session settings SHALL be configured via `instance->context->settings` using the `freerdp_settings_set_*` API. The client SHALL install `BeginPaint`, `EndPaint`, and `DesktopResize` update callbacks before connect, SHALL call `gdi_init(instance, PIXEL_FORMAT_BGRA32)` after session establishment, and SHALL use `PubSub_SubscribeChannelConnected()` with a callback of signature `(void* context, const ChannelConnectedEventArgs* e)` for channel discovery. When the application overrides `rdpgfx` callbacks, it SHALL preserve and chain the existing client-common callbacks retrieved from `e->pInterface`.

#### Scenario: FreeRDP connects, initializes GDI, and keeps the session alive
- **WHEN** the user provides a valid hostname, username, and password
- **THEN** `freerdp_connect` succeeds, `gdi_init(instance, PIXEL_FORMAT_BGRA32)` succeeds in `PostConnect`, and the RDP session remains established on the network thread

#### Scenario: GFX pipeline channel is discovered via PubSub and common callbacks are chained
- **WHEN** the server activates the RDP GFX virtual channel
- **THEN** the `ChannelConnected` PubSub event fires with `e->name == RDPGFX_DVC_CHANNEL_NAME`, `e->pInterface` is cast to `RdpgfxClientContext*`, and the application records and chains the existing `CreateSurface`, `ResetGraphics`, `SurfaceCommand`, `StartFrame`, mapping, and `EndFrame` callbacks instead of replacing client-common bootstrap behavior with no-op handlers

### Requirement: Display control channel negotiates 16-monitor support
The application SHALL open the `Microsoft::Windows::RDS::DisplayControl` virtual channel via `DispClientContext` and respond to the server's display control capabilities by requesting up to 16 monitors. If the server advertises fewer monitors, or if the display-control channel is unavailable after `ResetGraphics`, the application SHALL activate a degraded layout using the available monitor count instead of leaving all monitors inactive.

#### Scenario: CAPS callback is received and capability is acknowledged with MaxNumMonitors=16
- **WHEN** the server sends display control capabilities after the RDP session is established and reports support for 16 or more monitors
- **THEN** the `DisplayControlCaps` callback fires and the client sends a 16-monitor layout response

#### Scenario: Server degrades to a single desktop
- **WHEN** the server advertises only one monitor, or the `disp` channel is unavailable when `ResetGraphics` is received
- **THEN** the client activates a one-monitor degraded layout instead of leaving the monitor wall inactive

### Requirement: Client advertises H.264/AVC codec support via the GFX CapsAdvertise callback
The application SHALL implement the `CapsAdvertise` callback on `RdpgfxClientContext` to negotiate H.264 encoding with the server. The callback SHALL advertise `RDPGFX_CAPVERSION_10` (or the appropriate version covering AVC420/AVC444) so that the server selects H.264 as the surface compression format for all subsequent GFX surface commands.

#### Scenario: CapsAdvertise callback negotiates H.264 encoding with the server
- **WHEN** the GFX pipeline channel is established and the server solicits codec capability
- **THEN** the `CapsAdvertise` callback fires, the client advertises `RDPGFX_CAPVERSION_10` (AVC420/AVC444), and the server confirms H.264 as the active compression format for GFX surface data

### Requirement: Client sends a 4x4 monitor grid layout to the server
The application SHALL send a monitor layout defining 16 logical monitors arranged in a 4 column × 4 row grid, each at 1920×1080 resolution, with non-overlapping pixel coordinates. The layout SHALL be sent via `DispClientContext::SendMonitorLayout(ctx, numMonitors, monitorsArray)`.

#### Scenario: LAYOUT PDU defines 16 monitors in a 4x4 arrangement
- **WHEN** the display control channel is ready and capability negotiation is complete
- **THEN** `SendMonitorLayout` is called with 16 monitor entries whose `Left`, `Top`, `Width`, and `Height` fields describe a contiguous 4×4 grid with no gaps or overlaps

### Requirement: 16-monitor layout coordinate calculation is covered by GTest unit tests
The codebase SHALL include a `tests/DisplayManagerTests.cpp` file that uses Google Test to verify that the 4×4 monitor grid layout computation produces correct, non-overlapping pixel coordinates. Tests MUST verify that each of the 16 monitors has the expected `Left`, `Top`, `Width`, and `Height` values for a 1920×1080 grid, that no two monitor rectangles overlap, and that the total covered pixel area equals 16 × 1920 × 1080.

#### Scenario: 4x4 grid layout generates 16 non-overlapping monitor rectangles
- **WHEN** `DisplayManagerTests` invokes the layout computation with a 4-column × 4-row grid at 1920×1080 per monitor
- **THEN** the resulting 16 entries have `Left` values cycling through 0, 1920, 3840, 5760 and `Top` values cycling through 0, 1080, 2160, 3240 with no overlapping rectangles

#### Scenario: Total pixel coverage matches the expected area
- **WHEN** `DisplayManagerTests` computes the union area of all 16 monitor rectangles
- **THEN** the total equals exactly 33,177,600 pixels (16 × 1920 × 1080) with no gaps

### Requirement: GFX surfaces are mapped to VR monitors by desktop position
The application SHALL use the `outputOriginX` field from `MapSurfaceToOutput` and `MapSurfaceToScaledOutput` PDUs to assign each RDP GFX surface to the correct VR monitor. The mapping SHALL be: desktop x=[0,1920) → monitor 2 (right, yaw=-36°), x=[1920,3840) → monitor 0 (center, yaw=0°), x=[3840,5760) → monitor 1 (left, yaw=+36°). Surfaces SHALL NOT be assigned by sequential creation order.

#### Scenario: Surface at x=0 is mapped to the right VR monitor
- **WHEN** a `MapSurfaceToOutput` PDU is received with `outputOriginX` < 1920
- **THEN** the surface is assigned to monitor index 2 (right position in VR)

#### Scenario: Surface at x=1920 is mapped to the center VR monitor
- **WHEN** a `MapSurfaceToOutput` PDU is received with `outputOriginX` in [1920, 3840)
- **THEN** the surface is assigned to monitor index 0 (center position in VR)

#### Scenario: Surface at x=3840 is mapped to the left VR monitor
- **WHEN** a `MapSurfaceToOutput` PDU is received with `outputOriginX` >= 3840
- **THEN** the surface is assigned to monitor index 1 (left position in VR)

### Requirement: Auto-reconnect guards against racing with initial connection
The auto-reconnect logic SHALL NOT trigger until the RDP session has been successfully connected at least once. A `wasEverConnected` flag SHALL be set to `true` when `IsConnected()` first returns `true`, and reconnection attempts SHALL only occur when `wasEverConnected` is `true` and the connection has dropped.

#### Scenario: No reconnect before first successful connection
- **WHEN** connection parameters are available but `wasEverConnected` is `false`
- **THEN** auto-reconnect does not trigger, preventing two concurrent RDP sessions

#### Scenario: Reconnect after connection drops
- **WHEN** `wasEverConnected` is `true` and `IsConnected()` returns `false`
- **THEN** auto-reconnect triggers after the cooldown period

### Requirement: Degraded single-monitor presentation is centered in world space
When only one monitor is active, monitor 0 SHALL be positioned directly in front of the user instead of inheriting the top-left position of the normal 4x4 wall.

#### Scenario: Single-monitor fallback is centered
- **WHEN** the effective active monitor count is reduced to one
- **THEN** monitor 0 is presented at the centered world-space position for the user-facing fallback desktop

