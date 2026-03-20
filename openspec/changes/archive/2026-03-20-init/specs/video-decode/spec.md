## ADDED Requirements

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
