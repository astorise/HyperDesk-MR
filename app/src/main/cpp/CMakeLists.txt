cmake_minimum_required(VERSION 3.22)
project(hyperdesk_mr LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Compiler flags ─────────────────────────────────────────────────────────────
add_compile_options(
    -Wall -Wextra -Werror
    -Wno-missing-field-initializers
    -fvisibility=hidden
    -ffunction-sections
    -fdata-sections
    -O2
    -march=armv8-a
)
add_link_options(-Wl,--gc-sections)

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
# NDK glue source has unused-parameter warnings; don't apply our strict flags
set_target_properties(native_app_glue PROPERTIES COMPILE_OPTIONS "")

# ── Android NDK built-in libraries ────────────────────────────────────────────
find_library(log-lib     log)
find_library(android-lib android)
find_library(mediandk-lib mediandk)
find_library(vulkan-lib  vulkan)

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
    rdp/RdpConnectionManager.cpp
    rdp/RdpDisplayControl.cpp
    codec/MediaCodecDecoder.cpp
    codec/CodecSurfaceBridge.cpp
    scene/MonitorLayout.cpp
    scene/FrustumCuller.cpp
)

target_include_directories(hyperdesk_mr PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    "${OPENXR_SDK_ROOT}/include"
    "${OPENXR_SDK_ROOT}/include/openxr"
    "${FREERDP_ROOT}/include"
)

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
        ${log-lib}
        ${android-lib}
        ${mediandk-lib}
        ${vulkan-lib}
)
