#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

class RdpInputForwarder;

// Reads raw mouse events from /dev/input/eventX (Linux evdev) on a background
// thread.  This bypasses Horizon OS's input interception which prevents
// Bluetooth mouse events from reaching NativeActivity or Java dispatch methods
// in immersive VR apps.
class EvdevMouseReader {
public:
    explicit EvdevMouseReader(RdpInputForwarder& forwarder);
    ~EvdevMouseReader();

    // Scan /dev/input/ for a mouse device and start reading.
    bool Start();
    void Stop();
    bool IsRunning() const { return running_.load(); }

private:
    void ReadLoop();
    static std::string FindMouseDevice();

    RdpInputForwarder& forwarder_;
    std::thread        thread_;
    std::atomic<bool>  running_{false};
    std::atomic<bool>  stopFlag_{false};
    int                fd_ = -1;
};
