## Purpose
Forward Bluetooth keyboard and mouse events from the Quest 3 to the RDP session, translating Android input events into FreeRDP protocol messages with correct coordinate mapping across the multi-monitor desktop.

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
The `RdpInputForwarder` SHALL compute mouse movement from relative deltas between consecutive Android `MOTION` events, not from absolute window coordinates. A sensitivity multiplier (`kMouseSensitivity = 4.5`) SHALL scale the deltas so that a full mouse sweep covers the entire 5760x1080 RDP desktop. The internal cursor position SHALL be clamped to `[0, desktopW-1] x [0, desktopH-1]`.

#### Scenario: Mouse moves across all three monitors
- **WHEN** the user moves the Bluetooth mouse continuously in one direction
- **THEN** the internal cursor position traverses from x=0 to x=5759 across all three monitor regions

#### Scenario: Cursor position is clamped to desktop bounds
- **WHEN** accumulated deltas would move the cursor beyond the desktop edge
- **THEN** the cursor position is clamped to the desktop boundary

#### Scenario: Hover enter resets delta tracking
- **WHEN** a `HOVER_ENTER` event is received
- **THEN** the previous raw position is reset to avoid a large jump from stale coordinates

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
