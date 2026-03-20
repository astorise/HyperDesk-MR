#pragma once
#include "Logger.h"
#include <cstdlib>

#define XR_CHECK(expr)                                                         \
    do {                                                                       \
        XrResult _r = (expr);                                                  \
        if (XR_FAILED(_r)) {                                                   \
            LOGE("XR_CHECK failed: %s = %d at %s:%d",                         \
                 #expr, static_cast<int>(_r), __FILE__, __LINE__);             \
            std::abort();                                                      \
        }                                                                      \
    } while (0)

#define VK_CHECK(expr)                                                         \
    do {                                                                       \
        VkResult _r = (expr);                                                  \
        if (_r != VK_SUCCESS) {                                                \
            LOGE("VK_CHECK failed: %s = %d at %s:%d",                         \
                 #expr, static_cast<int>(_r), __FILE__, __LINE__);             \
            std::abort();                                                      \
        }                                                                      \
    } while (0)

#define MEDIA_CHECK(expr)                                                      \
    do {                                                                       \
        media_status_t _r = (expr);                                            \
        if (_r != AMEDIA_OK) {                                                 \
            LOGE("MEDIA_CHECK failed: %s = %d at %s:%d",                      \
                 #expr, static_cast<int>(_r), __FILE__, __LINE__);             \
            std::abort();                                                      \
        }                                                                      \
    } while (0)
