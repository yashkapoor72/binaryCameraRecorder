#ifndef DESKEW_HANDLER_H
#define DESKEW_HANDLER_H

#include <gst/gst.h>
#include <string>
#include <vector>
#include <utility> // for std::pair
#include <unordered_map>

class DeskewHandler {
public:
    DeskewHandler(int camIndex, int audioIndex);
    ~DeskewHandler();
    bool setupPipeline(int camIndex, int audioIndex);
    void updateSettings(const std::vector<std::pair<double, double>>& points, 
                       const std::string& flip_mode);
    void stopPipeline();
private:
    int cameraIndex;
    int audioIndex;
    GstElement* pipeline = nullptr;
    GstElement* src = nullptr;
    GstElement* convert = nullptr;
    GstElement* perspective = nullptr;  // Changed from crop to perspective
    GstElement* flip = nullptr;
    GstElement* tee = nullptr;
    GstElement* previewQueue = nullptr;
    GstElement* previewSink = nullptr;
    GstElement*capsFilter = nullptr;
    GstElement*scale = nullptr;
    const std::unordered_map<std::string, int> flip_methods = {
        {"none", 0}, {"horizontal", 1}, {"vertical", 2}, 
        {"clockwise", 3}, {"counterclockwise", 4}};
};

#endif