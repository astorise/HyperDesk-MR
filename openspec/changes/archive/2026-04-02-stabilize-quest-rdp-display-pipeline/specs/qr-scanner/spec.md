## MODIFIED Requirements

### Requirement: Scan-to-connect flow replaces hardcoded credentials
The `main.cpp` SHALL remove hardcoded `ConnectionParams`, start the QR scanner during application startup, and defer monitor video decoder initialization until a valid QR code is scanned. After a successful decode, the application SHALL stop camera capture, initialize the required video decoders lazily, and only then call `rdpManager->Connect()` via callback.

#### Scenario: Connection is initiated only after scan handoff completes
- **WHEN** a QR code containing valid connection JSON is decoded
- **THEN** the scanner callback stops the camera, initializes the video decoders, and only then calls `rdpManager->Connect()` with the parsed parameters

#### Scenario: Scanner owns the startup phase until a valid QR is found
- **WHEN** the app launches and no valid QR code has been scanned yet
- **THEN** the scanner continues processing camera frames and the RDP video decoders remain uninitialized

### Requirement: Camera access is closed after connection
The scanner SHALL stop camera frame acquisition before RDP video decoder bootstrap begins so the Quest camera is no longer competing with monitor startup resources once connection handoff starts.

#### Scenario: Camera is released before decoder bootstrap
- **WHEN** `rdpManager->Connect()` is about to be triggered by a scanned QR code
- **THEN** camera capture is stopped before video decoder initialization and before the network connection begins
