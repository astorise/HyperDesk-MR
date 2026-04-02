## Purpose
Meta Quest 3 camera frame acquisition via the `XR_FB_camera_access` OpenXR extension with Android runtime permission handling, providing raw camera buffers for QR code scanning.
## Requirements
### Requirement: XR_FB_camera_access extension is requested at instance creation
The application SHALL request the `XR_FB_camera_access` extension during `xrCreateInstance` to enable raw camera frame acquisition on Meta Quest 3.

#### Scenario: Camera access extension is enabled
- **WHEN** `xrCreateInstance` is called
- **THEN** `XR_FB_camera_access` is included in the enabled extensions list

### Requirement: Android CAMERA permission is requested at runtime
The application SHALL declare `android.permission.CAMERA` in `AndroidManifest.xml` and request it at runtime before starting the scanner.

#### Scenario: Camera permission is granted
- **WHEN** the app requests `android.permission.CAMERA` and the user grants it
- **THEN** the camera frame acquisition loop starts

#### Scenario: Camera permission is denied
- **WHEN** the app requests `android.permission.CAMERA` and the user denies it
- **THEN** the scanner does not start and an appropriate message is logged to Logcat under `HD_TAG`

### Requirement: CameraManager acquires raw camera frames
The `CameraManager` class SHALL use Meta's camera API to acquire raw YUV or Grayscale buffers from the Quest 3 sensors and deliver them to the `QrScanner`.

#### Scenario: Camera frames are delivered to the scanner
- **WHEN** the camera is active and permission has been granted
- **THEN** raw camera buffers are continuously delivered to the QR scanning pipeline

### Requirement: AndroidManifest declares Passthrough features
The `AndroidManifest.xml` SHALL include `<uses-feature android:name="com.oculus.feature.PASSTHROUGH" android:required="true" />` and `<uses-permission android:name="com.oculus.permission.USE_SCENE" />`.

#### Scenario: Manifest contains passthrough declarations
- **WHEN** the app is installed on the device
- **THEN** the manifest declares the passthrough feature and scene permission

### Requirement: Quest scanner uses a stable passthrough capture profile
The application SHALL prefer the known-good Quest passthrough-facing camera configuration for QR scanning and SHALL acquire frames as `YUV_420_888` at a stable 640x480 profile before any RDP video decoder is created.

#### Scenario: Scanner starts on the passthrough-friendly capture source
- **WHEN** the QR scanner starts on Quest 3
- **THEN** the camera session becomes active on the passthrough-friendly source and delivers frames suitable for QR decoding

#### Scenario: Scan phase does not compete with monitor decoder startup
- **WHEN** the QR scanner is active and no valid QR code has been accepted yet
- **THEN** no per-monitor RDP video decoder has been initialized

