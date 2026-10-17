#pragma once

#include "Common.h"
#include <string>
#include <functional>
#include <atomic>
#include <mutex>
#include <vector>
#include <map>
#include <filesystem>

class AudioEngine;

struct ImportProgressState {
    std::atomic<bool> active{false};
    std::atomic<float> progress{0.0f}; // 0.0 to 1.0
    std::string filename;
    std::string status;
    std::mutex mutex;

    void set(float p, const std::string& s) {
        progress.store(p);
        std::lock_guard<std::mutex> lock(mutex);
        status = s;
    }

    std::string get_status() {
        std::lock_guard<std::mutex> lock(mutex);
        return status;
    }
};

struct AsyncImportResult {
    enum class Type { Video, Image, Audio, Prefab };
    Type type;
    std::string filepath;
    VideoAsset video_asset;
    AudioAsset audio_asset;
    ProjectPrefab prefab_block;
    std::vector<uint8_t> thumb_rgba;
    int thumb_w = 0;
    int thumb_h = 0;
    bool success = false;
    std::function<void()> on_complete;
};

class ClipManager {
public:
    ClipManager();
    ~ClipManager();

    void set_audio_engine(AudioEngine* engine) { m_audio_engine = engine; }

    bool import_video(Project& project, const std::string& filepath);
    bool import_image(Project& project, const std::string& filepath, double default_duration = 5.0);
    bool import_audio(Project& project, const std::string& filepath);
    bool import_project_as_prefab(Project& project, const std::string& filepath);
    void instantiate_prefab_on_timeline(Project& project, const std::string& prefab_id, double insert_time);
    void remove_prefab(Project& project, const std::string& prefab_id, bool remove_from_timeline = true);

    // Asynchronous import with background worker & progress
    void start_async_import_video(const std::string& filepath, std::function<void()> on_complete = nullptr);
    void start_async_import_image(const std::string& filepath, double default_duration = 5.0, std::function<void()> on_complete = nullptr);
    void start_async_import_audio(const std::string& filepath, std::function<void()> on_complete = nullptr);
    void start_async_import_prefab(const std::string& filepath, std::function<void()> on_complete = nullptr);
    void poll_async_imports(Project& project);

    bool is_importing() const { return m_import_progress.active.load(); }
    float get_import_progress() const { return m_import_progress.progress.load(); }
    std::string get_import_status() { return m_import_progress.get_status(); }
    std::string get_import_filename() const { return m_import_progress.filename; }

    void remove_asset(Project& project, const std::string& asset_id, bool is_video, bool remove_from_timeline = true);
    int delete_unused_assets(Project& project);
    static int get_asset_usage_count(const Project& project, const std::string& asset_id);

    // Render Clip Panel (Top-Left panel)
    void render_ui(Project& project, double playhead_time,
                   const std::function<void(const std::string& asset_id, bool is_video)>& on_add_to_timeline,
                   const std::function<void(const std::string& prefab_id, double insert_time)>& on_add_prefab_to_timeline,
                   const std::function<void()>& on_project_changed,
                   const std::function<void()>& on_import_video,
                   const std::function<void()>& on_import_image,
                   const std::function<void()>& on_import_audio,
                   const std::function<void()>& on_import_prefab,
                   const std::function<void()>& on_add_text = nullptr,
                   const std::function<void()>& on_add_tts = nullptr,
                   const std::function<void()>& on_generate_demo = nullptr);

    const std::string& get_selected_asset_id() const { return m_selected_asset_id; }
    bool is_selected_video() const { return m_selected_is_video; }
    bool is_selected_prefab() const { return m_selected_is_prefab; }
    void select_asset(const std::string& id, bool is_video, bool is_prefab = false) {
        m_selected_asset_id = id;
        m_selected_is_video = is_video;
        m_selected_is_prefab = is_prefab;
    }
    void clear_selection() {
        m_selected_asset_id.clear();
        m_selected_is_prefab = false;
    }

    int get_view_mode() const { return m_view_mode; }
    void set_view_mode(int mode) { m_view_mode = mode; }

    void check_and_reload_modified_prefabs(Project& project, const std::function<void()>& on_project_changed = nullptr);

    // Folder management
    bool create_folder(Project& project, const std::string& folder_name);
    bool rename_folder(Project& project, const std::string& old_name, const std::string& new_name);
    bool delete_folder(Project& project, const std::string& folder_name);
    void set_asset_folder(Project& project, const std::string& asset_id, bool is_video, bool is_prefab, const std::string& folder_name);
    std::string get_asset_folder(const Project& project, const std::string& asset_id, bool is_video, bool is_prefab) const;
    const std::string& get_selected_folder_filter() const { return m_selected_folder_filter; }
    void set_selected_folder_filter(const std::string& folder) { m_selected_folder_filter = folder; }

private:
    std::string m_selected_asset_id;
    bool m_selected_is_video = true;
    bool m_selected_is_prefab = false;
    int m_filter_type = 0; // 0 = All, 1 = Videos, 2 = Images, 3 = Audio, 4 = Prefabs
    int m_view_mode = 0;   // 0 = List, 1 = Grid
    char m_search_filter[64] = "";
    AudioEngine* m_audio_engine = nullptr;

    // Folder organization state
    std::string m_selected_folder_filter; // "" = All Folders, "(Root)" = Root / No folder, otherwise folder name
    bool m_show_new_folder_dialog = false;
    bool m_show_rename_folder_dialog = false;
    char m_folder_input_buf[128] = "";
    std::string m_context_folder_name;
    std::string m_folder_target_asset_id;
    bool m_folder_target_is_video = true;
    bool m_folder_target_is_prefab = false;

    // Prefab hot reload tracking
    std::map<std::string, std::filesystem::file_time_type> m_prefab_write_times;

    // Progress and async worker
    ImportProgressState m_import_progress;
    std::mutex m_async_mutex;
    std::vector<AsyncImportResult> m_completed_async_results;

    // Context menu tracking
    std::string m_context_asset_id;
    bool m_context_is_video = true;
    bool m_context_is_prefab = false;
    bool m_open_asset_context = false;
};
