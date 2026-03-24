# Technical Specifications: RDP Network to Decoder Pipeline

## Architecture Component: FreeRDP Core
- **Threading:** The FreeRDP connection and event loop (`freerdp_connect`, `freerdp_check_event_handles`) MUST run on a dedicated standard `std::thread`. It must never block the OpenXR `xrWaitFrame` / `xrEndFrame` loop.
- **Context Setup:** Initialize `freerdp` instance, configure settings (hostname, username, password, ignore certificate warnings for local testing).
- **Settings:** Disable legacy features. Enable `SupportGraphicsPipeline` and force the use of the `[MS-RDPEGFX]` channel.

## Architecture Component: Graphics Pipeline (`[MS-RDPEGFX]`)
- **Initialization:** Use FreeRDP's Dynamic Virtual Channel (DVC) API to register the `RdpgfxClientContext`.
- **Capability Negotiation:** Implement the `CapsAdvertise` callback to tell the Windows server that the client supports `RDPGFX_CAPVERSION_10` (or appropriate version for H.264 AVC420/AVC444).
- **Data Extraction:** Implement the `SurfaceCommand` or H.264 specific callbacks within `RdpgfxClientContext` to capture the incoming H.264 byte stream.

## Architecture Component: MediaCodec Injection Bridge
- **Input Buffers:** The compressed H.264 bitstream (NAL units) extracted from FreeRDP must be copied into the hardware decoder's input memory.
- **Clarification on Zero-Copy:** While the *output* of the decoder is zero-copy (GPU to GPU), the *input* MUST be copied by the CPU from the network buffer to the `AMediaCodec` input buffer.
- **Execution:** 1. Call `AMediaCodec_dequeueInputBuffer` with a timeout.
  2. If a buffer is available, `memcpy` the FreeRDP H.264 payload into it.
  3. Call `AMediaCodec_queueInputBuffer` to submit it to the hardware for decoding.
- **Synchronization:** Use `std::mutex` or lock-free queues if the FreeRDP network thread and the `AMediaCodec` asynchronous callbacks need to share state safely.

## Error Handling
- Use the `DebugUtils.h` macros defined previously.
- Log FreeRDP connection errors, DVC channel failures, and input buffer timeouts heavily to logcat.