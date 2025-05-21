#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include "command_handler.h"
#include "deskew_handler.h"
#include <gst/gst.h>
#include <gst/gstmacos.h>

// Global variables (set once at startup)
static std::string g_camDevIndex = "null";
static std::string g_audioDevIndex = "null";

// 👇 Global width & height (initialized to -1)
static int g_width = -1;
static int g_height = -1;

// Split command into arguments (handles quotes and parentheses)
static std::vector<std::string> splitArguments(const std::string& input) {
    std::vector<std::string> args;
    bool inQuotes = false;
    bool inParentheses = false;
    std::string current;
    
    for (char c : input) {
        if (c == ' ' && !inQuotes && !inParentheses) {
            if (!current.empty()) {
                args.push_back(current);
                current.clear();
            }
        } else {
            if (c == '"') inQuotes = !inQuotes;
            if (c == '(') inParentheses = true;
            if (c == ')') inParentheses = false;
            current += c;
        }
    }
    
    if (!current.empty()) {
        args.push_back(current);
    }
    
    return args;
}

// Parse and execute a command (uses global width/height)
static void parseAndExecuteCommand(const std::string& command, CommandHandler& cmdHandler, DeskewHandler& deskewHandler) {
    auto args = splitArguments(command);
    
    std::string action;
    std::string outputPath;
    std::string channelName;
    std::string outputPathSs;
    std::vector<std::pair<double, double>> points;
    std::string flipMethod = "none";
    
    for (const auto& arg : args) {
        if (arg.find("--action=") == 0) {
            action = arg.substr(9);
        }
        else if (arg.find("--outputPath=") == 0) {
            outputPath = arg.substr(13);
        }
        else if (arg.find("--outputPathSs=") == 0) {
            outputPathSs = arg.substr(15);
        }
        else if (arg.find("--channelName=") == 0) {
            channelName = arg.substr(14);
        }
        else if (arg.find("--p1=") == 0) {
            auto coords = arg.substr(5);
            if (coords.front() == '(' && coords.back() == ')') {
                coords = coords.substr(1, coords.size() - 2);
            }
            size_t comma = coords.find(',');
            double x = std::stod(coords.substr(0, comma));
            double y = std::stod(coords.substr(comma+1));
            points.emplace_back(x, y);
        }
        else if (arg.find("--p2=") == 0) {
            auto coords = arg.substr(5);
            if (coords.front() == '(' && coords.back() == ')') {
                coords = coords.substr(1, coords.size() - 2);
            }
            size_t comma = coords.find(',');
            double x = std::stod(coords.substr(0, comma));
            double y = std::stod(coords.substr(comma+1));
            points.emplace_back(x, y);
        }
        else if (arg.find("--p3=") == 0) {
            auto coords = arg.substr(5);
            if (coords.front() == '(' && coords.back() == ')') {
                coords = coords.substr(1, coords.size() - 2);
            }
            size_t comma = coords.find(',');
            double x = std::stod(coords.substr(0, comma));
            double y = std::stod(coords.substr(comma+1));
            points.emplace_back(x, y);
        }
        else if (arg.find("--p4=") == 0) {
            auto coords = arg.substr(5);
            if (coords.front() == '(' && coords.back() == ')') {
                coords = coords.substr(1, coords.size() - 2);
            }
            size_t comma = coords.find(',');
            double x = std::stod(coords.substr(0, comma));
            double y = std::stod(coords.substr(comma+1));
            points.emplace_back(x, y);
        }
        else if (arg.find("--flipMethod=") == 0) {
            flipMethod = arg.substr(13);
        }
        else if (arg.find("--width=") == 0) {
            g_width = std::stoi(arg.substr(8));
            std::cout << "Set width: " << g_width << std::endl;
        }
        else if (arg.find("--height=") == 0) {
            g_height = std::stoi(arg.substr(9));
            std::cout << "Set height: " << g_height << std::endl;
        }
    }
    
    if (action == "start-recording") {
        if (outputPath.empty()) {
            std::cerr << "Error: outputPath is required for start-recording" << std::endl;
            return;
        }
        if (points.size() != 4) {
            std::cerr << "Error: Exactly 4 points (p1-p4) are required for quadrilateral cropping" << std::endl;
            return;
        }
        if (!cmdHandler.startRecording(outputPath, points, g_width, g_height, flipMethod, g_camDevIndex, g_audioDevIndex)) {
            std::cerr << "Failed to start recording: " << outputPath << std::endl;
        }
        deskewHandler.updateSettings(points, flipMethod);
    }
    else if (action == "start-streaming") {
        if (channelName.empty()) {
            std::cerr << "Error: channelName is required for start-streaming" << std::endl;
            return;
        }
        if (points.size() != 4) {
            std::cerr << "Error: Exactly 4 points (p1-p4) are required for quadrilateral cropping" << std::endl;
            return;
        }
        if (!cmdHandler.startStreaming(channelName, points, g_width, g_height, flipMethod, g_camDevIndex, g_audioDevIndex)) {
            std::cerr << "Failed to start streaming: " << channelName << std::endl;
        }
        deskewHandler.updateSettings(points, flipMethod);
    }
    else if (action == "take-screenshot") {
        if (outputPathSs.empty()) {
            std::cerr << "Error: Screenshot outputPath is required for take-screenshot" << std::endl;
            return;
        }
        if (!cmdHandler.takeScreenshot(outputPathSs)) {
            std::cerr << "Failed to start recording: " << outputPathSs << std::endl;
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
    else if (action == "stop-streaming") {
        if (channelName.empty()) {
            std::cerr << "Error: channelName is required for stop-streaming" << std::endl;
            return;
        }
        if (!cmdHandler.stopStreaming(channelName)) {
            std::cerr << "Failed to stop streaming: " << std::endl;
        }
    }
    else if (!action.empty()) {
        std::cerr << "Unknown action: " << action << std::endl;
    }
}

// Parse device indices at startup
static bool parseDeviceIndices(int argc, char* argv[]) {
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg.find("--CamDevIndex=") == 0) {
            g_camDevIndex = arg.substr(14);
        }
        else if (arg.find("--AudioDevIndex=") == 0) {
            g_audioDevIndex = arg.substr(16);  // Changed from stoi() to direct string assignment
        }
    }
    
    if (g_camDevIndex == "null" || g_audioDevIndex == "null") {
        std::cerr << "Error: Both --CamDevIndex and --AudioDevIndex must be specified" << std::endl;
        return false;
    }
    return true;
}

// Main app loop
static int run_app(int argc, char* argv[]) {
    CommandHandler cmdHandler;
    DeskewHandler deskewHandler(g_camDevIndex, g_audioDevIndex);
    
    if (!deskewHandler.setupPipeline(g_camDevIndex, g_audioDevIndex)) {
        std::cerr << "Failed to setup preview pipeline!" << std::endl;
        return 1;
    }
    
    // Check if command-line args were provided directly
    if (argc > 1) {
        std::string command;
        for (int i = 1; i < argc; i++) {
            command += argv[i];
            if (i < argc - 1) command += " ";
        }
        parseAndExecuteCommand(command, cmdHandler, deskewHandler);
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

// Main entry point
int main(int argc, char* argv[]) {
    // First parse and validate device indices
    if (!parseDeviceIndices(argc, argv)) {
        return 1;
    }
    
    // Set GStreamer plugin paths
    std::string build_dir = g_get_current_dir();
    std::string plugin_path = "/usr/local/lib/gstreamer-1.0:/opt/homebrew/lib/gstreamer-1.0:" + build_dir;
    setenv("GST_PLUGIN_PATH", plugin_path.c_str(), 1);
    
    if (!gst_init_check(&argc, &argv, nullptr)) {
        std::cerr << "Failed to initialize GStreamer" << std::endl;
        return 1;
    }

    GstRegistry* registry = gst_registry_get();
    gst_registry_scan_path(registry, build_dir.c_str());

    return gst_macos_main((GstMainFunc)run_app, argc, argv, nullptr);
}