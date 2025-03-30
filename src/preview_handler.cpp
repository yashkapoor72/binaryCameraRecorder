#include "preview_handler.h"
#include <iostream>
PreviewHandler::PreviewHandler() { gst_init(nullptr, nullptr); }
PreviewHandler::~PreviewHandler() {
    if (pipeline) {
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline);
    }
}
bool PreviewHandler::setupPipeline() {
    pipeline = gst_pipeline_new("preview-pipeline");
    src = gst_element_factory_make("avfvideosrc", "source");
    convert = gst_element_factory_make("videoconvert", "convert");
    sink = gst_element_factory_make("osxvideosink", "sink");
    
    if (!pipeline || !src || !convert || !sink) {
        std::cerr << "Failed to create preview pipeline elements" << std::endl;
        return false;
    }
    
    g_object_set(src, "do-timestamp", TRUE, NULL);
    gst_bin_add_many(GST_BIN(pipeline), src, convert, sink, NULL);
    
    if (!gst_element_link_many(src, convert, sink, NULL)) {
        std::cerr << "Failed to link preview pipeline elements" << std::endl;
        return false;
    }
    
    if (gst_element_set_state(pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        std::cerr << "Failed to start preview pipeline" << std::endl;
        return false;
    }
    
    return true;
}
