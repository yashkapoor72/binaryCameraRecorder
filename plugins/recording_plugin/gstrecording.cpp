#include "gstrecording.h"
#include <iostream>
#include <opencv2/opencv.hpp>
#include <glib.h>
#include <gst/video/video.h>
#include <algorithm>

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
                                int output_width,
                                int output_height,
                                const std::string& flip_mode) {
    std::lock_guard<std::mutex> lock(mutex);
    if (recordings.count(outputPath)) {
        std::cerr << "Recording already in progress for: " << outputPath << std::endl;
        return false;
    }
    std::cout << "Starting recording to: " << outputPath << std::endl;
    return createPipeline(outputPath, points, output_width, output_height, flip_mode);
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

    std::cout << "Stopping recording: " << outputPath << std::endl;
    
    if (!gst_element_send_event(session.pipeline, gst_event_new_eos())) {
        std::cerr << "Failed to send EOS event" << std::endl;
    }

    GstBus* bus = gst_element_get_bus(session.pipeline);
    if (bus) {
        GstMessage* msg = gst_bus_timed_pop_filtered(bus, 
            GST_CLOCK_TIME_NONE,
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

    gst_element_set_state(session.pipeline, GST_STATE_NULL);
    g_usleep(500000);
    recordings.erase(it);
    std::cout << "Successfully stopped recording: " << outputPath << std::endl;
    return true;
}

bool GstRecording::createPipeline(const std::string& outputPath,
                                const std::vector<std::pair<double, double>>& points,
                                int output_width,
                                int output_height,
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

    RecordingSession session;
    session.pipeline = gst_pipeline_new("recording-pipeline");
    if (!session.pipeline) {
        std::cerr << "Failed to create pipeline" << std::endl;
        return false;
    }

    // Create elements
    GstElement* src = gst_element_factory_make("avfvideosrc", "source");
    GstElement* capsfilter = gst_element_factory_make("capsfilter", "capsfilter");
    GstElement* convert1 = gst_element_factory_make("videoconvert", "convert1");
    GstElement* videoscale = gst_element_factory_make("videoscale", "scaler");
    GstElement* perspective = gst_element_factory_make("perspective", "perspective");
    GstElement* flip = gst_element_factory_make("videoflip", "flipper");
    GstElement* convert2 = gst_element_factory_make("videoconvert", "convert2");
    GstElement* videobox = gst_element_factory_make("videobox", "box");
    GstElement* capsink = gst_element_factory_make("capsfilter", "capsink");
    GstElement* queue = gst_element_factory_make("queue", "queue");
    GstElement* encoder = gst_element_factory_make("x264enc", "encoder");
    GstElement* muxer = gst_element_factory_make("mp4mux", "muxer");
    session.filesink = gst_element_factory_make("filesink", "filesink");

    // Audio elements
    GstElement* audio_src = gst_element_factory_make("osxaudiosrc", "audio_src");
    GstElement* audio_convert = gst_element_factory_make("audioconvert", "audio_convert");
    GstElement* audio_resample = gst_element_factory_make("audioresample", "audio_resample");
    GstElement* audio_encoder = gst_element_factory_make("avenc_aac", "audio_encoder");
    GstElement* audio_queue = gst_element_factory_make("queue", "audio_queue");

    if (!src || !capsfilter || !convert1 || !videoscale || !perspective || !flip || 
        !convert2 || !videobox || !capsink || !queue || !encoder || !muxer || !session.filesink ||
        !audio_src || !audio_convert || !audio_resample || !audio_encoder || !audio_queue) {
        std::cerr << "Failed to create one or more GStreamer elements" << std::endl;
        return false;
    }

    // Configure source
    g_object_set(src, 
        "do-timestamp", TRUE, 
        "device-unique-id", "0x100000046d085e",
        "capture-screen", FALSE,
        NULL);

    // Configure input caps
    GstCaps* caps = gst_caps_new_simple("video/x-raw",
        "format", G_TYPE_STRING, "NV12",
        "width", GST_TYPE_INT_RANGE, 640, 1280,
        "height", GST_TYPE_INT_RANGE, 480, 720,
        "framerate", GST_TYPE_FRACTION_RANGE, 15, 1, 60, 1,
        NULL);
    g_object_set(capsfilter, "caps", caps, NULL);
    gst_caps_unref(caps);

    // Configure perspective transform
    std::vector<cv::Point2f> dst_points = {
        cv::Point2f(static_cast<float>(points[0].first), static_cast<float>(points[0].second)),
        cv::Point2f(static_cast<float>(points[1].first), static_cast<float>(points[1].second)),
        cv::Point2f(static_cast<float>(points[2].first), static_cast<float>(points[2].second)),
        cv::Point2f(static_cast<float>(points[3].first), static_cast<float>(points[3].second))
    };

    std::vector<cv::Point2f> src_points = {
        cv::Point2f(0.0f, 0.0f),
        cv::Point2f(static_cast<float>(1280 - 1), 0.0f),
        cv::Point2f(static_cast<float>(1280 - 1), static_cast<float>(720 - 1)),
        cv::Point2f(0.0f, static_cast<float>(720 - 1))
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

    g_object_set(flip, "method", flip_methods.at(flip_mode), NULL);

    // Calculate scaling and padding
    double width_ratio = 1280.0 / output_width;
    double height_ratio = 720.0 / output_height;
    double scale = std::min(width_ratio, height_ratio);
    
    int scaled_width = static_cast<int>(output_width * scale);
    int scaled_height = static_cast<int>(output_height * scale);
    
    int pad_left = (1280 - scaled_width) / 2;
    int pad_right = 1280 - scaled_width - pad_left;
    int pad_top = (720 - scaled_height) / 2;
    int pad_bottom = 720 - scaled_height - pad_top;

    // Configure videobox for padding
    g_object_set(videobox,
        "border-alpha", 1.0,
        "fill", 0,  // Black borders
        "left", pad_left,
        "right", pad_right,
        "top", pad_top,
        "bottom", pad_bottom,
        NULL);

    // Configure output caps
    GstCaps* out_caps = gst_caps_new_simple("video/x-raw",
        "format", G_TYPE_STRING, "I420",
        "width", G_TYPE_INT, 1280,
        "height", G_TYPE_INT, 720,
        "framerate", GST_TYPE_FRACTION_RANGE, 15, 1, 60, 1,
        NULL);
    g_object_set(capsink, "caps", out_caps, NULL);
    gst_caps_unref(out_caps);

    // Configure encoder
    g_object_set(encoder,
        "bitrate", 2000,
        "tune", 0x00000004,  // zerolatency
        "key-int-max", 30,
        "speed-preset", 1,  // ultrafast
        NULL);

    // Configure audio encoder
    g_object_set(audio_encoder,
        "bitrate", 128000,
        NULL);

    // Configure filesink
    g_object_set(session.filesink,
        "location", outputPath.c_str(),
        "sync", TRUE,
        NULL);

    // Build the pipeline
    gst_bin_add_many(GST_BIN(session.pipeline),
        src, capsfilter, convert1, videoscale, perspective,
        flip, convert2, videobox, capsink, queue, encoder,
        audio_src, audio_convert, audio_resample, audio_encoder, audio_queue,
        muxer, session.filesink,
        NULL);

    // Link video elements
    if (!gst_element_link_many(
        src, capsfilter, convert1, videoscale, perspective,
        flip, convert2, videobox, capsink, queue, encoder, NULL)) {
        std::cerr << "Failed to link video elements" << std::endl;
        return false;
    }

    // Link audio elements
    if (!gst_element_link_many(
        audio_src, audio_convert, audio_resample, audio_encoder, audio_queue, NULL)) {
        std::cerr << "Failed to link audio elements" << std::endl;
        return false;
    }

    // Link to muxer
    GstPad* video_sink_pad = gst_element_get_request_pad(muxer, "video_%u");
    GstPad* audio_sink_pad = gst_element_get_request_pad(muxer, "audio_%u");
    GstPad* video_src_pad = gst_element_get_static_pad(encoder, "src");
    GstPad* audio_src_pad = gst_element_get_static_pad(audio_queue, "src");

    if (gst_pad_link(video_src_pad, video_sink_pad) != GST_PAD_LINK_OK ||
        gst_pad_link(audio_src_pad, audio_sink_pad) != GST_PAD_LINK_OK) {
        std::cerr << "Failed to link to muxer" << std::endl;
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
    if (!gst_element_link(muxer, session.filesink)) {
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

    // Start pipeline
    GstStateChangeReturn ret = gst_element_set_state(session.pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        std::cerr << "Failed to start pipeline" << std::endl;
        return false;
    }

    recordings.emplace(outputPath, std::move(session));
    std::cout << "Successfully started recording pipeline" << std::endl;
    return true;
}