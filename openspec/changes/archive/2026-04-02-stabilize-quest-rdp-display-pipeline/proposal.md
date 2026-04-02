## Why

HyperDesk-MR could reach the QR scanner, or reach an RDP session, but not reliably complete the full Quest 3 flow from scan to visible desktop. The final failure pattern combined three real runtime issues: Quest camera resource pressure during startup, FreeRDP 3.x bootstrap requirements around GDI and common `rdpgfx` callbacks, and Windows servers that negotiated `CLEARCODEC` / `CAPROGRESSIVE` instead of `AVC420`, leaving the headset with a connected but blank session.

## What Changes

- Stabilize the scan-to-connect startup sequence by keeping the Quest passthrough camera on the known-good configuration and deferring video decoder allocation until after a valid QR scan.
- Make the FreeRDP Android client resilient by bootstrapping GDI correctly, chaining the client-common `rdpgfx` callbacks instead of overwriting them, and keeping display-control negotiation alive even when the server degrades to fewer monitors.
- Add a software `rdpgfx` rendering fallback that uses FreeRDP/GDI-decoded desktop surfaces when the server refuses `AVC420/H.264`, then uploads those pixels into the OpenXR swapchain.
- Change frustum culling so hidden monitors stop presenting but do not tear down or restart `AMediaCodec`, preventing decoder churn from breaking the resumed view.
- Recenter the degraded single-monitor fallback layout directly in front of the user so the first recovered desktop is actually visible in-headset.

## Capabilities

### New Capabilities
- `rdpgfx-software-fallback`: Render a Quest-visible desktop even when the RDP server negotiates non-H.264 GFX codecs such as ClearCodec or Progressive.

### Modified Capabilities
- `camera-access`: Camera selection and scan-phase resource usage change to preserve stable QR acquisition on Quest 3.
- `qr-scanner`: The scan-to-connect flow now reserves an exclusive startup window before decoder creation and stops the camera before RDP video bootstrap.
- `rdp-multi-monitor`: FreeRDP connection setup, `disp` negotiation, monitor activation, and degraded single-monitor behavior change at the requirement level.
- `video-decode`: The hardware decode path is now lazy-initialized and coexists with a software upload path when the server does not deliver AVC420.
- `frustum-culling`: Culling behavior changes from codec suspension to render suppression while keeping decoders alive.

## Impact

- **Code**: `main.cpp`, `camera/CameraManager.cpp`, `camera/QrScanner.cpp`, `codec/MediaCodecDecoder.*`, `scene/VirtualMonitor.*`, `scene/MonitorLayout.*`, `rdp/RdpConnectionManager.*`, `rdp/RdpDisplayControl.*`, `xr/XrSwapchain.*`, `xr/StatusOverlay.*`
- **Runtime behavior**: Quest 3 can now complete the flow `QR scan -> RDP connect -> first visible desktop`, including servers that stay on ClearCodec / Progressive.
- **Dependencies**: No new third-party dependency. The change relies on FreeRDP's GDI/client-common bootstrap already linked in the Android build.
- **Breaking**: None. All changes are compatibility or recovery paths around the existing Quest + FreeRDP architecture.
