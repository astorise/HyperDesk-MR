# SPEC: Multi-Monitor Curved Arc Display

| Field       | Value                                           |
|-------------|-------------------------------------------------|
| Status      | Implemented                                     |
| Branch      | `fix_regression`                                |
| Author      | Sébastien ASTORI                                |
| Date        | 2026-04-05                                      |

## 1. Overview

The application SHALL display 3 virtual monitors arranged on a curved cylindrical arc in the user's VR headset (Meta Quest 3). Each monitor corresponds to a separate Windows display declared at RDP connection time. The arc follows the geometry of a regular decagon (10-sided polygon), with the 3 screens occupying 3 consecutive sides.

## 2. Motivation

A single flat screen in VR wastes peripheral vision. Multiple flat screens at close range overlap at their edges. A cylindrical arc keeps all screen areas equidistant from the viewer's eyes and eliminates edge overlap, simulating a curved multi-monitor desk setup.

## 3. Monitor Geometry

### 3.1 Decagonal Arc Parameters

| Parameter                  | Value     | Notes                                       |
|----------------------------|-----------|---------------------------------------------|
| Number of monitors         | 3         | Center, Left, Right                         |
| Polygon sides              | 10        | Regular decagon                             |
| Angular step per side      | 36°       | 2π / 10 radians                             |
| Cylinder radius            | 1.6 m     | Distance from cylinder center to screen     |
| Cylinder center offset     | -0.5 m    | Behind the viewer along gaze direction      |
| Screen resolution (each)   | 1920×1080 | Full HD, 16:9                               |
| Screen size (each)         | 1.92×1.08 m | Physical quad size in world space          |

### 3.2 Monitor Assignments

| Index | Role    | Yaw angle | Windows desktop position |
|-------|---------|-----------|--------------------------|
| 0     | Center  | 0°        | Left=1920 (primary)      |
| 1     | Left    | +36°      | Left=0                   |
| 2     | Right   | -36°      | Left=3840                |

### 3.3 Cylinder Composition Layers

Each monitor SHALL be rendered as an `XrCompositionLayerCylinderKHR` (OpenXR extension `XR_KHR_composition_layer_cylinder`):

- **Pose**: Positioned at the cylinder center (viewer position + anchor offset), with per-monitor yaw rotation.
- **Radius**: 1.6 m.
- **Central angle**: 36° (one decagon step) — screens are edge-to-edge with no gap.
- **Aspect ratio**: 16:9 (1.778).

### 3.4 Anchoring

When a QR code is scanned:
1. The system captures the headset pose (position + orientation).
2. The horizontal forward direction is extracted (Y component zeroed).
3. The cylinder center is placed **0.5 m behind** the viewer along the gaze direction.
4. The anchor yaw is derived from the gaze direction.
5. All 3 monitors are positioned relative to this anchor using quaternion composition: `anchorYaw × perMonitorYaw`.

The anchor MAY be refreshed when the DisplayControl channel applies a new monitor configuration.

## 4. RDP Multi-Monitor Declaration

### 4.1 Connection-Time Declaration

At RDP connection time, the client SHALL declare 3 monitors via FreeRDP settings:

- `FreeRDP_UseMultimon = TRUE`
- `FreeRDP_ForceMultimon = TRUE`
- `FreeRDP_HasMonitorAttributes = TRUE`
- `FreeRDP_MonitorCount = 3`

The `MonitorDefArray` SHALL define:
- Monitor 0: x=1920, primary, 1920×1080
- Monitor 1: x=0, 1920×1080
- Monitor 2: x=3840, 1920×1080

Physical attributes: 527×296 mm, landscape orientation, 100% DPI scale.

### 4.2 DisplayControl Channel

After channel negotiation, a LAYOUT PDU SHALL be sent with the same 3-monitor topology. The `DISPLAY_CONTROL_MONITOR_PRIMARY` flag SHALL be set on monitor index 0.

### 4.3 Software Fallback (GDI/CLEARCODEC)

When the server sends a single large surface (5760×1080) instead of 3 separate H.264 streams:

- The GDI framebuffer SHALL be split into 3 crops:
  - Monitor 0 (center): offsetX=1920, width=1920
  - Monitor 1 (left): offsetX=0, width=1920
  - Monitor 2 (right): offsetX=3840, width=1920
- Each crop SHALL be submitted to the corresponding `VirtualMonitor` via `SubmitSoftwareFrame()`.

## 5. Audio Playback

### 5.1 RDP Audio Channel

The client SHALL enable the `rdpsnd` static channel:

- `FreeRDP_AudioPlayback = TRUE`
- `FreeRDP_AudioCapture = FALSE`

### 5.2 Backend

FreeRDP SHALL be compiled with `CHANNEL_RDPSND=ON`, `CHANNEL_RDPSND_CLIENT=ON`, and `WITH_OPENSLES=ON`. The OpenSL ES backend is automatically selected on Android for audio output on the Quest 3.

## 6. Bluetooth Input Forwarding

### 6.1 Scope

The system SHALL forward Android input events from Bluetooth-connected keyboards and mice to the RDP session.

### 6.2 Keyboard

- The `RdpInputForwarder` component SHALL map Android keycodes (`AKEYCODE_*`) to RDP scancodes (`RDP_SCANCODE_*`).
- Supported keys:
  - Letters: A–Z
  - Digits: 0–9
  - Function keys: F1–F12
  - Modifiers: Shift (L/R), Ctrl (L/R), Alt (L/R), Meta/Win (L/R)
  - Control keys: Enter, Escape, Backspace, Delete, Tab, Space, Insert, Caps Lock, Num Lock, Scroll Lock
  - Navigation: Arrow keys, Home, End, Page Up, Page Down
  - Symbols: `-`, `=`, `[`, `]`, `\`, `;`, `'`, `` ` ``, `,`, `.`, `/`
  - System: Print Screen, Pause/Break
- Key events SHALL be sent via `freerdp_input_send_keyboard_event_ex()` with down/up state and repeat flag.

### 6.3 Mouse

- Mouse events SHALL be forwarded only when `AInputEvent_getSource()` includes `AINPUT_SOURCE_MOUSE`.
- Supported actions:
  - **Move / Hover**: `PTR_FLAGS_MOVE`
  - **Button press**: `PTR_FLAGS_DOWN` combined with `PTR_FLAGS_BUTTON1/2/3`
  - **Button release**: Button flags without `PTR_FLAGS_DOWN`
  - **Vertical scroll**: `PTR_FLAGS_WHEEL` with signed magnitude (×120)
  - **Horizontal scroll**: `PTR_FLAGS_HWHEEL` with signed magnitude (×120)
- Coordinates SHALL be clamped to `[0, desktopWidth-1] × [0, desktopHeight-1]`.
- Button state SHALL be determined via `AMotionEvent_getButtonState()`.

### 6.4 Integration

- The `RdpInputForwarder` SHALL be attached to the `freerdp` instance on `OnPostConnect` and detached on `OnPostDisconnect`.
- The Android native activity's `onInputEvent` callback SHALL delegate to `RdpInputForwarder::OnInputEvent()`.

## 7. OpenXR Extensions

The following OpenXR extensions SHALL be enabled when available:

| Extension                              | Purpose                              |
|----------------------------------------|--------------------------------------|
| `XR_KHR_android_create_instance`       | Android instance creation            |
| `XR_KHR_vulkan_enable2`               | Vulkan graphics binding              |
| `XR_FB_passthrough`                    | MR passthrough                       |
| `XR_FB_camera_access`                  | Camera for QR scanning               |
| `XR_KHR_composition_layer_cylinder`    | Curved screen rendering              |

## 8. Build Requirements

| Dependency  | Version | Configuration                                         |
|-------------|---------|-------------------------------------------------------|
| FreeRDP     | 3.8.0   | Static libs, CHANNEL_RDPSND=ON, WITH_OPENSLES=ON      |
| OpenSSL     | 3.x     | Prebuilt .so for Android arm64-v8a                     |
| NDK         | r29     | API level 32, arm64-v8a                                |
| ZXing-cpp   | 2.2.1   | QR code decoding                                      |
| OpenXR      | Meta XR SDK | Prebuilt libopenxr_loader.so                      |
