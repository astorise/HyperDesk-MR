## ADDED Requirements

### Requirement: AMediaCodec operates in asynchronous callback mode
The `VirtualMonitor` class SHALL configure `AMediaCodec` via `AMediaCodec_setAsyncNotifyCallback` so that `OnInputAvailable` and `OnOutputAvailable` events are handled on dedicated callback threads without blocking the OpenXR render loop. All `AMediaCodec_*` calls MUST be wrapped in error-checking macros that verify the return value against `AMEDIA_OK` and log failures via `__android_log_print` with the tag `"HyperDeskMR"`.

#### Scenario: Asynchronous callbacks queue NAL units without blocking the render loop
- **WHEN** compressed H.264 NAL unit data arrives from the RDP GFX channel
- **THEN** the `OnInputAvailable` callback queues it to the decoder input buffer and returns without stalling the render thread, and `OnOutputAvailable` delivers the decoded buffer to the `AImageReader` output surface

#### Scenario: Error-checking macro logs AMediaCodec failures
- **WHEN** any `AMediaCodec_*` function returns a value other than `AMEDIA_OK`
- **THEN** the error-checking macro logs the specific status code to Android logcat under the `"HyperDeskMR"` tag and the application does not silently continue execution

### Requirement: AImageReader is configured for GPU sampling and bound as the decoder output surface
The application SHALL create an `AImageReader` instance with usage flag `AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE`, acquire its `ANativeWindow`, assert its validity, and bind it as the output surface for the `AMediaCodec` instance, checking the bind result against `AMEDIA_OK`. The decoded frames MUST remain in GPU memory and SHALL NOT be mapped to CPU memory.

#### Scenario: AImageReader surface is bound to AMediaCodec output
- **WHEN** `VirtualMonitor` initializes its decoder
- **THEN** `AImageReader_new` returns a valid reader, its `ANativeWindow` is non-null, and `AMediaCodec_configure` succeeds with that window as the output surface, keeping decoded frames on the GPU
