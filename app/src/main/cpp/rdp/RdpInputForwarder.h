#pragma once

#include <android/input.h>
#include <atomic>
#include <cstdint>
#include <mutex>

#include <freerdp/freerdp.h>
#include <freerdp/input.h>
#include <freerdp/scancode.h>

// Forwards Android Bluetooth keyboard and mouse events to the RDP session.
class RdpInputForwarder {
public:
    void Attach(freerdp* instance) { instance_.store(instance); }
    void Detach() { instance_.store(nullptr); }

    void SetDesktopSize(uint32_t w, uint32_t h) {
        desktopW_ = w;
        desktopH_ = h;
        // Reset cursor to center of primary monitor.
        cursorX_ = 1920 + 960;  // center of monitor 0 (at left=1920)
        cursorY_ = 540;
    }

    // Set the Android window dimensions for coordinate mapping.
    void SetWindowSize(uint32_t w, uint32_t h) {
        windowW_ = w;
        windowH_ = h;
    }

    // Returns true if the event was consumed.
    bool OnInputEvent(AInputEvent* event);

    // ── Evdev mouse methods (called from EvdevMouseReader thread) ─────────
    // Relative movement — updates internal cursor, sends absolute coords.
    void SendMouseMove(int32_t dx, int32_t dy);
    // Button press/release — PTR_FLAGS_BUTTON1|PTR_FLAGS_DOWN etc.
    void SendMouseButton(uint16_t flags);
    // Wheel scroll (positive = up, negative = down).
    void SendMouseWheel(int32_t clicks);

    // Public accessors.
    rdpInput* GetInputPublic() const { return GetInput(); }
    uint32_t GetDesktopW() const { return desktopW_; }
    uint32_t GetDesktopH() const { return desktopH_; }

    // Returns the current absolute cursor position (thread-safe).
    void GetCursorPosition(int32_t& x, int32_t& y) {
        std::lock_guard<std::mutex> lock(cursorMutex_);
        x = cursorX_;
        y = cursorY_;
    }

private:
    bool HandleKeyEvent(AInputEvent* event);
    bool HandleMouseEvent(AInputEvent* event);
    static uint32_t AndroidKeycodeToRdp(int32_t akeycode);

    rdpInput* GetInput() const;

    std::atomic<freerdp*> instance_{nullptr};
    uint32_t desktopW_ = 5760;
    uint32_t desktopH_ = 1080;
    uint32_t windowW_  = 0;  // Android window size (0 = not set yet)
    uint32_t windowH_  = 0;

    // Internal absolute cursor position (updated by relative evdev deltas).
    std::mutex cursorMutex_;
    int32_t cursorX_ = 2880;  // center of 5760
    int32_t cursorY_ = 540;
};
