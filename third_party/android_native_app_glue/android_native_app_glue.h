/*
 * android_native_app_glue.h
 *
 * Sourced from Android NDK samples. Copy the actual file from:
 *   $ANDROID_NDK/sources/android/native_app_glue/android_native_app_glue.h
 *
 * This placeholder is a stub for repository structure purposes.
 * The CMake build resolves this from the NDK via:
 *   ${ANDROID_NDK}/sources/android/native_app_glue/
 */
#pragma once

#include <android/looper.h>
#include <android/configuration.h>
#include <android/input.h>
#include <android/native_activity.h>
#include <android/native_window.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>

#ifdef __cplusplus
extern "C" {
#endif

struct android_poll_source {
    int32_t id;
    struct android_app* app;
    void (*process)(struct android_app* app, struct android_poll_source* source);
};

struct android_app {
    void* userData;
    void (*onAppCmd)(struct android_app* app, int32_t cmd);
    int32_t (*onInputEvent)(struct android_app* app, AInputEvent* event);
    ANativeActivity* activity;
    AConfiguration* config;
    void* savedState;
    size_t savedStateSize;
    ALooper* looper;
    AInputQueue* inputQueue;
    ANativeWindow* window;
    ARect contentRect;
    int activityState;
    int destroyRequested;
    pthread_mutex_t mutex;
    pthread_cond_t  cond;
    int msgread;
    int msgwrite;
    pthread_t thread;
    struct android_poll_source cmdPollSource;
    struct android_poll_source inputPollSource;
    int running;
    int stateSaved;
    int destroyed;
    int redrawNeeded;
    AInputQueue* pendingInputQueue;
    ANativeWindow* pendingWindow;
    ARect pendingContentRect;
};

enum {
    LOOPER_ID_MAIN  = 1,
    LOOPER_ID_INPUT = 2,
    LOOPER_ID_USER  = 3,
};

enum {
    APP_CMD_INPUT_CHANGED,
    APP_CMD_INIT_WINDOW,
    APP_CMD_TERM_WINDOW,
    APP_CMD_WINDOW_RESIZED,
    APP_CMD_WINDOW_REDRAW_NEEDED,
    APP_CMD_CONTENT_RECT_CHANGED,
    APP_CMD_GAINED_FOCUS,
    APP_CMD_LOST_FOCUS,
    APP_CMD_CONFIG_CHANGED,
    APP_CMD_LOW_MEMORY,
    APP_CMD_START,
    APP_CMD_RESUME,
    APP_CMD_SAVE_STATE,
    APP_CMD_PAUSE,
    APP_CMD_STOP,
    APP_CMD_DESTROY,
};

void android_app_set_input_event_filter(struct android_app* app,
    bool (*filter)(struct android_app*, AInputEvent*));

extern void android_main(struct android_app* app);

#ifdef __cplusplus
}
#endif
