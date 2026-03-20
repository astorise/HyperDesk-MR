#pragma once
#include <android_native_app_glue.h>
#include <android/asset_manager.h>
#include <jni.h>

namespace AndroidBridge {

inline AAssetManager* GetAssetManager(android_app* app) {
    return app->activity->assetManager;
}

inline JavaVM* GetJavaVM(android_app* app) {
    return app->activity->vm;
}

inline ANativeWindow* GetNativeWindow(android_app* app) {
    return app->window;
}

}  // namespace AndroidBridge
