## Purpose
Manage the spatial arrangement, scrolling, and visibility of up to 16 virtual monitors on a curved cylindrical wall in VR, anchored to the user's head pose in STAGE reference space.

## Requirements

### Requirement: Monitors are arranged on a decagon-shaped cylindrical wall
The `MonitorLayout` class SHALL position up to 16 monitors (`kMaxMonitors=16`) on a curved wall with an angular step of 36° (`kAngularStepRadians = 2π/10`) between adjacent monitors. Each monitor is represented by a `MonitorDescriptor` containing its world pose, pixel dimensions, surface binding, and active state. The wall radius is 2.5 m (`kDepth = -2.5`).

#### Scenario: Default layout places monitors at 36° intervals
- **WHEN** `BuildDefaultLayout()` is called
- **THEN** each monitor's world pose is computed with yaw = `anchorOrient * YawQuat(-index * kAngularStepRadians + scrollYaw_)`, placing monitors sequentially around the curved wall

### Requirement: Primary anchor locks the wall to the user's heading
`AnchorPrimaryToHeadPose()` SHALL extract the yaw component of the headset orientation and set it as the wall's primary anchor. This anchors monitor 0 directly in front of the user's initial heading. Subsequent calls to `BuildDefaultLayout()` SHALL apply this anchor to all monitor poses.

#### Scenario: Wall is anchored on first head pose
- **WHEN** `AnchorPrimaryToHeadPose()` is called with the current headset pose
- **THEN** monitor 0 is placed directly in front of the user's heading and the wall curves around them

### Requirement: Split-row mode arranges monitors in two rows
When `SetSplitRows(true)` is called, the layout SHALL arrange monitors in two rows of 8 instead of one row of 16, with a vertical offset of `kVSpacing` (1.15 m) between rows. Odd-indexed monitors SHALL be placed in the bottom row.

#### Scenario: Toggling split mode redistributes monitors
- **WHEN** `SetSplitRows(true)` is called
- **THEN** `BuildDefaultLayout()` places monitors 0,2,4,... in the top row and monitors 1,3,5,... in the bottom row

### Requirement: Wall supports nudge and rotation adjustments
`NudgeAnchor(right, up, toward)` SHALL translate the wall in local space. `RotateAnchorYaw(yaw)` SHALL rotate around the wall's own vertical axis. `RotateAnchorYawAroundPivot(yaw, pivot)` SHALL rotate the wall around an external pivot (typically the headset position) to keep the wall orbiting the viewer.

#### Scenario: Nudge moves the entire wall
- **WHEN** `NudgeAnchor(0.5, 0, 0)` is called
- **THEN** all monitor poses shift 0.5 m to the right in local wall space

### Requirement: Carousel auto-scroll keeps the cursor within the comfort zone
`UpdateCarousel(cursorMonitorIdx)` SHALL be called each frame with the monitor index under the cursor. When the cursor monitor's effective yaw exceeds ±60° (the comfort zone), the carousel SHALL smoothly scroll the wall so that the cursor monitor moves back toward the center. Scroll is applied by adjusting `scrollYaw_` and calling `BuildDefaultLayout()`.

#### Scenario: Cursor at edge triggers scroll
- **WHEN** the cursor is on a monitor whose effective yaw exceeds 60°
- **THEN** `scrollYaw_` increases to bring that monitor back within the comfort zone

### Requirement: Head-tracking scroll moves the wall when the user turns
`UpdateHeadScroll(headPose)` SHALL monitor the user's head yaw relative to the wall anchor. When the head yaw exceeds ±75°, the wall SHALL scroll proportionally (faster at larger angles). The scroll direction SHALL match the head turn direction (looking left scrolls the wall left, revealing monitors on the right).

#### Scenario: Turning head beyond 75° scrolls the wall
- **WHEN** the user turns their head more than 75° from the wall center
- **THEN** the wall scrolls in the same direction as the head turn, proportional to the excess angle

### Requirement: Monitors beyond ±120° are culled from rendering
`IsMonitorInView(index)` SHALL compute the effective yaw as `MonitorBaseYaw(index) + scrollYaw_` and return `false` when `|effectiveYaw| > 120°` (2.094 rad). The `XrCompositor` SHALL not submit cylinder layers for monitors that fail this check.

#### Scenario: Monitor at 108° is visible
- **WHEN** a monitor's effective yaw is 108° (within ±120°)
- **THEN** `IsMonitorInView()` returns `true`

#### Scenario: Monitor at 144° is hidden
- **WHEN** a monitor's effective yaw is 144° (beyond ±120°)
- **THEN** `IsMonitorInView()` returns `false`

### Requirement: RevealMonitor auto-scrolls to show newly added monitors
`RevealMonitor(index)` SHALL check whether the target monitor's effective yaw falls within the ±120° visible arc. If not, it SHALL adjust `scrollYaw_` to the minimum value that places the monitor's right edge within the arc, then call `BuildDefaultLayout()`. This is called automatically when a secondary RDP session activates a new monitor.

#### Scenario: Adding monitor 5 scrolls the wall to reveal it
- **WHEN** `RevealMonitor(5)` is called and monitor 5's base yaw of -180° is outside the visible arc
- **THEN** `scrollYaw_` is increased so monitor 5's effective yaw is within ±120°, and the wall rebuilds

### Requirement: Toolbar anchor pose is independent of scroll
`GetToolbarAnchorPose()` SHALL return the unscrolled primary anchor pose so that the toolbar stays fixed at the center of the user's field of view regardless of carousel scroll offset.

#### Scenario: Toolbar stays centered while wall scrolls
- **WHEN** the carousel scrolls and `GetToolbarAnchorPose()` is called
- **THEN** the returned pose matches the original anchor orientation, ignoring `scrollYaw_`

### Requirement: Scroll can be reset
`ResetScroll()` SHALL set `scrollYaw_` to 0 and call `BuildDefaultLayout()`, returning the wall to its unscrolled position. This is intended for view-reset actions.

#### Scenario: ResetScroll returns wall to initial position
- **WHEN** `ResetScroll()` is called after the wall has been scrolled
- **THEN** `scrollYaw_` is 0 and all monitors are in their unscrolled positions
