# Change Proposal: FreeRDP Integration and H.264 Bitstream Injection

## Description
Integrate the FreeRDP 3.x core library to establish a connection with a Windows host. Implement the Graphics Pipeline Extension (`[MS-RDPEGFX]`) to negotiate an H.264/AVC video stream. Finally, extract the compressed H.264 NAL units from the network packets and feed them into the `AMediaCodec` input buffers designed in the previous change.

## Motivation
To achieve high-fidelity, low-bandwidth screen streaming, RDP must be configured to use modern graphics pipelines rather than legacy bitmap drawing orders. By utilizing `[MS-RDPEGFX]` with H.264 encoding, the Windows host does the heavy lifting via its own GPU, and the Quest 3 only receives a lightweight compressed video stream.

## User Goals
- The application successfully connects to a remote Windows machine using standard RDP credentials.
- The user sees a live, moving desktop rather than a static frame, with minimal network latency.

## Acceptance Criteria
- [ ] FreeRDP instance is initialized and runs on a dedicated background network thread (avoiding OpenXR frame drops).
- [ ] `RdpgfxClientContext` is registered to handle modern graphics commands.
- [ ] The client successfully advertises support for the H.264 codec (AVC420 or AVC444) to the Windows host.
- [ ] Compressed H.264 payload data is successfully extracted from FreeRDP callbacks and pushed into `AMediaCodec_dequeueInputBuffer`.
- [ ] Strict error checking is maintained for FreeRDP connection states and threading synchronization.