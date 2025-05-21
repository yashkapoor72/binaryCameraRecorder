#include "command_handler.h"
#include "gstrecording.h"
#include "gststreaming.h"

static GstRecording recorder;
static GstStreaming streamer;

bool CommandHandler::startRecording(const std::string& outputPath,
    const std::vector<std::pair<double, double>>& points,int width, int height,
    const std::string& flip_mode, std::string g_camDevIndex, std::string g_audioDevIndex) {
    // Verify we have exactly 4 points
    if (points.size() != 4) {
        std::cerr << "Error: Exactly 4 points required" << std::endl;
        return false;
    }

    // Verify points form a valid quadrilateral
    bool valid = false;
    for (size_t i = 0; i < points.size(); i++) {
        for (size_t j = i+1; j < points.size(); j++) {
            if (points[i] != points[j]) {
                valid = true;
                break;
            }
        }
        if (valid) break;
    }

    if (!valid) {
        std::cerr << "Error: Points must form a valid quadrilateral" << std::endl;
        return false;
    }

    return recorder.startRecording(outputPath, points, width, height, flip_mode, g_camDevIndex, g_audioDevIndex);
}

bool CommandHandler::startStreaming(const std::string& channelName,
    const std::vector<std::pair<double, double>>& points,int width, int height,
    const std::string& flip_mode, std::string g_camDevIndex, std::string g_audioDevIndex) {
    // Verify we have exactly 4 points
    if (points.size() != 4) {
        std::cerr << "Error: Exactly 4 points required" << std::endl;
        return false;
    }

    // Verify points form a valid quadrilateral
    bool valid = false;
    for (size_t i = 0; i < points.size(); i++) {
        for (size_t j = i+1; j < points.size(); j++) {
            if (points[i] != points[j]) {
                valid = true;
                break;
            }
        }
        if (valid) break;
    }

    if (!valid) {
        std::cerr << "Error: Points must form a valid quadrilateral" << std::endl;
        return false;
    }

    return streamer.startStreaming(channelName, points, width, height, flip_mode, g_camDevIndex, g_audioDevIndex);
}

bool CommandHandler::takeScreenshot(const std::string& outputPathSs) {
    return recorder.takeScreenshot(outputPathSs);
}

bool CommandHandler::stopRecording(const std::string& outputPath) {
    return recorder.stopRecording(outputPath);
}

bool CommandHandler::stopStreaming(const std::string& channelName) {
    return streamer.stopStreaming(channelName);
}