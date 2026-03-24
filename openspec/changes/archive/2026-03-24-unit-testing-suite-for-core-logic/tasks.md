# Tasks

- [x] Task 1: Update `CMakeLists.txt` to include Google Test using `FetchContent`.
- [x] Task 2: Create a `tests/` directory at the root and a `tests/CMakeLists.txt` file.
- [x] Task 3: Implement `DisplayManagerTests.cpp` to validate the 16-monitor coordinate generation logic.
- [x] Task 4: Implement `MathUtilsTests.cpp` to validate the frustum culling algorithms (Dot product and FOV checks).
- [x] Task 5: Implement `RdpParserTests.cpp` to validate H.264 NAL unit extraction from mock RDP payloads.
- [x] Task 6: Refactor `VirtualMonitor` if necessary to ensure logic is testable without a live `AMediaCodec` instance (Dependency Injection).
- [x] Task 7: Update the GitHub Actions `.yml` workflow to include a `Test` job that runs `mkdir build && cd build && cmake .. && make && ctest --output-on-failure`.
- [x] Task 8: Ensure all tests follow strict error-checking patterns and log failures to the CI console.