# Technical Specifications: Error Handling

## Error Retrieval Logic
- **API:** Call `freerdp_get_last_error(instance_->context)` when `freerdp_connect` returns `FALSE`.
- **Mapping:** Map codes like `FREERDP_ERROR_AUTHENTICATION_FAILED` or `FREERDP_ERROR_DNS_NAME_NOT_FOUND` to user-friendly strings.

## Visual Feedback (The "Status Quad")
- **Implementation:** Create a specialized `VirtualMonitor` instance or a dedicated `StatusLayer` class.
- **Rendering:** Use a small `XrCompositionLayerQuad` placed in front of the user (e.g., at `z = -1.5m`).
- **Texture:** Since we avoid UI engines, use a pre-rendered "Atlas" texture containing basic error icons/text, or a simple dynamic texture generator (STB_truetype or similar) to write the error message to a Vulkan-backed swapchain.

## Workflow Integration
- **Scanning:** Show "Ready to Scan..."
- **Connecting:** Show "Connecting to [Host]..."
- **Error:** Show the specific error message and wait 5 seconds before returning to "Ready to Scan".