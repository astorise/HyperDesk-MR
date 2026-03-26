## Purpose
Ensure the Android application correctly initializes JVM registration and environment variables required by WinPR and FreeRDP before any FreeRDP context or utility functions are called. This prevents crashes caused by null JVM pointers during JNI thread attachment and by missing HOME paths during WinPR path resolution.

## Requirements

### Requirement: JavaVM pointer is registered with WinPR before FreeRDP initialisation
The application SHALL assign the `JavaVM*` from `ANativeActivity` to WinPR's global `jniVm` variable via `extern "C" JavaVM* jniVm` before any FreeRDP or WinPR function is called.

#### Scenario: jniVm is set at the top of android_main
- **WHEN** `android_main` begins execution on Android
- **THEN** `jniVm` is assigned the value of `app->activity->vm` before `freerdp_context_new` or any WinPR utility function is invoked

#### Scenario: WinPR JNI thread attachment succeeds
- **WHEN** WinPR calls `winpr_jni_attach_thread` during Unicode conversion or timezone lookup
- **THEN** the call succeeds because `jniVm` is non-null and the current thread is attached to the JVM

### Requirement: HOME environment variable is set before FreeRDP initialisation
The application SHALL call `setenv("HOME", app->activity->internalDataPath, 0)` before any FreeRDP context creation. The `0` (no-overwrite) flag SHALL be used to preserve any existing `HOME` value.

#### Scenario: HOME is set to the app's internal data path
- **WHEN** `android_main` begins execution and `app->activity->internalDataPath` is non-null
- **THEN** the `HOME` environment variable is set to the internal data path (e.g. `/data/data/com.hyperdesk.mr/files`)

#### Scenario: HOME is not set when internalDataPath is null
- **WHEN** `android_main` begins execution and `app->activity->internalDataPath` is null
- **THEN** `setenv` is not called and no crash occurs

#### Scenario: Existing HOME is preserved
- **WHEN** the `HOME` environment variable is already set before `android_main` runs
- **THEN** `setenv` with the no-overwrite flag does not replace the existing value

### Requirement: WinPR path resolution succeeds on Android
`GetKnownPath(KNOWN_PATH_HOME)` SHALL return a valid non-null path after the `HOME` environment variable is set.

#### Scenario: FreeRDP context creation succeeds
- **WHEN** `freerdp_context_new` is called after `HOME` has been set
- **THEN** the call returns `true` and WinPR does not log `"GetKnownPath: Path KNOWN_PATH_HOME is 0x0"`

### Requirement: Bootstrap code is guarded by __ANDROID__ preprocessor
All JVM registration and `HOME` environment variable logic SHALL be enclosed in `#ifdef __ANDROID__` / `#endif` blocks to prevent compilation errors on non-Android platforms.

#### Scenario: Code compiles on non-Android targets
- **WHEN** the project is compiled without the `__ANDROID__` preprocessor define
- **THEN** the JVM registration and `setenv` calls are excluded and compilation succeeds

### Requirement: RDP error codes are captured on connection failure
`RdpConnectionManager::RunEventLoop` SHALL call `freerdp_get_last_error(instance_->context)` when `freerdp_connect` returns `FALSE` and store the resulting error code.

#### Scenario: Error code is captured on connection failure
- **WHEN** `freerdp_connect` returns `FALSE`
- **THEN** the specific FreeRDP error code is retrieved via `freerdp_get_last_error` and made available to the feedback layer

#### Scenario: No error code on successful connection
- **WHEN** `freerdp_connect` returns `TRUE`
- **THEN** no error code is stored and the connection proceeds normally

### Requirement: Error codes are translated to human-readable strings
An `ErrorUtils` helper SHALL map FreeRDP error codes (e.g. `FREERDP_ERROR_AUTHENTICATION_FAILED`, `FREERDP_ERROR_DNS_NAME_NOT_FOUND`, `FREERDP_ERROR_CONNECT_TRANSPORT_FAILED`) to user-friendly English strings.

#### Scenario: Known error code is translated
- **WHEN** a known FreeRDP error code is passed to `ErrorUtils`
- **THEN** a human-readable string describing the error is returned (e.g. "Access Denied", "Host Not Found", "Network Error")

#### Scenario: Unknown error code returns a generic message
- **WHEN** an unrecognized FreeRDP error code is passed to `ErrorUtils`
- **THEN** a generic "Connection Failed" message is returned with the numeric error code
