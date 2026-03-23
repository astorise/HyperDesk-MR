## Why

The GitHub Actions CI build was failing because the FreeRDP layer used FreeRDP 2.x API patterns against the 3.8.0 library, and the OpenSSL runtime libraries were not linked into the final APK. Fixing this unblocks all future development and ensures every push produces a verified arm64-v8a APK artifact.

## What Changes

- Replace `instance->settings` direct access with `instance->context->settings` (FreeRDP 3.x made the `freerdp` struct opaque)
- Replace `channels->onChannelsConnected` direct assignment with `PubSub_SubscribeChannelConnected()` and a new `(void*, ChannelConnectedEventArgs*)` callback signature; channel interfaces are now obtained via `e->pInterface`
- Rename GFX callback fields: `SurfaceCreated` → `CreateSurface`, `SurfaceToOutput` → `MapSurfaceToOutput`
- Update `DispClientContext::DisplayControlCaps` callback signature from `(DispClientContext*, DISPLAY_CONTROL_CAPS_PDU*)` to `(DispClientContext*, UINT32, UINT32, UINT32)`
- Update `SendMonitorLayout` call from 2-arg `(ctx, &pdu)` form to 3-arg `(ctx, numMonitors, monitors_ptr)` form
- Remove `freerdp_client_load_addins` call (removed in FreeRDP 3.x; DVC channels are server-activated)
- Remove unused `fovSlack_` private field from `FrustumCuller` (silences `-Wunused-private-field`)
- Copy KDAB prebuilt `libssl_3.so` / `libcrypto_3.so` to `third_party/openssl/` in CI; declare them as `IMPORTED SHARED` in CMakeLists.txt and link against `hyperdesk_mr`
- Use `find` to locate OpenSSL `.so` files dynamically (KDAB directory layout varies by version)

## Capabilities

### New Capabilities

- `openssl-android-packaging`: Prebuilt OpenSSL 3.x shared libraries for Android arm64-v8a are copied to `third_party/openssl/` during CI and packaged into the APK alongside FreeRDP.

### Modified Capabilities

- `ci-pipeline`: The third-party build step now also produces `third_party/openssl/libs/android.arm64-v8a/libssl_3.so` and `libcrypto_3.so`, and uses dynamic `find`-based path resolution for KDAB android_openssl libs.
- `rdp-multi-monitor`: The FreeRDP integration layer is updated to the FreeRDP 3.x public API (PubSub channel discovery, opaque settings, renamed GFX/DisplayControl callbacks).

## Impact

- **Files changed**: `app/src/main/cpp/rdp/RdpConnectionManager.{h,cpp}`, `rdp/RdpDisplayControl.{h,cpp}`, `scene/FrustumCuller.{h,cpp}`, `app/src/main/cpp/CMakeLists.txt`, `.github/workflows/build.yml`
- **Dependencies added**: `libssl_3.so`, `libcrypto_3.so` (KDAB android_openssl prebuilt, runtime `.so` packaged in APK)
- **Build**: `./gradlew assembleDebug` now succeeds and uploads a debug APK artifact on every push to `main`
