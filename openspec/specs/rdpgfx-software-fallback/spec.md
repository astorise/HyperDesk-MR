# rdpgfx-software-fallback Specification

## Purpose
TBD - created by archiving change stabilize-quest-rdp-display-pipeline. Update Purpose after archive.
## Requirements
### Requirement: Non-AVC rdpgfx sessions still produce a visible desktop
When the RDP server sends `CLEARCODEC` or `CAPROGRESSIVE` surface commands instead of `AVC420`, the application SHALL chain FreeRDP's existing `SurfaceCommand` handler so GDI can decode the desktop, then upload the resulting BGRA pixels into an OpenXR swapchain-backed monitor quad.

#### Scenario: ClearCodec session renders through software fallback
- **WHEN** the server delivers `CLEARCODEC` surface commands for the active desktop
- **THEN** GDI decodes the desktop and the application uploads a software frame to a visible monitor quad

#### Scenario: Progressive session renders through software fallback
- **WHEN** the server delivers `CAPROGRESSIVE` surface commands for the active desktop
- **THEN** the application presents the desktop through the software upload path without requiring `AVC420`

### Requirement: AVC420 remains the preferred presentation path
The application SHALL preserve the existing hardware-decoded AVC420 path and SHALL only use the software fallback for non-AVC `rdpgfx` sessions.

#### Scenario: H.264 session bypasses the software fallback
- **WHEN** `AVC420` surface commands are received from the server
- **THEN** the compressed H.264 payload is submitted to the monitor hardware decoder instead of the software upload path

