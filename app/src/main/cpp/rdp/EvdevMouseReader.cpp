#include "EvdevMouseReader.h"
#include "RdpInputForwarder.h"
#include "../util/Logger.h"

#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstring>

EvdevMouseReader::EvdevMouseReader(RdpInputForwarder& forwarder)
    : forwarder_(forwarder) {}

EvdevMouseReader::~EvdevMouseReader() {
    Stop();
}

// ── Find the mouse device ────────────────────────────────────────────────────

std::string EvdevMouseReader::FindMouseDevice() {
    DIR* dir = opendir("/dev/input");
    if (!dir) return {};

    std::string result;
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (strncmp(entry->d_name, "event", 5) != 0) continue;

        std::string path = std::string("/dev/input/") + entry->d_name;
        int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;

        // Check if device has REL_X and REL_Y (mouse-like).
        unsigned long relBits[(REL_MAX + 1 + 7) / 8] = {};
        if (ioctl(fd, EVIOCGBIT(EV_REL, sizeof(relBits)), relBits) >= 0) {
            const bool hasRelX = (relBits[REL_X / 8] >> (REL_X % 8)) & 1;
            const bool hasRelY = (relBits[REL_Y / 8] >> (REL_Y % 8)) & 1;
            if (hasRelX && hasRelY) {
                char name[256] = {};
                ioctl(fd, EVIOCGNAME(sizeof(name)), name);
                LOGI("EvdevMouseReader: found mouse '%s' at %s", name, path.c_str());
                close(fd);
                result = path;
                break;
            }
        }
        close(fd);
    }
    closedir(dir);
    return result;
}

// ── Start / Stop ─────────────────────────────────────────────────────────────

bool EvdevMouseReader::Start() {
    if (running_.load()) return true;

    std::string devPath = FindMouseDevice();
    if (devPath.empty()) {
        LOGW("EvdevMouseReader: no mouse device found in /dev/input/");
        return false;
    }

    fd_ = open(devPath.c_str(), O_RDONLY | O_NONBLOCK);
    if (fd_ < 0) {
        LOGE("EvdevMouseReader: failed to open %s: %s", devPath.c_str(), strerror(errno));
        return false;
    }

    stopFlag_.store(false);
    running_.store(true);
    thread_ = std::thread([this]() { ReadLoop(); });
    return true;
}

void EvdevMouseReader::Stop() {
    stopFlag_.store(true);
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
    if (thread_.joinable()) thread_.join();
    running_.store(false);
}

// ── Read loop ────────────────────────────────────────────────────────────────

void EvdevMouseReader::ReadLoop() {
    LOGI("EvdevMouseReader: read loop started");

    int32_t accumX = 0, accumY = 0;
    int32_t accumWheel = 0;
    uint32_t buttonMask = 0;  // tracks current button state

    while (!stopFlag_.load()) {
        struct pollfd pfd{};
        pfd.fd = fd_;
        pfd.events = POLLIN;

        int ret = poll(&pfd, 1, 50);  // 50ms timeout
        if (ret <= 0) {
            // Timeout or error — flush accumulated movement.
            if (accumX != 0 || accumY != 0) {
                forwarder_.SendMouseMove(accumX, accumY);
                accumX = accumY = 0;
            }
            continue;
        }

        struct input_event ev;
        while (read(fd_, &ev, sizeof(ev)) == sizeof(ev)) {
            switch (ev.type) {
                case EV_REL:
                    if (ev.code == REL_X)      accumX += ev.value;
                    else if (ev.code == REL_Y) accumY += ev.value;
                    else if (ev.code == REL_WHEEL) accumWheel += ev.value;
                    break;

                case EV_KEY:
                    if (ev.code == BTN_LEFT || ev.code == BTN_RIGHT || ev.code == BTN_MIDDLE) {
                        const bool pressed = (ev.value == 1);
                        uint16_t rdpButton = 0;
                        if (ev.code == BTN_LEFT)   rdpButton = PTR_FLAGS_BUTTON1;
                        if (ev.code == BTN_RIGHT)  rdpButton = PTR_FLAGS_BUTTON2;
                        if (ev.code == BTN_MIDDLE) rdpButton = PTR_FLAGS_BUTTON3;

                        if (pressed) {
                            buttonMask |= rdpButton;
                            forwarder_.SendMouseButton(rdpButton | PTR_FLAGS_DOWN);
                        } else {
                            buttonMask &= ~rdpButton;
                            forwarder_.SendMouseButton(rdpButton);
                        }
                    }
                    break;

                case EV_SYN:
                    // Flush movement on SYN_REPORT.
                    if (accumX != 0 || accumY != 0) {
                        forwarder_.SendMouseMove(accumX, accumY);
                        accumX = accumY = 0;
                    }
                    if (accumWheel != 0) {
                        forwarder_.SendMouseWheel(accumWheel);
                        accumWheel = 0;
                    }
                    break;
            }
        }
    }

    LOGI("EvdevMouseReader: read loop ended");
}
