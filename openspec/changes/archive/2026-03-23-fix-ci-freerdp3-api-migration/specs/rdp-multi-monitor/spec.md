## MODIFIED Requirements

### Requirement: Client connects to a Windows host via RDP using FreeRDP
The application SHALL establish an RDP connection to a Windows host using the FreeRDP 3.x library compiled as a static library for Android, running its event loop on a dedicated network thread. Session settings SHALL be configured via `instance->context->settings` using the `freerdp_settings_set_*` API. Channel discovery SHALL use `PubSub_SubscribeChannelConnected()` with a callback of signature `(void* context, const ChannelConnectedEventArgs* e)`; virtual channel interfaces SHALL be retrieved from `e->pInterface`.

#### Scenario: FreeRDP connects and authenticates to a Windows host
- **WHEN** the user provides a valid hostname, username, and password
- **THEN** FreeRDP's `freerdp_connect` succeeds and the RDP session is established on the network thread

#### Scenario: GFX pipeline channel is discovered via PubSub
- **WHEN** the server activates the RDP GFX virtual channel
- **THEN** the `ChannelConnected` PubSub event fires with `e->name == RDPGFX_DVC_CHANNEL_NAME` and `e->pInterface` is cast to `RdpgfxClientContext*`; the `CreateSurface`, `StartFrame`, `MapSurfaceToOutput`, and `EndFrame` callbacks are registered

#### Scenario: DisplayControl channel is discovered via PubSub
- **WHEN** the server activates the Display Control virtual channel
- **THEN** the `ChannelConnected` PubSub event fires with `e->name == DISP_DVC_CHANNEL_NAME` and `e->pInterface` is cast to `DispClientContext*`; `RdpDisplayControl::Attach()` is called with the context

### Requirement: Display control channel negotiates 16-monitor support
The application SHALL open the `Microsoft::Windows::RDS::DisplayControl` virtual channel via `DispClientContext` and respond to the server's display control capabilities by asserting support for a maximum of 16 monitors. The `DisplayControlCaps` callback on `DispClientContext` SHALL have signature `(DispClientContext*, UINT32 MaxNumMonitors, UINT32 MaxMonitorAreaFactorA, UINT32 MaxMonitorAreaFactorB)`.

#### Scenario: CAPS callback is received and capability is acknowledged with MaxNumMonitors=16
- **WHEN** the server sends display control capabilities after the RDP session is established
- **THEN** the `DisplayControlCaps` callback fires and the client sends a monitor layout response when `MaxNumMonitors >= 16`

### Requirement: Client sends a 4x4 monitor grid layout to the server
The application SHALL send a monitor layout defining 16 logical monitors arranged in a 4 column × 4 row grid, each at 1920×1080 resolution, with non-overlapping pixel coordinates. The layout SHALL be sent via `DispClientContext::SendMonitorLayout(ctx, numMonitors, monitorsArray)`.

#### Scenario: LAYOUT PDU defines 16 monitors in a 4x4 arrangement
- **WHEN** the display control channel is ready and capability negotiation is complete
- **THEN** `SendMonitorLayout` is called with 16 monitor entries whose `Left`, `Top`, `Width`, and `Height` fields describe a contiguous 4×4 grid with no gaps or overlaps
