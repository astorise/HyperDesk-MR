## ADDED Requirements

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
