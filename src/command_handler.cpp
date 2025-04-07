#include "command_handler.h"
#include "gstrecording.h"

static GstRecording recorder;

bool CommandHandler::startRecording(const std::string& outputPath,
    const std::vector<std::pair<double, double>>& points,
    const std::string& flip_mode) {
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

    return recorder.startRecording(outputPath, points, flip_mode);
}

bool CommandHandler::stopRecording(const std::string& outputPath) {
    return recorder.stopRecording(outputPath);
}