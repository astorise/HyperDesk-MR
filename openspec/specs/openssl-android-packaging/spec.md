## Purpose
Package OpenSSL shared libraries into the APK and locate them dynamically during CI to satisfy FreeRDP's OpenSSL symbol dependencies.

## Requirements

### Requirement: OpenSSL prebuilt shared libraries are packaged into the APK
The build system SHALL copy `libssl_3.so` and `libcrypto_3.so` (KDAB android_openssl 3.x, arm64-v8a) to `third_party/openssl/libs/android.arm64-v8a/` during the CI third-party build step and declare them as `IMPORTED SHARED` CMake targets (`ssl_3`, `crypto_3`) linked to the main `hyperdesk_mr` shared library.

#### Scenario: OpenSSL symbols are resolved at link time
- **WHEN** `./gradlew assembleDebug` links `libhyperdesk_mr.so`
- **THEN** all `EVP_*`, `SSL_*`, and `BIO_*` symbols referenced by the FreeRDP static libraries are resolved without linker errors

#### Scenario: OpenSSL shared libraries are present in the APK
- **WHEN** the debug APK is built
- **THEN** `lib/arm64-v8a/libssl_3.so` and `lib/arm64-v8a/libcrypto_3.so` are present inside the APK archive

### Requirement: OpenSSL library path is located dynamically during CI
The CI workflow SHALL locate the arm64-v8a OpenSSL `.so` files using `find` rather than a hardcoded path so that the step is robust to changes in the KDAB android_openssl directory layout across versions.

#### Scenario: CI locates OpenSSL libs regardless of directory layout
- **WHEN** KDAB android_openssl is cloned and the arm64-v8a `.so` files exist under either `arm64-v8a/` or `lib64/` subdirectory
- **THEN** the `find` command returns the correct path and both `cp` commands succeed without error
