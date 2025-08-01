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
                      int output_width = 1280,
                      int output_height = 720,
                      const std::string& flip_mode = "none",
                      std::string g_camDevIndex = "null", std::string g_audioDevIndex = "null");
    bool startStreaming(const std::string& channelName,
                      const std::vector<std::pair<double, double>>& points,
                      int output_width = 1280,
                      int output_height = 720,
                      const std::string& flip_mode = "none",
                      std::string g_camDevIndex = "null", std::string g_audioDevIndex = "null");
    bool takeScreenshot(const std::string& outputPathSs);
    bool stopRecording(const std::string& outputPath);
    bool stopStreaming(const std::string& channelName);
    
};

#endif