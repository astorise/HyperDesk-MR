## Purpose
Make the XR_FB_passthrough extension optional so the application can run on OpenXR runtimes that do not support passthrough. The application enumerates available extensions at startup and conditionally enables passthrough, with all passthrough-dependent code paths degrading gracefully when the extension is absent.

## Requirements

### Requirement: Runtime extension enumeration before instance creation
The application SHALL call `xrEnumerateInstanceExtensionProperties` before `xrCreateInstance` to discover all extensions supported by the OpenXR runtime.

#### Scenario: Available extensions are enumerated at startup
- **WHEN** `CreateInstance()` is called
- **THEN** the application queries the runtime for all supported extensions and stores the result set before building the enabled-extensions list

### Requirement: Passthrough extension is conditionally enabled
The application SHALL add `XR_FB_PASSTHROUGH_EXTENSION_NAME` to the enabled-extensions list passed to `xrCreateInstance` only if the extension was found in the enumerated set. A `bool passthroughAvailable_` member on `XrContext` SHALL record the result.

#### Scenario: Passthrough extension is present on the device
- **WHEN** `XR_FB_passthrough` appears in the enumerated extensions
- **THEN** `passthroughAvailable_` is set to `true` and `XR_FB_PASSTHROUGH_EXTENSION_NAME` is included in the enabled-extensions array

#### Scenario: Passthrough extension is absent from the device
- **WHEN** `XR_FB_passthrough` does not appear in the enumerated extensions
- **THEN** `passthroughAvailable_` remains `false` and `XR_FB_PASSTHROUGH_EXTENSION_NAME` is not included in the enabled-extensions array

### Requirement: Passthrough function loading is guarded by availability flag
The application SHALL only call `xrGetInstanceProcAddr` for `XR_FB_passthrough` function pointers when `passthroughAvailable_` is `true`.

#### Scenario: Function pointers are loaded when passthrough is available
- **WHEN** `LoadExtensionFunctions()` is called and `passthroughAvailable_` is `true`
- **THEN** all `XR_FB_passthrough` function pointers (`xrCreatePassthroughFB`, `xrDestroyPassthroughFB`, `xrPassthroughStartFB`, `xrPassthroughPauseFB`, `xrCreatePassthroughLayerFB`, `xrDestroyPassthroughLayerFB`, `xrPassthroughLayerResumeFB`, `xrPassthroughLayerPauseFB`) are resolved via `xrGetInstanceProcAddr`

#### Scenario: Function pointer loading is skipped when passthrough is unavailable
- **WHEN** `LoadExtensionFunctions()` is called and `passthroughAvailable_` is `false`
- **THEN** no `XR_FB_passthrough` function pointers are loaded and all passthrough PFN members remain null

### Requirement: Passthrough initialisation is guarded by availability flag
`XrContext::InitializePassthrough()` SHALL return immediately without error when `passthroughAvailable_` is `false`.

#### Scenario: InitializePassthrough is a no-op when extension is absent
- **WHEN** `InitializePassthrough()` is called and `passthroughAvailable_` is `false`
- **THEN** the function returns without calling any `xrCreatePassthrough*` functions

### Requirement: XrPassthrough wrapper degrades gracefully
The `XrPassthrough` class SHALL check `XrContext::IsPassthroughAvailable()` in its constructor and skip all handle creation when the extension is absent. `Start()`, `Pause()`, and `GetLayer()` SHALL be safe no-ops when handles are null.

#### Scenario: XrPassthrough constructed without extension
- **WHEN** `XrPassthrough` is constructed with an `XrContext` where `IsPassthroughAvailable()` returns `false`
- **THEN** no passthrough or passthrough-layer handles are created and no XR errors are raised

#### Scenario: GetLayer returns nullptr when passthrough is unavailable
- **WHEN** `GetLayer()` is called and the passthrough layer handle is `XR_NULL_HANDLE`
- **THEN** `nullptr` is returned

### Requirement: Compositor skips passthrough layer when unavailable
`XrCompositor` SHALL only include the passthrough layer in the `xrEndFrame` layer array when `XrPassthrough::GetLayer()` returns a non-null pointer.

#### Scenario: Frame submitted without passthrough layer
- **WHEN** `RenderFrame()` is called and `XrPassthrough::GetLayer()` returns `nullptr`
- **THEN** `xrEndFrame` is called with only the quad layers (no passthrough layer) and succeeds without error

#### Scenario: Frame submitted with passthrough layer
- **WHEN** `RenderFrame()` is called and `XrPassthrough::GetLayer()` returns a valid pointer
- **THEN** `xrEndFrame` is called with the passthrough layer as the first entry followed by quad layers
