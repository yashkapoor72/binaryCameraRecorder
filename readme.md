Binary Camera Recorder
======================

A macOS command-line utility for recording video with real-time cropping and flipping capabilities, built using GStreamer.

Features
--------
- Records video from built-in camera and audio from microphone
- Real-time video cropping (top, bottom, left, right)
- Video flipping options (horizontal, vertical, rotation)
- Preview window while recording
- Multiple simultaneous recordings to different files
- MP4 container format with H.264 video and AAC audio

Prerequisites
-------------
- macOS (tested on Apple Silicon M1)
- GStreamer 1.0 and plugins (installation instructions below)
- CMake 3.10 or later

Installation
------------
1. Clone this repo and in terminal switch to x86_64
   arch -x86_64 /bin/zsh
   uname -m #(verify)


2. Install Underlying dependencies 
      brew install cmake glib gobject-introspection pkg-config gtk+3 libusb v4l2loopback python3 pygobject3

3. Install GStreamer:
   cd gstreamer
   meson setup builddir \
   --prefix=$HOME/custom-gst \
   -Dugly=enabled \
   -Dgpl=enabled \
   -Dgst-plugins-ugly:x264=enabled

   ninja -C builddir
   ninja -C builddir install
   
   cd .. 
   source env.sh

4. kvsisink (optional)
   git clone https://github.com/awslabs/amazon-kinesis-video-streams-webrtc-sdk-c

   cmake .. \
   -DBUILD_GSTREAMER_PLUGIN=ON \
   -DBUILD_JNI=FALSE \
   -DBUILD_DEPENDENCIES=OFF \
   -DCMAKE_OSX_ARCHITECTURES=x86_64 \
   -DCURL_LIBRARY=/usr/local/opt/curl/lib/libcurl.dylib \
   -DCURL_INCLUDE_DIR=/usr/local/opt/curl/include \
   -DOPENSSL_ROOT_DIR=$(brew --prefix openssl@3) \
   -DCMAKE_PREFIX_PATH="/Users/mozark/custom-gst"

5 awskvswebrtcsink
   git clone https://adihk1l1ce1anibfyta203wyx7gwcv@bitbucket.org/mozarkai/gst-plugins-rs.git
   cd gst-plugin-rs/net/webrtc
   cargo cbuild -p gst-plugin-webrtc
   $ GST_PLUGIN_PATH="target/x86_64-unknown-linux-gnu/debug:$GST_PLUGIN_PATH" gst-inspect-1.0 webrtc
   Replace `x86_64-unknown-linux-gnu` with your system's Rust target triple (`rustc -vV`).






2. Clone this repository or create the project structure using the provided script.

3. Build the project:
   mkdir -p build && cd build
   cmake .. -DCMAKE_OSX_ARCHITECTURES=x86_64
   make

Usage
-----
Run the application:
   ./recording_app

Commands
--------
The application accepts commands via stdin in the following format:

1. Start recording:
   --action=start-recording --outputPath=/path/to/output.mp4 [--top=N] [--bottom=N] [--left=N] [--right=N] [--flipMethod=mode]

2. Stop recording:
   --action=stop-recording --outputPath=/path/to/output.mp4

--action=start-recording --outputPath=../output2.mp4 --p1=(622,77) --p2=(877,83) --p3=(900,684) --p4=(632,699) --width=262 --height=612
--action=start-streaming --channelName=webcam-gst-test --p1=(622,77) --p2=(877,83) --p3=(900,684) --p4=(632,699) --width=262 --height=612
GST_DEBUG=3 ./recording_app --CamDevIndex=FDF90FEB-59E5-4FCF-AABD-DA03C4E19BFB --AudioDevIndex=BuiltInMicrophoneDevice

Parameters
----------
- outputPath: Full path to the output MP4 file
- top, bottom, left, right: Number of pixels to crop from each side
- flipMethod: One of:
   - none (default)
   - horizontal
   - vertical
   - clockwise
   - counterclockwise

Examples
--------
1. Basic recording:
   --action=start-recording --outputPath=~/Desktop/recording.mp4

2. Recording with cropping (100px from top and left):
   --action=start-recording --outputPath=~/Desktop/cropped.mp4 --top=100 --left=100

3. Recording with vertical flip:
   --action=start-recording --outputPath=~/Desktop/flipped.mp4 --flipMethod=vertical

4. Stop recording:
   --action=stop-recording --outputPath=~/Desktop/recording.mp4
5. Take-Screenshot
   --action=take-screenshot --outputPathSs=../screenshot.jpeg

Technical Details
-----------------
- Uses GStreamer pipelines for media processing
- Video source: avfvideosrc (macOS AVFoundation)
- Audio source: osxaudiosrc
- Video codec: H.264 (x264enc)
- Audio codec: AAC (avenc_aac)
- Container format: MP4 (mp4mux)