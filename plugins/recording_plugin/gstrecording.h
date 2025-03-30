#ifndef GSTRECORDING_H
#define GSTRECORDING_H
#include <gst/gst.h>
#include <string>
#include <map>
#include <mutex>
#include <unordered_map>
class GstRecording {
public:
    GstRecording();
    ~GstRecording();
    bool startRecording(const std::string& outputPath,
                      int top = 0, int bottom = 0, int left = 0, int right = 0,
                      const std::string& flip_mode = "none");
    bool stopRecording(const std::string& outputPath);
private:
    struct RecordingSession {
        GstElement* pipeline = nullptr;
        GstElement* filesink = nullptr;
        GstElement* video_crop = nullptr;
        GstElement* video_flip = nullptr;
        GstElement* audio_src = nullptr;
        GstElement* audio_convert = nullptr;
        GstElement* audio_encoder = nullptr;
        
        RecordingSession() = default;
        RecordingSession(const RecordingSession&) = delete;
        RecordingSession& operator=(const RecordingSession&) = delete;
        
        RecordingSession(RecordingSession&& other) noexcept {
            *this = std::move(other);
        }
        
        RecordingSession& operator=(RecordingSession&& other) noexcept {
            if (this != &other) {
                pipeline = other.pipeline;
                filesink = other.filesink;
                video_crop = other.video_crop;
                video_flip = other.video_flip;
                audio_src = other.audio_src;
                audio_convert = other.audio_convert;
                audio_encoder = other.audio_encoder;
                other.pipeline = nullptr;
                other.filesink = nullptr;
                other.video_crop = nullptr;
                other.video_flip = nullptr;
                other.audio_src = nullptr;
                other.audio_convert = nullptr;
                other.audio_encoder = nullptr;
            }
            return *this;
        }
        
        ~RecordingSession() {
            if (pipeline) {
                gst_element_set_state(pipeline, GST_STATE_NULL);
                gst_object_unref(pipeline);
            }
        }
    };
    std::map<std::string, RecordingSession> recordings;
    std::mutex mutex;
    bool createPipeline(const std::string& outputPath,
                      int top, int bottom, int left, int right,
                      const std::string& flip_mode);
};
#endif
