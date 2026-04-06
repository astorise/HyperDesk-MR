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

    private static boolean sNativeReady = false;

    static {
        try {
            System.loadLibrary("hyperdesk_mr");
            sNativeReady = true;
        } catch (UnsatisfiedLinkError e) {
            // Library will be loaded later by NativeActivity.onCreate().
            sNativeReady = false;
        }
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        // After NativeActivity.onCreate the library is guaranteed loaded.
        sNativeReady = true;
    }

    @Override
    public boolean dispatchGenericMotionEvent(MotionEvent ev) {
        if (sNativeReady && isMouseEvent(ev)) {
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
        if (sNativeReady && isMouseEvent(ev)) {
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
     * JNI bridge — implemented in jni_mouse_bridge.cpp.
     */
    private static native void nativeOnMouseEvent(int action, float x, float y,
                                                   int buttonState,
                                                   float vscroll, float hscroll);
}
