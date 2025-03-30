#ifndef COMMAND_HANDLER_H
#define COMMAND_HANDLER_H
#include <string>
class CommandHandler {
public:
    bool startRecording(const std::string& outputPath);
    bool stopRecording(const std::string& outputPath);
};
#endif
