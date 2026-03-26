# Tasks

- [x] Task 1: Update `AndroidManifest.xml` with Passthrough features and Camera permissions.
- [x] Task 2: Modify `XrContext::CreateInstance` to request the `XR_FB_camera_access` extension.
- [x] Task 3: In `XrContext::EndFrame`, ensure the clear color alpha is set to 0.0f to let passthrough show through.
- [x] Task 4: Add ZBar or ZXing-cpp to `third_party/` and update `CMakeLists.txt` to link the scanner library.
- [x] Task 5: Implement `CameraManager` class to handle Meta's camera frame acquisition loop.
- [x] Task 6: Create `QrScanner` utility to convert raw camera buffers into decoded strings.
- [x] Task 7: Update `main.cpp` to remove hardcoded `ConnectionParams` and initialize the `QrScanner` loop.
- [x] Task 8: Implement a callback mechanism to trigger `rdpManager->Connect()` once a QR is scanned.
- [x] Task 9: Add a visual indicator (e.g., a simple 3D dot or log message) to confirm the scanner is active.