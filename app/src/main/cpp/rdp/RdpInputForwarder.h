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
        // Reset cursor to the center of monitor[0] (at x=0, toolbar anchor).
        cursorX_ = std::clamp<int32_t>(960, 0, static_cast<int32_t>(desktopW_ - 1));
        cursorY_ = std::clamp<int32_t>(540, 0, static_cast<int32_t>(desktopH_ - 1));
    }

    // Toolbar band: tuned to match ImGuiToolbar quad touching the monitor bottom.
    // Horizontal: central half of monitor[0] (toolbar anchor monitor).
    // Vertical: starts just below desktop to match the toolbar seam.
    static constexpr int32_t kToolbarBandLocalX0 = 480;
    static constexpr int32_t kToolbarBandLocalX1 = 1440;
    static constexpr int32_t kToolbarBandY0     = 8;
    static constexpr int32_t kToolbarBandHeight = 120;

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

    // Consume accumulated cursor deltas since the previous call.
    void ConsumeCursorDelta(int32_t& dx, int32_t& dy);

    // Consume accumulated wheel steps (+up / -down).
    int32_t ConsumeWheelSteps();

    // Reset all accumulated motion/scroll deltas.
    void ResetMotionAccumulators();

private:
    bool HandleKeyEvent(AInputEvent* event);
    bool HandleMouseEvent(AInputEvent* event);
    static uint32_t AndroidKeycodeToRdp(int32_t akeycode);

    rdpInput* GetInput() const;

    std::atomic<freerdp*> instance_{nullptr};
    uint32_t desktopW_ = 5760;
    uint32_t desktopH_ = 1080;

    // Internal absolute cursor position (updated by relative deltas).
    // Y is allowed to extend into the toolbar band below desktopH_ so the
    // cursor can enter the toolbar band below the central monitor.
    mutable std::mutex cursorMutex_;
    int32_t cursorX_ = 960;   // center of mon0 (at x=0)
    int32_t cursorY_ = 540;

    std::mutex motionMutex_;
    int32_t accumulatedDx_ = 0;
    int32_t accumulatedDy_ = 0;
    int32_t accumulatedWheelSteps_ = 0;

    // Latched left-button state for ImGui injection.
    std::atomic<bool> leftDown_{false};

    // Previous Android mouse position for delta computation.
    float prevRawX_ = -1.0f;
    float prevRawY_ = -1.0f;

    // Previous button state to detect which button was released.
    int32_t prevButtonState_ = 0;

    // Sensitivity multiplier: Android window is small (~1280x800) but
    // desktop is large (5760x1080 up to 30720x1080). Scale deltas so full mouse sweep
    // covers the entire desktop.
    static constexpr float kMouseSensitivity = 4.5f;

    bool IsCursorInToolbarBandLocked(int32_t x, int32_t y) const;
};
