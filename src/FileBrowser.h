#pragma once

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <filesystem>
#include <GL/glew.h>

enum class FileBrowserMode {
    OpenFile,
    SaveFile
};

struct FileFilter {
    std::string name;             // e.g. "Video Files (*.mp4, *.mkv, *.mov)"
    std::vector<std::string> exts; // e.g. {".mp4", ".mkv", ".mov"}
};

struct FileItem {
    std::string name;
    bool is_directory = false;
    uintmax_t size = 0;
    std::string size_str;
    std::string time_str;
    std::string extension;
    bool is_video = false;
    bool is_image = false;
    bool is_audio = false;
    bool is_project = false;
};

struct MountedDevice {
    std::string name;
    std::string path;
    std::string type; // "SD Card", "Removable Drive", "Mounted Volume"
    uintmax_t total_space = 0;
    uintmax_t free_space = 0;
};

class FileBrowser {
public:
    FileBrowser();
    ~FileBrowser();

    void open(const std::string& title,
              FileBrowserMode mode,
              const std::vector<FileFilter>& filters,
              const std::string& default_filename,
              const std::string& default_extension,
              const std::function<void(const std::string& path)>& on_confirm,
              const std::string& confirm_btn_label = "",
              const std::function<void()>& on_cancel = nullptr);

    void cancel();
    void close();
    bool is_open() const { return m_is_open; }

    void render();
    std::string find_system_thumbnail(const std::string& full_path) const;

    static bool is_video_file(const std::string& path);
    static bool is_audio_file(const std::string& path);
    static bool is_image_file(const std::string& path);

private:
    void refresh_directory();
    void refresh_mounted_devices();
    void clear_thumbnail_cache();
    GLuint get_or_load_thumbnail(const std::string& full_path, bool is_video, bool is_image);
    bool matches_filter(const std::string& filename, const std::string& ext) const;

    bool m_is_open = false;
    std::string m_title;
    FileBrowserMode m_mode = FileBrowserMode::OpenFile;
    std::vector<FileFilter> m_filters;
    int m_current_filter_idx = 0;
    std::string m_default_ext;

    std::filesystem::path m_current_dir;
    std::vector<FileItem> m_items;
    std::vector<MountedDevice> m_mounted_devices;

    char m_filename_input[512] = "";
    char m_path_input[1024] = "";
    char m_search_filter[64] = "";
    std::string m_confirm_btn_label;

    std::map<std::string, GLuint> m_thumbnail_cache;

    std::function<void(const std::string& path)> m_on_confirm;
    std::function<void()> m_on_cancel;
};
