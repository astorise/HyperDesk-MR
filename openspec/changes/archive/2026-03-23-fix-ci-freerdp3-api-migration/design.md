## Context

HyperDesk-MR builds FreeRDP 3.8.0 from source in CI as a static library trio (`libfreerdp3.a`, `libfreerdp-client3.a`, `libwinpr3.a`). The project's RDP integration layer (`RdpConnectionManager`, `RdpDisplayControl`) was written against FreeRDP 2.x patterns. FreeRDP 3.0 introduced several breaking API changes:

- `freerdp` struct fields made opaque (settings, channels)
- Channel discovery moved from a direct `onChannelsConnected` function pointer to a WinPR PubSub event
- GFX and DisplayControl callback field names changed
- `freerdp_client_load_addins` removed

Additionally, the FreeRDP static libraries depend on OpenSSL symbols at link time. KDAB provides prebuilt arm64-v8a OpenSSL 3.x `.so` files (`libssl_3.so`, `libcrypto_3.so`); these must be included both in the APK (as runtime `.so`) and in the CMake link graph.

## Goals / Non-Goals

**Goals:**
- Compile all RDP source files against FreeRDP 3.8.0 with zero errors
- Resolve all OpenSSL undefined symbol linker errors
- Package OpenSSL `.so` files into the debug APK so they are available at runtime on device
- Keep the CI pipeline self-contained (no manual pre-provisioning of third-party files)

**Non-Goals:**
- Upgrading to a newer FreeRDP version beyond 3.8.0
- Supporting architectures other than arm64-v8a
- Runtime testing of the RDP connection on a real device (CI only verifies the build)

## Decisions

### 1. Use `instance->context->settings` instead of a `freerdp_get_settings()` helper

FreeRDP 3.x removed the `settings` field from the `freerdp` struct. The new API has `freerdp_get_settings(freerdp*)` but this function is not present in 3.8.0 headers. `rdpContext` retains its `settings` field as a public member, so `instance->context->settings` is the correct access path in 3.8.0.

**Alternative considered**: Use a version-guard macro to call either `instance->settings` or `freerdp_get_settings()` — rejected as unnecessary complexity for a single target version.

### 2. PubSub-based channel discovery

FreeRDP 3.x removed the direct `rdpChannels::onChannelsConnected` function pointer. The replacement is `PubSub_SubscribeChannelConnected(instance->context->pubSub, callback)` with callback signature `(void* context, const ChannelConnectedEventArgs* e)`. Each channel's interface pointer is available as `e->pInterface` — eliminating the need for `freerdp_client_channel_get_interface()` (also removed).

### 3. Remove `freerdp_client_load_addins`

In FreeRDP 3.x the channel add-in loading was restructured. Dynamic Virtual Channels (GFX, DisplayControl) are activated by the server; the client-side plugin loading in `PreConnect` is no longer necessary and the function was removed from the public API.

### 4. Dynamic OpenSSL `.so` path discovery

KDAB/android_openssl changed its directory layout between versions (from `lib64/` to `arm64-v8a/`). Using `find "$OPENSSL_PREBUILT" -name "libssl_3.so" | grep -E "arm64|lib64"` makes the CI step robust to layout changes without needing to track the exact KDAB commit.

### 5. OpenSSL as `IMPORTED SHARED` in CMake

Declaring `ssl_3` and `crypto_3` as `IMPORTED SHARED` targets (rather than `IMPORTED STATIC`) means:
- The linker resolves OpenSSL symbols against the `.so` at link time
- Android Gradle Plugin automatically copies the `.so` into the APK's `lib/arm64-v8a/` directory
- At runtime on device, the dynamic linker loads them from the APK

**Alternative considered**: Build OpenSSL from source as static libs and link them into FreeRDP — rejected as too slow for CI and unnecessary given the KDAB prebuilt.

## Risks / Trade-offs

- **KDAB android_openssl ABI stability** → OpenSSL 3.x has a stable ABI; risk is low. The `find`-based path discovery handles minor layout changes.
- **`rdpContext::settings` may become opaque in a future FreeRDP patch** → Only affects builds that upgrade FreeRDP beyond 3.8.0; mitigated by pinning `--branch 3.8.0`.
- **PubSub callback receives one channel at a time** → Our `OnChannelsConnected` checks `e->name` and dispatches; if a channel name changes in a future FreeRDP version, the handler will silently ignore it. Logged via `LOGW` if neither GFX nor DisplayControl is matched.

## Migration Plan

All changes are already applied and CI is green. No migration steps are required for existing developers — a clean `git pull` + CI run is sufficient.

## Open Questions

_(none)_
