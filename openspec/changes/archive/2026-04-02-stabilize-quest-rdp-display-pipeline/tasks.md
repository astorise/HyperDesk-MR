## 1. QR Startup Stabilization

- [x] 1.1 Keep the Quest scanner on the stable passthrough-facing 640x480 capture profile
- [x] 1.2 Defer per-monitor decoder allocation until after a valid QR code is accepted
- [x] 1.3 Stop camera capture before decoder bootstrap and `rdpManager->Connect()`

## 2. FreeRDP Bootstrap Hardening

- [x] 2.1 Install `BeginPaint`, `EndPaint`, and `DesktopResize` callbacks before connect
- [x] 2.2 Initialize GDI in `PostConnect` and preserve FreeRDP client-common channel bootstrap
- [x] 2.3 Chain `rdpgfx` lifecycle callbacks instead of replacing them with no-op handlers
- [x] 2.4 Keep display-control negotiation alive with degraded monitor activation when the server exposes fewer monitors or no `disp` channel

## 3. Display Recovery Paths

- [x] 3.1 Preserve the preferred AVC420 hardware decode path for compatible hosts
- [x] 3.2 Add a software `rdpgfx` fallback that uploads GDI-decoded BGRA frames into the OpenXR swapchain
- [x] 3.3 Degrade non-AVC sessions to a visible single-monitor presentation
- [x] 3.4 Center the single-monitor fallback in front of the user instead of the top-left wall position

## 4. Runtime Stability

- [x] 4.1 Change frustum culling to hide presentation without stopping or restarting live codecs
- [x] 4.2 Keep headset overlay output concise while preserving detailed codec and fallback diagnostics in logcat
- [x] 4.3 Add or update regression tests for degraded monitor layout behavior

## 5. Validation

- [x] 5.1 Validate Quest QR scanning after GDI/bootstrap changes
- [x] 5.2 Validate first visible desktop on a host that negotiates AVC420
- [x] 5.3 Validate first visible desktop on a host that remains on `CLEARCODEC` / `CAPROGRESSIVE`
- [x] 5.4 Capture CI artifacts and Quest log evidence for the recovered scan-to-display path
