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

struct BatchJob {
    int id = 0;
    std::string project_path;
    std::string output_path;
    std::string format = "mp4";
    int width = 1920;
    int height = 1080;
    int fps = 30;
    enum class Status { Queued, Processing, Completed, Failed, Cancelled } status = Status::Queued;
    float progress = 0.0f;
    std::string error_message;
};

class ExportManager {
public:
    ExportManager();
    ~ExportManager();

    bool start_export(const Project& project, const ExportConfig& config);
    void cancel_export();

    bool export_headless_sync(const Project& project, const ExportConfig& config,
                              const std::function<void(float progress, const std::string& status)>& progress_cb = nullptr);
    std::string get_error_message();

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
        return m_show_dialog || m_is_exporting.load() || m_is_finished.load() || m_is_error.load() || m_is_batch_running.load();
    }
    void render_ui(const std::function<void(const ExportConfig& cfg)>& on_start_export = nullptr, bool standalone_window = false);

    void set_browse_callback(const std::function<void()>& cb) { m_on_browse = cb; }
    void set_output_path(const std::string& path);
    const std::string& get_output_path() const { return m_config.output_path; }

    void load_format_presets();
    bool save_format_preset(const std::string& name, int width, int height, int fps, const std::string& format);
    bool delete_format_preset(int index);
    const std::vector<FormatPreset>& get_presets() const { return m_presets; }

    // Batch Processing API
    const std::vector<BatchJob>& get_batch_jobs() const { return m_batch_jobs; }
    std::vector<BatchJob>& get_batch_jobs_mut() { return m_batch_jobs; }
    void add_batch_job(const std::string& project_path, const std::string& output_path = "",
                       const std::string& format = "mp4", int width = 1920, int height = 1080, int fps = 30);
    bool edit_batch_job(int job_id, const std::string& project_path, const std::string& output_path,
                        const std::string& format, int width, int height, int fps);
    bool delete_batch_job(int job_id);
    void clear_batch_jobs();
    void start_batch_processing();
    void cancel_batch_processing();
    bool is_batch_running() const { return m_is_batch_running.load(); }
    int get_current_batch_job_id() const { return m_current_batch_job_id.load(); }
    float get_batch_overall_progress() const;

private:
    void export_thread_func(Project project_snapshot, ExportConfig config);
    void render_settings_contents(const std::function<void(const ExportConfig& cfg)>& on_start_export);
    void render_progress_contents();
    void render_batch_contents();
    void batch_thread_func();

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

    // Batch Processing State
    std::vector<BatchJob> m_batch_jobs;
    mutable std::mutex m_batch_mutex;
    std::thread m_batch_thread;
    std::atomic<bool> m_is_batch_running{false};
    std::atomic<bool> m_batch_cancel_requested{false};
    std::atomic<int> m_current_batch_job_id{0};
    int m_selected_batch_job_id = -1;
    int m_next_batch_job_id = 1;

    // Batch dialog modal state
    bool m_show_add_batch_dialog = false;
    bool m_show_edit_batch_dialog = false;
    char m_batch_input_project[512] = "";
    char m_batch_input_output[512] = "";
    int m_batch_input_width = 1920;
    int m_batch_input_height = 1080;
    int m_batch_input_fps = 30;
    int m_batch_input_fmt_idx = 0;
};
