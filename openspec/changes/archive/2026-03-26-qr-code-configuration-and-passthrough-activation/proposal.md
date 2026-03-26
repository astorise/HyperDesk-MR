## Why

The current implementation has hardcoded connection parameters and lacks the necessary Android manifest features to display Passthrough. Mixed Reality is essential for the user to see and scan a physical QR code (on a phone or paper) to configure the 16-monitor setup without a virtual keyboard. Without this change, users cannot dynamically configure RDP connections on the headset.

## What Changes

- Add Passthrough manifest declarations (`com.oculus.feature.PASSTHROUGH`, `com.oculus.permission.USE_SCENE`) and `android.permission.CAMERA` to `AndroidManifest.xml`.
- Request the `XR_FB_camera_access` extension during `xrCreateInstance` for raw camera frame acquisition.
- Set clear color alpha to 0.0f in `XrContext::EndFrame` so passthrough shows through.
- Integrate a lightweight C++ QR library (ZBar or ZXing-cpp) into `third_party/` and link via CMake.
- Implement `CameraManager` class to acquire camera frames from Meta's camera API.
- Create `QrScanner` utility to decode raw camera buffers into connection strings.
- Replace hardcoded `ConnectionParams` in `main.cpp` with a scan-to-connect flow.
- Add callback mechanism to trigger `rdpManager->Connect()` once a QR code is scanned.

## Capabilities

### New Capabilities
- `qr-scanner`: QR code scanning system using Meta's camera access API and a C++ decoding library to parse RDP connection parameters from physical QR codes.
- `camera-access`: Meta Quest 3 camera frame acquisition via the `XR_FB_camera_access` OpenXR extension with Android runtime permission handling.

### Modified Capabilities
- `xr-session`: EndFrame clear color alpha set to 0.0f to enable passthrough visibility, and `XR_FB_camera_access` extension added to instance creation.

## Impact

- **Code**: `AndroidManifest.xml`, `CMakeLists.txt`, `main.cpp`, `XrContext.h`, `XrContext.cpp`, new `CameraManager.h/cpp`, new `QrScanner.h/cpp`
- **Dependencies**: New dependency on ZBar or ZXing-cpp (added to `third_party/`)
- **Runtime behaviour**: App launches in Passthrough mode, scans for QR codes containing RDP credentials, and auto-connects when a valid code is found
- **Permissions**: Requires `android.permission.CAMERA` at runtime
- **Breaking**: Removes hardcoded connection parameters from `main.cpp`; connection now requires a QR code scan
