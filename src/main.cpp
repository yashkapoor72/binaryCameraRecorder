#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include "command_handler.h"
#include "preview_handler.h"
#include <gst/gst.h>
#include <gst/gstmacos.h>

static std::vector<std::string> splitArguments(const std::string& input) {
    std::vector<std::string> args;
    size_t start = 0;
    size_t end = input.find(' ');
    while (end != std::string::npos) {
        args.push_back(input.substr(start, end - start));
        start = end + 1;
        end = input.find(' ', start);
    }
    args.push_back(input.substr(start));
    return args;
}

static void parseAndExecuteCommand(const std::string& command, CommandHandler& cmdHandler) {
    auto args = splitArguments(command);
    std::string action;
    std::string outputPath;
    
    for (const auto& arg : args) {
        if (arg.find("--action=") == 0) {
            action = arg.substr(9);
        }
        else if (arg.find("--outputPath=") == 0) {
            outputPath = arg.substr(13);
        }
    }
    
    if (action == "start-recording") {
        if (outputPath.empty()) {
            std::cerr << "Error: outputPath is required for start-recording" << std::endl;
            return;
        }
        if (!cmdHandler.startRecording(outputPath)) {
            std::cerr << "Failed to start recording: " << outputPath << std::endl;
        }
    }
    else if (action == "stop-recording") {
        if (outputPath.empty()) {
            std::cerr << "Error: outputPath is required for stop-recording" << std::endl;
            return;
        }
        if (!cmdHandler.stopRecording(outputPath)) {
            std::cerr << "Failed to stop recording: " << outputPath << std::endl;
        }
    }
    else {
        std::cerr << "Unknown action: " << action << std::endl;
    }
}

static int run_app(int argc, char* argv[]) {
    CommandHandler cmdHandler;
    PreviewHandler previewHandler;
    
    if (!previewHandler.setupPipeline()) {
        std::cerr << "Failed to setup preview pipeline!" << std::endl;
        return 1;
    }
    
    std::cout << "Ready to accept commands (--action=start-recording/stop-recording --outputPath=...)" << std::endl;
    
    std::string command;
    while (std::getline(std::cin, command)) {
        if (!command.empty()) {
            parseAndExecuteCommand(command, cmdHandler);
        }
    }
    
    return 0;
}

int main(int argc, char* argv[]) {
    setenv("GST_DEBUG", "2", 0);
    return gst_macos_main((GstMainFunc)run_app, argc, argv, nullptr);
}
