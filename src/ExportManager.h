#pragma once

#include "Common.h"
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>

struct ExportConfig {
    std::string output_path = "output.mp4";
    std::string format = "mp4"; // "mp4", "gif", "webp"
    int width = 1920;
    int height = 1080;
    int fps = 30;
    int video_bitrate = 6000000;
    int audio_sample_rate = 48000;
    int audio_channels = 2;
    int audio_bitrate = 192000;
    bool export_srt = false;
    bool auto_length = true;
    double manual_duration = 0.0;
};

struct FormatPreset {
    std::string id;
    std::string name;
    int width = 1920;
    int height = 1080;
    int fps = 30;
    std::string format = "mp4";
    bool is_custom = false;
};

class ExportManager {
public:
    ExportManager();
    ~ExportManager();

    bool start_export(const Project& project, const ExportConfig& config);
    void cancel_export();

    static bool write_srt_file(const std::string& srt_path, const Project& project);

    bool is_exporting() const { return m_is_exporting.load(); }
    float get_progress() const { return m_progress.load(); }
    float get_audio_progress() const { return m_audio_progress.load(); }
    bool is_finished() const { return m_is_finished.load(); }
    bool is_error() const { return m_is_error.load(); }
    std::string get_status_text();

    void open_export_dialog() { m_show_dialog = true; m_request_focus = true; }
    void close_export_dialog() { m_show_dialog = false; }
    bool is_dialog_open() const { return m_show_dialog; }
    void dismiss_status() { m_is_finished.store(false); m_is_error.store(false); }
    bool is_window_active() const {
        return m_show_dialog || m_is_exporting.load() || m_is_finished.load() || m_is_error.load();
    }
    void render_ui(const std::function<void(const ExportConfig& cfg)>& on_start_export = nullptr, bool standalone_window = false);

    void set_browse_callback(const std::function<void()>& cb) { m_on_browse = cb; }
    void set_output_path(const std::string& path);
    const std::string& get_output_path() const { return m_config.output_path; }

    void load_format_presets();
    bool save_format_preset(const std::string& name, int width, int height, int fps, const std::string& format);
    bool delete_format_preset(int index);
    const std::vector<FormatPreset>& get_presets() const { return m_presets; }

private:
    void export_thread_func(Project project_snapshot, ExportConfig config);
    void render_settings_contents(const std::function<void(const ExportConfig& cfg)>& on_start_export);
    void render_progress_contents();

    std::atomic<bool> m_is_exporting{false};
    std::atomic<bool> m_cancel_requested{false};
    std::atomic<float> m_progress{0.0f};
    std::atomic<float> m_audio_progress{0.0f};
    std::atomic<int> m_current_frame{0};
    std::atomic<int> m_total_frames{0};
    std::atomic<bool> m_is_finished{false};
    std::atomic<bool> m_is_error{false};

    std::mutex m_status_mutex;
    std::string m_status_text = "Idle";
    std::string m_error_message = "";

    std::thread m_worker_thread;

    // UI state
    bool m_show_dialog = false;
    bool m_request_focus = false;
    ExportConfig m_config;
    char m_output_path_buf[512] = "minicut_export.mp4";
    std::function<void()> m_on_browse;

    // Presets
    std::vector<FormatPreset> m_presets;
    int m_selected_preset_idx = 0;
    char m_new_preset_name_buf[128] = "";
    bool m_show_save_preset_dialog = false;
};
