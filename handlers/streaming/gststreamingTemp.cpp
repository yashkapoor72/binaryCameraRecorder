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
                                std::string audioDevIndex) {
    std::lock_guard<std::mutex> lock(session_mutex);
    if (streaming_sessions.count(channelName)) {
        std::cerr << "Streaming already in progress for channel: " << channelName << std::endl;
        return false;
    }
    return createPipeline(channelName, points, output_width, output_height, 
                         flip_mode, camIndex, audioDevIndex);
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
                                std::string audioDevIndex) {
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

    StreamingSession session;
    session.pipeline = gst_pipeline_new(("streaming-pipeline-" + channelName).c_str());
    if (!session.pipeline) {
        std::cerr << "Failed to create pipeline" << std::endl;
        return false;
    }

    // Create WebRTC sink first to configure properties in NULL state
    session.webrtc_sink = gst_element_factory_make("awskvswebrtcsink", "webrtc_sink");
    if (!session.webrtc_sink) {
        std::cerr << "Failed to create WebRTC sink" << std::endl;
        return false;
    }

    // Configure WebRTC sink properties - must be done in NULL state
    GObject* signaller = g_object_new(GST_TYPE_AWS_KVS_WEBRTC_SIGNALLER, NULL);
    if (!signaller) {
        std::cerr << "Failed to create WebRTC signaller" << std::endl;
        return false;
    }

    
    g_object_set(signaller,
        "channel-name", channelName.c_str(),
        NULL);

    g_object_set(session.webrtc_sink,
        "signaller", signaller,
        "do-retransmission", TRUE,
        "do-fec", TRUE,
        "video-caps", "video/x-h264",
        "congestion-control", 2,  // GCC
        NULL);

    // Create remaining elements
    GstElement* src = gst_element_factory_make("avfvideosrc", "source");
    GstElement* capsfilter = gst_element_factory_make("capsfilter", "capsfilter");
    GstElement* convert1 = gst_element_factory_make("videoconvert", "convert1");
    GstElement* perspective = gst_element_factory_make("perspective", "perspective");
    GstElement* flip = gst_element_factory_make("videoflip", "flipper");
    GstElement* convert2 = gst_element_factory_make("videoconvert", "convert2");
    GstElement* videoscale = gst_element_factory_make("videoscale", "scaler");
    GstElement* capsink = gst_element_factory_make("capsfilter", "capsink");
    session.video_tee = gst_element_factory_make("tee", "video_tee");
    session.audio_tee = gst_element_factory_make("tee", "audio_tee");
    GstElement* video_queue = gst_element_factory_make("queue", "video_queue");
    GstElement* audio_queue = gst_element_factory_make("queue", "audio_queue");
    GstElement* audio_src = gst_element_factory_make("osxaudiosrc", "audio_src");
    GstElement* audio_convert = gst_element_factory_make("audioconvert", "audio_convert");
    GstElement* audio_resample = gst_element_factory_make("audioresample", "audio_resample");
    GstElement* audio_encoder = gst_element_factory_make("avenc_aac", "audio_encoder");

    if (!src || !capsfilter || !convert1 || !videoscale || !perspective || !flip || 
        !convert2 || !capsink || !session.video_tee || !session.audio_tee || !video_queue || 
        !audio_queue || !audio_src || !audio_convert || !audio_resample || !audio_encoder) {
        std::cerr << "Failed to create one or more GStreamer elements" << std::endl;
        gst_object_unref(signaller);
        return false;
    }

    // Configure source
    g_object_set(src, 
        "do-timestamp", TRUE, 
        "device-unique-id", camIndex.c_str(),
        "capture-screen", FALSE,
        NULL);

    // Configure caps
    GstCaps* caps = gst_caps_new_simple("video/x-raw",
        "format", G_TYPE_STRING, "NV12",
        "width", G_TYPE_INT, 1280,
        "height", G_TYPE_INT, 720,
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

    GstCaps* out_caps = gst_caps_new_simple("video/x-raw",
        "format", G_TYPE_STRING, "I420",
        "width", G_TYPE_INT, output_width,
        "height", G_TYPE_INT, output_height,
        "pixel-aspect-ratio", GST_TYPE_FRACTION, 1, 1,
        "framerate", GST_TYPE_FRACTION_RANGE, 15, 1, 60, 1,
        NULL);
    g_object_set(capsink, "caps", out_caps, NULL);
    gst_caps_unref(out_caps);

    g_object_set(audio_src, "unique-id", audioDevIndex.c_str(), NULL);
    g_object_set(audio_encoder, "bitrate", 128000, NULL);

    // Build the pipeline
    gst_bin_add_many(GST_BIN(session.pipeline),
        src, capsfilter, convert1, perspective,
        flip, convert2, videoscale, capsink, session.video_tee,
        video_queue, session.webrtc_sink,
        audio_src, audio_convert, audio_resample, audio_encoder, session.audio_tee, audio_queue,
        NULL);

    // Link video elements
    if (!gst_element_link_many(
        src, capsfilter, convert1, perspective,
        flip, convert2, videoscale, capsink, session.video_tee, video_queue, session.webrtc_sink, NULL)) {
        std::cerr << "Failed to link video elements" << std::endl;
        gst_object_unref(signaller);
        return false;
    }

    // Link audio elements
    if (!gst_element_link_many(
        audio_src, audio_convert, audio_resample, audio_encoder, session.audio_tee, audio_queue, session.webrtc_sink, NULL)) {
        std::cerr << "Failed to link audio elements" << std::endl;
        gst_object_unref(signaller);
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
                GstElement* pipeline = nullptr;
                gst_message_parse_state_changed(msg, nullptr, nullptr, nullptr);
                pipeline = GST_ELEMENT(GST_MESSAGE_SRC(msg));
                
                if (pipeline && GST_IS_PIPELINE(pipeline)) {
                    GstState old_state, new_state, pending;
                    gst_message_parse_state_changed(msg, &old_state, &new_state, &pending);
                    std::cout << "Pipeline state changed from " 
                              << gst_element_state_get_name(old_state) 
                              << " to " << gst_element_state_get_name(new_state) 
                              << std::endl;
                }
                break;
            }
            case GST_MESSAGE_APPLICATION: {
                const GstStructure* structure = gst_message_get_structure(msg);
                if (gst_structure_has_name(structure, "remove-screenshot-elements")) {
                    std::lock_guard<std::mutex> lock(self->session_mutex);
                    
                    // Find which session this belongs to
                    for (auto& [channel, session] : self->streaming_sessions) {
                        if (session.pipeline == GST_ELEMENT(GST_MESSAGE_SRC(msg))) {
                            const gchar* queue_name = gst_structure_get_string(structure, "queue");
                            const gchar* jpegenc_name = gst_structure_get_string(structure, "jpegenc");
                            const gchar* filesink_name = gst_structure_get_string(structure, "filesink");
                            
                            GstElement* queue = gst_bin_get_by_name(GST_BIN(session.pipeline), queue_name);
                            GstElement* jpegenc = gst_bin_get_by_name(GST_BIN(session.pipeline), jpegenc_name);
                            GstElement* filesink = gst_bin_get_by_name(GST_BIN(session.pipeline), filesink_name);
                            
                            if (queue && jpegenc && filesink) {
                                gst_element_set_state(queue, GST_STATE_NULL);
                                gst_element_set_state(jpegenc, GST_STATE_NULL);
                                gst_element_set_state(filesink, GST_STATE_NULL);
                                
                                // Get the tee and release the pad
                                GstElement* video_tee = session.video_tee;
                                if (video_tee) {
                                    GstPad* queue_sink_pad = gst_element_get_static_pad(queue, "sink");
                                    if (queue_sink_pad) {
                                        GstPad* tee_src_pad = gst_pad_get_peer(queue_sink_pad);
                                        if (tee_src_pad) {
                                            gst_pad_unlink(tee_src_pad, queue_sink_pad);
                                            gst_element_release_request_pad(video_tee, tee_src_pad);
                                            gst_object_unref(tee_src_pad);
                                        }
                                        gst_object_unref(queue_sink_pad);
                                    }
                                }
                                
                                gst_bin_remove_many(GST_BIN(session.pipeline), queue, jpegenc, filesink, NULL);
                                gst_object_unref(queue);
                                gst_object_unref(jpegenc);
                                gst_object_unref(filesink);
                            }
                            break;
                        }
                    }
                }
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
        GST_DEBUG_GRAPH_SHOW_ALL, ("streaming_pipeline_" + channelName).c_str());

    // Start pipeline
    GstStateChangeReturn ret = gst_element_set_state(session.pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        std::cerr << "Failed to start pipeline" << std::endl;
        gst_object_unref(signaller);
        return false;
    }

    // Clean up signaller reference
    gst_object_unref(signaller);

    session.is_active = true;
    streaming_sessions.emplace(channelName, std::move(session));
    std::cout << "Started streaming to channel: " << channelName << std::endl;
    return true;
}