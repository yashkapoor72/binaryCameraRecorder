#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include "command_handler.h"
#include "deskew_handler.h"
#include <gst/gst.h>
#include <gst/gstmacos.h>
#include "gstopencvperspective.h"

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

static void parseAndExecuteCommand(const std::string& command, CommandHandler& cmdHandler, DeskewHandler& deskewHandler) {
    auto args = splitArguments(command);
    
    std::string action;
    std::string outputPath;
    std::vector<std::pair<double, double>> points;
    std::string flipMethod = "none";
    
    for (const auto& arg : args) {
        if (arg.find("--action=") == 0) {
            action = arg.substr(9);
        }
        else if (arg.find("--outputPath=") == 0) {
            outputPath = arg.substr(13);
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
        if (!cmdHandler.startRecording(outputPath, points, flipMethod)) {
            std::cerr << "Failed to start recording: " << outputPath << std::endl;
        }
        deskewHandler.updateSettings(points, flipMethod);
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
    // Set paths to include both system locations and our build directory
    std::string build_dir = g_get_current_dir();
    std::string plugin_path = "/usr/local/lib/gstreamer-1.0:/opt/homebrew/lib/gstreamer-1.0:" + build_dir;
    setenv("GST_PLUGIN_PATH", plugin_path.c_str(), 1);
    
    if (!gst_init_check(&argc, &argv, nullptr)) {
        std::cerr << "Failed to initialize GStreamer" << std::endl;
        return 1;
    }

    GstRegistry* registry = gst_registry_get();
    gst_registry_scan_path(registry, build_dir.c_str());  // Force scan build directory

    GstPluginFeature* feature = gst_registry_lookup_feature(registry, "opencvperspective");
    if (!feature) {
        std::cerr << "ERROR: Plugin registration failed. Tried paths: " << plugin_path << std::endl;
        std::cerr << "Built plugin should be at: " << build_dir << "/opencvperspective.dylib" << std::endl;
        return 1;
    }
    
    if (!feature) {
        std::cerr << "ERROR: Plugin not found in registry. Current GST_PLUGIN_PATH: " 
                 << getenv("GST_PLUGIN_PATH") << std::endl;
        return 1;
    }
    
    gst_object_unref(feature);
    return gst_macos_main((GstMainFunc)run_app, argc, argv, nullptr);
}