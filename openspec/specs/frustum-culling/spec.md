## Purpose
Suspend video decoding for virtual monitors that are outside the headset's field of view in order to reduce battery consumption and thermal load.

## Requirements

### Requirement: Monitors outside the headset field of view pause decoding
For each frame, the application SHALL compute a dot product between the headset's forward gaze vector (derived from `XrView`) and the direction vector to each monitor's center. Monitors whose dot product falls below a configured FOV threshold SHALL have their `AMediaCodec` processing suspended to preserve battery and thermal budget.

#### Scenario: Dot-product check detects a monitor outside the FOV and suspends its decoder
- **WHEN** the render loop computes per-monitor visibility and the dot product of the headset forward vector and a monitor's center direction is less than the FOV threshold (e.g., cos(60°))
- **THEN** `AMediaCodec_flush` is called on that monitor's decoder and no new input buffers are queued until the monitor re-enters the FOV

### Requirement: Monitors that re-enter the field of view resume decoding

When a previously culled monitor's dot-product rises above the FOV threshold, the application SHALL resume queuing input buffers to its `AMediaCodec` instance and restore the corresponding `XrCompositionLayerQuad` to the `xrEndFrame` layer array.

#### Scenario: Monitor moves back into FOV and decoding resumes
- **WHEN** the dot product of the headset forward vector and a previously culled monitor's direction rises above the FOV threshold
- **THEN** the application resumes queuing compressed video data to that monitor's `AMediaCodec` and its `XrCompositionLayerQuad` is included in the next `xrEndFrame` call
