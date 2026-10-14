#pragma once

#include "Common.h"
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>

struct ExportConfig {
    std::string output_path = "output.mp4";
    int width = 1280;
    int height = 720;
    int fps = 30;
    int video_bitrate = 4000000;
    int audio_sample_rate = 48000;
    int audio_channels = 2;
    int audio_bitrate = 192000;
};

class ExportManager {
public:
    ExportManager();
    ~ExportManager();

    bool start_export(const Project& project, const ExportConfig& config);
    void cancel_export();

    bool is_exporting() const { return m_is_exporting.load(); }
    float get_progress() const { return m_progress.load(); }
    bool is_finished() const { return m_is_finished.load(); }
    bool is_error() const { return m_is_error.load(); }

    void open_export_dialog() { m_show_dialog = true; }
    bool is_dialog_open() const { return m_show_dialog; }
    void render_ui(const std::function<void(const ExportConfig& cfg)>& on_start_export = nullptr);

    void set_browse_callback(const std::function<void()>& cb) { m_on_browse = cb; }
    void set_output_path(const std::string& path);
    const std::string& get_output_path() const { return m_config.output_path; }

private:
    void export_thread_func(Project project_snapshot, ExportConfig config);

    std::atomic<bool> m_is_exporting{false};
    std::atomic<bool> m_cancel_requested{false};
    std::atomic<float> m_progress{0.0f};
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
    ExportConfig m_config;
    char m_output_path_buf[512] = "minicut_export.mp4";
    std::function<void()> m_on_browse;
};
