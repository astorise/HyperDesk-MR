#include "RdpInputForwarder.h"
#include "../util/Logger.h"

#include <android/keycodes.h>
#include <algorithm>
#include <cmath>

// ── helpers ──────────────────────────────────────────────────────────────────

rdpInput* RdpInputForwarder::GetInput() const {
    freerdp* inst = instance_.load();
    if (!inst || !inst->context || !inst->context->input)
        return nullptr;
    return inst->context->input;
}

bool RdpInputForwarder::IsCursorInToolbarBandLocked(int32_t x, int32_t y) const {
    const int32_t bandTop = static_cast<int32_t>(desktopH_) + kToolbarBandY0;
    const int32_t bandBottom = bandTop + kToolbarBandHeight;
    return (y >= bandTop) && (y < bandBottom) &&
           (x >= kToolbarBandX0) && (x < kToolbarBandX1);
}

bool RdpInputForwarder::GetToolbarCursor(float& u, float& v) const {
    int32_t x, y;
    {
        std::lock_guard<std::mutex> lock(cursorMutex_);
        x = cursorX_;
        y = cursorY_;
    }
    if (!IsCursorInToolbarBandLocked(x, y)) return false;

    const int32_t bandTop = static_cast<int32_t>(desktopH_) + kToolbarBandY0;
    u = static_cast<float>(x - kToolbarBandX0) /
        static_cast<float>(kToolbarBandX1 - kToolbarBandX0);
    v = static_cast<float>(y - bandTop) /
        static_cast<float>(kToolbarBandHeight);
    return true;
}

void RdpInputForwarder::ConsumeCursorDelta(int32_t& dx, int32_t& dy) {
    std::lock_guard<std::mutex> lock(motionMutex_);
    dx = accumulatedDx_;
    dy = accumulatedDy_;
    accumulatedDx_ = 0;
    accumulatedDy_ = 0;
}

int32_t RdpInputForwarder::ConsumeWheelSteps() {
    std::lock_guard<std::mutex> lock(motionMutex_);
    const int32_t steps = accumulatedWheelSteps_;
    accumulatedWheelSteps_ = 0;
    return steps;
}

void RdpInputForwarder::ResetMotionAccumulators() {
    std::lock_guard<std::mutex> lock(motionMutex_);
    accumulatedDx_ = 0;
    accumulatedDy_ = 0;
    accumulatedWheelSteps_ = 0;
}

// ── public entry point ───────────────────────────────────────────────────────

bool RdpInputForwarder::OnInputEvent(AInputEvent* event) {
    if (!event) return false;

    const int32_t type = AInputEvent_getType(event);
    const int32_t source = AInputEvent_getSource(event);

    if (type == AINPUT_EVENT_TYPE_KEY) {
        return HandleKeyEvent(event);
    }

    // Mouse — accept AINPUT_SOURCE_MOUSE, AINPUT_SOURCE_TOUCHPAD, or
    // AINPUT_SOURCE_MOUSE_RELATIVE (Bluetooth mice on Quest can report any of these).
    if (type == AINPUT_EVENT_TYPE_MOTION) {
        const bool isMouse    = (source & AINPUT_SOURCE_MOUSE) == AINPUT_SOURCE_MOUSE;
        const bool isTouchpad = (source & AINPUT_SOURCE_TOUCHPAD) == AINPUT_SOURCE_TOUCHPAD;
        const bool isMouseRel = (source & 0x20004) == 0x20004;  // AINPUT_SOURCE_MOUSE_RELATIVE
        LOGD("InputForwarder: MOTION source=0x%08X isMouse=%d isTouchpad=%d isMouseRel=%d",
             source, isMouse, isTouchpad, isMouseRel);
        if (isMouse || isTouchpad || isMouseRel) {
            return HandleMouseEvent(event);
        }
    }

    return false;
}

// ── keyboard ─────────────────────────────────────────────────────────────────

bool RdpInputForwarder::HandleKeyEvent(AInputEvent* event) {
    rdpInput* input = GetInput();
    if (!input) return false;

    const int32_t action = AKeyEvent_getAction(event);
    const int32_t akeycode = AKeyEvent_getKeyCode(event);
    const int32_t repeat = AKeyEvent_getRepeatCount(event);

    const uint32_t rdpScancode = AndroidKeycodeToRdp(akeycode);
    if (rdpScancode == 0) return false;

    const BOOL down = (action == AKEY_EVENT_ACTION_DOWN) ? TRUE : FALSE;
    freerdp_input_send_keyboard_event_ex(input, down, repeat > 0, rdpScancode);
    return true;
}

// ── mouse ────────────────────────────────────────────────────────────────────

bool RdpInputForwarder::HandleMouseEvent(AInputEvent* event) {
    rdpInput* input = GetInput();
    if (!input) return false;

    const int32_t action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
    const float rawX = AMotionEvent_getX(event, 0);
    const float rawY = AMotionEvent_getY(event, 0);

    // Compute delta from previous position and accumulate into the internal
    // absolute cursor.  This decouples us from the Android window size and
    // lets the cursor traverse the full RDP desktop width (up to 7680x1080).
    // Y is allowed to extend into the toolbar band below the desktop.
    uint16_t x, y;
    bool inToolbar = false;
    {
        std::lock_guard<std::mutex> lock(cursorMutex_);
        if (prevRawX_ >= 0.0f && prevRawY_ >= 0.0f) {
            const int32_t dxi =
                static_cast<int32_t>((rawX - prevRawX_) * kMouseSensitivity);
            const int32_t dyi =
                static_cast<int32_t>((rawY - prevRawY_) * kMouseSensitivity);
            cursorX_ = std::clamp(cursorX_ + dxi,
                                  0, static_cast<int32_t>(desktopW_ - 1));
            cursorY_ = std::clamp(
                cursorY_ + dyi,
                0,
                static_cast<int32_t>(desktopH_) + kToolbarBandY0 + kToolbarBandHeight - 1);
            {
                std::lock_guard<std::mutex> motionLock(motionMutex_);
                accumulatedDx_ += dxi;
                accumulatedDy_ += dyi;
            }
        }
        prevRawX_ = rawX;
        prevRawY_ = rawY;
        // RDP coordinates are clamped to the actual desktop bounds.
        x = static_cast<uint16_t>(cursorX_);
        y = static_cast<uint16_t>(std::min<int32_t>(cursorY_,
                                                    static_cast<int32_t>(desktopH_) - 1));
        inToolbar = IsCursorInToolbarBandLocked(cursorX_, cursorY_);
    }

    switch (action) {
        case AMOTION_EVENT_ACTION_HOVER_MOVE:
        case AMOTION_EVENT_ACTION_MOVE:
            // Suppress RDP move when in toolbar so the host cursor doesn't
            // wander off the bottom edge of the screen.
            if (!inToolbar) {
                freerdp_input_send_mouse_event(input, PTR_FLAGS_MOVE, x, y);
            }
            break;

        case AMOTION_EVENT_ACTION_DOWN:
        case AMOTION_EVENT_ACTION_BUTTON_PRESS: {
            const int32_t btn = AMotionEvent_getButtonState(event);
            uint16_t flags = PTR_FLAGS_DOWN;
            if (btn & AMOTION_EVENT_BUTTON_PRIMARY)   flags |= PTR_FLAGS_BUTTON1;
            if (btn & AMOTION_EVENT_BUTTON_SECONDARY) flags |= PTR_FLAGS_BUTTON2;
            if (btn & AMOTION_EVENT_BUTTON_TERTIARY)  flags |= PTR_FLAGS_BUTTON3;
            if (flags == PTR_FLAGS_DOWN) flags |= PTR_FLAGS_BUTTON1;
            prevButtonState_ = btn;
            if (flags & PTR_FLAGS_BUTTON1) leftDown_.store(true);
            // Toolbar consumes left clicks; right/middle still go to RDP.
            if (!(inToolbar && (flags & PTR_FLAGS_BUTTON1) &&
                  !(flags & (PTR_FLAGS_BUTTON2 | PTR_FLAGS_BUTTON3)))) {
                freerdp_input_send_mouse_event(input, flags, x, y);
            }
            break;
        }

        case AMOTION_EVENT_ACTION_UP:
        case AMOTION_EVENT_ACTION_BUTTON_RELEASE: {
            // Detect which button(s) were released by comparing with previous state.
            const int32_t btn = AMotionEvent_getButtonState(event);
            const int32_t released = prevButtonState_ & ~btn;
            prevButtonState_ = btn;
            if (released & AMOTION_EVENT_BUTTON_PRIMARY) leftDown_.store(false);
            if (released & AMOTION_EVENT_BUTTON_SECONDARY) {
                freerdp_input_send_mouse_event(input, PTR_FLAGS_BUTTON2, x, y);
            } else if (released & AMOTION_EVENT_BUTTON_TERTIARY) {
                freerdp_input_send_mouse_event(input, PTR_FLAGS_BUTTON3, x, y);
            } else {
                leftDown_.store(false);
                if (!inToolbar) {
                    freerdp_input_send_mouse_event(input, PTR_FLAGS_BUTTON1, x, y);
                }
            }
            break;
        }

        case AMOTION_EVENT_ACTION_SCROLL: {
            const float vscroll = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_VSCROLL, 0);
            const float hscroll = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_HSCROLL, 0);
            if (std::fabs(vscroll) > 0.01f) {
                {
                    std::lock_guard<std::mutex> motionLock(motionMutex_);
                    accumulatedWheelSteps_ += (vscroll > 0.0f) ? 1 : -1;
                }
                uint16_t flags = PTR_FLAGS_WHEEL;
                int clicks = static_cast<int>(vscroll * 120.0f);
                if (clicks < 0) {
                    flags |= PTR_FLAGS_WHEEL_NEGATIVE;
                    clicks = -clicks;
                }
                flags |= static_cast<uint16_t>(std::min(clicks, 0xFF));
                if (!inToolbar) {
                    freerdp_input_send_mouse_event(input, flags, x, y);
                }
            }
            if (std::fabs(hscroll) > 0.01f) {
                uint16_t flags = PTR_FLAGS_HWHEEL;
                int clicks = static_cast<int>(hscroll * 120.0f);
                if (clicks < 0) {
                    flags |= PTR_FLAGS_WHEEL_NEGATIVE;
                    clicks = -clicks;
                }
                flags |= static_cast<uint16_t>(std::min(clicks, 0xFF));
                if (!inToolbar) {
                    freerdp_input_send_mouse_event(input, flags, x, y);
                }
            }
            break;
        }

        case AMOTION_EVENT_ACTION_HOVER_ENTER:
        case AMOTION_EVENT_ACTION_HOVER_EXIT:
            // Reset previous position on enter/exit to avoid a jump.
            {
                std::lock_guard<std::mutex> lock(cursorMutex_);
                prevRawX_ = rawX;
                prevRawY_ = rawY;
            }
            break;

        default:
            return false;
    }
    return true;
}

// ── Evdev mouse methods ─────────────────────────────────────────────────────

void RdpInputForwarder::SendMouseMove(int32_t dx, int32_t dy) {
    rdpInput* input = GetInput();
    if (!input) return;

    uint16_t x, y;
    bool inToolbar = false;
    {
        std::lock_guard<std::mutex> lock(cursorMutex_);
        cursorX_ = std::clamp(cursorX_ + dx, 0, static_cast<int32_t>(desktopW_ - 1));
        cursorY_ = std::clamp(
            cursorY_ + dy,
            0,
            static_cast<int32_t>(desktopH_) + kToolbarBandY0 + kToolbarBandHeight - 1);
        x = static_cast<uint16_t>(cursorX_);
        y = static_cast<uint16_t>(std::min<int32_t>(cursorY_,
                                                    static_cast<int32_t>(desktopH_) - 1));
        inToolbar = IsCursorInToolbarBandLocked(cursorX_, cursorY_);
    }
    {
        std::lock_guard<std::mutex> motionLock(motionMutex_);
        accumulatedDx_ += dx;
        accumulatedDy_ += dy;
    }
    if (!inToolbar) {
        freerdp_input_send_mouse_event(input, PTR_FLAGS_MOVE, x, y);
    }
}

void RdpInputForwarder::SendMouseButton(uint16_t flags) {
    rdpInput* input = GetInput();
    if (!input) return;

    uint16_t x, y;
    bool inToolbar = false;
    {
        std::lock_guard<std::mutex> lock(cursorMutex_);
        x = static_cast<uint16_t>(cursorX_);
        y = static_cast<uint16_t>(std::min<int32_t>(cursorY_,
                                                    static_cast<int32_t>(desktopH_) - 1));
        inToolbar = IsCursorInToolbarBandLocked(cursorX_, cursorY_);
    }
    // Track left-button state for ImGui injection.
    if (flags & PTR_FLAGS_BUTTON1) {
        leftDown_.store((flags & PTR_FLAGS_DOWN) != 0);
    }
    // Toolbar consumes left clicks; right/middle still go to RDP.
    const bool isLeftOnly = (flags & PTR_FLAGS_BUTTON1) &&
                            !(flags & (PTR_FLAGS_BUTTON2 | PTR_FLAGS_BUTTON3));
    if (inToolbar && isLeftOnly) return;
    freerdp_input_send_mouse_event(input, flags, x, y);
}

void RdpInputForwarder::SendMouseWheel(int32_t clicks) {
    rdpInput* input = GetInput();
    if (!input) return;

    uint16_t x, y;
    {
        std::lock_guard<std::mutex> lock(cursorMutex_);
        x = static_cast<uint16_t>(cursorX_);
        y = static_cast<uint16_t>(std::min<int32_t>(cursorY_,
                                                    static_cast<int32_t>(desktopH_) - 1));
    }

    uint16_t flags = PTR_FLAGS_WHEEL;
    int magnitude = clicks * 120;
    if (magnitude < 0) {
        flags |= PTR_FLAGS_WHEEL_NEGATIVE;
        magnitude = -magnitude;
    }
    flags |= static_cast<uint16_t>(std::min(magnitude, 0xFF));
    bool inToolbar = false;
    {
        std::lock_guard<std::mutex> lock(cursorMutex_);
        inToolbar = IsCursorInToolbarBandLocked(cursorX_, cursorY_);
    }
    {
        std::lock_guard<std::mutex> motionLock(motionMutex_);
        accumulatedWheelSteps_ += (clicks >= 0) ? 1 : -1;
    }
    if (!inToolbar) {
        freerdp_input_send_mouse_event(input, flags, x, y);
    }
}

// ── Android keycode → RDP scancode mapping ───────────────────────────────────

uint32_t RdpInputForwarder::AndroidKeycodeToRdp(int32_t ak) {
    switch (ak) {
        // Letters
        case AKEYCODE_A: return RDP_SCANCODE_KEY_A;
        case AKEYCODE_B: return RDP_SCANCODE_KEY_B;
        case AKEYCODE_C: return RDP_SCANCODE_KEY_C;
        case AKEYCODE_D: return RDP_SCANCODE_KEY_D;
        case AKEYCODE_E: return RDP_SCANCODE_KEY_E;
        case AKEYCODE_F: return RDP_SCANCODE_KEY_F;
        case AKEYCODE_G: return RDP_SCANCODE_KEY_G;
        case AKEYCODE_H: return RDP_SCANCODE_KEY_H;
        case AKEYCODE_I: return RDP_SCANCODE_KEY_I;
        case AKEYCODE_J: return RDP_SCANCODE_KEY_J;
        case AKEYCODE_K: return RDP_SCANCODE_KEY_K;
        case AKEYCODE_L: return RDP_SCANCODE_KEY_L;
        case AKEYCODE_M: return RDP_SCANCODE_KEY_M;
        case AKEYCODE_N: return RDP_SCANCODE_KEY_N;
        case AKEYCODE_O: return RDP_SCANCODE_KEY_O;
        case AKEYCODE_P: return RDP_SCANCODE_KEY_P;
        case AKEYCODE_Q: return RDP_SCANCODE_KEY_Q;
        case AKEYCODE_R: return RDP_SCANCODE_KEY_R;
        case AKEYCODE_S: return RDP_SCANCODE_KEY_S;
        case AKEYCODE_T: return RDP_SCANCODE_KEY_T;
        case AKEYCODE_U: return RDP_SCANCODE_KEY_U;
        case AKEYCODE_V: return RDP_SCANCODE_KEY_V;
        case AKEYCODE_W: return RDP_SCANCODE_KEY_W;
        case AKEYCODE_X: return RDP_SCANCODE_KEY_X;
        case AKEYCODE_Y: return RDP_SCANCODE_KEY_Y;
        case AKEYCODE_Z: return RDP_SCANCODE_KEY_Z;

        // Digits
        case AKEYCODE_0: return RDP_SCANCODE_KEY_0;
        case AKEYCODE_1: return RDP_SCANCODE_KEY_1;
        case AKEYCODE_2: return RDP_SCANCODE_KEY_2;
        case AKEYCODE_3: return RDP_SCANCODE_KEY_3;
        case AKEYCODE_4: return RDP_SCANCODE_KEY_4;
        case AKEYCODE_5: return RDP_SCANCODE_KEY_5;
        case AKEYCODE_6: return RDP_SCANCODE_KEY_6;
        case AKEYCODE_7: return RDP_SCANCODE_KEY_7;
        case AKEYCODE_8: return RDP_SCANCODE_KEY_8;
        case AKEYCODE_9: return RDP_SCANCODE_KEY_9;

        // Function keys
        case AKEYCODE_F1:  return RDP_SCANCODE_F1;
        case AKEYCODE_F2:  return RDP_SCANCODE_F2;
        case AKEYCODE_F3:  return RDP_SCANCODE_F3;
        case AKEYCODE_F4:  return RDP_SCANCODE_F4;
        case AKEYCODE_F5:  return RDP_SCANCODE_F5;
        case AKEYCODE_F6:  return RDP_SCANCODE_F6;
        case AKEYCODE_F7:  return RDP_SCANCODE_F7;
        case AKEYCODE_F8:  return RDP_SCANCODE_F8;
        case AKEYCODE_F9:  return RDP_SCANCODE_F9;
        case AKEYCODE_F10: return RDP_SCANCODE_F10;
        case AKEYCODE_F11: return RDP_SCANCODE_F11;
        case AKEYCODE_F12: return RDP_SCANCODE_F12;

        // Modifiers
        case AKEYCODE_SHIFT_LEFT:  return RDP_SCANCODE_LSHIFT;
        case AKEYCODE_SHIFT_RIGHT: return RDP_SCANCODE_RSHIFT;
        case AKEYCODE_CTRL_LEFT:   return RDP_SCANCODE_LCONTROL;
        case AKEYCODE_CTRL_RIGHT:  return RDP_SCANCODE_RCONTROL;
        case AKEYCODE_ALT_LEFT:    return RDP_SCANCODE_LMENU;
        case AKEYCODE_ALT_RIGHT:   return RDP_SCANCODE_RMENU;
        case AKEYCODE_META_LEFT:   return RDP_SCANCODE_LWIN;
        case AKEYCODE_META_RIGHT:  return RDP_SCANCODE_RWIN;

        // Control keys
        case AKEYCODE_ENTER:     return RDP_SCANCODE_RETURN;
        case AKEYCODE_ESCAPE:    return RDP_SCANCODE_ESCAPE;
        case AKEYCODE_DEL:       return RDP_SCANCODE_BACKSPACE;
        case AKEYCODE_FORWARD_DEL: return RDP_SCANCODE_DELETE;
        case AKEYCODE_TAB:       return RDP_SCANCODE_TAB;
        case AKEYCODE_SPACE:     return RDP_SCANCODE_SPACE;
        case AKEYCODE_INSERT:    return RDP_SCANCODE_INSERT;
        case AKEYCODE_CAPS_LOCK: return RDP_SCANCODE_CAPSLOCK;
        case AKEYCODE_NUM_LOCK:  return RDP_SCANCODE_NUMLOCK;
        case AKEYCODE_SCROLL_LOCK: return RDP_SCANCODE_SCROLLLOCK;

        // Navigation
        case AKEYCODE_DPAD_UP:    return RDP_SCANCODE_UP;
        case AKEYCODE_DPAD_DOWN:  return RDP_SCANCODE_DOWN;
        case AKEYCODE_DPAD_LEFT:  return RDP_SCANCODE_LEFT;
        case AKEYCODE_DPAD_RIGHT: return RDP_SCANCODE_RIGHT;
        case AKEYCODE_MOVE_HOME:  return RDP_SCANCODE_HOME;
        case AKEYCODE_MOVE_END:   return RDP_SCANCODE_END;
        case AKEYCODE_PAGE_UP:    return RDP_SCANCODE_PRIOR;
        case AKEYCODE_PAGE_DOWN:  return RDP_SCANCODE_NEXT;

        // Symbols
        case AKEYCODE_MINUS:         return RDP_SCANCODE_OEM_MINUS;
        case AKEYCODE_EQUALS:        return RDP_SCANCODE_OEM_PLUS;
        case AKEYCODE_LEFT_BRACKET:  return RDP_SCANCODE_OEM_4;
        case AKEYCODE_RIGHT_BRACKET: return RDP_SCANCODE_OEM_6;
        case AKEYCODE_BACKSLASH:     return RDP_SCANCODE_OEM_5;
        case AKEYCODE_SEMICOLON:     return RDP_SCANCODE_OEM_1;
        case AKEYCODE_APOSTROPHE:    return RDP_SCANCODE_OEM_7;
        case AKEYCODE_GRAVE:         return RDP_SCANCODE_OEM_3;
        case AKEYCODE_COMMA:         return RDP_SCANCODE_OEM_COMMA;
        case AKEYCODE_PERIOD:        return RDP_SCANCODE_OEM_PERIOD;
        case AKEYCODE_SLASH:         return RDP_SCANCODE_OEM_2;

        // Print screen, pause
        case AKEYCODE_SYSRQ:  return RDP_SCANCODE_PRINTSCREEN;
        case AKEYCODE_BREAK:  return RDP_SCANCODE_PAUSE;

        default: return 0;
    }
}
