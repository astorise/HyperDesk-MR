## Context

HyperDesk-MR is a Quest 3 native application that combines three subsystems which all compete for device resources during startup:

1. Quest passthrough camera capture for QR-based RDP credentials
2. FreeRDP 3.x session bootstrap with `disp` and `rdpgfx`
3. Up to 16 OpenXR-backed monitor surfaces with per-monitor video decoders

The field failure was not a single bug. It was a chain:

- Adding `gdi_init()` fixed immediate FreeRDP disconnects, but exposed Quest camera instability (`CameraManager: device error 4`) during startup when decoders were also created.
- After camera stability returned, some Windows servers still negotiated `CLEARCODEC` / `CAPROGRESSIVE` instead of `AVC420`, so the headset connected successfully but displayed no monitors.
- After software fallback was added, the first desktop still appeared missing because the degraded single-monitor layout was presented in the top-left position of the normal 4x4 monitor wall rather than centered in front of the user.

The change therefore spans camera startup sequencing, FreeRDP/GDI bootstrap, `rdpgfx` callback ownership, software rendering fallback, frustum-culling semantics, and degraded monitor layout.

## Goals / Non-Goals

**Goals:**
- Preserve a reliable Quest 3 QR scan phase before RDP video resources are created.
- Keep FreeRDP 3.x stable on Android by satisfying its GDI/client-common bootstrap assumptions.
- Render at least one desktop on Quest even when the server refuses to negotiate AVC420/H.264.
- Preserve the existing zero-copy H.264 path when AVC420 is available.
- Make the degraded single-monitor fallback visible without requiring the user to look toward the top-left of the 4x4 wall.

**Non-Goals:**
- Implement full multi-monitor software rendering for every non-AVC codec path.
- Change Windows server policy or guarantee AVC420 from every RDP host.
- Replace Quest Guardian / interstitial behavior.
- Rewrite the monitor compositor or remove the existing hardware decode path.

## Decisions

### 1. Split startup into a scan phase and a render phase

**Decision:** Keep the QR scanner active first, and delay `VirtualMonitor::InitCodec()` for all monitors until after a valid QR scan. Stop the camera before decoder allocation and before `rdpManager->Connect()`.

**Why:** The observed Quest failure was `CameraManager: device error 4` only after GDI and the monitor bootstrap were added. The simplest explanation, confirmed by experiment, was startup resource pressure. Reserving the initial window for camera-only work restored stable QR scanning without discarding the later RDP path.

**Alternatives considered:**
- *Keep eager decoder creation and reduce monitor count permanently:* rejected because it weakens the product goal and still leaves scan stability hostage to startup pressure.
- *Move GDI loading later with `dlopen` / split library loading:* rejected as a larger structural change than needed once lazy decoder creation fixed the camera failure.

### 2. Treat FreeRDP client-common bootstrap as authoritative

**Decision:** Keep `gdi_init(instance, PIXEL_FORMAT_BGRA32)`, install `BeginPaint`, `EndPaint`, and `DesktopResize`, and chain the client-common `rdpgfx` callbacks (`CapsAdvertise`, `ResetGraphics`, `CreateSurface`, `DeleteSurface`, `StartFrame`, `EndFrame`, mapping callbacks) instead of replacing them with no-op handlers.

**Why:** FreeRDP 3.x expects the GDI/client-common path to stay coherent even when graphics ultimately travel through `rdpgfx`. Overwriting those callbacks prevented internal bootstrap state from being maintained and either aborted the session or left the application connected with no usable surfaces.

**Alternatives considered:**
- *Run without `gdi_init()`:* rejected because the session dropped immediately after connect.
- *Replace every callback with app-owned implementations:* rejected because it duplicated fragile FreeRDP internals and broke common-channel bootstrap behavior.

### 3. Keep AVC420 as the preferred path, but accept codec reality

**Decision:** Continue to advertise H.264 aggressively, but add a software fallback path for `CLEARCODEC` / `CAPROGRESSIVE` instead of assuming the server will honor AVC420.

**Why:** On real hardware, the logs proved the server still selected `CLEARCODEC(8)` and `CAPROGRESSIVE(9)` after the AVC-only negotiation attempt. Since connection success without pixels is worse than a slower first desktop, the client needs a recovery path rather than a pure negotiation strategy.

**Alternatives considered:**
- *Keep forcing AVC and rely on server-side settings:* rejected because it makes Quest rendering dependent on opaque Windows policy and failed on the test hosts.
- *Implement every software codec natively in app code:* rejected because FreeRDP/GDI already performs that decode work.

### 4. Reuse FreeRDP/GDI output instead of inventing a second software desktop path

**Decision:** For non-AVC surface commands, chain FreeRDP's prior `SurfaceCommand` callback so GDI decodes the desktop, read the resulting BGRA framebuffer, and upload it into an OpenXR swapchain image through a CPU staging buffer.

**Why:** This reuses the codec support FreeRDP already has and limits new work to presentation, not decompression. It also preserves the existing zero-copy path for H.264 and only pays the CPU upload cost when the server forces the fallback.

**Alternatives considered:**
- *Map unsupported codec IDs to a blank quad and log only:* rejected because it leaves the user with a connected but useless session.
- *Add a new standalone software renderer outside the existing swapchain abstraction:* rejected because it would duplicate presentation plumbing already solved by `HdSwapchain`.

### 5. Degrade to one centered monitor when software fallback is active

**Decision:** When the effective layout collapses to one monitor, keep only monitor 0 active and place it at `(0, 0, kDepth)` in world space.

**Why:** The first software desktop was already reaching the OpenXR swapchain, but the user still saw nothing because monitor 0 inherited the top-left position from the normal 4x4 wall. Centering the degraded layout turns a technically correct but invisible recovery path into a visible one.

**Alternatives considered:**
- *Keep the original 4x4 positions:* rejected because the fallback monitor can stay outside the user's forward view.
- *Spawn a special overlay-only desktop quad:* rejected because monitor layout already owns placement and visibility.

### 6. Make frustum culling non-destructive to decoders

**Decision:** Pause monitor presentation without calling `AMediaCodec_stop`, `AMediaCodec_flush`, or any restart path. Resume by showing the monitor again while leaving the codec alive.

**Why:** The previous stop/restart approach produced repeated `resume start failed` failures and prevented monitors from reappearing after culling. The product goal is thermal reduction, not decoder reinitialization churn.

**Alternatives considered:**
- *Continue stopping codecs out of view:* rejected because it broke real sessions.
- *Disable frustum culling entirely:* rejected because it removes an intended battery/thermal control.

## Risks / Trade-offs

- **Software fallback is slower than zero-copy H.264** → Mitigation: keep AVC420 as the preferred path and use CPU upload only for non-AVC sessions.
- **Fallback currently targets a single visible desktop, not a full 16-monitor software wall** → Mitigation: codify the degraded behavior explicitly so it is predictable and testable.
- **The GDI fallback depends on FreeRDP internal framebuffer state staying stable across version bumps** → Mitigation: document the dependency in specs and revalidate on FreeRDP upgrades.
- **Quest startup still depends on camera-first sequencing** → Mitigation: keep decoder creation behind the scan-to-connect handoff and preserve the known-good 640x480 scan profile.

## Migration Plan

1. Ship the Quest build with lazy decoder boot, FreeRDP bootstrap chaining, and the software fallback path enabled.
2. Validate on Quest against at least one AVC-capable host and one ClearCodec/Progressive host.
3. If regressions appear in production, roll back to the previous CI build while keeping the OpenSpec change for the recovered design record.

## Open Questions

- Should the software fallback evolve from a single centered desktop into a true multi-monitor software wall?
- Can host-side RDP policy be constrained enough to prefer AVC420 consistently, reducing use of the software path?
- Do we want an explicit user-visible indication when the session is running on software fallback instead of hardware AVC?
