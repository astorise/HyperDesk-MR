# Tasks

- [x] Task 1: Initialize the Android NDK project structure with CMake, linking OpenXR, Vulkan, and FreeRDP 3.x static libraries.
- [x] Task 2: Implement the OpenXR initialization sequence, including the `XR_FB_passthrough` extension and Vulkan swapchain creation.
- [x] Task 3: Create the FreeRDP connection manager and configure the `DispClientContext` to open the `Microsoft::Windows::RDS::DisplayControl` channel.
- [x] Task 4: Implement the `DISPLAYCONTROL_CAPS_PDU` callback to negotiate the 16-monitor capability with the Windows host.
- [x] Task 5: Implement the `DISPLAYCONTROL_MONITOR_LAYOUT_PDU` logic to define and send the 4x4 monitor grid coordinates to the server.
- [x] Task 6: Create a hardware decoder wrapper class using `AMediaCodec` configured for low-latency H.264 video playback.
- [x] Task 7: Implement the zero-copy memory bridge binding the `AMediaCodec` output surface to the OpenXR swapchain via `AImageReader`.
- [x] Task 8: Update the OpenXR render loop (`xrEndFrame`) to submit an array of 16 `XrCompositionLayerQuad` structures (and the Passthrough layer) mapped to the active decoders.
- [x] Task 9: Add a basic frustum culling function (dot-product) to pause `AMediaCodec` processing for screens currently outside the user's FOV.