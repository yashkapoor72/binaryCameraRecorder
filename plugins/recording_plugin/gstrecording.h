#ifndef GSTRECORDING_H
#define GSTRECORDING_H

#include <gst/gst.h>
#include <string>
#include <map>
#include <mutex>
#include <vector>
#include <utility> // for std::pair
#include <glib-object.h>
#include <filesystem>
#include <sys/wait.h>

class GstRecording {
public:
    GstRecording();
    ~GstRecording();
    
    bool startRecording(const std::string& outputPath,
                      const std::vector<std::pair<double, double>>& points,
                      int output_width,
                      int output_height,
                      const std::string& flip_mode = "none",
                      std::string g_camDevIndex = "null", std::string g_audioDevIndex = "null");
    
    bool stopRecording(const std::string& outputPath, int output_width, int output_height);

    bool takeScreenshot(const std::string& outputPath);  // New method

private:
struct RecordingSession {
    GstElement* pipeline = nullptr;
    GstElement* filesink = nullptr;
    GstElement* tee = nullptr;  // Added for screenshot functionality
    
    RecordingSession() = default;

    // Add copy constructor and assignment operator
    RecordingSession(const RecordingSession&) = delete;
    RecordingSession& operator=(const RecordingSession&) = delete;
    
    // Move constructor
    RecordingSession(RecordingSession&& other) noexcept 
        : pipeline(other.pipeline), filesink(other.filesink), tee(other.tee) {
        other.pipeline = nullptr;
        other.filesink = nullptr;
        other.tee = nullptr;
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
            tee = other.tee;
            other.pipeline = nullptr;
            other.filesink = nullptr;
            other.tee = nullptr;
        }
        return *this;
    }
    
    ~RecordingSession() {
        if (pipeline) {
            gst_element_set_state(pipeline, GST_STATE_NULL);
            gst_object_unref(pipeline);
        }
        // tee is part of pipeline and will be automatically unreffed
    }
};
    
    std::map<std::string, RecordingSession> recordings;
    std::mutex mutex;
    
    bool createPipeline(const std::string& outputPath,
                      const std::vector<std::pair<double, double>>& points,
                      int output_width,
                      int output_height,
                      const std::string& flip_mode,
                      std::string g_camDevIndex = "null", std::string g_audioDevIndex = "null");
};

#endif // GSTRECORDING_H