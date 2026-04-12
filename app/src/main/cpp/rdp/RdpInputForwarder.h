#pragma once

#include <android/input.h>
#include <algorithm>
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
        desktopW_ = std::max<uint32_t>(1u, w);
        desktopH_ = std::max<uint32_t>(1u, h);
        // Reset cursor to center of primary monitor.
        cursorX_ = std::clamp<int32_t>(1920 + 960, 0, static_cast<int32_t>(desktopW_ - 1));
        cursorY_ = std::clamp<int32_t>(540, 0, static_cast<int32_t>(desktopH_ - 1));
    }

    // Toolbar band: a virtual region below the desktop where mouse events are
    // routed to the ImGui toolbar instead of RDP.  Vertical extent is in
    // virtual cursor pixels (same units as desktopH_).
    static constexpr int32_t kToolbarBandHeight = 220;
    static constexpr int32_t kToolbarBandX0     = 1920;
    static constexpr int32_t kToolbarBandX1     = 3840;

    // Returns true if the cursor is currently inside the toolbar band, and
    // sets u/v to its normalized position within the band.
    bool GetToolbarCursor(float& u, float& v) const;

    // Returns the latched left-button state for ImGui injection.
    bool IsLeftButtonDown() const { return leftDown_.load(); }

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

    // Internal absolute cursor position (updated by relative deltas).
    // Y is allowed to extend up to desktopH_ + kToolbarBandHeight - 1 so the
    // cursor can enter the toolbar band below the central monitor.
    mutable std::mutex cursorMutex_;
    int32_t cursorX_ = 2880;  // center of 5760
    int32_t cursorY_ = 540;

    // Latched left-button state for ImGui injection.
    std::atomic<bool> leftDown_{false};

    // Previous Android mouse position for delta computation.
    float prevRawX_ = -1.0f;
    float prevRawY_ = -1.0f;

    // Previous button state to detect which button was released.
    int32_t prevButtonState_ = 0;

    // Sensitivity multiplier: Android window is small (~1280x800) but
    // desktop is large (5760x1080 or 7680x1080). Scale deltas so full mouse sweep
    // covers the entire desktop.
    static constexpr float kMouseSensitivity = 4.5f;
};
