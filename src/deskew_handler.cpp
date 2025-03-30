#include "deskew_handler.h"
#include <iostream>
DeskewHandler::DeskewHandler() { gst_init(nullptr, nullptr); }
DeskewHandler::~DeskewHandler() {
    if (pipeline) {
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline);
    }
}
bool DeskewHandler::setupPipeline() {
    pipeline = gst_pipeline_new("preview-pipeline");
    src = gst_element_factory_make("avfvideosrc", "source");
    convert = gst_element_factory_make("videoconvert", "convert");
    crop = gst_element_factory_make("videocrop", "cropper");
    flip = gst_element_factory_make("videoflip", "flipper");
    tee = gst_element_factory_make("tee", "tee");
    previewQueue = gst_element_factory_make("queue", "preview_queue");
    previewSink = gst_element_factory_make("osxvideosink", "preview_sink");
    
    if (!pipeline || !src || !convert || !crop || !flip || !tee || !previewQueue || !previewSink) {
        std::cerr << "Failed to create pipeline elements" << std::endl;
        return false;
    }
    
    g_object_set(crop, "top", 0, "bottom", 0, "left", 0, "right", 0, NULL);
    g_object_set(tee, "allow-not-linked", TRUE, NULL);
    g_object_set(src, "do-timestamp", TRUE, NULL);
    
    gst_bin_add_many(GST_BIN(pipeline), src, convert, crop, flip, tee, previewQueue, previewSink, NULL);
    
    if (!gst_element_link_many(src, convert, crop, flip, tee, NULL)) {
        std::cerr << "Failed to link main pipeline elements" << std::endl;
        return false;
    }
    
    if (!gst_element_link(previewQueue, previewSink)) {
        std::cerr << "Failed to link preview elements" << std::endl;
        return false;
    }
    
    GstPad* teePad = gst_element_request_pad_simple(tee, "src_%u");
    GstPad* queuePad = gst_element_get_static_pad(previewQueue, "sink");
    
    if (!teePad || !queuePad || gst_pad_link(teePad, queuePad) != GST_PAD_LINK_OK) {
        std::cerr << "Failed to link tee to preview queue" << std::endl;
        if (teePad) gst_object_unref(teePad);
        if (queuePad) gst_object_unref(queuePad);
        return false;
    }
    
    gst_object_unref(teePad);
    gst_object_unref(queuePad);
    
    if (gst_element_set_state(pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        std::cerr << "Failed to start pipeline" << std::endl;
        return false;
    }
    
    return true;
}
void DeskewHandler::updateSettings(int top, int bottom, int left, int right, const std::string& flip_mode) {
    if (crop) {
        g_object_set(crop, "top", top, "bottom", bottom, "left", left, "right", right, NULL);
        std::cout << "Crop settings updated: top=" << top << " bottom=" << bottom 
                  << " left=" << left << " right=" << right << std::endl;
    }
    if (flip && flip_methods.count(flip_mode)) {
        g_object_set(flip, "method", flip_methods.at(flip_mode), NULL);
        std::cout << "Flip mode set to: " << flip_mode << std::endl;
    }
}
