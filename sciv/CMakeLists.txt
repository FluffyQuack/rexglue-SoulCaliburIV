# sciv - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(sciv LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

# Sources
set(SCIV_SOURCES
    src/main.cpp
    src/sciv_crash.cpp
    src/sciv_hooks.cpp
    src/sciv_timing.cpp
    src/sciv_settings.cpp
    src/sciv_screenshot.cpp
    # Vendored lodepng (zlib license) - PNG encoder for the F12 guest-output
    # capture (src/sciv_screenshot.cpp).
    thirdparty/lodepng/lodepng.cpp
)

if(WIN32)
    enable_language(RC)

    set(SCIV_ICON
        "${CMAKE_CURRENT_SOURCE_DIR}/../IMG_0867 (0-00-00-00).ico")
    if(NOT EXISTS "${SCIV_ICON}")
        message(FATAL_ERROR "SCIV icon not found: ${SCIV_ICON}")
    endif()

    set(SCIV_RESOURCE
        "${CMAKE_CURRENT_BINARY_DIR}/sciv.rc")
    configure_file(
        resources/sciv.rc.in
        "${SCIV_RESOURCE}"
        @ONLY
    )
    set_property(
        SOURCE "${SCIV_RESOURCE}"
        PROPERTY OBJECT_DEPENDS "${SCIV_ICON}"
    )

    add_executable(sciv WIN32 ${SCIV_SOURCES})
    target_sources(sciv PRIVATE "${SCIV_RESOURCE}")
else()
    add_executable(sciv ${SCIV_SOURCES})
endif()

rexglue_setup_target(sciv)

# Vendored lodepng header (PNG capture in src/sciv_screenshot.cpp).
target_include_directories(sciv PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/lodepng")

# dbghelp: SymFromAddr / StackWalk64 for the native-crash backtrace handler
# (src/sciv_crash.cpp). winmm: timeBeginPeriod for the timer-resolution fix
# (src/sciv_timing.cpp). #pragma comment(lib) covers them too, but be explicit.
if(WIN32)
    target_link_libraries(sciv PRIVATE dbghelp winmm)
endif()

# Note: the injected SDK rex_app.cpp needs the rexcore/imgui header dirs
# (<imgui.h>, generated <rex/version.h>), but rexglue_configure_target() now
# propagates those to this target centrally (see cmake/rexglue_helpers.cmake),
# so no per-project REXSDK_DIR include block is required here.
