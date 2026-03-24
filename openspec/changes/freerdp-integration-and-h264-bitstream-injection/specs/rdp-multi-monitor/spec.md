## ADDED Requirements

### Requirement: Client advertises H.264/AVC codec support via the GFX CapsAdvertise callback
After the `RdpgfxClientContext` is registered through the PubSub `ChannelConnected` event, the application SHALL implement the `CapsAdvertise` callback on `RdpgfxClientContext` and SHALL advertise support for `RDPGFX_CAPVERSION_10` (or equivalent AVC420/AVC444 capability version) so that the Windows host selects H.264 as the graphics pipeline codec.

#### Scenario: CapsAdvertise callback negotiates H.264 encoding with the server
- **WHEN** the GFX channel is ready and the server sends a capability request
- **THEN** the `CapsAdvertise` callback fires, the client responds with `RDPGFX_CAPVERSION_10` (or the highest AVC-capable version supported), and the server confirms H.264 as the active codec for subsequent surface commands
