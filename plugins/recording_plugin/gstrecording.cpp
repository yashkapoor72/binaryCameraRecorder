#include "gstrecording.h"
#include <iostream>
#include <opencv2/opencv.hpp>
#include <glib.h>

GstRecording::GstRecording() {
    gst_init(nullptr, nullptr);
}

GstRecording::~GstRecording() {
    std::lock_guard<std::mutex> lock(mutex);
    for (auto& [path, session] : recordings) {
        if (session.pipeline) {
            gst_element_set_state(session.pipeline, GST_STATE_NULL);
            gst_object_unref(session.pipeline);
        }
    }
    recordings.clear();
}

bool GstRecording::startRecording(const std::string& outputPath,
                                const std::vector<std::pair<double, double>>& points,
                                const std::string& flip_mode) {
    std::lock_guard<std::mutex> lock(mutex);
    if (recordings.count(outputPath)) {
        std::cerr << "Recording already in progress for: " << outputPath << std::endl;
        return false;
    }
    return createPipeline(outputPath, points, flip_mode);
}

bool GstRecording::stopRecording(const std::string& outputPath) {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = recordings.find(outputPath);
    if (it == recordings.end()) {
        std::cerr << "No active recording found for: " << outputPath << std::endl;
        return false;
    }

    RecordingSession& session = it->second;
    
    if (!session.pipeline || !GST_IS_ELEMENT(session.pipeline)) {
        std::cerr << "Invalid pipeline for: " << outputPath << std::endl;
        recordings.erase(it);
        return false;
    }

    // Send EOS
    if (!gst_element_send_event(session.pipeline, gst_event_new_eos())) {
        std::cerr << "Failed to send EOS event" << std::endl;
    }

    // Wait for EOS with proper GStreamer types
    GstBus* bus = gst_element_get_bus(session.pipeline);
    if (bus) {
        GstMessage* msg = gst_bus_timed_pop_filtered(bus, 
            GST_CLOCK_TIME_NONE,  // This is already of type GstClockTime
            static_cast<GstMessageType>(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));
        
        if (msg) {
            if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR) {
                GError* err = nullptr;
                gchar* debug = nullptr;
                gst_message_parse_error(msg, &err, &debug);
                std::cerr << "Error while stopping: " << err->message << std::endl;
                if (debug) std::cerr << "Debug: " << debug << std::endl;
                g_error_free(err);
                g_free(debug);
            }
            gst_message_unref(msg);
        }
        gst_object_unref(bus);
    }

    // Stop pipeline
    gst_element_set_state(session.pipeline, GST_STATE_NULL);
    
    // Remove from map
    recordings.erase(it);

    std::cout << "Successfully stopped recording: " << outputPath << std::endl;
    return true;
}

bool GstRecording::createPipeline(const std::string& outputPath,
                                const std::vector<std::pair<double, double>>& points,
                                const std::string& flip_mode) {
    if (points.size() != 4) {
        std::cerr << "Need exactly 4 points for perspective transform" << std::endl;
        return false;
    }

    const std::unordered_map<std::string, int> flip_methods = {
        {"none", 0}, {"horizontal", 1}, {"vertical", 2}, 
        {"clockwise", 3}, {"counterclockwise", 4}};

    if (!flip_methods.count(flip_mode)) {
        std::cerr << "Invalid flip mode: " << flip_mode << std::endl;
        return false;
    }

    // Verify required plugins are available
    GstPluginFeature* perspective_plugin = gst_registry_find_feature(
        gst_registry_get(), "perspective", GST_TYPE_ELEMENT_FACTORY);
    if (!perspective_plugin) {
        std::cerr << "Perspective plugin not found. Install with:\n"
                  << "  brew install gst-plugins-bad" << std::endl;
        return false;
    }
    gst_object_unref(perspective_plugin);

    RecordingSession session;
    session.pipeline = gst_pipeline_new("recording-pipeline");
    if (!session.pipeline) {
        std::cerr << "Failed to create pipeline" << std::endl;
        return false;
    }

    // Create elements with error checking
    auto create_element = [](const gchar* factoryname, const gchar* name) -> GstElement* {
        GstElement* element = gst_element_factory_make(factoryname, name);
        if (!element) {
            std::cerr << "Failed to create element: " << name 
                      << " (is plugin '" << factoryname << "' installed?)" << std::endl;
        }
        return element;
    };

    // Video pipeline elements
    GstElement* video_src = create_element("avfvideosrc", "video_src");
    GstElement* video_convert1 = create_element("videoconvert", "video_convert1");
    GstElement* videoscale = create_element("videoscale", "videoscale");
    GstElement* video_capsfilter = create_element("capsfilter", "video_caps");
    GstElement* video_convert_before_perspective = create_element("videoconvert", "video_convert_before_perspective");
    GstElement* perspective = create_element("perspective", "perspective");
    GstElement* video_flip = create_element("videoflip", "video_flip");
    GstElement* video_convert2 = create_element("videoconvert", "video_convert2");
    GstElement* video_queue = create_element("queue", "video_queue");
    GstElement* video_encoder = create_element("x264enc", "video_encoder");
    
    // Audio pipeline elements
    GstElement* audio_src = create_element("osxaudiosrc", "audio_src");
    GstElement* audio_convert = create_element("audioconvert", "audio_convert");
    GstElement* audio_resample = create_element("audioresample", "audio_resample");
    GstElement* audio_encoder = create_element("avenc_aac", "audio_encoder");
    GstElement* audio_queue = create_element("queue", "audio_queue");
    
    // Muxer and sink
    GstElement* muxer = create_element("mp4mux", "muxer");
    GstElement* filesink = create_element("filesink", "filesink");

    // Check if all elements were created
    if (!video_src || !video_convert1 || !videoscale || !video_capsfilter || 
        !video_convert_before_perspective || !perspective || !video_flip || 
        !video_convert2 || !video_queue || !video_encoder ||
        !audio_src || !audio_convert || !audio_resample || !audio_encoder ||
        !audio_queue || !muxer || !filesink) {
        return false;
    }

    // Configure video source
    g_object_set(video_src,
                "do-timestamp", TRUE,
                "capture-screen", FALSE,
                NULL);

    // Set video caps - using BGRA format for better compatibility
    GstCaps* video_caps = gst_caps_new_simple("video/x-raw",
                                            "format", G_TYPE_STRING, "BGRA",
                                            "width", G_TYPE_INT, 1280,
                                            "height", G_TYPE_INT, 720,
                                            "framerate", GST_TYPE_FRACTION, 30, 1,
                                            NULL);
    g_object_set(video_capsfilter, "caps", video_caps, NULL);
    gst_caps_unref(video_caps);

    // Configure audio source


    // Configure perspective transform
    const int output_width = 1280;
    const int output_height = 720;

    std::vector<cv::Point2f> dst_points = {
        cv::Point2f(static_cast<float>(points[0].first), static_cast<float>(points[0].second)),
        cv::Point2f(static_cast<float>(points[1].first), static_cast<float>(points[1].second)),
        cv::Point2f(static_cast<float>(points[2].first), static_cast<float>(points[2].second)),
        cv::Point2f(static_cast<float>(points[3].first), static_cast<float>(points[3].second))
    };

    std::vector<cv::Point2f> src_points = {
        cv::Point2f(0.0f, 0.0f),
        cv::Point2f(static_cast<float>(output_width - 1), 0.0f),
        cv::Point2f(static_cast<float>(output_width - 1), static_cast<float>(output_height - 1)),
        cv::Point2f(0.0f, static_cast<float>(output_height - 1))
    };

    cv::Mat transform = cv::getPerspectiveTransform(src_points, dst_points);

    GValueArray* matrix_array = g_value_array_new(9);
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            GValue val = G_VALUE_INIT;
            g_value_init(&val, G_TYPE_DOUBLE);
            g_value_set_double(&val, transform.at<double>(i, j));
            g_value_array_append(matrix_array, &val);
            g_value_unset(&val);
        }
    }
    g_object_set(G_OBJECT(perspective), "matrix", matrix_array, NULL);
    g_value_array_free(matrix_array);

    // Configure flip
    g_object_set(video_flip, "method", flip_methods.at(flip_mode), NULL);

    // Configure encoders
    g_object_set(video_encoder,
                "bitrate", 2000,
                "tune", 0x00000004,  // zerolatency
                "key-int-max", 30,
                NULL);

    g_object_set(audio_encoder,
                "bitrate", 128000,
                NULL);

    // Configure filesink
    g_object_set(filesink,
                "location", outputPath.c_str(),
                "sync", FALSE,
                NULL);

    // Build the pipeline
    gst_bin_add_many(GST_BIN(session.pipeline),
                    video_src, video_convert1, videoscale, video_capsfilter,
                    video_convert_before_perspective, perspective, video_flip, 
                    video_convert2,video_encoder, video_queue,
                    audio_src, audio_convert, audio_resample, audio_encoder,
                    audio_queue,
                    muxer, filesink,
                    NULL);

    // Link elements with detailed error reporting
    auto link_elements = [](GstElement* elem1, GstElement* elem2) -> bool {
        if (!gst_element_link(elem1, elem2)) {
            GstPad* srcpad = gst_element_get_static_pad(elem1, "src");
            GstPad* sinkpad = gst_element_get_static_pad(elem2, "sink");
            
            gchar* src_caps_str = g_strdup("(none)");
            gchar* sink_caps_str = g_strdup("(none)");
            
            if (srcpad) {
                GstCaps* src_caps = gst_pad_get_current_caps(srcpad);
                if (src_caps) {
                    g_free(src_caps_str);
                    src_caps_str = gst_caps_to_string(src_caps);
                    gst_caps_unref(src_caps);
                }
                gst_object_unref(srcpad);
            }
            
            if (sinkpad) {
                GstCaps* sink_caps = gst_pad_get_allowed_caps(sinkpad);
                if (sink_caps) {
                    g_free(sink_caps_str);
                    sink_caps_str = gst_caps_to_string(sink_caps);
                    gst_caps_unref(sink_caps);
                }
                gst_object_unref(sinkpad);
            }
            
            std::cerr << "Failed to link " << GST_OBJECT_NAME(elem1) 
                      << " -> " << GST_OBJECT_NAME(elem2) << "\n"
                      << "  Source caps: " << src_caps_str << "\n"
                      << "  Sink caps:   " << sink_caps_str << std::endl;
            
            g_free(src_caps_str);
            g_free(sink_caps_str);
            return false;
        }
        return true;
    };

    // Link video chain
    if (!link_elements(video_src, video_convert1) ||
        !link_elements(video_convert1, videoscale) ||
        !link_elements(videoscale, video_capsfilter) ||
        !link_elements(video_capsfilter, video_convert_before_perspective) ||
        !link_elements(video_convert_before_perspective, perspective) ||
        !link_elements(perspective, video_flip) ||
        !link_elements(video_flip, video_convert2) ||
        !link_elements(video_convert2, video_encoder) ||
        !link_elements(video_encoder, video_queue)) {
        return false;
    }

    // Link audio chain - with additional audioresample element
    if (!link_elements(audio_src, audio_convert) ||
        !link_elements(audio_convert, audio_resample) ||
        !link_elements(audio_resample, audio_encoder) ||
        !link_elements(audio_encoder, audio_queue)) {
        return false;
    }

    // Get muxer pads and link
    GstPad* video_sink_pad = gst_element_get_request_pad(muxer, "video_%u");
    GstPad* audio_sink_pad = gst_element_get_request_pad(muxer, "audio_%u");

    GstPad* video_src_pad = gst_element_get_static_pad(video_queue, "src");
    GstPad* audio_src_pad = gst_element_get_static_pad(audio_queue, "src");

    if (!video_sink_pad || !audio_sink_pad || !video_src_pad || !audio_src_pad) {
        std::cerr << "Failed to get muxer pads" << std::endl;
        if (video_sink_pad) gst_object_unref(video_sink_pad);
        if (audio_sink_pad) gst_object_unref(audio_sink_pad);
        if (video_src_pad) gst_object_unref(video_src_pad);
        if (audio_src_pad) gst_object_unref(audio_src_pad);
        return false;
    }

    if (gst_pad_link(video_src_pad, video_sink_pad) != GST_PAD_LINK_OK) {
        std::cerr << "Failed to link video encoder to muxer" << std::endl;
        gst_object_unref(video_sink_pad);
        gst_object_unref(audio_sink_pad);
        gst_object_unref(video_src_pad);
        gst_object_unref(audio_src_pad);
        return false;
    }

    if (gst_pad_link(audio_src_pad, audio_sink_pad) != GST_PAD_LINK_OK) {
        std::cerr << "Failed to link audio encoder to muxer" << std::endl;
        gst_object_unref(video_sink_pad);
        gst_object_unref(audio_sink_pad);
        gst_object_unref(video_src_pad);
        gst_object_unref(audio_src_pad);
        return false;
    }

    gst_object_unref(video_sink_pad);
    gst_object_unref(audio_sink_pad);
    gst_object_unref(video_src_pad);
    gst_object_unref(audio_src_pad);

    // Link muxer to filesink
    if (!gst_element_link(muxer, filesink)) {
        std::cerr << "Failed to link muxer to filesink" << std::endl;
        return false;
    }

    // Set up bus monitoring
    GstBus* bus = gst_element_get_bus(session.pipeline);
    gst_bus_add_watch(bus, [](GstBus* bus, GstMessage* msg, gpointer user_data) -> gboolean {
        switch (GST_MESSAGE_TYPE(msg)) {
            case GST_MESSAGE_ERROR: {
                GError* err = nullptr;
                gchar* debug = nullptr;
                gst_message_parse_error(msg, &err, &debug);
                std::cerr << "Error: " << err->message << std::endl;
                if (debug) std::cerr << "Debug info: " << debug << std::endl;
                g_error_free(err);
                g_free(debug);
                break;
            }
            case GST_MESSAGE_EOS:
                std::cout << "End of stream" << std::endl;
                break;
            case GST_MESSAGE_STATE_CHANGED: {
                if (GST_MESSAGE_SRC(msg) == GST_OBJECT(user_data)) {
                    GstState old_state, new_state, pending;
                    gst_message_parse_state_changed(msg, &old_state, &new_state, &pending);
                    std::cout << "Pipeline state changed from " 
                              << gst_element_state_get_name(old_state) 
                              << " to " << gst_element_state_get_name(new_state) 
                              << std::endl;
                }
                break;
            }
            default:
                break;
        }
        return TRUE;
    }, session.pipeline);
    gst_object_unref(bus);

    // Generate pipeline diagram for debugging
    GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS(GST_BIN(session.pipeline), 
        GST_DEBUG_GRAPH_SHOW_ALL, "pipeline");

    // Start pipeline
    GstStateChangeReturn ret = gst_element_set_state(session.pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        std::cerr << "Failed to start pipeline" << std::endl;
        
        // Get more detailed error message
        GError* err = nullptr;
        gchar* debug = nullptr;
        GstMessage* msg = gst_bus_poll(gst_element_get_bus(session.pipeline), 
            GST_MESSAGE_ERROR, 0);
        if (msg) {
            gst_message_parse_error(msg, &err, &debug);
            std::cerr << "Error details: " << err->message << std::endl;
            if (debug) std::cerr << "Debug info: " << debug << std::endl;
            g_error_free(err);
            g_free(debug);
            gst_message_unref(msg);
        }
        return false;
    }

    recordings.emplace(outputPath, std::move(session));
    std::cout << "Started recording to: " << outputPath << std::endl;
    return true;
}