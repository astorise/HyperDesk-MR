## ADDED Requirements

### Requirement: Client connects to a Windows host via RDP using FreeRDP
The application SHALL establish an RDP connection to a Windows host using the FreeRDP 3.x library compiled as a static library for Android, running its event loop on a dedicated network thread.

#### Scenario: FreeRDP connects and authenticates to a Windows host
- **WHEN** the user provides a valid hostname, username, and password
- **THEN** FreeRDP's `freerdp_connect` succeeds and the RDP session is established on the network thread

### Requirement: Display control channel negotiates 16-monitor support
The application SHALL open the `Microsoft::Windows::RDS::DisplayControl` virtual channel via `DispClientContext` and respond to the server's `DISPLAYCONTROL_CAPS_PDU` by asserting support for a maximum of 16 monitors.

#### Scenario: CAPS PDU is received and capability is acknowledged with MaxNumMonitors=16
- **WHEN** the server sends a `DISPLAYCONTROL_CAPS_PDU` after the RDP session is established
- **THEN** the `DispClientContext` callback fires and the client sends a response asserting `MaxNumMonitors = 16` and `MaxMonitorAreaFactorA / B` values sufficient for a 4×4 grid of 1920×1080 screens

### Requirement: Client sends a 4x4 monitor grid layout to the server
The application SHALL send a `DISPLAYCONTROL_MONITOR_LAYOUT_PDU` defining 16 logical monitors arranged in a 4 column × 4 row grid, each at 1920×1080 resolution, with non-overlapping pixel coordinates.

#### Scenario: LAYOUT PDU defines 16 monitors in a 4x4 arrangement
- **WHEN** the display control channel is ready and capability negotiation is complete
- **THEN** the client sends a `DISPLAYCONTROL_MONITOR_LAYOUT_PDU` containing exactly 16 monitor entries whose `Left`, `Top`, `Width`, and `Height` fields describe a contiguous 4×4 grid with no gaps or overlaps
