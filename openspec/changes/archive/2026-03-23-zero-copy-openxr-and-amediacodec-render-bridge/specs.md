# Technical Specifications: MediaCodec to OpenXR Bridge

## Error Handling & Logging (Strict Requirement)
The AI must implement and use strict error-checking macros or inline functions for all C API calls. Silent failures are unacceptable.
- **Logging:** Use `<android/log.h>` (`__android_log_print`) with a specific tag (e.g., `"HyperDeskMR"`).
- **OpenXR:** All `xr*` functions returning `XrResult` must be wrapped in a check using the `XR_FAILED()` or `XR_SUCCEEDED()` macros. If an error occurs, log the specific `XrResult` code.
- **Vulkan:** All `vk*` functions returning `VkResult` must be checked against `VK_SUCCESS`.
- **MediaCodec:** All `AMediaCodec_*` functions must be checked against `AMEDIA_OK`. 

## Architecture Component: Video Decoder (`AMediaCodec`)
- **API:** Android NDK `AMediaCodec`.
- **Format:** Configured for `"video/avc"` (H.264).
- **Asynchronous Mode:** Use `AMediaCodec_setAsyncNotifyCallback` to handle input/output buffer availability without blocking the main render loop.

## Architecture Component: Zero-Copy Memory Bridge (`AImageReader`)
- **API:** Android NDK `AImageReader` or `AHardwareBuffer`.
- **Configuration:** Set usage flags to `AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE`.
- **Constraint:** DO NOT map the buffer to CPU memory. The decoded frame must remain in GPU memory.

## Architecture Component: Render Loop (OpenXR)
- **Graphics API:** Vulkan (`XrGraphicsBindingVulkanKHR`).
- **Swapchain:** Create an OpenXR swapchain (`xrCreateSwapchain`) mapped to the Vulkan format of the decoded images.
- **Composition Layer:** During `xrEndFrame`, populate an `XrCompositionLayerQuad` setting its space to `XR_REFERENCE_SPACE_TYPE_LOCAL` and its swapchain to the one updated by `AImageReader`.