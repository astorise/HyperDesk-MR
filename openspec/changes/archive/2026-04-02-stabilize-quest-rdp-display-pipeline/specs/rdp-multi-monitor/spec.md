## MODIFIED Requirements

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

## ADDED Requirements

### Requirement: Degraded single-monitor presentation is centered in world space
When only one monitor is active, monitor 0 SHALL be positioned directly in front of the user instead of inheriting the top-left position of the normal 4x4 wall.

#### Scenario: Single-monitor fallback is centered
- **WHEN** the effective active monitor count is reduced to one
- **THEN** monitor 0 is presented at the centered world-space position for the user-facing fallback desktop
