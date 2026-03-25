## 1. Runtime passthrough extension detection

- [x] 1.1 Add `passthroughAvailable_` member and `IsPassthroughAvailable()` accessor to `XrContext.h`
- [x] 1.2 In `XrContext::CreateInstance()`, call `xrEnumerateInstanceExtensionProperties` and check for `XR_FB_PASSTHROUGH_EXTENSION_NAME`
- [x] 1.3 Replace fixed `enabledExtensions` C array with `std::vector<const char*>`; conditionally push `XR_FB_PASSTHROUGH_EXTENSION_NAME`
- [x] 1.4 Guard passthrough function pointer loading in `LoadExtensionFunctions()` behind `if (passthroughAvailable_)`
- [x] 1.5 Make `InitializePassthrough()` return early when `passthroughAvailable_` is `false`

## 2. Passthrough graceful degradation

- [x] 2.1 Guard `XrPassthrough` constructor: skip handle creation when `IsPassthroughAvailable()` is `false`
- [x] 2.2 Guard `XrPassthrough::Start()` and `Pause()`: return early when function pointers are null
- [x] 2.3 Make `XrPassthrough::GetLayer()` return `nullptr` when `passthroughLayer_` is `XR_NULL_HANDLE`
- [x] 2.4 In `XrCompositor::RenderFrame()`, conditionally include passthrough layer only when `GetLayer()` returns non-null

## 3. OpenXR session lifecycle compliance

- [x] 3.1 Remove `xrBeginSession` call from `XrContext::CreateSession()`
- [x] 3.2 Remove `xrCreateReferenceSpace` call from `XrContext::CreateSession()`
- [x] 3.3 Add `XR_SESSION_STATE_READY` case in `HandleSessionStateChange` that calls `xrBeginSession` with `PRIMARY_STEREO`
- [x] 3.4 Create stage reference space with identity pose in the `SESSION_STATE_READY` handler, immediately after `xrBeginSession`

## 4. Android FreeRDP/WinPR bootstrap

- [x] 4.1 Add `#ifdef __ANDROID__` block with `extern "C" JavaVM* jniVm` declaration and `#include <jni.h>` in `main.cpp`
- [x] 4.2 Assign `jniVm = app->activity->vm` at the top of `android_main`, before any FreeRDP call
- [x] 4.3 Call `setenv("HOME", app->activity->internalDataPath, 0)` guarded by null check on `internalDataPath`
