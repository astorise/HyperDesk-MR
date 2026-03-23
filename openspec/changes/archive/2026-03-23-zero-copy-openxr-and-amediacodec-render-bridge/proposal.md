# Change Proposal: Zero-Copy OpenXR & AMediaCodec Render Bridge

## Description
Implement the core rendering pipeline that bridges Android's native hardware video decoder (`AMediaCodec`) directly to OpenXR's display system (`XrCompositionLayerQuad`). This includes setting up robust error handling for all native C APIs to prevent silent failures on the Quest 3.

## Motivation
For HyperDesk MR to handle multiple 1080p/4K RDP streams on a mobile chipset (Snapdragon XR2 Gen 2) without thermal throttling, CPU-side memory copies must be strictly avoided. Furthermore, dealing with Android NDK, Vulkan, and OpenXR requires rigorous error checking; failing to catch an invalid swapchain state or a decoder underflow will crash the compositor.

## User Goals
- RDP video frames are hardware-decoded and displayed with sub-millisecond local rendering latency.
- The application remains perfectly stable, logging explicit error messages to Android logcat if a hardware or API failure occurs.

## Acceptance Criteria
- [ ] A C++ wrapper class for `AMediaCodec` is created and configured for H.264 (`"video/avc"`).
- [ ] An `AImageReader` is established to receive decoder output frames natively.
- [ ] An OpenXR Vulkan swapchain is created to match the `AImageReader` output format.
- [ ] The OpenXR `xrEndFrame` loop successfully submits an `XrCompositionLayerQuad`.
- [ ] **Strict error checking** is implemented for every Vulkan, OpenXR, and MediaCodec API call, logging failures explicitly.