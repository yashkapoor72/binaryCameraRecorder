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

# Package configuration
find_package(PkgConfig REQUIRED)

# GStreamer configuration
pkg_check_modules(GST REQUIRED
    gstreamer-1.0
    gstreamer-video-1.0
    gstreamer-app-1.0
    gstreamer-audio-1.0
)

# OpenCV configuration
find_package(OpenCV REQUIRED)
if(NOT OpenCV_FOUND)
    set(OpenCV_INCLUDE_DIRS "/opt/homebrew/Cellar/opencv/4.11.0_1/include/opencv4")
    set(OpenCV_LIBS opencv_core opencv_imgproc opencv_videoio)
    message(STATUS "Using fallback OpenCV paths: ${OpenCV_INCLUDE_DIRS}")
endif()

# Include directories
include_directories(
    ${GST_INCLUDE_DIRS}
    ${OpenCV_INCLUDE_DIRS}
    /usr/local/include/gstreamer-1.0
    /usr/local/include/glib-2.0
    /usr/local/lib/glib-2.0/include
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/plugins/recording_plugin
)

# Link directories
link_directories(
    ${GST_LIBRARY_DIRS}
    /usr/local/lib
    /opt/homebrew/lib
)

# Build the OpenCV perspective plugin
add_library(opencvperspective MODULE
    plugins/recording_plugin/gstopencvperspective.cpp
)
target_link_libraries(opencvperspective
    ${GST_LIBRARIES}
    ${OpenCV_LIBS}
)
set_target_properties(opencvperspective PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    PREFIX ""
    OUTPUT_NAME "opencvperspective"
)

if(APPLE)
    set_target_properties(opencvperspective PROPERTIES
        SUFFIX ".dylib"
        MACOSX_RPATH ON
        INSTALL_RPATH "@loader_path"
    )
endif()

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
    
    # Set plugin path for runtime
    set(GST_PLUGIN_PATH "$ENV{GST_PLUGIN_PATH}:/usr/local/lib/gstreamer-1.0:/opt/homebrew/lib/gstreamer-1.0:${CMAKE_BINARY_DIR}")
    set(ENV{GST_PLUGIN_PATH} ${GST_PLUGIN_PATH})
endif()

# Installation
install(TARGETS recording_app DESTINATION bin)
install(TARGETS opencvperspective DESTINATION lib/gstreamer-1.0)

# Post-build verification
add_custom_command(TARGET recording_app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "Verifying plugin registration..."
    COMMAND sh -c "GST_DEBUG=3 ./recording_app --gst-plugin-path=${GST_PLUGIN_PATH} --gst-disable-registry-fork --version"
    COMMENT "Running basic plugin verification"
)