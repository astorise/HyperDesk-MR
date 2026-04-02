## ADDED Requirements

### Requirement: Hardware decoder allocation is deferred until scan handoff
The application SHALL postpone `AMediaCodec`, `AImageReader`, and monitor output-surface allocation until after a valid QR code has been scanned and the camera has been stopped.

#### Scenario: Startup phase does not allocate monitor decoders
- **WHEN** the application is running the QR scan phase before a valid QR code has been accepted
- **THEN** no per-monitor `AMediaCodec` instance or decoder output surface has been created yet

#### Scenario: Decoder bootstrap begins only after camera shutdown
- **WHEN** a valid QR code has been decoded and accepted
- **THEN** the application stops camera capture before initializing monitor decoders for the RDP session
