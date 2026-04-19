## Purpose
Forward Bluetooth keyboard and mouse events from the Quest 3 to the RDP session, translating Android input events into FreeRDP protocol messages with correct coordinate mapping across the multi-monitor desktop. Support dual input paths: Android MotionEvent (with sensitivity scaling) and Linux evdev (raw deltas).

## Requirements

### Requirement: Bluetooth keyboard events are forwarded to the RDP session
The `RdpInputForwarder` class SHALL handle `AINPUT_EVENT_TYPE_KEY` events from `NativeActivity.onInputEvent`, translate Android keycodes to RDP scancodes via a static mapping table, and send them to the RDP session using `freerdp_input_send_keyboard_event_ex`. The mapping SHALL cover letters, digits, function keys, modifiers, control keys, navigation keys, and common symbols.

#### Scenario: Key press is forwarded as RDP scancode
- **WHEN** a `AKEY_EVENT_ACTION_DOWN` event is received for a mapped keycode
- **THEN** `freerdp_input_send_keyboard_event_ex` is called with `down=TRUE` and the corresponding RDP scancode

#### Scenario: Key release is forwarded
- **WHEN** a `AKEY_EVENT_ACTION_UP` event is received for a mapped keycode
- **THEN** `freerdp_input_send_keyboard_event_ex` is called with `down=FALSE`

#### Scenario: Unmapped keycode is ignored
- **WHEN** an Android keycode has no RDP scancode mapping
- **THEN** `OnInputEvent` returns `false` and no RDP event is sent

### Requirement: Mouse movement uses relative deltas to traverse the full desktop
The `RdpInputForwarder` SHALL support two input paths for mouse movement:
1. **Android MotionEvent path**: Computes deltas between consecutive `MOTION` events and applies a sensitivity multiplier (`kMouseSensitivity = 4.5`) to scale movement across the full 30720×1080 RDP desktop (16 monitors × 1920).
2. **Linux evdev path**: An `EvdevMouseReader` reads raw `REL_X`/`REL_Y` events from `/dev/input/` without `EVIOCGRAB` (coexisting with Android), forwarding unscaled deltas directly. The evdev reader runs on a background thread and calls `HandleRelativeMotion()` on the forwarder.

The internal cursor position SHALL be clamped to `[0, desktopW-1] × [0, desktopH-1]`.

#### Scenario: Mouse moves across all monitors
- **WHEN** the user moves the Bluetooth mouse continuously in one direction
- **THEN** the internal cursor position traverses from x=0 to x=30719 across all 16 monitor regions

#### Scenario: Cursor position is clamped to desktop bounds
- **WHEN** accumulated deltas would move the cursor beyond the desktop edge
- **THEN** the cursor position is clamped to the desktop boundary

#### Scenario: Hover enter resets delta tracking
- **WHEN** a `HOVER_ENTER` event is received
- **THEN** the previous raw position is reset to avoid a large jump from stale coordinates

#### Scenario: Evdev raw deltas are forwarded without sensitivity scaling
- **WHEN** an `EvdevMouseReader` delivers `REL_X`/`REL_Y` events
- **THEN** the deltas are applied directly to the cursor position without the `kMouseSensitivity` multiplier

### Requirement: Mouse button press and release are correctly identified
The `RdpInputForwarder` SHALL track the previous Android button state to detect which specific button was released. On `BUTTON_PRESS`, the current `getButtonState()` identifies which button is pressed. On `BUTTON_RELEASE`, the difference between the previous and current button state identifies which button was released. Left, right, and middle buttons SHALL be mapped to `PTR_FLAGS_BUTTON1`, `PTR_FLAGS_BUTTON2`, and `PTR_FLAGS_BUTTON3` respectively.

#### Scenario: Left click press and release
- **WHEN** the user presses and releases the primary mouse button
- **THEN** `PTR_FLAGS_DOWN | PTR_FLAGS_BUTTON1` is sent on press and `PTR_FLAGS_BUTTON1` (without DOWN) is sent on release

#### Scenario: Right click press and release
- **WHEN** the user presses and releases the secondary mouse button
- **THEN** `PTR_FLAGS_DOWN | PTR_FLAGS_BUTTON2` is sent on press and `PTR_FLAGS_BUTTON2` is sent on release

### Requirement: Mouse scroll events are forwarded
The `RdpInputForwarder` SHALL handle `AMOTION_EVENT_ACTION_SCROLL` events, reading `AXIS_VSCROLL` and `AXIS_HSCROLL` values. Vertical scroll SHALL use `PTR_FLAGS_WHEEL` and horizontal scroll SHALL use `PTR_FLAGS_HWHEEL`, with `PTR_FLAGS_WHEEL_NEGATIVE` set for negative scroll directions. The scroll magnitude SHALL be scaled by 120 units per click.

#### Scenario: Vertical scroll is forwarded
- **WHEN** a scroll event is received with a non-zero `AXIS_VSCROLL` value
- **THEN** a mouse event with `PTR_FLAGS_WHEEL` and the scaled magnitude is sent

### Requirement: Mouse events from multiple sources are accepted
The `RdpInputForwarder` SHALL accept motion events from `AINPUT_SOURCE_MOUSE`, `AINPUT_SOURCE_TOUCHPAD`, and `AINPUT_SOURCE_MOUSE_RELATIVE` (0x20004) to cover the various source codes that Bluetooth mice report on Quest 3.

#### Scenario: Bluetooth mouse on Quest reports AINPUT_SOURCE_MOUSE
- **WHEN** a motion event arrives with `source & AINPUT_SOURCE_MOUSE`
- **THEN** the event is processed as a mouse event

### Requirement: Cursor position is accessible for 3D rendering
The `RdpInputForwarder` SHALL provide a thread-safe `GetCursorPosition(int32_t& x, int32_t& y)` method that returns the current internal absolute cursor coordinates for use by the `CursorOverlay` rendering system.

#### Scenario: CursorOverlay reads cursor position each frame
- **WHEN** the render loop queries `GetCursorPosition`
- **THEN** the current desktop-coordinate cursor position is returned under mutex protection

### Requirement: Toolbar hit-testing with scroll compensation
The `RdpInputForwarder` SHALL provide a `GetToolbarCursor(float& u, float& v)` method that returns normalized UV coordinates for the toolbar region. A `toolbarOffsetX` value SHALL be set each frame to compensate for the carousel scroll offset, computed as `scrollYaw / kAngularStepRadians * 1920`. The toolbar cursor hit-test SHALL use this offset to correctly identify whether the cursor is within the toolbar band regardless of scroll position.

#### Scenario: Toolbar hover is detected under scroll
- **WHEN** the carousel is scrolled and the cursor is over the toolbar's screen region
- **THEN** `GetToolbarCursor()` returns `true` with correct UV coordinates after applying the scroll offset

### Requirement: Cursor position is preserved across reconnects
When the RDP session reconnects, the `RdpInputForwarder` SHALL retain the previous cursor position rather than resetting to (0,0), so the user does not experience a cursor jump on reconnect.

#### Scenario: Cursor stays in place after reconnect
- **WHEN** the RDP session disconnects and reconnects
- **THEN** the internal cursor position is unchanged from its pre-disconnect value
