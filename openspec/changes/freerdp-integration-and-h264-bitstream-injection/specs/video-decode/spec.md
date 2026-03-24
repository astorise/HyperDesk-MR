## ADDED Requirements

### Requirement: H.264 NAL units extracted from the FreeRDP GFX callback are injected into AMediaCodec input buffers
The application SHALL implement the `SurfaceCommand` (or H.264-specific) callback on `RdpgfxClientContext` to intercept the compressed H.264 bitstream delivered by the RDP GFX channel. The network thread SHALL call `AMediaCodec_dequeueInputBuffer` with a non-zero timeout, `memcpy` the FreeRDP payload into the acquired buffer, and submit it via `AMediaCodec_queueInputBuffer`. Any failure to dequeue an input buffer SHALL be logged to Android logcat under the `"HyperDeskMR"` tag and SHALL NOT crash the application.

#### Scenario: Compressed H.264 payload is extracted from FreeRDP and queued into AMediaCodec
- **WHEN** the FreeRDP GFX `SurfaceCommand` (H.264 frame) callback fires on the network thread
- **THEN** `AMediaCodec_dequeueInputBuffer` returns a valid buffer index, the NAL unit data is `memcpy`'d into that buffer, and `AMediaCodec_queueInputBuffer` submits it to the hardware decoder without error

#### Scenario: Input buffer dequeue timeout is logged and the frame is dropped gracefully
- **WHEN** `AMediaCodec_dequeueInputBuffer` returns a negative index (no buffer available within the timeout)
- **THEN** the failure is logged with the specific return code under the `"HyperDeskMR"` tag and the GFX callback returns without crashing or blocking the network thread
