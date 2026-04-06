#pragma once

#include <android/input.h>
#include <atomic>
#include <cstdint>

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
    }

    // Returns true if the event was consumed.
    bool OnInputEvent(AInputEvent* event);

    // Public accessors for the JNI mouse bridge.
    rdpInput* GetInputPublic() const { return GetInput(); }
    uint32_t GetDesktopW() const { return desktopW_; }
    uint32_t GetDesktopH() const { return desktopH_; }

private:
    bool HandleKeyEvent(AInputEvent* event);
    bool HandleMouseEvent(AInputEvent* event);
    static uint32_t AndroidKeycodeToRdp(int32_t akeycode);

    rdpInput* GetInput() const;

    std::atomic<freerdp*> instance_{nullptr};
    uint32_t desktopW_ = 1920;
    uint32_t desktopH_ = 1080;
};
