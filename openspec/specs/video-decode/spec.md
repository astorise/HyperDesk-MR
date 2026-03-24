## Purpose
Decode RDP video streams using hardware-accelerated AMediaCodec and deliver decoded frames to the OpenXR swapchain via a zero-copy GPU path.

## Requirements

### Requirement: Video streams are decoded via hardware-accelerated AMediaCodec
The application SHALL create one `AMediaCodec` instance per active monitor, configured for H.264/AVC decoding in low-latency mode using the device's hardware decoder.

#### Scenario: Decoder initializes in low-latency mode for H.264
- **WHEN** a monitor stream begins and an `AMediaFormat` is configured with `KEY_MIME = video/avc` and `KEY_LOW_LATENCY = 1`
- **THEN** `AMediaCodec_createDecoderByType` returns a valid codec handle and `AMediaCodec_configure` succeeds with the hardware decoder selected

#### Scenario: Compressed H.264 NAL units are submitted and decoded frames are produced
- **WHEN** compressed video data from the RDP `[MS-RDPEGFX]` channel is queued via `AMediaCodec_queueInputBuffer`
- **THEN** `AMediaCodec_dequeueOutputBuffer` returns a decoded frame within the configured low-latency deadline

### Requirement: Decoded frames reach the OpenXR swapchain without a CPU copy
The application SHALL bind each `AMediaCodec` output surface to an `AImageReader`, then bind the `AImageReader` surface as the OpenXR swapchain image source, establishing a zero-copy GPU path from decoder output to compositor input.

#### Scenario: AMediaCodec output surface is bound to AImageReader and feeds the OpenXR swapchain
- **WHEN** a decoder is initialized for a monitor slot
- **THEN** `AImageReader_new` creates a reader, its `ANativeWindow` surface is passed to `AMediaCodec_configure` as the output surface, and the same `AHardwareBuffer` is accessible to the OpenXR swapchain without any intermediate CPU readback

### Requirement: AMediaCodec operates in asynchronous callback mode
The `VirtualMonitor` class SHALL configure `AMediaCodec` via `AMediaCodec_setAsyncNotifyCallback` so that `OnInputAvailable` and `OnOutputAvailable` events are handled on dedicated callback threads without blocking the OpenXR render loop. All `AMediaCodec_*` calls MUST be wrapped in error-checking macros that verify the return value against `AMEDIA_OK` and log failures via `__android_log_print` with the tag `"HyperDeskMR"`.

#### Scenario: Asynchronous callbacks queue NAL units without blocking the render loop
- **WHEN** compressed H.264 NAL unit data arrives from the RDP GFX channel
- **THEN** the `OnInputAvailable` callback queues it to the decoder input buffer and returns without stalling the render thread, and `OnOutputAvailable` delivers the decoded buffer to the `AImageReader` output surface

#### Scenario: Error-checking macro logs AMediaCodec failures
- **WHEN** any `AMediaCodec_*` function returns a value other than `AMEDIA_OK`
- **THEN** the error-checking macro logs the specific status code to Android logcat under the `"HyperDeskMR"` tag and the application does not silently continue execution

### Requirement: H.264 NAL units extracted from the FreeRDP GFX callback are injected into AMediaCodec input buffers
The application SHALL extract the compressed H.264 bitstream (NAL units) from the FreeRDP `RdpgfxClientContext` surface-command or H.264-specific callback and copy it into the hardware decoder input memory via `AMediaCodec_dequeueInputBuffer` / `memcpy` / `AMediaCodec_queueInputBuffer`. When `AMediaCodec_dequeueInputBuffer` returns a timeout (no buffer available), the frame SHALL be dropped and the timeout SHALL be logged to Android logcat under the `"HyperDeskMR"` tag without crashing or stalling the network thread.

#### Scenario: Compressed H.264 payload is extracted from FreeRDP and queued into AMediaCodec
- **WHEN** the FreeRDP GFX callback delivers a surface command containing a compressed H.264 payload
- **THEN** `AMediaCodec_dequeueInputBuffer` returns a valid buffer index, the payload is copied into the buffer via `memcpy`, and `AMediaCodec_queueInputBuffer` submits it to the hardware decoder

#### Scenario: Input buffer dequeue timeout is logged and the frame is dropped gracefully
- **WHEN** `AMediaCodec_dequeueInputBuffer` returns a timeout value indicating no input buffer is available
- **THEN** the application logs the timeout to logcat under the `"HyperDeskMR"` tag, discards the current H.264 frame, and returns control to the FreeRDP network thread without blocking or crashing

### Requirement: AImageReader is configured for GPU sampling and bound as the decoder output surface
The application SHALL create an `AImageReader` instance with usage flag `AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE`, acquire its `ANativeWindow`, assert its validity, and bind it as the output surface for the `AMediaCodec` instance, checking the bind result against `AMEDIA_OK`. The decoded frames MUST remain in GPU memory and SHALL NOT be mapped to CPU memory.

#### Scenario: AImageReader surface is bound to AMediaCodec output
- **WHEN** `VirtualMonitor` initializes its decoder
- **THEN** `AImageReader_new` returns a valid reader, its `ANativeWindow` is non-null, and `AMediaCodec_configure` succeeds with that window as the output surface, keeping decoded frames on the GPU

### Requirement: H.264 NAL unit extraction logic is covered by GTest unit tests
The codebase SHALL include a `tests/RdpParserTests.cpp` file that uses Google Test to verify H.264 NAL unit extraction from mock RDP payloads. Tests MUST cover: correct identification of 3-byte start codes (`0x000001`), correct identification of 4-byte start codes (`0x00000001`), handling of multiple NAL units in a single buffer, and graceful handling of corrupted or truncated payloads without crashing.

#### Scenario: Parser identifies a 4-byte H.264 start code in a mock payload
- **WHEN** `RdpParserTests` feeds a buffer beginning with `0x00 0x00 0x00 0x01` to the NAL unit extractor
- **THEN** the extractor identifies a valid NAL unit boundary at offset 0 and the test asserts the extracted unit length is correct

#### Scenario: Parser handles a corrupted payload without crashing
- **WHEN** `RdpParserTests` feeds a buffer with no valid start code to the NAL unit extractor
- **THEN** the extractor returns zero NAL units and the test verifies this with `EXPECT_NO_FATAL_FAILURE`
