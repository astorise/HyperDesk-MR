## MODIFIED Requirements

### Requirement: Monitors outside the headset field of view pause decoding
For each frame, the application SHALL compute a dot product between the headset's forward gaze vector (derived from `XrView`) and the direction vector to each monitor's center. Monitors whose dot product falls below a configured FOV threshold SHALL suppress presentation of their monitor quad, but the application SHALL NOT call `AMediaCodec_flush`, `AMediaCodec_stop`, or destroy decoder resources as part of frustum culling.

#### Scenario: Dot-product check detects a monitor outside the FOV without tearing down the codec
- **WHEN** the render loop computes per-monitor visibility and the dot product of the headset forward vector and a monitor's center direction is less than the FOV threshold
- **THEN** the monitor's rendering is hidden while its existing `AMediaCodec` instance remains alive

### Requirement: Monitors that re-enter the field of view resume decoding
When a previously culled monitor's dot-product rises above the FOV threshold, the application SHALL resume presenting the existing decoder output and restore the corresponding `XrCompositionLayerQuad` to the `xrEndFrame` layer array without restarting the codec instance.

#### Scenario: Monitor moves back into FOV and presentation resumes on the existing decoder
- **WHEN** the dot product of the headset forward vector and a previously culled monitor's direction rises above the FOV threshold
- **THEN** the application resumes presenting that monitor's quad using the same live decoder instance
