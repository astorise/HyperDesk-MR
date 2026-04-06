package com.hyperdesk.mr;

import android.app.NativeActivity;
import android.os.Bundle;
import android.view.InputDevice;
import android.view.MotionEvent;

/**
 * Thin subclass of NativeActivity that intercepts mouse motion events.
 *
 * On Meta Quest (Horizon OS) the system renders a Bluetooth mouse pointer
 * and consumes HOVER_MOVE events before they reach NativeActivity's native
 * input queue.  By overriding dispatchGenericMotionEvent / dispatchTouchEvent
 * we capture these events and forward them to native code via JNI.
 */
public class HyperDeskActivity extends NativeActivity {

    @Override
    public boolean dispatchGenericMotionEvent(MotionEvent ev) {
        if (isMouseEvent(ev)) {
            nativeOnMouseEvent(ev.getAction(), ev.getX(), ev.getY(),
                               ev.getButtonState(),
                               ev.getAxisValue(MotionEvent.AXIS_VSCROLL),
                               ev.getAxisValue(MotionEvent.AXIS_HSCROLL));
            return true;
        }
        return super.dispatchGenericMotionEvent(ev);
    }

    @Override
    public boolean dispatchTouchEvent(MotionEvent ev) {
        if (isMouseEvent(ev)) {
            nativeOnMouseEvent(ev.getAction(), ev.getX(), ev.getY(),
                               ev.getButtonState(), 0.0f, 0.0f);
            return true;
        }
        return super.dispatchTouchEvent(ev);
    }

    private static boolean isMouseEvent(MotionEvent ev) {
        final int src = ev.getSource();
        return (src & InputDevice.SOURCE_MOUSE) == InputDevice.SOURCE_MOUSE
            || (src & InputDevice.SOURCE_MOUSE_RELATIVE) == InputDevice.SOURCE_MOUSE_RELATIVE;
    }

    /**
     * JNI bridge — implemented in RdpInputForwarder (jni_mouse_bridge.cpp).
     */
    private static native void nativeOnMouseEvent(int action, float x, float y,
                                                   int buttonState,
                                                   float vscroll, float hscroll);
}
