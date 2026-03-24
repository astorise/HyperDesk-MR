# Change Proposal: Unit Testing Suite for Core Logic

## Description
Integrate the Google Test (GTest) framework into the project to validate core logical components. This includes screen layout calculations, frustum culling mathematics, and RDP packet parsing logic. These tests will run in the CI pipeline (GitHub Actions) on every push.

## Motivation
Developing low-level C++ for XR is prone to subtle mathematical errors (e.g., incorrect coordinate offsets for the 16 monitors or faulty dot products for culling). Testing these on the headset is slow. Unit tests allow for "Headless" verification of the "Brain" of the app, ensuring that regressions are caught immediately without needing the physical Quest 3.

## User Goals
- Ensure the 4x4 monitor grid is mathematically sound.
- Guarantee that the Frustum Culling logic correctly identifies which screens to pause, saving battery life.
- Verify that RDP bitstream extraction logic doesn't corrupt data before it reaches the decoder.

## Acceptance Criteria
- [ ] Google Test is integrated into the CMake build system.
- [ ] Tests for `DisplayManager` (16-monitor layout) pass.
- [ ] Tests for `CullingUtils` (Math/FOV) pass.
- [ ] Tests for `RdpParser` (NAL unit extraction) pass.
- [ ] GitHub Actions is updated to execute `ctest` and report results.