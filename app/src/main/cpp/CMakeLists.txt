cmake_minimum_required(VERSION 3.22)
project(hyperdesk_mr LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Compiler flags (non-Werror, safe for third-party code) ────────────────────
add_compile_options(
    -ffunction-sections
    -fdata-sections
    -O2
    -march=armv8-a
)
add_link_options(-Wl,--gc-sections)

# ── ZXing-cpp (QR code decoding) ──────────────────────────────────────────────
include(FetchContent)
FetchContent_Declare(
    zxing-cpp
    GIT_REPOSITORY https://github.com/zxing-cpp/zxing-cpp.git
    GIT_TAG        v2.2.1
    GIT_SHALLOW    TRUE
)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_BLACKBOX_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_PYTHON_MODULE OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(zxing-cpp)

# ── Dear ImGui (UI toolkit) ───────────────────────────────────────────────────
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.5
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(imgui)
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
)
target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_compile_definitions(imgui PUBLIC
    IMGUI_IMPL_VULKAN_NO_PROTOTYPES=0
    VK_NO_PROTOTYPES=0
)
target_link_libraries(imgui PUBLIC ${vulkan-lib})

# ── Paths to third-party dependencies ─────────────────────────────────────────
# Resolve relative to the repo root (4 levels up from app/src/main/cpp/)
get_filename_component(REPO_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../.."
    ABSOLUTE)

set(THIRD_PARTY_DIR   "${REPO_ROOT}/third_party")
set(OPENXR_SDK_ROOT   "${THIRD_PARTY_DIR}/openxr")
set(FREERDP_ROOT      "${THIRD_PARTY_DIR}/freerdp")
set(GLUE_DIR          "${THIRD_PARTY_DIR}/android_native_app_glue")

# ── android_native_app_glue ────────────────────────────────────────────────────
# Prefer the version shipped with the NDK over our repo stub.
if(DEFINED ANDROID_NDK)
    set(GLUE_SRC "${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c")
    set(GLUE_INC "${ANDROID_NDK}/sources/android/native_app_glue")
else()
    set(GLUE_SRC "${GLUE_DIR}/android_native_app_glue.c")
    set(GLUE_INC "${GLUE_DIR}")
endif()

add_library(native_app_glue STATIC ${GLUE_SRC})
target_include_directories(native_app_glue PUBLIC ${GLUE_INC})
# NDK glue must not use -fvisibility=hidden: ANativeActivity_onCreate must be
# exported from the final .so so the Android runtime can find it.
target_compile_options(native_app_glue PRIVATE -Wno-unused-parameter)

# ── Android NDK built-in libraries ────────────────────────────────────────────
find_library(log-lib     log)
find_library(android-lib android)
find_library(mediandk-lib mediandk)
find_library(vulkan-lib   vulkan)
find_library(camera2ndk-lib camera2ndk)
find_library(nativewindow-lib nativewindow)
find_library(opensles-lib OpenSLES)
find_library(z-lib z)

# ── OpenXR Loader (Meta XR SDK prebuilt) ──────────────────────────────────────
add_library(openxr_loader SHARED IMPORTED)
set_target_properties(openxr_loader PROPERTIES
    IMPORTED_LOCATION
        "${OPENXR_SDK_ROOT}/libs/android.arm64-v8a/libopenxr_loader.so"
)
target_include_directories(openxr_loader INTERFACE
    "${OPENXR_SDK_ROOT}/include"
    "${OPENXR_SDK_ROOT}/include/openxr"
)

# ── OpenSSL prebuilt shared libraries ─────────────────────────────────────────
set(OPENSSL_ROOT "${THIRD_PARTY_DIR}/openssl")
foreach(LIB ssl_3 crypto_3)
    add_library(${LIB} SHARED IMPORTED)
    set_target_properties(${LIB} PROPERTIES
        IMPORTED_LOCATION "${OPENSSL_ROOT}/libs/android.arm64-v8a/lib${LIB}.so"
    )
endforeach()

# ── FreeRDP 3.x static libraries ──────────────────────────────────────────────
foreach(LIB freerdp3 freerdp-client3 winpr3)
    add_library(${LIB} STATIC IMPORTED)
    set_target_properties(${LIB} PROPERTIES
        IMPORTED_LOCATION "${FREERDP_ROOT}/lib/lib${LIB}.a"
    )
endforeach()

# ── Main shared library ────────────────────────────────────────────────────────
add_library(hyperdesk_mr SHARED
    main.cpp
    xr/XrContext.cpp
    xr/XrSwapchain.cpp
    xr/XrPassthrough.cpp
    xr/XrCompositor.cpp
    xr/StatusOverlay.cpp
    xr/CursorOverlay.cpp
    xr/ImGuiToolbar.cpp
    rdp/RdpConnectionManager.cpp
    rdp/RdpDisplayControl.cpp
    rdp/RdpInputForwarder.cpp
    rdp/EvdevMouseReader.cpp
    codec/MediaCodecDecoder.cpp
    codec/CodecSurfaceBridge.cpp
    scene/MonitorLayout.cpp
    scene/FrustumCuller.cpp
    scene/VirtualMonitor.cpp
    camera/CameraManager.cpp
    camera/QrScanner.cpp
)

target_include_directories(hyperdesk_mr PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    "${OPENXR_SDK_ROOT}/include"
    "${OPENXR_SDK_ROOT}/include/openxr"
    "${FREERDP_ROOT}/include"
)

# Strict warnings only for our own code (not third-party FetchContent deps).
target_compile_options(hyperdesk_mr PRIVATE
    -Wall -Wextra -Werror
    -Wno-missing-field-initializers
    -fvisibility=hidden
)

# --gc-sections would otherwise eliminate ANativeActivity_onCreate because
# nothing in our code calls it directly — only the Android runtime does.
# --undefined forces the linker to pull it from native_app_glue and keep it.
target_link_options(hyperdesk_mr PRIVATE "-Wl,--undefined=ANativeActivity_onCreate")

target_compile_definitions(hyperdesk_mr PRIVATE
    XR_USE_GRAPHICS_API_VULKAN
    XR_USE_PLATFORM_ANDROID
)

target_link_libraries(hyperdesk_mr
    PRIVATE
        native_app_glue
        openxr_loader
        freerdp3
        freerdp-client3
        winpr3
        ssl_3
        crypto_3
        ZXing::ZXing
        imgui
        ${log-lib}
        ${android-lib}
        ${mediandk-lib}
        ${vulkan-lib}
        ${camera2ndk-lib}
        ${nativewindow-lib}
        ${opensles-lib}
        ${z-lib}
        jnigraphics
)
