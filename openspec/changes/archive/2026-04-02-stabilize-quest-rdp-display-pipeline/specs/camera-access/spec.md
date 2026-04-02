## ADDED Requirements

### Requirement: Quest scanner uses a stable passthrough capture profile
The application SHALL prefer the known-good Quest passthrough-facing camera configuration for QR scanning and SHALL acquire frames as `YUV_420_888` at a stable 640x480 profile before any RDP video decoder is created.

#### Scenario: Scanner starts on the passthrough-friendly capture source
- **WHEN** the QR scanner starts on Quest 3
- **THEN** the camera session becomes active on the passthrough-friendly source and delivers frames suitable for QR decoding

#### Scenario: Scan phase does not compete with monitor decoder startup
- **WHEN** the QR scanner is active and no valid QR code has been accepted yet
- **THEN** no per-monitor RDP video decoder has been initialized
