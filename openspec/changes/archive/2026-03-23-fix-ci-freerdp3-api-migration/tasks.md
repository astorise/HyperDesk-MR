## 1. FreeRDP 3.x Settings API

- [x] 1.1 Replace `instance_->settings` with `instance_->context->settings` in `RdpConnectionManager::Connect`
- [x] 1.2 Replace `instance_->settings` with `instance_->context->settings` in `RdpConnectionManager::RunEventLoop`
- [x] 1.3 Replace `instance->settings` with `instance->context->settings` in `RdpConnectionManager::OnPreConnect`

## 2. FreeRDP 3.x Channel Discovery (PubSub)

- [x] 2.1 Replace `instance_->context->channels->onChannelsConnected = OnChannelsConnected` with `PubSub_SubscribeChannelConnected(instance_->context->pubSub, OnChannelsConnected)`
- [x] 2.2 Change `OnChannelsConnected` signature in `RdpConnectionManager.h` from `(freerdp*, rdpChannels*)` to `(void*, const ChannelConnectedEventArgs*)`
- [x] 2.3 Rewrite `OnChannelsConnected` body to cast `void* context` → `rdpContext*` → `HyperDeskRdpContext*` and dispatch on `e->name` / `e->pInterface`
- [x] 2.4 Remove `freerdp_client_load_addins` call from `OnPreConnect` (function removed in FreeRDP 3.x)
- [x] 2.5 Remove unused `<freerdp/gdi/gdi.h>` and `<freerdp/client/channels.h>` includes from `RdpConnectionManager.cpp`

## 3. FreeRDP 3.x GFX Callback Renames

- [x] 3.1 Rename `ctx->gfx->SurfaceCreated` → `ctx->gfx->CreateSurface` in `OnChannelsConnected`
- [x] 3.2 Rename `ctx->gfx->SurfaceToOutput` → `ctx->gfx->MapSurfaceToOutput` in `OnChannelsConnected`
- [x] 3.3 Mark `OnPreConnect` and `OnPostConnect` `instance` parameters as unused (`/*instance*/`) to suppress `-Wunused-parameter`

## 4. FreeRDP 3.x DisplayControl API

- [x] 4.1 Change `OnDisplayControlCaps` signature in `RdpDisplayControl.h` from `(DispClientContext*, DISPLAY_CONTROL_CAPS_PDU*)` to `(DispClientContext*, UINT32, UINT32, UINT32)`
- [x] 4.2 Update `OnDisplayControlCaps` implementation in `RdpDisplayControl.cpp` to use the individual `UINT32` params instead of `DISPLAY_CONTROL_CAPS_PDU*`
- [x] 4.3 Rename `ctx_->DispCaps` back to `ctx_->DisplayControlCaps` in `RdpDisplayControl::Attach` (correct FreeRDP 3.x field name)
- [x] 4.4 Replace `ctx_->SendMonitorLayout(ctx_, &pdu)` with `ctx_->SendMonitorLayout(ctx_, numMonitors, entries.data())` — remove `DISPLAY_CONTROL_MONITOR_LAYOUT_PDU` construction

## 5. FrustumCuller Cleanup

- [x] 5.1 Remove unused `fovSlack_` private field from `FrustumCuller` class declaration (`FrustumCuller.h`)
- [x] 5.2 Remove `fovSlack_(fovSlackRadians)` from the member-initializer list in `FrustumCuller.cpp`

## 6. OpenSSL Linking (CMake)

- [x] 6.1 Add `OPENSSL_ROOT` path variable pointing to `${THIRD_PARTY_DIR}/openssl` in `CMakeLists.txt`
- [x] 6.2 Declare `ssl_3` and `crypto_3` as `IMPORTED SHARED` CMake targets with `IMPORTED_LOCATION` pointing to `third_party/openssl/libs/android.arm64-v8a/lib{ssl_3,crypto_3}.so`
- [x] 6.3 Add `ssl_3` and `crypto_3` to `target_link_libraries(hyperdesk_mr …)`

## 7. OpenSSL CI Provisioning

- [x] 7.1 Add `find`-based commands to locate `libssl_3.so` and `libcrypto_3.so` in the KDAB android_openssl clone, filtering for `arm64` or `lib64` directory names
- [x] 7.2 Update FreeRDP cmake invocation to use the dynamic `$LIBSSL_SO` / `$LIBCRYPTO_SO` variables for `-DOPENSSL_SSL_LIBRARY` and `-DOPENSSL_CRYPTO_LIBRARY`
- [x] 7.3 Add `cp` commands to copy located `.so` files to `third_party/openssl/libs/android.arm64-v8a/` with canonical names
