#include "App.h"
#include "SettingsLogic.h"
#include "ExportManager.h"
#include "ProjectSerializer.h"
#include <iostream>
#include <iomanip>
#include <fstream>
#include <csignal>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static void print_usage(const char* prog) {
    std::cout << "minicut - Modern C++17 Multi-Track Video Editor (v" MINICUT_VERSION ")\n"
              << "Usage: " << prog << " [options] [project.minicut]\n\n"
              << "Options:\n"
              << "  --render, --export <file.minicut>   Render video in headless mode without GUI\n"
              << "  -o, --output <file>                 Output video path (default: <project>.mp4)\n"
              << "  --format <mp4|gif|webp>             Output format (default: mp4)\n"
              << "  --width <pixels>                    Video width (default: 1920)\n"
              << "  --height <pixels>                   Video height (default: 1080)\n"
              << "  --fps <framerate>                   Frames per second (default: 30)\n"
              << "  --bitrate <bps>                     Video bitrate (default: 6000000)\n"
              << "  --export-srt                        Also export subtitles to .srt file\n"
              << "  -r, --reconfig                      Reset settings and window layout to defaults\n"
              << "  -h, --help                          Display this help message and exit\n"
              << std::endl;
}

int main(int argc, char* argv[]) {
    std::signal(SIGPIPE, SIG_IGN);

    bool is_render_mode = false;
    std::string render_project_path;
    std::string output_path;
    std::string format = "mp4";
    int width = 1920;
    int height = 1080;
    int fps = 30;
    int video_bitrate = 6000000;
    bool export_srt = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "-r" || arg == "--reconfig" || arg == "--reset-config") {
            SettingsLogic::instance().reset_to_defaults();
            std::cout << "minicut: Settings and UI layout reset to factory defaults successfully." << std::endl;
            return 0;
        } else if (arg == "--render" || arg == "--export" || arg == "-render" || arg == "-export") {
            is_render_mode = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                render_project_path = argv[++i];
            }
        } else if (arg == "-o" || arg == "--output") {
            if (i + 1 < argc) {
                output_path = argv[++i];
            }
        } else if (arg == "--format") {
            if (i + 1 < argc) {
                format = argv[++i];
            }
        } else if (arg == "--width") {
            if (i + 1 < argc) {
                width = std::stoi(argv[++i]);
            }
        } else if (arg == "--height") {
            if (i + 1 < argc) {
                height = std::stoi(argv[++i]);
            }
        } else if (arg == "--fps") {
            if (i + 1 < argc) {
                fps = std::stoi(argv[++i]);
            }
        } else if (arg == "--bitrate") {
            if (i + 1 < argc) {
                video_bitrate = std::stoi(argv[++i]);
            }
        } else if (arg == "--export-srt") {
            export_srt = true;
        }
    }

    if (is_render_mode) {
        if (render_project_path.empty()) {
            std::cerr << "minicut error: No project file specified for --render.\n"
                      << "Usage: minicut --render <project.minicut> [-o <output.mp4>]" << std::endl;
            return 1;
        }

        if (!fs::exists(render_project_path)) {
            std::cerr << "minicut error: Project file does not exist: " << render_project_path << std::endl;
            return 1;
        }

        std::ifstream ifs(render_project_path);
        if (!ifs.is_open()) {
            std::cerr << "minicut error: Could not open project file: " << render_project_path << std::endl;
            return 1;
        }

        std::string json_str((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        Project project;
        if (!ProjectSerializer::deserialize_json(project, json_str)) {
            std::cerr << "minicut error: Failed to parse project file JSON: " << render_project_path << std::endl;
            return 1;
        }

        if (output_path.empty()) {
            fs::path p(render_project_path);
            std::string stem = p.stem().string();
            std::string ext = "." + format;
            if (!p.parent_path().empty()) {
                output_path = (p.parent_path() / (stem + ext)).string();
            } else {
                output_path = stem + ext;
            }
        } else {
            std::string ext = fs::path(output_path).extension().string();
            if (ext == ".gif") format = "gif";
            else if (ext == ".webp") format = "webp";
            else if (ext == ".mp4") format = "mp4";
        }

        ExportConfig config;
        config.output_path = output_path;
        config.format = format;
        config.width = width;
        config.height = height;
        config.fps = fps;
        config.video_bitrate = video_bitrate;
        config.export_srt = export_srt;

        std::cout << "minicut headless render: Project '" << project.name << "' (" << render_project_path << ")\n"
                  << "  Output: " << config.output_path << " [" << config.width << "x" << config.height
                  << " @ " << config.fps << "fps, format: " << config.format << "]" << std::endl;

        ExportManager export_mgr;
        bool ok = export_mgr.export_headless_sync(project, config, [](float progress, const std::string& status) {
            std::cout << "\r[" << std::setw(3) << static_cast<int>(progress * 100.0f) << "%] "
                      << status << "            " << std::flush;
        });
        std::cout << std::endl;

        if (ok) {
            std::cout << "minicut: Headless export completed successfully -> " << config.output_path << std::endl;
            return 0;
        } else {
            std::cerr << "minicut: Headless export failed: " << export_mgr.get_error_message() << std::endl;
            return 1;
        }
    }

    std::cout << "Starting minicut - Modern C++17 Multi-Track Video Editor..." << std::endl;
    App app;
    if (!app.init(argc, argv)) {
        std::cerr << "Failed to initialize minicut application!" << std::endl;
        return 1;
    }

    app.run();
    std::cout << "minicut shut down cleanly." << std::endl;
    return 0;
}

