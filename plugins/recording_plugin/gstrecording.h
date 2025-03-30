#ifndef GSTRECORDING_H
#define GSTRECORDING_H
#include <gst/gst.h>
#include <string>
#include <map>
#include <mutex>
class GstRecording {
public:
    GstRecording();
    ~GstRecording();
    bool startRecording(const std::string& outputPath);
    bool stopRecording(const std::string& outputPath);
private:
    struct RecordingSession {
        GstElement* pipeline = nullptr;
        GstElement* filesink = nullptr;
        GstElement* audio_src = nullptr;
        
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
                audio_src = other.audio_src;
                other.pipeline = nullptr;
                other.filesink = nullptr;
                other.audio_src = nullptr;
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
    bool createPipeline(const std::string& outputPath);
};
#endif
