## Context

HyperDesk-MR is an OpenXR + FreeRDP native application for Meta Quest 3. It uses `android_native_app_glue` (NativeActivity), links FreeRDP 3.8.0 as static libraries (`libfreerdp3.a`, `libwinpr3.a`), and renders through an OpenXR Vulkan graphics binding.

The app crashed on Quest 3 at four successive points during startup:
1. `xrCreateInstance` returned `XR_ERROR_EXTENSION_NOT_PRESENT (-9)` because `XR_FB_passthrough` was unconditionally requested but absent from the runtime's 68-extension list.
2. `xrBeginSession` returned `XR_ERROR_SESSION_NOT_READY (-28)` because it was called immediately after `xrCreateSession` instead of waiting for the `XR_SESSION_STATE_READY` event.
3. `winpr_jni_attach_thread` called `abort()` because WinPR's global `JavaVM* jniVm` was never set — `JNI_OnLoad` from the static library was stripped by `--gc-sections`.
4. `freerdp_context_new` returned false because WinPR's `GetKnownPath(KNOWN_PATH_HOME)` returned NULL — Android does not set the `HOME` environment variable.

## Goals / Non-Goals

**Goals:**
- Eliminate all four launch-blocking crashes so the app enters its main loop on Quest 3.
- Make passthrough a runtime-optional feature: works when available, degrades gracefully when not.
- Comply with the OpenXR session lifecycle specification.
- Establish the minimal Android-specific bootstrap for FreeRDP/WinPR in a static-linking context.

**Non-Goals:**
- Implementing a replacement for `XR_FB_passthrough` (e.g. `XR_META_passthrough_preferences`).
- Fixing the RDP connection (placeholder address `192.168.1.100` is expected to fail).
- Supporting passthrough on devices that expose a different extension name.
- Modifying FreeRDP source code or switching to shared library linking.

## Decisions

### 1. Runtime extension enumeration for passthrough

**Decision**: Enumerate extensions via `xrEnumerateInstanceExtensionProperties` before `xrCreateInstance`. Build the enabled-extensions list dynamically using `std::vector<const char*>` instead of a fixed C array. Store the result in a `bool passthroughAvailable_` member on `XrContext`.

**Alternatives considered**:
- *Compile-time `#ifdef`*: Would require separate builds per firmware version. Rejected — runtime detection is strictly better.
- *Try-catch around `xrCreateInstance`*: OpenXR errors are return codes, not exceptions. A failed `xrCreateInstance` leaves no valid instance. Rejected.

### 2. Defer xrBeginSession to SESSION_STATE_READY

**Decision**: Remove `xrBeginSession` and `xrCreateReferenceSpace` from `CreateSession()`. Handle them in `HandleSessionStateChange` when `XR_SESSION_STATE_READY` is received.

**Rationale**: The OpenXR spec mandates that `xrBeginSession` is only valid after the runtime signals `SESSION_STATE_READY`. Calling it earlier produces `XR_ERROR_SESSION_NOT_READY`. This is the only correct approach.

### 3. Direct `extern` access to WinPR's `jniVm`

**Decision**: Declare `extern "C" JavaVM* jniVm` and assign `app->activity->vm` at the top of `android_main`, before any FreeRDP call.

**Alternatives considered**:
- *Force `JNI_OnLoad` via `--undefined=JNI_OnLoad` linker flag*: Tested and failed — the Android runtime did not reliably call `JNI_OnLoad` from a statically-linked archive, even when the symbol was present in the final `.so`. Rejected after empirical testing.
- *Forward-declare `winpr_InitializeJvm`*: The function does not exist in FreeRDP 3.8.0's `libwinpr3.a` (linker error: undefined symbol). Rejected.
- *Set via `JNI_OnLoad` defined in our own code*: Would shadow WinPR's `JNI_OnLoad` without setting its internal `jniVm`. Rejected.

The `extern` approach works because `jniVm` is a **global** (not static) variable in `winpr/libwinpr/utils/android.c`, confirmed by reading the FreeRDP 3.8.0 source.

### 4. Set HOME environment variable

**Decision**: Call `setenv("HOME", app->activity->internalDataPath, 0)` (no-overwrite) before FreeRDP context creation.

**Rationale**: WinPR's `GetKnownPath(KNOWN_PATH_HOME)` reads `$HOME`. Android does not set this variable. The app's internal data path (`/data/data/com.hyperdesk.mr/files`) is the correct location for per-app config files. The `0` flag avoids overwriting if `HOME` is already set.

## Risks / Trade-offs

- **No passthrough = opaque background**: When `XR_FB_passthrough` is unavailable, the user sees black behind virtual monitors. This is acceptable for initial launch but degrades the MR experience. → *Mitigation*: Log clearly at startup; future work can add `XR_META_*` passthrough support.
- **Dependency on FreeRDP internal symbol (`jniVm`)**: The `extern JavaVM* jniVm` approach relies on an implementation detail of FreeRDP 3.8.0. A FreeRDP upgrade could rename or make this variable static. → *Mitigation*: Document the dependency; verify on FreeRDP version bumps; upstream has kept this symbol global since FreeRDP 2.x.
- **`internalDataPath` may be null**: `ANativeActivity.internalDataPath` can theoretically be null before the activity is fully initialised. → *Mitigation*: Guard with `if (app->activity->internalDataPath)` before calling `setenv`.
