# Tasks

- [x] Task 1: Update `RdpConnectionManager::RunEventLoop` to capture `freerdp_get_last_error` on failure.
- [x] Task 2: Create an `ErrorUtils` helper to convert RDP error codes to human-readable English/French strings.
- [x] Task 3: Implement `StatusOverlay` class that manages its own `XrSwapchain` and `XrCompositionLayerQuad`.
- [x] Task 4: Create a simple texture update mechanism to display "Host Not Found", "Access Denied", or "Network Error".
- [x] Task 5: Integrate `StatusOverlay` into the main `XrCompositor` loop so it renders above the Passthrough but below the monitors.
- [x] Task 6: Add a "Scan Successful" haptic pulse to the controllers when the QR code is read.