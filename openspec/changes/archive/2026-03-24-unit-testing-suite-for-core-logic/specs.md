# Technical Specifications: Unit Testing Architecture

## Framework & Integration
- **Framework:** Google Test (GTest) & Google Mock (GMock).
- **Build System:** CMake integration via `FetchContent` (to keep the repo light) or git submodules.
- **Test Runner:** `CTest` (built into CMake).

## Mocking Strategy
Since the Android NDK and OpenXR headers are not available on a standard Linux CI runner:
- **Separation of Concerns:** Business logic (math, protocol parsing) must be decoupled from hardware-dependent calls (Vulkan, AMediaCodec).
- **Mocks:** Use GMock to simulate the `VirtualMonitor` state if necessary, but prioritize testing "Pure Functions" (Data In -> Data Out).

## Core Test Suites
1. **Layout Tests:** Verify that for a requested 4x4 grid of 1920x1080 screens, the $x,y$ offsets sent to `MS-RDPEDISP` are correct and non-overlapping.
2. **Culling Tests:** Given a headset Pose (Quaternion/Vector) and a Screen Quad position, verify the dot-product result correctly triggers a "Visible" or "Hidden" state.
3. **Protocol Tests:** Feed raw RDP byte arrays into the parser and verify that it correctly identifies H.264 Start Codes (`0x000001` or `0x00000001`).

## Error Handling
- Tests must include "Negative Testing": pass corrupted RDP packets or invalid FOV values to ensure the code handles them gracefully without crashing (using the `DebugUtils.h` macros).