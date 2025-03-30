#ifndef COMMAND_HANDLER_H
#define COMMAND_HANDLER_H
#include <string>
class CommandHandler {
public:
    bool startRecording(const std::string& outputPath,
                      int top = 0, int bottom = 0, int left = 0, int right = 0,
                      const std::string& flip_mode = "none");
    bool stopRecording(const std::string& outputPath);
};
#endif
