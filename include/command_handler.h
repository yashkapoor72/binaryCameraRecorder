#ifndef COMMAND_HANDLER_H
#define COMMAND_HANDLER_H

#include <string>
#include <vector>
#include <utility> // for std::pair
#include <iostream>

class CommandHandler {
public:
    bool startRecording(const std::string& outputPath,
                      const std::vector<std::pair<double, double>>& points,
                      const std::string& flip_mode = "none");
    bool stopRecording(const std::string& outputPath);
};

#endif