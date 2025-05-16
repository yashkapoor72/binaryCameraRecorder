cmake_minimum_required(VERSION 3.10)
project(binaryCameraRecorder)

# Basic setup
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=hidden")

# Plugin visibility configuration
add_definitions(
    -DGST_PLUGIN_BUILD_STATIC
)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

set(CUSTOM_GST_PREFIX "$ENV{HOME}/custom-gst")

if(NOT EXISTS "${CUSTOM_GST_PREFIX}")
    message(FATAL_ERROR "Custom GStreamer not found at ${CUSTOM_GST_PREFIX}. "
        "Please build and install with:\n"
        "  meson setup builddir --prefix=${CUSTOM_GST_PREFIX}\n"
        "  ninja -C builddir\n"
        "  ninja -C builddir install")
endif()

# Only search in our custom prefix
set(CMAKE_PREFIX_PATH "${CUSTOM_GST_PREFIX}")
set(ENV{PKG_CONFIG_PATH} "${CUSTOM_GST_PREFIX}/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")

find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED
    gstreamer-1.0
    gstreamer-video-1.0
    gstreamer-app-1.0
    gstreamer-audio-1.0
)

# Verify paths point to our custom installation
if(NOT "${GST_INCLUDE_DIRS}" MATCHES "${CUSTOM_GST_PREFIX}")
    message(FATAL_ERROR "GStreamer include paths don't point to custom installation: ${GST_INCLUDE_DIRS}")
endif()
# =============================================

# OpenCV configuration
find_package(OpenCV REQUIRED)

# Include directories
include_directories(
    ${GST_INCLUDE_DIRS}
    ${OpenCV_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/plugins/recording_plugin
)

# Link directories
link_directories(
    ${GST_LIBRARY_DIRS}
)

# Build main application
add_executable(recording_app
    src/main.cpp
    src/command_handler.cpp
    src/deskew_handler.cpp
    plugins/recording_plugin/gstrecording.cpp
)
target_link_libraries(recording_app
    ${GST_LIBRARIES}
    ${OpenCV_LIBS}
)

# macOS specific settings
if(APPLE)
    find_library(COCOA_LIBRARY Cocoa)
    find_library(COREVIDEO_LIBRARY CoreVideo)
    find_library(COREAUDIO_LIBRARY CoreAudio)
    target_link_libraries(recording_app
        ${COCOA_LIBRARY}
        ${COREVIDEO_LIBRARY}
        ${COREAUDIO_LIBRARY}
    )
    set(CMAKE_OSX_ARCHITECTURES "arm64")
    
    # Use only our custom plugin path
    set(GST_PLUGIN_PATH "${CUSTOM_GST_PREFIX}/lib/gstreamer-1.0")
    set(ENV{GST_PLUGIN_PATH} ${GST_PLUGIN_PATH})
endif()

# Installation
install(TARGETS recording_app DESTINATION bin)

# Post-build verification
add_custom_command(TARGET recording_app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "Verifying plugin registration..."
    COMMAND sh -c "system_profiler SPCameraDataType"
    COMMAND ${CMAKE_COMMAND} -E echo "Verifying GStreamer plugin path: $ENV{GST_PLUGIN_PATH}"
    COMMAND sh -c "gst-device-monitor-1.0 Audio"
    VERBATIM
)