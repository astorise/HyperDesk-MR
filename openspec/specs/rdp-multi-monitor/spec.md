## Purpose
Establish an RDP connection to a Windows host using FreeRDP and negotiate a 16-monitor display layout over the Display Control virtual channel.

## Requirements

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
