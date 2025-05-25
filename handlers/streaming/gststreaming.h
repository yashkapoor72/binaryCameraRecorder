#ifndef GSTSTREAMING_H
#define GSTSTREAMING_H

#include <gst/gst.h>
#include <string>
#include <map>
#include <mutex>
#include <vector>
#include <utility>
#include <glib-object.h>

class GstStreaming {
public:
    GstStreaming();
    ~GstStreaming();
    
    bool startStreaming(const std::string& channelName,
                      const std::vector<std::pair<double, double>>& points,
                      int output_width,
                      int output_height,
                      const std::string& flip_mode = "none",
                      std::string camIndex = "null",
                      std::string g_audioDevIndex = "null");
    
    bool stopStreaming(const std::string& channelName);
    bool takeScreenshot(const std::string& channelName, const std::string& outputPath);

private:
    struct StreamingSession {
        GstElement* pipeline = nullptr;
        GstElement* webrtc_sink = nullptr;
        GstElement* video_tee = nullptr;
        GstElement* audio_tee = nullptr;
        bool is_active = false;

        StreamingSession() = default;
        ~StreamingSession();

        StreamingSession(const StreamingSession&) = delete;
        StreamingSession& operator=(const StreamingSession&) = delete;

        StreamingSession(StreamingSession&& other) noexcept;
        StreamingSession& operator=(StreamingSession&& other) noexcept;
    };

    std::map<std::string, StreamingSession> streaming_sessions;
    std::mutex session_mutex;
    
    bool createPipeline(const std::string& channelName,
                      const std::vector<std::pair<double, double>>& points,
                      int output_width,
                      int output_height,
                      const std::string& flip_mode,
                      std::string camIndex,
                      std::string audioDevIndex);
};

inline GstStreaming::StreamingSession::~StreamingSession() {
    if (pipeline) {
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline);
        pipeline = nullptr;
    }
}

inline GstStreaming::StreamingSession::StreamingSession(StreamingSession&& other) noexcept 
    : pipeline(other.pipeline),
      webrtc_sink(other.webrtc_sink),
      video_tee(other.video_tee),
      audio_tee(other.audio_tee),
      is_active(other.is_active) {
    other.pipeline = nullptr;
    other.webrtc_sink = nullptr;
    other.video_tee = nullptr;
    other.audio_tee = nullptr;
    other.is_active = false;
}

inline GstStreaming::StreamingSession& GstStreaming::StreamingSession::operator=(StreamingSession&& other) noexcept {
    if (this != &other) {
        if (pipeline) {
            gst_element_set_state(pipeline, GST_STATE_NULL);
            gst_object_unref(pipeline);
        }

        pipeline = other.pipeline;
        webrtc_sink = other.webrtc_sink;
        video_tee = other.video_tee;
        audio_tee = other.audio_tee;
        is_active = other.is_active;
        
        other.pipeline = nullptr;
        other.webrtc_sink = nullptr;
        other.video_tee = nullptr;
        other.audio_tee = nullptr;
        other.is_active = false;
    }
    return *this;
}

#endif // GSTSTREAMING_H