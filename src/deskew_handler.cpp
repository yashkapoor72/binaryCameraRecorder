#include "deskew_handler.h"
#include <iostream>
#include <opencv2/opencv.hpp>
#include <opencv2/imgproc.hpp>
#include <glib.h> 

DeskewHandler::DeskewHandler() { 
    gst_init(nullptr, nullptr); 
    pipeline = nullptr;
    perspective = nullptr;
    flip = nullptr;
    tee = nullptr;
    previewQueue = nullptr;
    previewSink = nullptr;
    src = nullptr;
    capsFilter = nullptr;
    scale = nullptr;
}

DeskewHandler::~DeskewHandler() {
    stopPipeline();
}

void DeskewHandler::stopPipeline() {
    if (pipeline) {
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline);
        pipeline = nullptr;
    }
}

bool DeskewHandler::setupPipeline() {
    // Cleanup if already running
    stopPipeline();

    // Initialize GStreamer if not already done
    if (!gst_is_initialized()) {
        gst_init(nullptr, nullptr);
    }

    // Create pipeline
    pipeline = gst_pipeline_new("preview-pipeline");
    if (!pipeline) {
        std::cerr << "Failed to create pipeline" << std::endl;
        return false;
    }

    // Create elements
    src = gst_element_factory_make("avfvideosrc", "source");
    GstElement* capsFilter = gst_element_factory_make("capsfilter", "capsfilter");
    GstElement* convert1 = gst_element_factory_make("videoconvert", "convert1");
    GstElement* scale = gst_element_factory_make("videoscale", "scaler");
    perspective = gst_element_factory_make("perspective", "perspective");
    
    if (!perspective) {
        std::cerr << "Failed to create perspective element. Make sure gst-plugins-bad is installed." << std::endl;
        std::cerr << "Try: brew install gst-plugins-bad" << std::endl;
        stopPipeline();
        return false;
    }

    flip = gst_element_factory_make("videoflip", "flipper");
    GstElement* convert2 = gst_element_factory_make("videoconvert", "convert2");
    tee = gst_element_factory_make("tee", "tee");
    previewQueue = gst_element_factory_make("queue", "preview_queue");
    GstElement* convert3 = gst_element_factory_make("videoconvert", "convert3");
    previewSink = gst_element_factory_make("osxvideosink", "preview_sink");

    // Verify all elements were created
    if (!src || !capsFilter || !convert1 || !scale || !perspective || !flip || 
        !convert2 || !tee || !previewQueue || !convert3 || !previewSink) {
        std::cerr << "Failed to create one or more GStreamer elements:" << std::endl;
        if (!src) std::cerr << " - avfvideosrc" << std::endl;
        if (!capsFilter) std::cerr << " - capsfilter" << std::endl;
        if (!convert1) std::cerr << " - videoconvert" << std::endl;
        if (!scale) std::cerr << " - videoscale" << std::endl;
        if (!perspective) std::cerr << " - perspective" << std::endl;
        if (!flip) std::cerr << " - videoflip" << std::endl;
        if (!convert2) std::cerr << " - videoconvert" << std::endl;
        if (!tee) std::cerr << " - tee" << std::endl;
        if (!previewQueue) std::cerr << " - queue" << std::endl;
        if (!convert3) std::cerr << " - videoconvert" << std::endl;
        if (!previewSink) std::cerr << " - osxvideosink" << std::endl;
        
        stopPipeline();
        return false;
    }

    // Configure source
    g_object_set(src, 
        "do-timestamp", TRUE, 
        "device-index", 1,
        "capture-screen", FALSE,
        NULL);

    // Set flexible caps that work with most cameras
    GstCaps* caps = gst_caps_new_simple("video/x-raw",
        "format", G_TYPE_STRING, "NV12",
        "width", GST_TYPE_INT_RANGE, 640, 1920,
        "height", GST_TYPE_INT_RANGE, 480, 1080,
        "framerate", GST_TYPE_FRACTION_RANGE, 15, 1, 60, 1,
        NULL);
    g_object_set(capsFilter, "caps", caps, NULL);
    gst_caps_unref(caps);

    // Initialize perspective with identity matrix
    GValueArray *matrix_array = g_value_array_new(9);
    for (int i = 0; i < 9; i++) {
        GValue val = G_VALUE_INIT;
        g_value_init(&val, G_TYPE_DOUBLE);
        g_value_set_double(&val, (i % 4 == 0) ? 1.0 : 0.0); // Identity matrix
        g_value_array_append(matrix_array, &val);
        g_value_unset(&val);
    }
    g_object_set(G_OBJECT(perspective), "matrix", matrix_array, NULL);
    g_value_array_free(matrix_array);

    // Configure tee
    g_object_set(tee, "allow-not-linked", TRUE, NULL);

    // Add elements to pipeline
    gst_bin_add_many(GST_BIN(pipeline), 
        src, capsFilter, convert1, scale, perspective, flip, 
        convert2, tee, previewQueue, convert3, previewSink, NULL);

    // Link main elements
    if (!gst_element_link_many(src, capsFilter, convert1, scale, perspective, flip, convert2, tee, NULL)) {
        std::cerr << "Failed to link main elements" << std::endl;
        stopPipeline();
        return false;
    }

    // Link preview branch
    if (!gst_element_link_many(previewQueue, convert3, previewSink, NULL)) {
        std::cerr << "Failed to link preview branch" << std::endl;
        stopPipeline();
        return false;
    }

    // Request tee pad and get queue pad
    GstPad* teePad = gst_element_request_pad_simple(tee, "src_%u");
    if (!teePad) {
        std::cerr << "Failed to request tee pad" << std::endl;
        stopPipeline();
        return false;
    }

    GstPad* queuePad = gst_element_get_static_pad(previewQueue, "sink");
    if (!queuePad) {
        std::cerr << "Failed to get queue pad" << std::endl;
        gst_object_unref(teePad);
        stopPipeline();
        return false;
    }

    // Link pads
    GstPadLinkReturn ret = gst_pad_link(teePad, queuePad);
    if (ret != GST_PAD_LINK_OK) {
        std::cerr << "Failed to link tee to preview queue: " << gst_pad_link_get_name(ret) << std::endl;
        gst_object_unref(teePad);
        gst_object_unref(queuePad);
        stopPipeline();
        return false;
    }

    // Clean up pad references
    gst_object_unref(teePad);
    gst_object_unref(queuePad);

    // Add bus watch for error handling
    GstBus* bus = gst_element_get_bus(pipeline);
    gst_bus_add_watch(bus, [](GstBus* bus, GstMessage* msg, gpointer data) -> gboolean {
        DeskewHandler* handler = static_cast<DeskewHandler*>(data);
        
        switch (GST_MESSAGE_TYPE(msg)) {
            case GST_MESSAGE_ERROR: {
                GError* err = nullptr;
                gchar* debug = nullptr;
                gst_message_parse_error(msg, &err, &debug);
                
                std::cerr << "Error: " << err->message << std::endl;
                if (debug) {
                    std::cerr << "Debug info: " << debug << std::endl;
                    g_free(debug);
                }
                
                g_error_free(err);
                handler->stopPipeline();
                break;
            }
            case GST_MESSAGE_WARNING: {
                GError* err = nullptr;
                gchar* debug = nullptr;
                gst_message_parse_warning(msg, &err, &debug);
                
                std::cerr << "Warning: " << err->message << std::endl;
                if (debug) {
                    std::cerr << "Debug info: " << debug << std::endl;
                    g_free(debug);
                }
                
                g_error_free(err);
                break;
            }
            case GST_MESSAGE_STATE_CHANGED: {
                if (GST_MESSAGE_SRC(msg) == GST_OBJECT(handler->pipeline)) {
                    GstState old_state, new_state, pending;
                    gst_message_parse_state_changed(msg, &old_state, &new_state, &pending);
                    std::cerr << "Pipeline state changed from " 
                              << gst_element_state_get_name(old_state) 
                              << " to " << gst_element_state_get_name(new_state) 
                              << std::endl;
                }
                break;
            }
            case GST_MESSAGE_EOS:
                std::cerr << "End of stream" << std::endl;
                handler->stopPipeline();
                break;
            default:
                break;
        }
        return TRUE;
    }, this);
    gst_object_unref(bus);

    // Start pipeline
    GstStateChangeReturn stateRet = gst_element_set_state(pipeline, GST_STATE_PLAYING);
    if (stateRet == GST_STATE_CHANGE_FAILURE) {
        std::cerr << "Failed to start pipeline" << std::endl;
        stopPipeline();
        return false;
    }

    std::cout << "Pipeline started successfully with device index = 1 "<< std::endl;
    return true;
}

void DeskewHandler::updateSettings(const std::vector<std::pair<double, double>>& points, 
    const std::string& flip_mode) {
    if (points.size() != 4) {
        std::cerr << "Need exactly 4 points for perspective transform" << std::endl;
        return;
    }

    const int output_width = 1280;
    const int output_height = 720;

    // Destination points - these should be the coordinates you want to map TO
    std::vector<cv::Point2f> dst_points = {
        cv::Point2f(static_cast<float>(points[0].first), static_cast<float>(points[0].second)),
        cv::Point2f(static_cast<float>(points[1].first), static_cast<float>(points[1].second)),
        cv::Point2f(static_cast<float>(points[2].first), static_cast<float>(points[2].second)),
        cv::Point2f(static_cast<float>(points[3].first), static_cast<float>(points[3].second))
    };

    // Source points - these should be the full frame coordinates you're mapping FROM
    std::vector<cv::Point2f> src_points = {
        cv::Point2f(0.0f, 0.0f),
        cv::Point2f(static_cast<float>(output_width - 1), 0.0f),
        cv::Point2f(static_cast<float>(output_width - 1), static_cast<float>(output_height - 1)),
        cv::Point2f(0.0f, static_cast<float>(output_height - 1))
    };

    // Get the transformation matrix
    cv::Mat transform = cv::getPerspectiveTransform(src_points, dst_points);

    GValueArray *matrix_array = g_value_array_new(9);
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            GValue val = G_VALUE_INIT;
            g_value_init(&val, G_TYPE_DOUBLE);
            g_value_set_double(&val, transform.at<double>(i, j));
            g_value_array_append(matrix_array, &val);
            g_value_unset(&val);
        }
    }

    if (perspective) {
        g_object_set(G_OBJECT(perspective), "matrix", matrix_array, NULL);
        std::cout << "Updated perspective transform matrix" << std::endl;

        std::cout << "Perspective matrix:" << std::endl;
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) {
                std::cout << transform.at<double>(i, j) << " ";
            }
            std::cout << std::endl;
        }
    }
    g_value_array_free(matrix_array);

    if (flip && flip_methods.count(flip_mode)) {
        g_object_set(flip, "method", flip_methods.at(flip_mode), NULL);
        std::cout << "Updated flip method to: " << flip_mode << std::endl;
    }
}