#include "command_handler.h"
#include "gstrecording.h"
static GstRecording recorder;
bool CommandHandler::startRecording(const std::string& outputPath,
                                 int top, int bottom, int left, int right,
                                 const std::string& flip_mode) {
    return recorder.startRecording(outputPath, top, bottom, left, right, flip_mode);
}
bool CommandHandler::stopRecording(const std::string& outputPath) {
    return recorder.stopRecording(outputPath);
}
