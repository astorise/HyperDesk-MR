## ADDED Requirements

### Requirement: H.264 NAL unit extraction logic is covered by GTest unit tests
The codebase SHALL include a `tests/RdpParserTests.cpp` file that uses Google Test to verify H.264 NAL unit extraction from mock RDP payloads. Tests MUST cover: correct identification of 3-byte start codes (`0x000001`), correct identification of 4-byte start codes (`0x00000001`), handling of multiple NAL units in a single buffer, and graceful handling of corrupted or truncated payloads without crashing.

#### Scenario: Parser identifies a 4-byte H.264 start code in a mock payload
- **WHEN** `RdpParserTests` feeds a buffer beginning with `0x00 0x00 0x00 0x01` to the NAL unit extractor
- **THEN** the extractor identifies a valid NAL unit boundary at offset 0 and the test asserts the extracted unit length is correct

#### Scenario: Parser handles a corrupted payload without crashing
- **WHEN** `RdpParserTests` feeds a buffer with no valid start code to the NAL unit extractor
- **THEN** the extractor returns zero NAL units and the test verifies this with `EXPECT_NO_FATAL_FAILURE`
