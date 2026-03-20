#pragma once
// Host stub for <media/NdkMediaFormat.h>

#include <cstdint>

typedef struct AMediaFormat AMediaFormat;

// Format key constants (string macros matching the real NDK values).
#define AMEDIAFORMAT_KEY_MIME          "mime"
#define AMEDIAFORMAT_KEY_WIDTH         "width"
#define AMEDIAFORMAT_KEY_HEIGHT        "height"
#define AMEDIAFORMAT_KEY_COLOR_FORMAT  "color-format"
#define AMEDIAFORMAT_KEY_LOW_LATENCY   "low-latency"

#ifdef __cplusplus
extern "C" {
#endif

AMediaFormat* AMediaFormat_new();
void          AMediaFormat_delete(AMediaFormat* format);
void          AMediaFormat_setString(AMediaFormat* format, const char* name, const char* value);
void          AMediaFormat_setInt32(AMediaFormat* format, const char* name, int32_t value);

#ifdef __cplusplus
}
#endif
