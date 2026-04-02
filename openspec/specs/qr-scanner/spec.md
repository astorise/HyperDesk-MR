## Purpose
QR code scanning system using Meta's camera access API and a C++ decoding library to parse RDP connection parameters from physical QR codes, enabling a scan-to-connect flow that replaces hardcoded credentials.
## Requirements
### Requirement: QR library is integrated into the build system
A lightweight C++ QR decoding library (ZBar or ZXing-cpp) SHALL be added to `third_party/` and linked via `CMakeLists.txt`.

#### Scenario: QR library compiles and links successfully
- **WHEN** the project is built with CMake for Android (arm64-v8a)
- **THEN** the QR library is compiled and linked into the final `.so` without errors

### Requirement: QrScanner decodes raw camera buffers into strings
The `QrScanner` utility SHALL accept raw YUV or Grayscale camera buffers and return decoded QR content as a string.

#### Scenario: Valid QR code is decoded from a camera buffer
- **WHEN** a camera buffer containing a valid QR code is passed to `QrScanner`
- **THEN** the decoded string content is returned

#### Scenario: No QR code is present in the buffer
- **WHEN** a camera buffer without a QR code is passed to `QrScanner`
- **THEN** an empty result is returned and no error is raised

### Requirement: QR content is parsed as RDP connection parameters
The scanner SHALL parse the decoded QR string as JSON with the format `{"h":"host", "u":"user", "p":"pass", "d":"domain", "port":3389}` and produce a valid `ConnectionParams` struct.

#### Scenario: Valid JSON QR content is parsed into ConnectionParams
- **WHEN** a QR code containing valid JSON with host, user, password, domain, and port fields is decoded
- **THEN** a `ConnectionParams` struct is populated with the parsed values

#### Scenario: Invalid or incomplete JSON is rejected
- **WHEN** a QR code containing malformed or incomplete JSON is decoded
- **THEN** the scanner logs an error and does not trigger a connection

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

### Requirement: Haptic feedback confirms successful QR scan
The application SHALL trigger a haptic pulse on the controllers when a QR code is successfully decoded and parsed.

#### Scenario: Haptic pulse fires on valid QR scan
- **WHEN** the QR scanner successfully decodes and parses a valid connection JSON
- **THEN** a short haptic pulse is sent to both controllers via `xrApplyHapticFeedback`

#### Scenario: No haptic feedback on failed decode
- **WHEN** a camera frame is processed but no valid QR code is detected
- **THEN** no haptic feedback is triggered

