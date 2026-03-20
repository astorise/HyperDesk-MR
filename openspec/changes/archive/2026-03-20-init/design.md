# Technical Specifications: Quest 3 RDP Client

## Architecture
The application acts as a zero-overhead bridge between the network and the Quest compositor. 
1. **Network Thread:** Runs FreeRDP 3.x event loop independently.
2. **Decoder Pool:** Uses up to 16 `AMediaCodec` instances for hardware-accelerated H.264/AVC decoding (`[MS-RDPEGFX]`).
3. **Render Loop:** Uses OpenXR 1.0. Binds `AMediaCodec` output surfaces directly to OpenXR swapchains using `AImageReader` (Zero-copy path).

## Technology Stack
- **Language:** C++ 20 (compiled via Android NDK / Clang).
- **XR API:** Meta XR Native SDK (OpenXR).
- **RDP Core:** FreeRDP 3.x (Static library for Android).
- **Graphics API:** Vulkan (for OpenXR swapchain initialization only).

## RDP Protocol Details
- **Channel:** `Microsoft::Windows::RDS::DisplayControl`
- **PDU Handling:** - Send/Receive `DISPLAYCONTROL_CAPS_PDU` asserting support for 16 monitors.
  - Send `DISPLAYCONTROL_MONITOR_LAYOUT_PDU` defining a logical 4x4 grid (e.g., each monitor 1920x1080).

## Optimization Constraints
- **NO Game Engines:** Strict prohibition on Unity, Unreal, or Godot.
- **NO Meshes:** Screens must not be rendered as textures on 3D geometry. They must be submitted as an array of `XrCompositionLayerQuad` pointers during `xrEndFrame`.
- **Frustum Culling:** Implement a dot-product check against the headset pose. Monitors outside the Field of View (FOV) must drop frame decoding or request lower refresh rates to preserve battery.