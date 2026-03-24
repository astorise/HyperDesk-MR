# Tasks

- [x] Task 1: Update CMakeLists.txt to properly link the statically compiled FreeRDP 3.x libraries and their dependencies (OpenSSL, WinPR, etc.).
- [x] Task 2: Create an `RdpClient.h` and `RdpClient.cpp` wrapper class.
- [x] Task 3: Implement the FreeRDP context initialization, setting connection parameters and enabling the `SupportGraphicsPipeline` flag.
- [x] Task 4: Create a dedicated background `std::thread` to run the FreeRDP event loop (`freerdp_check_event_handles` and `freerdp_check_ipc_channel`).
- [x] Task 5: Register the `RdpgfxClientContext` DVC plugin during the `PostConnect` phase of FreeRDP.
- [x] Task 6: Implement the `CapsAdvertise` callback within the GFX context to request H.264 (AVC) encoding from the Windows host.
- [x] Task 7: Implement the GFX data callback to intercept the incoming H.264 bitstream payload.
- [x] Task 8: In the GFX data callback, request an input buffer from the `VirtualMonitor`'s `AMediaCodec` instance using `AMediaCodec_dequeueInputBuffer`.
- [x] Task 9: Safely `memcpy` the network payload into the acquired input buffer.
- [x] Task 10: Submit the filled buffer to the hardware decoder using `AMediaCodec_queueInputBuffer` and log any queue failures.