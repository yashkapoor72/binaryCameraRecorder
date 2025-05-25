#include "gststreaming.h"
#include <iostream>
#include <opencv2/opencv.hpp>
#include <glib.h>
#include <unordered_map>


GstStreaming::GstStreaming() {
    gst_init(nullptr, nullptr);
}

GstStreaming::~GstStreaming() {
    std::lock_guard<std::mutex> lock(session_mutex);
    for (auto& [channel, session] : streaming_sessions) {
        if (session.pipeline) {
            gst_element_set_state(session.pipeline, GST_STATE_NULL);
            gst_object_unref(session.pipeline);
        }
    }
    streaming_sessions.clear();
}

bool GstStreaming::startStreaming(const std::string& channelName,
                                const std::vector<std::pair<double, double>>& points,
                                int output_width,
                                int output_height,
                                const std::string& flip_mode,
                                std::string camIndex,
                                std::string g_audioDevIndex) {
    std::lock_guard<std::mutex> lock(session_mutex);
    if (streaming_sessions.count(channelName)) {
        std::cerr << "Streaming already in progress for channel: " << channelName << std::endl;
        return false;
    }
    return createPipeline(channelName, points, output_width, output_height, 
                         flip_mode, camIndex, g_audioDevIndex);
}

bool GstStreaming::stopStreaming(const std::string& channelName) {
    std::lock_guard<std::mutex> lock(session_mutex);
    auto it = streaming_sessions.find(channelName);
    if (it == streaming_sessions.end()) {
        std::cerr << "No active streaming found for channel: " << channelName << std::endl;
        return false;
    }
    
    StreamingSession& session = it->second;
    if (!session.pipeline || !GST_IS_ELEMENT(session.pipeline)) {
        std::cerr << "Invalid pipeline for channel: " << channelName << std::endl;
        streaming_sessions.erase(it);
        return false;
    }

    // Send EOS
    if (!gst_element_send_event(session.pipeline, gst_event_new_eos())) {
        std::cerr << "Failed to send EOS event for channel: " << channelName << std::endl;
    }

    // Wait for EOS
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
                std::cerr << "Error while stopping channel " << channelName << ": " 
                          << err->message << std::endl;
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
    g_usleep(500000); // 500ms delay
    
    // Remove from map
    streaming_sessions.erase(it);
    std::cout << "Successfully stopped streaming for channel: " << channelName << std::endl;
    return true;
}

bool GstStreaming::takeScreenshot(const std::string& channelName, const std::string& outputPath) {
    std::lock_guard<std::mutex> lock(session_mutex);
    auto it = streaming_sessions.find(channelName);
    if (it == streaming_sessions.end()) {
        std::cerr << "No active streaming found for channel: " << channelName << std::endl;
        return false;
    }

    StreamingSession& session = it->second;
    if (!session.pipeline || !GST_IS_ELEMENT(session.pipeline)) {
        std::cerr << "Invalid pipeline for channel: " << channelName << std::endl;
        return false;
    }

    // Create screenshot elements
    GstElement* queue_screenshot = gst_element_factory_make("queue", "queue_screenshot");
    GstElement* jpegenc = gst_element_factory_make("jpegenc", "jpegenc");
    GstElement* filesink = gst_element_factory_make("filesink", "screenshot_filesink");
    
    if (!queue_screenshot || !jpegenc || !filesink) {
        std::cerr << "Failed to create screenshot elements" << std::endl;
        if (queue_screenshot) gst_object_unref(queue_screenshot);
        if (jpegenc) gst_object_unref(jpegenc);
        if (filesink) gst_object_unref(filesink);
        return false;
    }

    // Configure filesink
    g_object_set(filesink, 
        "location", outputPath.c_str(),
        "sync", FALSE,
        NULL);

    // Configure jpegenc
    g_object_set(jpegenc,
        "quality", 85,
        NULL);

    // Add elements to pipeline
    gst_bin_add_many(GST_BIN(session.pipeline), 
        queue_screenshot, jpegenc, filesink, 
        NULL);

    // Link elements
    if (!gst_element_link_many(queue_screenshot, jpegenc, filesink, NULL)) {
        std::cerr << "Failed to link screenshot elements" << std::endl;
        gst_bin_remove_many(GST_BIN(session.pipeline), 
            queue_screenshot, jpegenc, filesink, 
            NULL);
        return false;
    }

    // Get the video tee element
    GstElement* video_tee = session.video_tee;
    if (!video_tee) {
        std::cerr << "Failed to find video tee element" << std::endl;
        gst_bin_remove_many(GST_BIN(session.pipeline), 
            queue_screenshot, jpegenc, filesink, 
            NULL);
        return false;
    }

    // Request a new src pad from the tee
    GstPad* tee_src_pad = gst_element_get_request_pad(video_tee, "src_%u");
    GstPad* queue_sink_pad = gst_element_get_static_pad(queue_screenshot, "sink");
    
    if (!tee_src_pad || !queue_sink_pad) {
        std::cerr << "Failed to get pads for screenshot branch" << std::endl;
        if (tee_src_pad) gst_object_unref(tee_src_pad);
        if (queue_sink_pad) gst_object_unref(queue_sink_pad);
        gst_bin_remove_many(GST_BIN(session.pipeline), 
            queue_screenshot, jpegenc, filesink, 
            NULL);
        return false;
    }

    // Link tee to queue
    if (gst_pad_link(tee_src_pad, queue_sink_pad) != GST_PAD_LINK_OK) {
        std::cerr << "Failed to link tee to screenshot branch" << std::endl;
        gst_object_unref(tee_src_pad);
        gst_object_unref(queue_sink_pad);
        gst_bin_remove_many(GST_BIN(session.pipeline), 
            queue_screenshot, jpegenc, filesink, 
            NULL);
        return false;
    }

    // Set up a probe to remove the screenshot branch after capturing one frame
    GstPad* jpegenc_sink_pad = gst_element_get_static_pad(jpegenc, "sink");
    if (jpegenc_sink_pad) {
        gulong probe_id = gst_pad_add_probe(jpegenc_sink_pad, 
            GST_PAD_PROBE_TYPE_BUFFER,
            [](GstPad* pad, GstPadProbeInfo* info, gpointer user_data) -> GstPadProbeReturn {
                GstElement* pipeline = static_cast<GstElement*>(user_data);
                
                gst_element_post_message(pipeline,
                    gst_message_new_application(GST_OBJECT(pipeline),
                        gst_structure_new("remove-screenshot-elements",
                            "queue", G_TYPE_STRING, "queue_screenshot",
                            "jpegenc", G_TYPE_STRING, "jpegenc",
                            "filesink", G_TYPE_STRING, "screenshot_filesink",
                            NULL)));
                
                return GST_PAD_PROBE_REMOVE;
            }, session.pipeline, NULL);
        
        gst_object_unref(jpegenc_sink_pad);
    }

    // Sync states
    gst_element_sync_state_with_parent(queue_screenshot);
    gst_element_sync_state_with_parent(jpegenc);
    gst_element_sync_state_with_parent(filesink);

    // Clean up references
    gst_object_unref(tee_src_pad);
    gst_object_unref(queue_sink_pad);

    return true;
}

bool GstStreaming::createPipeline(const std::string& channelName,
                                const std::vector<std::pair<double, double>>& points,
                                int output_width,
                                int output_height,
                                const std::string& flip_mode,
                                std::string camIndex,
                                std::string g_audioDevIndex) {
    if (points.size() != 4) {
        std::cerr << "Need exactly 4 points for perspective transform" << std::endl;
        return false;
    }

    if (output_width == -1 || output_height == -1) {
        output_width = 1280;
        output_height = 720;
        std::cout << "Using default resolution: 1280x720" << std::endl;
    }

    const std::unordered_map<std::string, int> flip_methods = {
        {"none", 0}, {"horizontal", 1}, {"vertical", 2}, 
        {"clockwise", 3}, {"counterclockwise", 4}};

    if (!flip_methods.count(flip_mode)) {
        std::cerr << "Invalid flip mode: " << flip_mode << std::endl;
        return false;
    }

    // Create pipeline and elements
    StreamingSession session;
    session.pipeline = gst_pipeline_new(("streaming-pipeline-" + channelName).c_str());
    if (!session.pipeline) {
        std::cerr << "Failed to create pipeline" << std::endl;
        return false;
    }

    // Video elements
    GstElement* src = gst_element_factory_make("avfvideosrc", "source");
    GstElement* capsfilter = gst_element_factory_make("capsfilter", "capsfilter");
    GstElement* convert1 = gst_element_factory_make("videoconvert", "convert1");
    GstElement* perspective = gst_element_factory_make("perspective", "perspective");
    GstElement* flip = gst_element_factory_make("videoflip", "flipper");
    GstElement* convert2 = gst_element_factory_make("videoconvert", "convert2");
    GstElement* videoscale = gst_element_factory_make("videoscale", "scaler");
    GstElement* capsink = gst_element_factory_make("capsfilter", "capsink");
    session.video_tee = gst_element_factory_make("tee", "video_tee");
    GstElement* video_queue = gst_element_factory_make("queue", "video_queue");
    
    // Video encoding elements
    GstElement* video_encoder = gst_element_factory_make("x264enc", "video_encoder");
    GstElement* h264parse = gst_element_factory_make("h264parse", "h264parse");
    
    // Audio elements
    GstElement* audio_src = gst_element_factory_make("osxaudiosrc", "audio_src");
    GstElement* audio_convert = gst_element_factory_make("audioconvert", "audio_convert");
    GstElement* audio_resample = gst_element_factory_make("audioresample", "audio_resample");
    GstElement* audio_encoder = gst_element_factory_make("avenc_aac", "audio_encoder");
    session.audio_tee = gst_element_factory_make("tee", "audio_tee");
    GstElement* audio_queue = gst_element_factory_make("queue", "audio_queue");

    // Create AWS KVS WebRTC sink
    std::string sink_str = "awskvswebrtcsink name=webrtc_sink "
                          "signaller::channel-name=\"" + channelName + "\" "
                          "do-retransmission=true do-fec=true "
                          "congestion-control=2";

    GError* error = nullptr;
    session.webrtc_sink = gst_parse_bin_from_description(sink_str.c_str(), TRUE, &error);
    if (error) {
        std::cerr << "Failed to create WebRTC sink: " << error->message << std::endl;
        g_error_free(error);
        return false;
    }

    // Verify all elements were created
    if (!src || !capsfilter || !convert1 || !videoscale || !perspective || !flip || 
        !convert2 || !capsink || !session.video_tee || !video_queue || !video_encoder || 
        !h264parse || !audio_src || !audio_convert || !audio_resample || !audio_encoder || 
        !session.audio_tee || !audio_queue || !session.webrtc_sink) {
        std::cerr << "Failed to create one or more GStreamer elements" << std::endl;
        return false;
    }

    // Configure source
    g_object_set(src, 
        "do-timestamp", TRUE, 
        "device-unique-id", camIndex.c_str(),
        "capture-screen", FALSE,
        NULL);

    // Configure video caps
    GstCaps* caps = gst_caps_new_simple("video/x-raw",
        "format", G_TYPE_STRING, "NV12",
        "width", G_TYPE_INT, 1280,
        "height", G_TYPE_INT, 720,
        "framerate", GST_TYPE_FRACTION, 30, 1,
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

    // Configure output caps
    GstCaps* out_caps = gst_caps_new_simple("video/x-raw",
        "format", G_TYPE_STRING, "I420",
        "width", G_TYPE_INT, output_width,
        "height", G_TYPE_INT, output_height,
        "framerate", GST_TYPE_FRACTION, 30, 1,
        NULL);
    g_object_set(capsink, "caps", out_caps, NULL);
    gst_caps_unref(out_caps);

    // Configure video encoder
    g_object_set(video_encoder,
        "bitrate", 2000,         // 2000 kbps
        "speed-preset", 1,       // ultrafast
        "tune", 4,               // zerolatency
        "key-int-max", 30,       // keyframe interval
        "threads", 4,            // encoding threads
        NULL);

    // Configure audio
    g_object_set(audio_src, "unique-id", g_audioDevIndex.c_str(), NULL);
    g_object_set(audio_encoder, "bitrate", 128000, NULL);

    // Build the pipeline
    gst_bin_add_many(GST_BIN(session.pipeline),
        src, capsfilter, convert1, perspective,
        flip, convert2, videoscale, capsink, session.video_tee,
        video_queue, video_encoder, h264parse,
        audio_src, audio_convert, audio_resample, audio_encoder, session.audio_tee, audio_queue,
        session.webrtc_sink,
        NULL);

    // Link video pipeline
    if (!gst_element_link_many(
        src, capsfilter, convert1, perspective,
        flip, convert2, videoscale, capsink, session.video_tee, NULL)) {
        std::cerr << "Failed to link video elements before tee" << std::endl;
        return false;
    }

    // Link video_tee → video_queue → x264enc → h264parse → awskvswebrtcsink
    if (!gst_element_link_many(
        video_queue, video_encoder, h264parse, session.webrtc_sink, NULL)) {
        std::cerr << "Failed to link video encoding pipeline" << std::endl;
        return false;
    }

    // Link audio pipeline
    if (!gst_element_link_many(
        audio_src, audio_convert, audio_resample, audio_encoder, session.audio_tee, audio_queue, NULL) ||
        !gst_element_link(audio_queue, session.webrtc_sink)) {
        std::cerr << "Failed to link audio elements" << std::endl;
        return false;
    }

    // Set up bus monitoring
    GstBus* bus = gst_element_get_bus(session.pipeline);
    gst_bus_add_watch(bus, [](GstBus* bus, GstMessage* msg, gpointer user_data) -> gboolean {
        GstStreaming* self = static_cast<GstStreaming*>(user_data);
        
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
                GstState old_state, new_state, pending;
                gst_message_parse_state_changed(msg, &old_state, &new_state, &pending);
                std::cout << "Pipeline state changed from " 
                          << gst_element_state_get_name(old_state) 
                          << " to " << gst_element_state_get_name(new_state) 
                          << std::endl;
                break;
            }
            case GST_MESSAGE_WARNING: {
                GError* err = nullptr;
                gchar* debug = nullptr;
                gst_message_parse_warning(msg, &err, &debug);
                std::cerr << "Warning: " << err->message << std::endl;
                if (debug) std::cerr << "Debug info: " << debug << std::endl;
                g_error_free(err);
                g_free(debug);
                break;
            }
            default:
                break;
        }
        return TRUE;
    }, this);
    gst_object_unref(bus);

    // Generate pipeline diagram for debugging
    GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS(GST_BIN(session.pipeline), 
        GST_DEBUG_GRAPH_SHOW_ALL, ("pipeline-" + channelName).c_str());

    // Start pipeline
    GstStateChangeReturn ret = gst_element_set_state(session.pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        std::cerr << "Failed to start pipeline" << std::endl;
        return false;
    }

    session.is_active = true;
    streaming_sessions.emplace(channelName, std::move(session));
    std::cout << "Successfully started streaming to channel: " << channelName << std::endl;
    return true;
}