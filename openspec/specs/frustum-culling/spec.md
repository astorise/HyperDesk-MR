## Purpose
Suspend video decoding for virtual monitors that are outside the headset's field of view or beyond the carousel visibility arc, in order to reduce battery consumption and thermal load.

## Requirements

### Requirement: Monitors outside the headset field of view pause decoding
For each frame, the `FrustumCuller` SHALL compute a dot product between the headset's forward gaze vector (derived from `XrView`) and the direction vector to each monitor's center. Monitors whose dot product falls below a configured FOV threshold SHALL suppress presentation of their monitor cylinder layer, but the application SHALL NOT call `AMediaCodec_flush`, `AMediaCodec_stop`, or destroy decoder resources as part of frustum culling.

#### Scenario: Dot-product check detects a monitor outside the FOV without tearing down the codec
- **WHEN** the render loop computes per-monitor visibility and the dot product of the headset forward vector and a monitor's center direction is less than the FOV threshold
- **THEN** the monitor's rendering is hidden while its existing `AMediaCodec` instance remains alive

### Requirement: Monitors that re-enter the field of view resume decoding
When a previously culled monitor's dot-product rises above the FOV threshold, the application SHALL resume presenting the existing decoder output and restore the corresponding `XrCompositionLayerCylinderKHR` to the `xrEndFrame` layer array without restarting the codec instance.

#### Scenario: Monitor moves back into FOV and presentation resumes on the existing decoder
- **WHEN** the dot product of the headset forward vector and a previously culled monitor's direction rises above the FOV threshold
- **THEN** the application resumes presenting that monitor's cylinder layer using the same live decoder instance

### Requirement: Carousel visibility culling hides monitors beyond ±120°
The `MonitorLayout::IsMonitorInView()` method SHALL compute the effective yaw angle of each monitor as `MonitorBaseYaw(index) + scrollYaw_` and return `false` when the absolute value exceeds 120° (2.094 rad). The `XrCompositor` SHALL skip monitors for which `IsMonitorInView()` returns `false`, preventing cylinder layers from being submitted for monitors behind the user.

#### Scenario: Monitor within ±120° is rendered
- **WHEN** a monitor's effective yaw angle (base yaw + scroll offset) has an absolute value ≤ 120°
- **THEN** `IsMonitorInView()` returns `true` and the monitor's cylinder layer is submitted to `xrEndFrame`

#### Scenario: Monitor beyond ±120° is hidden
- **WHEN** a monitor's effective yaw angle exceeds ±120°
- **THEN** `IsMonitorInView()` returns `false` and the monitor's cylinder layer is not submitted

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
