#!/bin/bash

# Isolate GStreamer for this project
export GSTREAMER_ROOT="$HOME/custom-gst"
export PATH="$GSTREAMER_ROOT/bin:$PATH"
export LD_LIBRARY_PATH="$GSTREAMER_ROOT/lib:$LD_LIBRARY_PATH"
export GST_PLUGIN_PATH="$GSTREAMER_ROOT/lib/gstreamer-1.0"
export PKG_CONFIG_PATH="$GSTREAMER_ROOT/lib/pkgconfig:$PKG_CONFIG_PATH"
export DYLD_LIBRARY_PATH="$HOME/custom-gst/lib"


# Verify
echo "Using GStreamer from: $GSTREAMER_ROOT"
gst-inspect-1.0 --version