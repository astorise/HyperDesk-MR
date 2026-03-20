cmake_minimum_required(VERSION 3.22)
project(hyperdesk_mr_tests CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# M_PI needs _USE_MATH_DEFINES on MSVC; on glibc it is available by default
# when _GNU_SOURCE is set (which GCC does automatically). Be explicit.
add_compile_definitions(_USE_MATH_DEFINES)

# ── GoogleTest via FetchContent ────────────────────────────────────────────────
include(FetchContent)
FetchContent_Declare(
    googletest
    URL      https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
    URL_HASH SHA256=8ad598c73ad796e0d8280b082cebd82a630d73e73cd3c70057938a6501bba5d7
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# ── OpenXR headers ─────────────────────────────────────────────────────────────
# Provided by `libopenxr-dev` on Ubuntu 24.04.
find_package(OpenXR REQUIRED)

# ── Paths ──────────────────────────────────────────────────────────────────────
set(APP_CPP_DIR   "${CMAKE_CURRENT_SOURCE_DIR}/../app/src/main/cpp")
set(STUBS_DIR     "${CMAKE_CURRENT_SOURCE_DIR}/stubs")

# ── Production source files compiled for tests ────────────────────────────────
set(PROD_SOURCES
    "${APP_CPP_DIR}/scene/MonitorLayout.cpp"
    "${APP_CPP_DIR}/scene/FrustumCuller.cpp"
    "${APP_CPP_DIR}/codec/MediaCodecDecoder.cpp"
    "${APP_CPP_DIR}/rdp/RdpDisplayControl.cpp"
)

# ── Test executable ────────────────────────────────────────────────────────────
add_executable(hyperdesk_tests
    test_MonitorLayout.cpp
    test_FrustumCuller.cpp
    test_RdpDisplayControl.cpp
    # Stub implementations (no-op Android NDK functions)
    stubs/android_stubs.cpp
    # Production sources under test
    ${PROD_SOURCES}
)

target_include_directories(hyperdesk_tests PRIVATE
    # Production headers (include as if we were in app/src/main/cpp/)
    "${APP_CPP_DIR}"
    # Stub headers shadow Android/FreeRDP system headers
    "${STUBS_DIR}"
    # OpenXR headers (from libopenxr-dev)
    OpenXR::Headers
)

target_link_libraries(hyperdesk_tests
    PRIVATE
        GTest::gtest_main
        OpenXR::Headers
)

# Suppress warnings that trip on the stubs or production code built on Linux.
target_compile_options(hyperdesk_tests PRIVATE
    -Wno-unused-parameter
    -Wno-unused-variable
)

# ── CTest integration ──────────────────────────────────────────────────────────
include(GoogleTest)
gtest_discover_tests(hyperdesk_tests)
