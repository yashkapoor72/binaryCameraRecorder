#include "command_handler.h"
#include "gstrecording.h"
static GstRecording recorder;
bool CommandHandler::startRecording(const std::string& outputPath) {
    return recorder.startRecording(outputPath);
}
bool CommandHandler::stopRecording(const std::string& outputPath) {
    return recorder.stopRecording(outputPath);
}
