#include <jni.h>
#include <cmath>
#include <algorithm>

#include "RdpInputForwarder.h"
#include "../util/Logger.h"

// ── Global forwarder pointer (set from main.cpp) ─────────────────────────────
// The Java Activity calls nativeOnMouseEvent → we route to the forwarder.
static RdpInputForwarder* g_mouseForwarder = nullptr;

void JniMouseBridge_SetForwarder(RdpInputForwarder* fwd) {
    g_mouseForwarder = fwd;
}

// ── Android MotionEvent action constants (matching MotionEvent.java) ─────────
static constexpr int ACTION_DOWN          = 0;
static constexpr int ACTION_UP            = 1;
static constexpr int ACTION_MOVE          = 2;
static constexpr int ACTION_HOVER_MOVE    = 7;
static constexpr int ACTION_SCROLL        = 8;
static constexpr int ACTION_BUTTON_PRESS  = 11;
static constexpr int ACTION_BUTTON_RELEASE = 12;
static constexpr int ACTION_HOVER_ENTER   = 9;
static constexpr int ACTION_HOVER_EXIT    = 10;

// ── Android MotionEvent button constants ─────────────────────────────────────
static constexpr int BUTTON_PRIMARY   = 1;
static constexpr int BUTTON_SECONDARY = 2;
static constexpr int BUTTON_TERTIARY  = 4;

extern "C"
JNIEXPORT void JNICALL
Java_com_hyperdesk_mr_HyperDeskActivity_nativeOnMouseEvent(
        JNIEnv* /*env*/, jclass /*clazz*/,
        jint action, jfloat x, jfloat y,
        jint buttonState, jfloat vscroll, jfloat hscroll) {

    if (!g_mouseForwarder) return;

    rdpInput* input = g_mouseForwarder->GetInputPublic();
    if (!input) return;

    const uint32_t desktopW = g_mouseForwarder->GetDesktopW();
    const uint32_t desktopH = g_mouseForwarder->GetDesktopH();

    const uint16_t cx = static_cast<uint16_t>(
        std::clamp(static_cast<int>(x), 0, static_cast<int>(desktopW - 1)));
    const uint16_t cy = static_cast<uint16_t>(
        std::clamp(static_cast<int>(y), 0, static_cast<int>(desktopH - 1)));

    switch (action) {
        case ACTION_HOVER_MOVE:
        case ACTION_MOVE:
        case ACTION_HOVER_ENTER:
        case ACTION_HOVER_EXIT:
            freerdp_input_send_mouse_event(input, PTR_FLAGS_MOVE, cx, cy);
            break;

        case ACTION_DOWN:
        case ACTION_BUTTON_PRESS: {
            uint16_t flags = PTR_FLAGS_DOWN;
            if (buttonState & BUTTON_PRIMARY)   flags |= PTR_FLAGS_BUTTON1;
            if (buttonState & BUTTON_SECONDARY) flags |= PTR_FLAGS_BUTTON2;
            if (buttonState & BUTTON_TERTIARY)  flags |= PTR_FLAGS_BUTTON3;
            if (flags == PTR_FLAGS_DOWN) flags |= PTR_FLAGS_BUTTON1;  // default to left click
            freerdp_input_send_mouse_event(input, flags, cx, cy);
            break;
        }

        case ACTION_UP:
        case ACTION_BUTTON_RELEASE: {
            uint16_t flags = 0;
            // On release, buttonState has the buttons still held. We send
            // release for buttons that are no longer pressed.
            if (!(buttonState & BUTTON_PRIMARY))   flags |= PTR_FLAGS_BUTTON1;
            if (!(buttonState & BUTTON_SECONDARY)) flags |= PTR_FLAGS_BUTTON2;
            if (!(buttonState & BUTTON_TERTIARY))  flags |= PTR_FLAGS_BUTTON3;
            if (flags == 0) flags = PTR_FLAGS_BUTTON1;
            freerdp_input_send_mouse_event(input, flags, cx, cy);
            break;
        }

        case ACTION_SCROLL: {
            if (std::fabs(vscroll) > 0.01f) {
                uint16_t flags = PTR_FLAGS_WHEEL;
                int clicks = static_cast<int>(vscroll * 120.0f);
                if (clicks < 0) {
                    flags |= PTR_FLAGS_WHEEL_NEGATIVE;
                    clicks = -clicks;
                }
                flags |= static_cast<uint16_t>(std::min(clicks, 0xFF));
                freerdp_input_send_mouse_event(input, flags, cx, cy);
            }
            if (std::fabs(hscroll) > 0.01f) {
                uint16_t flags = PTR_FLAGS_HWHEEL;
                int clicks = static_cast<int>(hscroll * 120.0f);
                if (clicks < 0) {
                    flags |= PTR_FLAGS_WHEEL_NEGATIVE;
                    clicks = -clicks;
                }
                flags |= static_cast<uint16_t>(std::min(clicks, 0xFF));
                freerdp_input_send_mouse_event(input, flags, cx, cy);
            }
            break;
        }

        default:
            LOGD("JNI mouse: unhandled action=%d", action);
            break;
    }
}
