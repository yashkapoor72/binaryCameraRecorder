#ifndef GSTRECORDING_H
#define GSTRECORDING_H

#include <gst/gst.h>
#include <string>
#include <map>
#include <mutex>
#include <vector>
#include <utility> // for std::pair

class GstRecording {
public:
    GstRecording();
    ~GstRecording();
    
    bool startRecording(const std::string& outputPath,
                      const std::vector<std::pair<double, double>>& points,
                      const std::string& flip_mode = "none");
    
    bool stopRecording(const std::string& outputPath);

private:
struct RecordingSession {
    GstElement* pipeline = nullptr;
    GstElement* filesink = nullptr;
    
    RecordingSession() = default;

    // Add copy constructor and assignment operator
    RecordingSession(const RecordingSession&) = delete;
    RecordingSession& operator=(const RecordingSession&) = delete;
    
    // Move constructor
    RecordingSession(RecordingSession&& other) noexcept 
        : pipeline(other.pipeline), filesink(other.filesink) {
        other.pipeline = nullptr;
        other.filesink = nullptr;
    }
    
    // Move assignment
    RecordingSession& operator=(RecordingSession&& other) noexcept {
        if (this != &other) {
            if (pipeline) {
                gst_element_set_state(pipeline, GST_STATE_NULL);
                gst_object_unref(pipeline);
            }
            pipeline = other.pipeline;
            filesink = other.filesink;
            other.pipeline = nullptr;
            other.filesink = nullptr;
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
                      const std::vector<std::pair<double, double>>& points,
                      const std::string& flip_mode);
};

#endif // GSTRECORDING_H