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

### Requirement: Frustum culling dot-product logic is covered by GTest unit tests
The codebase SHALL include a `tests/MathUtilsTests.cpp` file that uses Google Test to verify the frustum culling dot-product computation. Tests MUST cover: a monitor within the FOV producing a dot product above the threshold, a monitor outside the FOV producing a dot product below the threshold, and boundary cases at exactly the threshold value. All tests MUST pass without requiring a physical headset or OpenXR runtime.

#### Scenario: Within-FOV monitor is correctly identified as visible
- **WHEN** `MathUtilsTests` computes the dot product for a monitor whose center is directly in front of the headset gaze vector
- **THEN** the result is greater than the FOV threshold (e.g., cos(60°)) and the test asserts `IsVisible == true`

#### Scenario: Out-of-FOV monitor is correctly identified as culled
- **WHEN** `MathUtilsTests` computes the dot product for a monitor positioned 90° off-axis from the headset gaze vector
- **THEN** the result is less than the FOV threshold and the test asserts `IsVisible == false`

#### Scenario: Invalid gaze vector is handled without crashing
- **WHEN** `MathUtilsTests` passes a zero-length gaze vector to the culling function
- **THEN** the function does not crash and the test asserts `EXPECT_NO_FATAL_FAILURE` around the call
