## ADDED Requirements

### Requirement: 16-monitor layout coordinate calculation is covered by GTest unit tests
The codebase SHALL include a `tests/DisplayManagerTests.cpp` file that uses Google Test to verify that the 4×4 monitor grid layout computation produces correct, non-overlapping pixel coordinates. Tests MUST verify that each of the 16 monitors has the expected `Left`, `Top`, `Width`, and `Height` values for a 1920×1080 grid, that no two monitor rectangles overlap, and that the total covered pixel area equals 16 × 1920 × 1080.

#### Scenario: 4x4 grid layout generates 16 non-overlapping monitor rectangles
- **WHEN** `DisplayManagerTests` invokes the layout computation with a 4-column × 4-row grid at 1920×1080 per monitor
- **THEN** the resulting 16 entries have `Left` values cycling through 0, 1920, 3840, 5760 and `Top` values cycling through 0, 1080, 2160, 3240 with no overlapping rectangles

#### Scenario: Total pixel coverage matches the expected area
- **WHEN** `DisplayManagerTests` computes the union area of all 16 monitor rectangles
- **THEN** the total equals exactly 33,177,600 pixels (16 × 1920 × 1080) with no gaps
