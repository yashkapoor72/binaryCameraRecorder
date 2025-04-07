#ifndef GSTRECORDING_H
#define GSTRECORDING_H

#include <gst/gst.h>
#include <string>
#include <vector>
#include <utility> // for std::pair
#include <map>
#include <mutex>
#include <unordered_map>
#include <iostream> // for std::cerr

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
        GstElement* perspective = nullptr;
        GstElement* video_flip = nullptr;
        GstElement* audio_src = nullptr;
        GstElement* audio_convert = nullptr;
        GstElement* audio_encoder = nullptr;
        
        // Rule of Five
        RecordingSession() = default;
        RecordingSession(const RecordingSession&) = delete;
        RecordingSession& operator=(const RecordingSession&) = delete;
        RecordingSession(RecordingSession&& other) noexcept;
        RecordingSession& operator=(RecordingSession&& other) noexcept;
        ~RecordingSession();
    };

    static void print_element_properties(GstElement *element);
    void calculate_perspective_matrix(const std::vector<std::pair<double, double>>& points, 
                                    gdouble matrix[9]);
    bool createPipeline(const std::string& outputPath,
                      const std::vector<std::pair<double, double>>& points,
                      const std::string& flip_mode);

    std::map<std::string, RecordingSession> recordings;
    std::mutex mutex;
    
    const std::unordered_map<std::string, int> flip_methods = {
        {"none", 0}, {"horizontal", 1}, {"vertical", 2}, 
        {"clockwise", 3}, {"counterclockwise", 4}};
};

#endif