#include "gstrecording.h"
#include <iostream>
#include <cmath> // for fabs
#include <opencv2/opencv.hpp>
#include <opencv2/imgproc.hpp>

// RecordingSession move constructor
GstRecording::RecordingSession::RecordingSession(RecordingSession&& other) noexcept
    : pipeline(other.pipeline),
      filesink(other.filesink),
      perspective(other.perspective),
      video_flip(other.video_flip),
      audio_src(other.audio_src),
      audio_convert(other.audio_convert),
      audio_encoder(other.audio_encoder) {
    other.pipeline = nullptr;
    other.filesink = nullptr;
    other.perspective = nullptr;
    other.video_flip = nullptr;
    other.audio_src = nullptr;
    other.audio_convert = nullptr;
    other.audio_encoder = nullptr;
}

// RecordingSession move assignment
GstRecording::RecordingSession& GstRecording::RecordingSession::operator=(RecordingSession&& other) noexcept {
    if (this != &other) {
        pipeline = other.pipeline;
        filesink = other.filesink;
        perspective = other.perspective;
        video_flip = other.video_flip;
        audio_src = other.audio_src;
        audio_convert = other.audio_convert;
        audio_encoder = other.audio_encoder;
        
        other.pipeline = nullptr;
        other.filesink = nullptr;
        other.perspective = nullptr;
        other.video_flip = nullptr;
        other.audio_src = nullptr;
        other.audio_convert = nullptr;
        other.audio_encoder = nullptr;
    }
    return *this;
}

// RecordingSession destructor
GstRecording::RecordingSession::~RecordingSession() {
    if (pipeline) {
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline);
    }
}

GstRecording::GstRecording() { 
    gst_init(nullptr, nullptr); 
}

GstRecording::~GstRecording() {
    std::lock_guard<std::mutex> lock(mutex);
    recordings.clear();
}

void GstRecording::print_element_properties(GstElement *element) {
    if (!element) return;
    
    GParamSpec **properties;
    guint count;
    GObjectClass *klass = G_OBJECT_GET_CLASS(element);
    properties = g_object_class_list_properties(klass, &count);
    
    std::cout << "Properties for " << GST_ELEMENT_NAME(element) << ":\n";
    for (guint i = 0; i < count; i++) {
        std::cout << "  " << properties[i]->name << " (" 
                 << g_type_name(properties[i]->value_type) << ")\n";
    }
    g_free(properties);
}

void GstRecording::calculate_perspective_matrix(
    const std::vector<std::pair<double, double>>& points,
    gdouble matrix[16]) 
{
    // Default identity matrix
    for (int i = 0; i < 16; i++) {
        matrix[i] = (i % 5 == 0) ? 1.0 : 0.0;
    }

    if (points.size() != 4) return;

    // Calculate homography matrix (simplified example)
    matrix[0] = points[1].first - points[0].first;  // a11
    matrix[1] = points[3].first - points[0].first;  // a12
    matrix[3] = points[0].first;                   // a13
    
    matrix[4] = points[1].second - points[0].second; // a21
    matrix[5] = points[3].second - points[0].second; // a22
    matrix[7] = points[0].second;                  // a23
    
    matrix[15] = 1.0;  // a33
}

bool GstRecording::createPipeline(const std::string& outputPath,
    const std::vector<std::pair<double, double>>& points,
    const std::string& flip_mode) {
    RecordingSession session;

    // Create pipeline elements
    session.pipeline = gst_pipeline_new("recording-pipeline");
    GstElement* video_src = gst_element_factory_make("avfvideosrc", "video_src");
    GstElement* video_convert = gst_element_factory_make("videoconvert", "video_convert");
    session.perspective = gst_element_factory_make("perspective", "perspective");
    if (!session.perspective) {
        g_print("Failed to create perspective element. Ensure you have gst-plugins-bad installed.\n");
        g_print("Available plugins:\n");
        GList *features = gst_registry_get_feature_list(
            gst_registry_get(), GST_TYPE_ELEMENT_FACTORY);
        for (GList *item = features; item; item = item->next) {
            GstPluginFeature *feature = GST_PLUGIN_FEATURE(item->data);
            g_print(" - %s\n", gst_plugin_feature_get_name(feature));
        }
        gst_plugin_feature_list_free(features);
        return false;
    }
    session.video_flip = gst_element_factory_make("videoflip", "video_flip");
    GstElement* video_encoder = gst_element_factory_make("x264enc", "video_encoder");
    GstElement* video_queue = gst_element_factory_make("queue", "video_queue");

    session.audio_src = gst_element_factory_make("osxaudiosrc", "audio_src");
    session.audio_convert = gst_element_factory_make("audioconvert", "audio_convert");
    GstElement* audio_encoder = gst_element_factory_make("avenc_aac", "audio_encoder");
    GstElement* audio_queue = gst_element_factory_make("queue", "audio_queue");

    GstElement* muxer = gst_element_factory_make("mp4mux", "muxer");
    session.filesink = gst_element_factory_make("filesink", "filesink");

    // Check element creation
    if (!video_src || !video_convert || !session.perspective || !session.video_flip || 
    !video_encoder || !session.audio_src || !session.audio_convert || 
    !audio_encoder || !muxer || !session.filesink) {
        std::cerr << "Failed to create elements. Missing elements:\n";
        if (!video_src) std::cerr << "- avfvideosrc\n";
        if (!video_convert) std::cerr << "- videoconvert\n";
        if (!session.perspective) std::cerr << "- opencvperspective (is plugin registered?)\n";
        if (!session.video_flip) std::cerr << "- videoflip\n";
        if (!video_encoder) std::cerr << "- x264enc\n";
        if (!session.audio_src) std::cerr << "- osxaudiosrc\n";
        if (!session.audio_convert) std::cerr << "- audioconvert\n";
        if (!audio_encoder) std::cerr << "- avenc_aac\n";
        if (!muxer) std::cerr << "- mp4mux\n";
        if (!session.filesink) std::cerr << "- filesink\n";
        return false;
    }

    // Configure elements
    g_object_set(video_src, "do-timestamp", TRUE, "device-index", 0, "capture-screen", FALSE,NULL);
    g_object_set(session.audio_src, "do-timestamp", TRUE, NULL);
    g_object_set(video_encoder, "tune", 0x00000004, "key-int-max", 30, "bitrate", 2000, NULL);
    g_object_set(audio_encoder, "bitrate", 128000, NULL);
    g_object_set(muxer, "streamable", TRUE, "faststart", TRUE,NULL);
    g_object_set(session.filesink, "location", outputPath.c_str(), "sync", FALSE, NULL);

    // Set fixed resolution caps
    GstCaps* caps = gst_caps_new_simple("video/x-raw",
            "width", G_TYPE_INT, 1280,
            "height", G_TYPE_INT, 720,
            "framerate", GST_TYPE_FRACTION, 30, 1,
            "format", G_TYPE_STRING, "NV12",
            NULL);
    GstElement* capsfilter = gst_element_factory_make("capsfilter", "capsfilter");
    g_object_set(capsfilter, "caps", caps, NULL);
    gst_caps_unref(caps);

    if (points.size() == 4) {
        GValueArray *matrix_array = g_value_array_new(9);
        for (int i = 0; i < 9; i++) {
            GValue val = G_VALUE_INIT;
            g_value_init(&val, G_TYPE_DOUBLE);
            g_value_set_double(&val, (i % 4 == 0) ? 1.0 : 0.0); // Identity matrix initially
            g_value_array_append(matrix_array, &val);
            g_value_unset(&val);
        }
    
        // Compute proper perspective transform
        std::vector<cv::Point2f> src_points = {
            cv::Point2f(points[0].first, points[0].second),
            cv::Point2f(points[1].first, points[1].second),
            cv::Point2f(points[2].first, points[2].second),
            cv::Point2f(points[3].first, points[3].second)
        };
        
        std::vector<cv::Point2f> dst_points = {
            cv::Point2f(0.0f, 0.0f),
            cv::Point2f(1.0f, 0.0f),
            cv::Point2f(1.0f, 1.0f),
            cv::Point2f(0.0f, 1.0f)
        };
        
        cv::Mat transform = cv::getPerspectiveTransform(src_points, dst_points);
        
        // Update the matrix array
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) {
                GValue *val = g_value_array_get_nth(matrix_array, i*3 + j);
                g_value_set_double(val, transform.at<double>(i,j));
            }
        }
    
        g_object_set(session.perspective, "matrix", matrix_array, NULL);
        g_value_array_free(matrix_array);
    }

    // Configure video flip
    auto flip_it = flip_methods.find(flip_mode);
    if (flip_it != flip_methods.end()) {
        g_object_set(session.video_flip, "method", flip_it->second, NULL);
    }

    // Build pipeline
    gst_bin_add_many(GST_BIN(session.pipeline), 
    video_src, video_convert, capsfilter, session.perspective,
    session.video_flip, video_encoder, video_queue,
    session.audio_src, session.audio_convert, audio_encoder, audio_queue,
    muxer, session.filesink, NULL);

    // Link video elements
    if (!gst_element_link_many(video_src, video_convert, capsfilter, session.perspective,
        session.video_flip, video_encoder, video_queue, NULL)) {
        std::cerr << "Failed to link video elements\n";
        return false;
    }

    // Link audio elements
    if (!gst_element_link_many(session.audio_src, session.audio_convert, audio_encoder,
        audio_queue, NULL)) {
        std::cerr << "Failed to link audio elements\n";
        return false;
    }

    // Link to muxer
    GstPad *video_pad = gst_element_get_static_pad(video_queue, "src");
    GstPad *mux_video_pad = gst_element_request_pad_simple(muxer, "video_%u");
    GstPad *audio_pad = gst_element_get_static_pad(audio_queue, "src");
    GstPad *mux_audio_pad = gst_element_request_pad_simple(muxer, "audio_%u");

    if (!video_pad || !mux_video_pad || !audio_pad || !mux_audio_pad) {
        std::cerr << "Failed to get pads for muxer connection\n";
        if (video_pad) gst_object_unref(video_pad);
        if (mux_video_pad) gst_object_unref(mux_video_pad);
        if (audio_pad) gst_object_unref(audio_pad);
        if (mux_audio_pad) gst_object_unref(mux_audio_pad);
        return false;
    }

    if (gst_pad_link(video_pad, mux_video_pad) != GST_PAD_LINK_OK) {
        std::cerr << "Failed to link video pad to muxer\n";
        gst_object_unref(video_pad);
        gst_object_unref(mux_video_pad);
        gst_object_unref(audio_pad);
        gst_object_unref(mux_audio_pad);
        return false;
    }

    if (gst_pad_link(audio_pad, mux_audio_pad) != GST_PAD_LINK_OK) {
        std::cerr << "Failed to link audio pad to muxer\n";
        gst_object_unref(video_pad);
        gst_object_unref(mux_video_pad);
        gst_object_unref(audio_pad);
        gst_object_unref(mux_audio_pad);
        return false;
    }

    gst_object_unref(video_pad);
    gst_object_unref(mux_video_pad);
    gst_object_unref(audio_pad);
    gst_object_unref(mux_audio_pad);

    // Link muxer to filesink
    if (!gst_element_link(muxer, session.filesink)) {
        std::cerr << "Failed to link muxer to filesink\n";
        return false;
    }

    // Start pipeline
    GstStateChangeReturn ret = gst_element_set_state(session.pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        std::cerr << "Failed to start pipeline\n";
        return false;
    }

    // Add to recordings map
    recordings[outputPath] = std::move(session);
    std::cout << "Started recording: " << outputPath << "\n";
    return true;
}

bool GstRecording::startRecording(const std::string& outputPath,
                                const std::vector<std::pair<double, double>>& points,
                                const std::string& flip_mode) {
    std::lock_guard<std::mutex> lock(mutex);
    if (recordings.count(outputPath)) {
        std::cerr << "Recording already in progress for: " << outputPath << "\n";
        return false;
    }
    return createPipeline(outputPath, points, flip_mode);
}

bool GstRecording::stopRecording(const std::string& outputPath) {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = recordings.find(outputPath);
    if (it == recordings.end()) {
        std::cerr << "No active recording found for: " << outputPath << "\n";
        return false;
    }

    if (it->second.pipeline) {
        gst_element_send_event(it->second.pipeline, gst_event_new_eos());
        GstBus* bus = gst_element_get_bus(it->second.pipeline);
        if (bus) {
            GstMessage* msg = gst_bus_timed_pop_filtered(bus, GST_CLOCK_TIME_NONE, 
                static_cast<GstMessageType>(GST_MESSAGE_EOS|GST_MESSAGE_ERROR));
            if (msg) gst_message_unref(msg);
            gst_object_unref(bus);
        }
    }
    recordings.erase(it);
    std::cout << "Stopped recording: " << outputPath << "\n";
    return true;
}