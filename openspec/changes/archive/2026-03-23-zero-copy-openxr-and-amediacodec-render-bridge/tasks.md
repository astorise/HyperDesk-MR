# Tasks

- [x] Task 1: Create a `DebugUtils.h` file containing error-checking macros for `XrResult`, `VkResult`, and `media_status_t` that log to Android logcat using `<android/log.h>`.
- [x] Task 2: Create a `VirtualMonitor.h` and `VirtualMonitor.cpp` class skeleton.
- [x] Task 3: Implement `AMediaCodec` initialization for `"video/avc"` inside the `VirtualMonitor` class, wrapping all setup calls in the new error-checking macros.
- [x] Task 4: Create an `AImageReader` instance configured for GPU sampling, acquire its native `ANativeWindow`, and assert its validity.
- [x] Task 5: Bind the `AImageReader`'s `ANativeWindow` as the output surface for the `AMediaCodec` instance, checking for `AMEDIA_OK`.
- [x] Task 6: Implement `AMediaCodec` asynchronous callbacks (`OnInputAvailable`, `OnOutputAvailable`) to process NAL units, adding error logs if buffers cannot be dequeued.
- [x] Task 7: In the OpenXR initialization phase, implement Vulkan-specific swapchain creation (`xrCreateSwapchain`), wrapping it in OpenXR and Vulkan error checks.
- [x] Task 8: Create a method to safely acquire the latest image from `AImageReader` and bind it to the active OpenXR swapchain image.
- [x] Task 9: Update the main OpenXR render loop to call a `GetCompositionLayer()` method on the `VirtualMonitor` class.
- [x] Task 10: Populate and return an `XrCompositionLayerQuad` pointer during `xrEndFrame`, ensuring no null pointers are passed to the compositor.