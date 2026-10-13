#include "NativeFileDialog.h"
#include <unistd.h>
#include <thread>
#include <atomic>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

std::mutex NativeFileDialog::s_mutex;
std::vector<NativeFileDialog::PendingCallback> NativeFileDialog::s_pending_callbacks;
std::atomic<bool> NativeFileDialog::s_dialog_active{false};

bool NativeFileDialog::is_available() {
    return is_kdialog() || is_zenity();
}

bool NativeFileDialog::is_kdialog() {
    return access("/usr/bin/kdialog", X_OK) == 0;
}

bool NativeFileDialog::is_zenity() {
    return access("/usr/bin/zenity", X_OK) == 0;
}

bool NativeFileDialog::is_dialog_open() {
    return s_dialog_active.load();
}

std::string NativeFileDialog::escape_shell(const std::string& str) {
    std::string result = "\"";
    for (char c : str) {
        if (c == '"' || c == '\\' || c == '$' || c == '`') {
            result += '\\';
        }
        result += c;
    }
    result += "\"";
    return result;
}

std::string NativeFileDialog::run_process(const std::string& cmd) {
    std::string output;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        return "";
    }
    char buffer[512];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output += buffer;
    }
    pclose(pipe);

    // Strip trailing \r, \n, spaces
    while (!output.empty() && (output.back() == '\n' || output.back() == '\r' || output.back() == ' ')) {
        output.pop_back();
    }
    return output;
}

std::string NativeFileDialog::open_file_sync(const std::string& title,
                                             const std::string& default_path,
                                             const std::string& filter_desc,
                                             const std::string& filter_patterns) {
    if (!is_available()) return "";

    std::string start_dir = default_path.empty() ? fs::current_path().string() : default_path;
    if (!fs::is_directory(start_dir)) {
        start_dir = fs::path(start_dir).parent_path().string();
    }
    if (start_dir.empty()) start_dir = ".";

    std::string cmd;
    if (is_kdialog()) {
        std::string kfilter;
        if (!filter_patterns.empty()) {
            kfilter = filter_patterns + " | " + filter_desc + "\n* | All Files (*)";
        } else {
            kfilter = "* | All Files (*)";
        }
        cmd = "/usr/bin/kdialog --title " + escape_shell(title) +
              " --getopenfilename " + escape_shell(start_dir) +
              " " + escape_shell(kfilter) + " 2>/dev/null";
    } else {
        cmd = "/usr/bin/zenity --file-selection --title=" + escape_shell(title) +
              " --filename=" + escape_shell(start_dir + "/");
        if (!filter_patterns.empty()) {
            cmd += " --file-filter=" + escape_shell(filter_desc + " | " + filter_patterns);
        }
        cmd += " 2>/dev/null";
    }

    s_dialog_active.store(true);
    std::string path = run_process(cmd);
    s_dialog_active.store(false);
    return path;
}

void NativeFileDialog::open_file(const std::string& title,
                                const std::string& default_path,
                                const std::string& filter_desc,
                                const std::string& filter_patterns,
                                std::function<void(const std::string&)> on_chosen) {
    if (!is_available()) return;

    std::thread([title, default_path, filter_desc, filter_patterns, on_chosen]() {
        std::string path = open_file_sync(title, default_path, filter_desc, filter_patterns);
        if (!path.empty() && on_chosen) {
            std::lock_guard<std::mutex> lock(s_mutex);
            s_pending_callbacks.push_back({on_chosen, path});
        }
    }).detach();
}

std::string NativeFileDialog::save_file_sync(const std::string& title,
                                             const std::string& default_path,
                                             const std::string& filter_desc,
                                             const std::string& filter_patterns) {
    if (!is_available()) return "";

    std::string start_file = default_path.empty() ? (fs::current_path().string() + "/untitled.minicut") : default_path;

    std::string cmd;
    if (is_kdialog()) {
        std::string kfilter;
        if (!filter_patterns.empty()) {
            kfilter = filter_patterns + " | " + filter_desc + "\n* | All Files (*)";
        } else {
            kfilter = "* | All Files (*)";
        }
        cmd = "/usr/bin/kdialog --title " + escape_shell(title) +
              " --getsavefilename " + escape_shell(start_file) +
              " " + escape_shell(kfilter) + " 2>/dev/null";
    } else {
        cmd = "/usr/bin/zenity --file-selection --save --confirm-overwrite --title=" + escape_shell(title) +
              " --filename=" + escape_shell(start_file);
        if (!filter_patterns.empty()) {
            cmd += " --file-filter=" + escape_shell(filter_desc + " | " + filter_patterns);
        }
        cmd += " 2>/dev/null";
    }

    s_dialog_active.store(true);
    std::string path = run_process(cmd);
    s_dialog_active.store(false);
    return path;
}

void NativeFileDialog::save_file(const std::string& title,
                                const std::string& default_path,
                                const std::string& filter_desc,
                                const std::string& filter_patterns,
                                std::function<void(const std::string&)> on_chosen) {
    if (!is_available()) return;

    std::thread([title, default_path, filter_desc, filter_patterns, on_chosen]() {
        std::string path = save_file_sync(title, default_path, filter_desc, filter_patterns);
        if (!path.empty() && on_chosen) {
            std::lock_guard<std::mutex> lock(s_mutex);
            s_pending_callbacks.push_back({on_chosen, path});
        }
    }).detach();
}


void NativeFileDialog::select_directory(const std::string& title,
                                        const std::string& default_path,
                                        std::function<void(const std::string&)> on_chosen) {
    if (!is_available()) return;

    s_dialog_active.store(true);
    std::thread([title, default_path, on_chosen]() {
        std::string start_dir = default_path.empty() ? fs::current_path().string() : default_path;
        if (!fs::is_directory(start_dir)) {
            start_dir = fs::path(start_dir).parent_path().string();
        }
        if (start_dir.empty()) start_dir = ".";

        std::string cmd;
        if (is_kdialog()) {
            cmd = "/usr/bin/kdialog --title " + escape_shell(title) +
                  " --getexistingdirectory " + escape_shell(start_dir) + " 2>/dev/null";
        } else {
            cmd = "/usr/bin/zenity --file-selection --directory --title=" + escape_shell(title) +
                  " --filename=" + escape_shell(start_dir + "/") + " 2>/dev/null";
        }

        std::string path = run_process(cmd);
        s_dialog_active.store(false);

        if (!path.empty() && on_chosen) {
            std::lock_guard<std::mutex> lock(s_mutex);
            s_pending_callbacks.push_back({on_chosen, path});
        }
    }).detach();
}

void NativeFileDialog::poll_callbacks() {
    std::vector<PendingCallback> callbacks;
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (s_pending_callbacks.empty()) return;
        callbacks.swap(s_pending_callbacks);
    }

    for (const auto& item : callbacks) {
        if (item.callback) {
            item.callback(item.path);
        }
    }
}
