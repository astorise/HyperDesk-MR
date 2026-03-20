#pragma once
#include <android/log.h>

#define HD_TAG "HyperDesk-MR"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  HD_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, HD_TAG, __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, HD_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  HD_TAG, __VA_ARGS__)
