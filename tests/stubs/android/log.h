#pragma once
// Host stub for <android/log.h> — redirects to no-op for unit tests.

#define ANDROID_LOG_VERBOSE 2
#define ANDROID_LOG_DEBUG   3
#define ANDROID_LOG_INFO    4
#define ANDROID_LOG_WARN    5
#define ANDROID_LOG_ERROR   6

// Swallow all log calls in host tests.
#define __android_log_print(prio, tag, ...) (0)
