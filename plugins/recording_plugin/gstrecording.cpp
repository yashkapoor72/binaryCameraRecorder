#include "gstrecording.h"
#include <iostream>

GstRecording::GstRecording() { gst_init(nullptr, nullptr); }

GstRecording::~GstRecording() {
    std::lock_guard<std::mutex> lock(mutex);
    recordings.clear();
}

bool GstRecording::startRecording(const std::string& outputPath,
                                int top, int bottom, int left, int right,
                                const std::string& flip_mode) {
    std::lock_guard<std::mutex> lock(mutex);
    if (recordings.count(outputPath)) {
        std::cerr << "Recording already in progress for: " << outputPath << std::endl;
        return false;
    }
    return createPipeline(outputPath, top, bottom, left, right, flip_mode);
}

bool GstRecording::stopRecording(const std::string& outputPath) {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = recordings.find(outputPath);
    if (it == recordings.end()) {
        std::cerr << "No active recording found for: " << outputPath << std::endl;
        return false;
    }
    if (it->second.pipeline) {
        gst_element_send_event(it->second.pipeline, gst_event_new_eos());
        GstBus* bus = gst_element_get_bus(it->second.pipeline);
        if (bus) {
            GstMessage* msg = gst_bus_timed_pop_filtered(bus, GST_CLOCK_TIME_NONE, 
                static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS));
            if (msg) gst_message_unref(msg);
            gst_object_unref(bus);
        }
        recordings.erase(it);
    }
    std::cout << "Stopped recording: " << outputPath << std::endl;
    return true;
}

bool GstRecording::createPipeline(const std::string& outputPath,
                                int top, int bottom, int left, int right,
                                const std::string& flip_mode) {
    const std::unordered_map<std::string, int> flip_methods = {
        {"none", 0}, {"horizontal", 1}, {"vertical", 2}, 
        {"clockwise", 3}, {"counterclockwise", 4}};

    RecordingSession session;
    session.pipeline = gst_pipeline_new("recording-pipeline");
    
    GstElement* video_src = gst_element_factory_make("avfvideosrc", "video_src");
    GstElement* video_convert = gst_element_factory_make("videoconvert", "video_convert");
    session.video_crop = gst_element_factory_make("videocrop", "video_crop");
    session.video_flip = gst_element_factory_make("videoflip", "video_flip");
    GstElement* video_encoder = gst_element_factory_make("x264enc", "video_encoder");
    GstElement* video_queue = gst_element_factory_make("queue", "video_queue");
    
    session.audio_src = gst_element_factory_make("osxaudiosrc", "audio_src");
    GstElement* audio_convert = gst_element_factory_make("audioconvert", "audio_convert");
    GstElement* audio_resample = gst_element_factory_make("audioresample", "audio_resample");
    GstElement* audio_encoder = gst_element_factory_make("avenc_aac", "audio_encoder");
    GstElement* audio_queue = gst_element_factory_make("queue", "audio_queue");
    
    GstElement* muxer = gst_element_factory_make("mp4mux", "muxer");
    session.filesink = gst_element_factory_make("filesink", "filesink");
    
    if (!video_src || !video_convert || !session.video_crop || !session.video_flip || 
        !video_encoder || !session.audio_src || !audio_convert || !audio_resample ||
        !audio_encoder || !muxer || !session.filesink) {
        return false;
    }
    
    g_object_set(video_src, "do-timestamp", TRUE, NULL);
    g_object_set(session.audio_src, "do-timestamp", TRUE, NULL);
    g_object_set(video_encoder, "tune", 0x00000004, "key-int-max", 30, NULL);
    g_object_set(muxer, "streamable", TRUE, "faststart", TRUE, NULL);
    g_object_set(session.filesink, "location", outputPath.c_str(), "sync", FALSE, NULL);
    
    g_object_set(session.video_crop, "top", top, "bottom", bottom, "left", left, "right", right, NULL);
    if (flip_methods.count(flip_mode)) {
        g_object_set(session.video_flip, "method", flip_methods.at(flip_mode), NULL);
    }
    
    gst_bin_add_many(GST_BIN(session.pipeline), 
        video_src, video_convert, session.video_crop, session.video_flip, 
        video_encoder, video_queue, session.audio_src, audio_convert, 
        audio_resample, audio_encoder, audio_queue, muxer, session.filesink, NULL);
    
    if (!gst_element_link_many(video_src, video_convert, session.video_crop, 
                             session.video_flip, video_encoder, video_queue, NULL)) {
        return false;
    }
    
    if (!gst_element_link_many(session.audio_src, audio_convert, audio_resample,
                             audio_encoder, audio_queue, NULL)) {
        return false;
    }
    
    GstPad* video_pad = gst_element_get_static_pad(video_queue, "src");
    GstPad* mux_video_pad = gst_element_request_pad_simple(muxer, "video_%u");
    GstPad* audio_pad = gst_element_get_static_pad(audio_queue, "src");
    GstPad* mux_audio_pad = gst_element_request_pad_simple(muxer, "audio_%u");
    
    if (gst_pad_link(video_pad, mux_video_pad) != GST_PAD_LINK_OK ||
        gst_pad_link(audio_pad, mux_audio_pad) != GST_PAD_LINK_OK) {
        return false;
    }
    
    gst_object_unref(video_pad);
    gst_object_unref(mux_video_pad);
    gst_object_unref(audio_pad);
    gst_object_unref(mux_audio_pad);
    
    if (!gst_element_link(muxer, session.filesink)) {
        return false;
    }
    
    GstStateChangeReturn ret = gst_element_set_state(session.pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        return false;
    }
    
    recordings[outputPath] = std::move(session);
    return true;
}
