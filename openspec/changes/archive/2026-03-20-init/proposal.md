# Change Proposal: Quest 3 Ultra-Light RDP Multi-Monitor Client

## Description
Create a standalone, ultra-lightweight Remote Desktop Protocol (RDP) client for Meta Quest 3 operating entirely in Passthrough Mixed Reality. The client must support up to 16 virtual monitors simultaneously.

## Motivation
To provide a massive multi-monitor productivity environment without the heavy overhead, battery drain, and latency associated with traditional 3D game engines (like Unity/Unreal). It needs to be as close to the metal as possible to preserve the Quest's thermal budget.

## User Goals
- Connect to a Windows host via RDP.
- See up to 16 virtual monitors arranged in a spatial grid within the real-world environment (Mixed Reality).
- Experience low latency and high text legibility.

## Acceptance Criteria
- [ ] Connects to a Windows host using FreeRDP.
- [ ] Activates the `[MS-RDPEDISP]` channel to negotiate 16 screens.
- [ ] Renders the RDP streams using OpenXR `XrCompositionLayerQuad` (no 3D meshes).
- [ ] Decodes video streams using Android NDK `AMediaCodec` via hardware acceleration.
- [ ] Displays the physical environment using OpenXR Passthrough.