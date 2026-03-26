## ADDED Requirements

### Requirement: Haptic feedback confirms successful QR scan
The application SHALL trigger a haptic pulse on the controllers when a QR code is successfully decoded and parsed.

#### Scenario: Haptic pulse fires on valid QR scan
- **WHEN** the QR scanner successfully decodes and parses a valid connection JSON
- **THEN** a short haptic pulse is sent to both controllers via `xrApplyHapticFeedback`

#### Scenario: No haptic feedback on failed decode
- **WHEN** a camera frame is processed but no valid QR code is detected
- **THEN** no haptic feedback is triggered
