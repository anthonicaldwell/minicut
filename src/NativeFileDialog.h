#pragma once

#include <string>
#include <functional>
#include <vector>
#include <mutex>
#include <atomic>

class NativeFileDialog {
public:
    static bool is_available();
    static bool is_kdialog();
    static bool is_zenity();

    static void open_file(const std::string& title,
                          const std::string& default_path,
                          const std::string& filter_desc,
                          const std::string& filter_patterns,
                          std::function<void(const std::string&)> on_chosen);

    static void save_file(const std::string& title,
                          const std::string& default_path,
                          const std::string& filter_desc,
                          const std::string& filter_patterns,
                          std::function<void(const std::string&)> on_chosen);

    static void select_directory(const std::string& title,
                                 const std::string& default_path,
                                 std::function<void(const std::string&)> on_chosen);

    // Call this each frame on main thread (App::update) to execute callbacks safely
    static void poll_callbacks();

    static bool is_dialog_open();

private:
    struct PendingCallback {
        std::function<void(const std::string&)> callback;
        std::string path;
    };

    static std::mutex s_mutex;
    static std::vector<PendingCallback> s_pending_callbacks;
    static std::atomic<bool> s_dialog_active;

    static std::string run_process(const std::string& cmd);
    static std::string escape_shell(const std::string& str);
};
