## Why

HyperDesk-MR crashes on launch on Meta Quest 3 due to four independent blockers: a missing OpenXR extension (`XR_FB_passthrough`), a spec-violating session lifecycle, an unregistered JavaVM pointer in WinPR, and a missing `HOME` environment variable. Each blocker produces a fatal error (SIGABRT or `XR_ERROR_*`) that terminates the process before the main loop can run.

## What Changes

- Make `XR_FB_passthrough` optional: enumerate available extensions at startup and only enable passthrough if the extension is present. Guard all passthrough function loading, initialisation, and composition-layer submission behind an availability flag.
- Defer `xrBeginSession` and `xrCreateReferenceSpace` from `CreateSession()` to the `XR_SESSION_STATE_READY` event handler, complying with the OpenXR session lifecycle spec.
- Register the JavaVM pointer with WinPR's global `jniVm` variable before any FreeRDP context is created, preventing `winpr_jni_attach_thread` from aborting.
- Set the `HOME` environment variable to the app's internal data path so WinPR's `GetKnownPath(KNOWN_PATH_HOME)` can resolve config directories.

## Capabilities

### New Capabilities
- `optional-passthrough`: Runtime detection and graceful degradation when `XR_FB_passthrough` is unavailable on the device.
- `android-freerdp-bootstrap`: Android-specific initialisation (JavaVM registration, HOME path) required before FreeRDP/WinPR can operate.

### Modified Capabilities
- `xr-session`: Session lifecycle now defers `xrBeginSession` and reference-space creation to the `READY` state event, and passthrough initialisation is conditional.

## Impact

- **Code**: `XrContext.h`, `XrContext.cpp`, `XrPassthrough.cpp`, `XrCompositor.cpp`, `main.cpp`, `CMakeLists.txt`
- **Runtime behaviour**: App launches and enters the main loop on Quest 3 firmware that lacks `XR_FB_passthrough`. Without passthrough, the user sees opaque black behind virtual monitors instead of the real world.
- **Dependencies**: No new dependencies. Relies on FreeRDP 3.8.0's global `jniVm` symbol in `libwinpr3.a`.
- **Breaking**: None. All changes are additive or defensive guards around existing code paths.
