#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include "command_handler.h"
#include "deskew_handler.h"
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

static void parseAndExecuteCommand(const std::string& command, CommandHandler& cmdHandler, DeskewHandler& deskewHandler) {
    auto args = splitArguments(command);
    
    std::string action;
    std::string outputPath;
    int top = 0, bottom = 0, left = 0, right = 0;
    std::string flipMethod = "none";
    
    for (const auto& arg : args) {
        if (arg.find("--action=") == 0) {
            action = arg.substr(9);
        }
        else if (arg.find("--outputPath=") == 0) {
            outputPath = arg.substr(13);
        }
        else if (arg.find("--top=") == 0) {
            top = std::stoi(arg.substr(6));
        }
        else if (arg.find("--bottom=") == 0) {
            bottom = std::stoi(arg.substr(9));
        }
        else if (arg.find("--left=") == 0) {
            left = std::stoi(arg.substr(7));
        }
        else if (arg.find("--right=") == 0) {
            right = std::stoi(arg.substr(8));
        }
        else if (arg.find("--flipMethod=") == 0) {
            flipMethod = arg.substr(13);
        }
    }
    
    if (action == "start-recording") {
        if (outputPath.empty()) {
            std::cerr << "Error: outputPath is required for start-recording" << std::endl;
            return;
        }
        if (!cmdHandler.startRecording(outputPath, top, bottom, left, right, flipMethod)) {
            std::cerr << "Failed to start recording: " << outputPath << std::endl;
        }
        if (top != 0 || bottom != 0 || left != 0 || right != 0 || flipMethod != "none") {
            deskewHandler.updateSettings(top, bottom, left, right, flipMethod);
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
    DeskewHandler deskewHandler;
    
    if (!deskewHandler.setupPipeline()) {
        std::cerr << "Failed to setup preview pipeline!" << std::endl;
        return 1;
    }
    
    std::cout << "Ready to accept commands (--action=start-recording/stop-recording --outputPath=...)" << std::endl;
    
    std::string command;
    while (std::getline(std::cin, command)) {
        if (!command.empty()) {
            parseAndExecuteCommand(command, cmdHandler, deskewHandler);
        }
    }
    
    return 0;
}

int main(int argc, char* argv[]) {
    setenv("GST_DEBUG", "2", 0);
    return gst_macos_main((GstMainFunc)run_app, argc, argv, nullptr);
}
