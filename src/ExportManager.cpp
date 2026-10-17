#include "ExportManager.h"
#include "MediaDecoder.h"
#include "TempManager.h"
#include "TextGenerator.h"
#include "ProjectSerializer.h"
#include <imgui.h>
#include <filesystem>
#include <iostream>
#include <vector>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

static std::string format_srt_time(double seconds) {
    if (seconds < 0.0) seconds = 0.0;
    int total_ms = static_cast<int>(seconds * 1000.0);
    int ms = total_ms % 1000;
    int total_sec = total_ms / 1000;
    int sec = total_sec % 60;
    int total_min = total_sec / 60;
    int min = total_min % 60;
    int hr = total_min / 60;
    char buf[64];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d,%03d", hr, min, sec, ms);
    return std::string(buf);
}


bool ExportManager::write_srt_file(const std::string& srt_path, const Project& project) {
    struct Cue {
        double start;
        double end;
        std::string text;
    };
    std::vector<Cue> cues;
    for (const auto& track : project.tracks) {
        if (track.type != "subtitle" || track.muted) continue;
        for (const auto& clip : track.clips) {
            if (!clip.subtitle_text.empty()) {
                cues.push_back({ clip.start_time, clip.end_time(), clip.subtitle_text });
            }
        }
    }
    std::sort(cues.begin(), cues.end(), [](const Cue& a, const Cue& b) {
        return a.start < b.start;
    });

    std::ofstream ofs(srt_path);
    if (!ofs.is_open()) return false;

    for (size_t i = 0; i < cues.size(); ++i) {
        ofs << (i + 1) << "\n";
        ofs << format_srt_time(cues[i].start) << " --> " << format_srt_time(cues[i].end) << "\n";
        ofs << cues[i].text << "\n\n";
    }
    return true;
}

ExportManager::ExportManager() {
    load_format_presets();
    for (size_t i = 0; i < m_presets.size(); ++i) {
        if (m_presets[i].id == "1080p_full_hd" || (m_presets[i].width == 1920 && m_presets[i].height == 1080)) {
            m_selected_preset_idx = static_cast<int>(i);
            m_config.width = m_presets[i].width;
            m_config.height = m_presets[i].height;
            m_config.fps = m_presets[i].fps;
            m_config.format = m_presets[i].format;
            break;
        }
    }
}

void ExportManager::load_format_presets() {
    m_presets.clear();

    const char* home = std::getenv("HOME");
    if (!home) return;

    fs::path preset_dir = fs::path(home) / ".config" / "minicut" / "format_presets";
    std::error_code ec;
    fs::create_directories(preset_dir, ec);

    // Default presets definitions
    std::vector<FormatPreset> defaults = {
        { "1080p_full_hd", "1920x1080 (1080p Full HD)", 1920, 1080, 30, "mp4", false },
        { "720p_hd", "1280x720 (720p HD)", 1280, 720, 30, "mp4", false },
        { "4k_uhd", "3840x2160 (4K Ultra HD)", 3840, 2160, 60, "mp4", false },
        { "480p_sd", "854x480 (480p SD)", 854, 480, 30, "mp4", false },
        { "shorts_vertical", "1080x1920 (Vertical / Shorts)", 1080, 1920, 30, "mp4", false },
        { "square", "1080x1080 (Square)", 1080, 1080, 30, "mp4", false },
        { "animated_gif", "Animated GIF (640x360)", 640, 360, 15, "gif", false },
        { "animated_webp", "Animated WebP (800x450)", 800, 450, 24, "webp", false }
    };

    // If directory is empty or missing defaults, seed them
    bool has_files = false;
    if (fs::exists(preset_dir)) {
        for (const auto& entry : fs::directory_iterator(preset_dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                has_files = true;
                break;
            }
        }
    }

    if (!has_files) {
        for (const auto& d : defaults) {
            fs::path p = preset_dir / (d.id + ".json");
            nlohmann::json j;
            j["id"] = d.id;
            j["name"] = d.name;
            j["width"] = d.width;
            j["height"] = d.height;
            j["fps"] = d.fps;
            j["format"] = d.format;
            j["is_custom"] = false;
            try {
                std::ofstream ofs(p);
                if (ofs.is_open()) ofs << j.dump(2);
            } catch (...) {}
        }
    }

    // Load all presets from directory
    try {
        for (const auto& entry : fs::directory_iterator(preset_dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                try {
                    std::ifstream ifs(entry.path());
                    if (ifs.is_open()) {
                        nlohmann::json j;
                        ifs >> j;
                        FormatPreset preset;
                        preset.id = j.value("id", entry.path().stem().string());
                        preset.name = j.value("name", entry.path().stem().string());
                        preset.width = j.value("width", 1280);
                        preset.height = j.value("height", 720);
                        preset.fps = j.value("fps", 30);
                        preset.format = j.value("format", "mp4");
                        preset.is_custom = j.value("is_custom", true);
                        m_presets.push_back(preset);
                    }
                } catch (...) {}
            }
        }
    } catch (...) {}

    if (m_presets.empty()) {
        m_presets = defaults;
    }

    // Sort: built-ins first, then custom
    std::sort(m_presets.begin(), m_presets.end(), [](const FormatPreset& a, const FormatPreset& b) {
        if (a.is_custom != b.is_custom) return !a.is_custom;
        return a.name < b.name;
    });

    if (m_selected_preset_idx >= static_cast<int>(m_presets.size())) {
        m_selected_preset_idx = 0;
    }
}

bool ExportManager::save_format_preset(const std::string& name, int width, int height, int fps, const std::string& format) {
    if (name.empty()) return false;

    const char* home = std::getenv("HOME");
    if (!home) return false;

    fs::path preset_dir = fs::path(home) / ".config" / "minicut" / "format_presets";
    std::error_code ec;
    fs::create_directories(preset_dir, ec);

    std::string clean_id;
    for (char c : name) {
        if (std::isalnum(c) || c == '_' || c == '-') clean_id += c;
        else if (c == ' ') clean_id += '_';
    }
    if (clean_id.empty()) clean_id = "custom_preset";

    fs::path p = preset_dir / (clean_id + ".json");
    nlohmann::json j;
    j["id"] = clean_id;
    j["name"] = name;
    j["width"] = width;
    j["height"] = height;
    j["fps"] = fps;
    j["format"] = format;
    j["is_custom"] = true;

    try {
        std::ofstream ofs(p);
        if (ofs.is_open()) {
            ofs << j.dump(2);
            load_format_presets();
            for (size_t i = 0; i < m_presets.size(); ++i) {
                if (m_presets[i].id == clean_id) {
                    m_selected_preset_idx = static_cast<int>(i);
                    break;
                }
            }
            return true;
        }
    } catch (...) {}
    return false;
}

bool ExportManager::delete_format_preset(int index) {
    if (index < 0 || index >= static_cast<int>(m_presets.size())) return false;

    const auto& preset = m_presets[index];
    const char* home = std::getenv("HOME");
    if (!home) return false;

    fs::path preset_dir = fs::path(home) / ".config" / "minicut" / "format_presets";
    fs::path p = preset_dir / (preset.id + ".json");
    std::error_code ec;
    fs::remove(p, ec);

    load_format_presets();
    return true;
}

ExportManager::~ExportManager() {
    cancel_export();
    cancel_batch_processing();
    if (m_worker_thread.joinable()) {
        m_worker_thread.join();
    }
    if (m_batch_thread.joinable()) {
        m_batch_thread.join();
    }
}

std::string ExportManager::get_error_message() {
    std::lock_guard<std::mutex> lock(m_status_mutex);
    return m_error_message;
}

bool ExportManager::export_headless_sync(const Project& project, const ExportConfig& config,
                                        const std::function<void(float progress, const std::string& status)>& progress_cb) {
    if (m_is_exporting.load()) {
        return false;
    }

    if (m_worker_thread.joinable()) {
        m_worker_thread.join();
    }

    m_config = config;
    m_progress.store(0.0f);
    m_audio_progress.store(0.0f);
    m_current_frame.store(0);
    m_total_frames.store(0);
    m_is_finished.store(false);
    m_is_error.store(false);
    m_cancel_requested.store(false);
    m_is_exporting.store(true);

    {
        std::lock_guard<std::mutex> lock(m_status_mutex);
        m_status_text = "Starting export pipeline...";
        m_error_message.clear();
    }

    std::thread worker(&ExportManager::export_thread_func, this, project, config);
    float last_reported = -1.0f;
    std::string last_status = "";
    while (m_is_exporting.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        float p = m_progress.load();
        std::string s = get_status_text();
        if (progress_cb && (std::abs(p - last_reported) >= 0.01f || s != last_status)) {
            last_reported = p;
            last_status = s;
            progress_cb(p, s);
        }
    }

    if (worker.joinable()) {
        worker.join();
    }

    if (progress_cb) {
        progress_cb(m_progress.load(), get_status_text());
    }

    return m_is_finished.load() && !m_is_error.load();
}

void ExportManager::add_batch_job(const std::string& project_path, const std::string& output_path,
                                  const std::string& format, int width, int height, int fps) {
    std::lock_guard<std::mutex> lock(m_batch_mutex);
    BatchJob job;
    job.id = m_next_batch_job_id++;
    job.project_path = project_path;
    if (output_path.empty()) {
        fs::path p(project_path);
        std::string stem = p.stem().string();
        std::string ext = "." + format;
        if (!p.parent_path().empty()) {
            job.output_path = (p.parent_path() / (stem + ext)).string();
        } else {
            job.output_path = stem + ext;
        }
    } else {
        job.output_path = output_path;
    }
    job.format = format;
    job.width = width;
    job.height = height;
    job.fps = fps;
    job.status = BatchJob::Status::Queued;
    job.progress = 0.0f;
    m_batch_jobs.push_back(job);
}

bool ExportManager::edit_batch_job(int job_id, const std::string& project_path, const std::string& output_path,
                                   const std::string& format, int width, int height, int fps) {
    std::lock_guard<std::mutex> lock(m_batch_mutex);
    for (auto& job : m_batch_jobs) {
        if (job.id == job_id) {
            if (job.status == BatchJob::Status::Processing) return false;
            job.project_path = project_path;
            job.output_path = output_path;
            job.format = format;
            job.width = width;
            job.height = height;
            job.fps = fps;
            return true;
        }
    }
    return false;
}

bool ExportManager::delete_batch_job(int job_id) {
    std::lock_guard<std::mutex> lock(m_batch_mutex);
    auto it = std::remove_if(m_batch_jobs.begin(), m_batch_jobs.end(), [&](const BatchJob& j) {
        return j.id == job_id && j.status != BatchJob::Status::Processing;
    });
    if (it != m_batch_jobs.end()) {
        m_batch_jobs.erase(it, m_batch_jobs.end());
        if (m_selected_batch_job_id == job_id) {
            m_selected_batch_job_id = -1;
        }
        return true;
    }
    return false;
}

void ExportManager::clear_batch_jobs() {
    std::lock_guard<std::mutex> lock(m_batch_mutex);
    if (m_is_batch_running.load()) return;
    m_batch_jobs.clear();
    m_selected_batch_job_id = -1;
}

float ExportManager::get_batch_overall_progress() const {
    std::lock_guard<std::mutex> lock(m_batch_mutex);
    if (m_batch_jobs.empty()) return 0.0f;
    float sum = 0.0f;
    for (const auto& job : m_batch_jobs) {
        if (job.status == BatchJob::Status::Completed) {
            sum += 1.0f;
        } else if (job.status == BatchJob::Status::Processing) {
            sum += job.progress;
        }
    }
    return sum / static_cast<float>(m_batch_jobs.size());
}

void ExportManager::start_batch_processing() {
    if (m_is_batch_running.load() || m_is_exporting.load()) return;
    if (m_batch_thread.joinable()) {
        m_batch_thread.join();
    }
    m_is_batch_running.store(true);
    m_batch_cancel_requested.store(false);
    m_batch_thread = std::thread(&ExportManager::batch_thread_func, this);
}

void ExportManager::cancel_batch_processing() {
    m_batch_cancel_requested.store(true);
    cancel_export();
}

void ExportManager::batch_thread_func() {
    m_is_batch_running.store(true);
    m_batch_cancel_requested.store(false);

    for (size_t i = 0; i < m_batch_jobs.size(); ++i) {
        if (m_batch_cancel_requested.load()) {
            std::lock_guard<std::mutex> lock(m_batch_mutex);
            if (m_batch_jobs[i].status == BatchJob::Status::Queued) {
                m_batch_jobs[i].status = BatchJob::Status::Cancelled;
            }
            continue;
        }

        BatchJob current_job;
        {
            std::lock_guard<std::mutex> lock(m_batch_mutex);
            if (m_batch_jobs[i].status != BatchJob::Status::Queued) {
                continue;
            }
            m_batch_jobs[i].status = BatchJob::Status::Processing;
            m_batch_jobs[i].progress = 0.0f;
            m_current_batch_job_id.store(m_batch_jobs[i].id);
            current_job = m_batch_jobs[i];
        }

        if (!fs::exists(current_job.project_path)) {
            std::lock_guard<std::mutex> lock(m_batch_mutex);
            m_batch_jobs[i].status = BatchJob::Status::Failed;
            m_batch_jobs[i].error_message = "Project file does not exist: " + current_job.project_path;
            continue;
        }

        std::ifstream ifs(current_job.project_path);
        if (!ifs.is_open()) {
            std::lock_guard<std::mutex> lock(m_batch_mutex);
            m_batch_jobs[i].status = BatchJob::Status::Failed;
            m_batch_jobs[i].error_message = "Could not open project file";
            continue;
        }

        std::string json_str((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        Project sub_project;
        if (!ProjectSerializer::deserialize_json(sub_project, json_str)) {
            std::lock_guard<std::mutex> lock(m_batch_mutex);
            m_batch_jobs[i].status = BatchJob::Status::Failed;
            m_batch_jobs[i].error_message = "Failed to deserialize project JSON";
            continue;
        }

        ExportConfig cfg;
        cfg.output_path = current_job.output_path;
        cfg.format = current_job.format;
        cfg.width = current_job.width;
        cfg.height = current_job.height;
        cfg.fps = current_job.fps;

        int job_idx = static_cast<int>(i);
        bool ok = export_headless_sync(sub_project, cfg, [this, job_idx](float p, const std::string& st) {
            std::lock_guard<std::mutex> lock(m_batch_mutex);
            if (job_idx < static_cast<int>(m_batch_jobs.size())) {
                m_batch_jobs[job_idx].progress = p;
            }
        });

        {
            std::lock_guard<std::mutex> lock(m_batch_mutex);
            if (m_batch_cancel_requested.load()) {
                m_batch_jobs[i].status = BatchJob::Status::Cancelled;
            } else if (ok) {
                m_batch_jobs[i].status = BatchJob::Status::Completed;
                m_batch_jobs[i].progress = 1.0f;
            } else {
                m_batch_jobs[i].status = BatchJob::Status::Failed;
                m_batch_jobs[i].error_message = get_error_message();
            }
        }
    }

    m_current_batch_job_id.store(0);
    m_is_batch_running.store(false);
}

void ExportManager::set_output_path(const std::string& path) {
    if (path.empty()) return;
    fs::path p(path);
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext == ".gif") {
        m_config.format = "gif";
    } else if (ext == ".webp") {
        m_config.format = "webp";
    } else if (ext == ".mp4") {
        m_config.format = "mp4";
    } else if (ext.empty()) {
        p.replace_extension("." + m_config.format);
    }
    strncpy(m_output_path_buf, p.string().c_str(), sizeof(m_output_path_buf) - 1);
    m_config.output_path = p.string();
}

std::string ExportManager::get_status_text() {
    std::lock_guard<std::mutex> lock(m_status_mutex);
    return m_status_text;
}

bool ExportManager::start_export(const Project& project, const ExportConfig& config) {
    if (m_is_exporting.load()) return false;

    m_config = config;
    m_audio_progress.store(0.10f);
    m_progress.store(0.025f);
    m_current_frame.store(0);
    m_total_frames.store(0);
    m_is_finished.store(false);
    m_is_error.store(false);
    m_cancel_requested.store(false);
    m_is_exporting.store(true);

    {
        std::lock_guard<std::mutex> lock(m_status_mutex);
        m_status_text = "Mixing timeline audio: 10%";
        m_error_message.clear();
    }

    if (m_worker_thread.joinable()) {
        m_worker_thread.join();
    }

    // Pass deep copy of project to worker thread
    m_worker_thread = std::thread(&ExportManager::export_thread_func, this, project, config);
    return true;
}

void ExportManager::cancel_export() {
    if (m_is_exporting.load()) {
        m_cancel_requested.store(true);
        {
            std::lock_guard<std::mutex> lock(m_status_mutex);
            m_status_text = "Cancelling export...";
        }
    }
}

void ExportManager::export_thread_func(Project project_snapshot, ExportConfig config) {
    double total_duration = 0.0;
    if (config.auto_length) {
        total_duration = project_snapshot.get_assets_duration();
        if (total_duration <= 0.0) total_duration = 5.0;
    } else {
        total_duration = (config.manual_duration > 0.0) ? config.manual_duration : project_snapshot.get_assets_duration();
        if (total_duration <= 0.0) total_duration = 5.0;
    }

    int total_frames = std::max(1, static_cast<int>(std::ceil(total_duration * config.fps)));
    m_total_frames.store(total_frames);

    fs::path temp_dir = TempManager::get_temp_dir();
    fs::path temp_audio_path = temp_dir / ("minicut_audio_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) + ".wav");

    // ------------------------------------------------------------------------
    // Step 1: Render and Mix Timeline Audio
    // ------------------------------------------------------------------------
    {
        std::lock_guard<std::mutex> lock(m_status_mutex);
        m_status_text = "Mixing timeline audio: 10%";
    }

    // Check if any active audio track exists
    bool has_audio_tracks = false;
    for (const auto& track : project_snapshot.tracks) {
        if (track.type == "audio" && !track.muted && !track.clips.empty()) {
            has_audio_tracks = true;
            break;
        }
    }

    // Generate WAV file if audio exists
    bool audio_rendered = false;
    if (has_audio_tracks) {
        std::map<std::string, std::vector<float>> audio_buffers;
        for (const auto& track : project_snapshot.tracks) {
            if (track.type != "audio" || track.muted) continue;
            for (const auto& clip : track.clips) {
                std::string path;
                const auto* a = project_snapshot.find_audio_asset(clip.asset_id);
                if (a) {
                    path = a->path;
                } else {
                    const auto* v = project_snapshot.find_video_asset(clip.asset_id);
                    if (v && (v->has_audio || !v->is_image)) path = v->path;
                }

                if (!path.empty() && audio_buffers.find(path) == audio_buffers.end()) {
                    fs::path pcm_temp = temp_dir / ("minicut_clip_" + std::to_string(audio_buffers.size()) + ".raw");
                    std::string cmd = "ffmpeg -y -i \"" + path + "\" -f f32le -ar 48000 -ac 2 \"" + pcm_temp.string() + "\" > /dev/null 2>&1";
                    if (std::system(cmd.c_str()) == 0) {
                        FILE* f = fopen(pcm_temp.string().c_str(), "rb");
                        if (f) {
                            fseek(f, 0, SEEK_END);
                            long sz = ftell(f);
                            fseek(f, 0, SEEK_SET);
                            std::vector<float> samples(sz / sizeof(float));
                            if (fread(samples.data(), sizeof(float), samples.size(), f) == samples.size()) {
                                audio_buffers[path] = std::move(samples);
                            }
                            fclose(f);
                        }
                        fs::remove(pcm_temp);
                    }
                }
            }
        }

        int64_t total_samples = static_cast<int64_t>(total_duration * 48000.0) * 2;
        std::vector<int16_t> mixed_pcm(total_samples, 0);

        bool has_solo = false;
        for (const auto& track : project_snapshot.tracks) {
            if (track.type == "audio" && track.solo) { has_solo = true; break; }
        }

        m_audio_progress.store(0.0f);
        for (int64_t i = 0; i < total_samples; i += 2) {
            if ((i % 96000) == 0 || i == 0) {
                float a_p = (total_samples > 0) ? (static_cast<float>(i) / static_cast<float>(total_samples)) : 1.0f;
                m_audio_progress.store(a_p);
                m_progress.store(a_p * 0.25f);
                {
                    std::lock_guard<std::mutex> lock(m_status_mutex);
                    m_status_text = "Mixing timeline audio: " + std::to_string(static_cast<int>(a_p * 100.0f)) + "%";
                }
            }
            double current_t = (i / 2) / 48000.0;
            float mix_l = 0.0f;
            float mix_r = 0.0f;

            for (const auto& track : project_snapshot.tracks) {
                if (track.type != "audio" || track.muted) continue;
                if (has_solo && !track.solo) continue;

                for (const auto& clip : track.clips) {
                    if (!clip.contains_time(current_t)) continue;

                    std::string path;
                    const auto* a = project_snapshot.find_audio_asset(clip.asset_id);
                    if (a) {
                        path = a->path;
                    } else {
                        const auto* v = project_snapshot.find_video_asset(clip.asset_id);
                        if (v && (v->has_audio || !v->is_image)) path = v->path;
                    }

                    if (path.empty() || audio_buffers.find(path) == audio_buffers.end()) continue;

                    const auto& buf = audio_buffers[path];
                    double media_sec = clip.media_time(current_t);
                    int64_t s_idx = static_cast<int64_t>(media_sec * 48000.0) * 2;
                    if (s_idx + 1 < static_cast<int64_t>(buf.size())) {
                        double rel_t = clip.clip_relative_time(current_t);
                        double gain = project_snapshot.evaluate_keyframe(clip.id, "volume", rel_t, 1.0);
                        double gain_l = project_snapshot.evaluate_keyframe(clip.id, "volume_left", rel_t, 1.0);
                        double gain_r = project_snapshot.evaluate_keyframe(clip.id, "volume_right", rel_t, 1.0);
                        float eff_l = static_cast<float>(std::clamp(gain * gain_l, 0.0, 4.0));
                        float eff_r = static_cast<float>(std::clamp(gain * gain_r, 0.0, 4.0));
                        mix_l += buf[s_idx + 0] * eff_l;
                        mix_r += buf[s_idx + 1] * eff_r;
                    }
                }
            }

            mixed_pcm[i + 0] = static_cast<int16_t>(std::clamp(mix_l, -1.0f, 1.0f) * 32767.0f);
            mixed_pcm[i + 1] = static_cast<int16_t>(std::clamp(mix_r, -1.0f, 1.0f) * 32767.0f);
        }

        FILE* wav_file = fopen(temp_audio_path.string().c_str(), "wb");
        if (wav_file) {
            uint32_t data_bytes = static_cast<uint32_t>(mixed_pcm.size() * sizeof(int16_t));
            uint32_t file_bytes = 36 + data_bytes;

            fwrite("RIFF", 1, 4, wav_file);
            fwrite(&file_bytes, 4, 1, wav_file);
            fwrite("WAVEfmt ", 1, 8, wav_file);
            uint32_t subchunk1_size = 16;
            uint16_t audio_format = 1;
            uint16_t num_channels = 2;
            uint32_t sample_rate = 48000;
            uint32_t byte_rate = sample_rate * num_channels * sizeof(int16_t);
            uint16_t block_align = num_channels * sizeof(int16_t);
            uint16_t bits_per_sample = 16;
            fwrite(&subchunk1_size, 4, 1, wav_file);
            fwrite(&audio_format, 2, 1, wav_file);
            fwrite(&num_channels, 2, 1, wav_file);
            fwrite(&sample_rate, 4, 1, wav_file);
            fwrite(&byte_rate, 4, 1, wav_file);
            fwrite(&block_align, 2, 1, wav_file);
            fwrite(&bits_per_sample, 2, 1, wav_file);
            fwrite("data", 1, 4, wav_file);
            fwrite(&data_bytes, 4, 1, wav_file);
            fwrite(mixed_pcm.data(), sizeof(int16_t), mixed_pcm.size(), wav_file);
            fclose(wav_file);
            audio_rendered = true;
        }
    } else {
        m_audio_progress.store(1.0f);
    }
    m_audio_progress.store(1.0f);
    m_progress.store(0.25f);

    if (m_cancel_requested.load()) {
        fs::remove(temp_audio_path);
        m_is_exporting.store(false);
        return;
    }

    // ------------------------------------------------------------------------
    // Step 2: Render Composited Video Frames and Encode
    // ------------------------------------------------------------------------
    if (config.format == "gif") {
        std::lock_guard<std::mutex> lock(m_status_mutex);
        m_status_text = "Encoding animated GIF...";
    } else if (config.format == "webp") {
        std::lock_guard<std::mutex> lock(m_status_mutex);
        m_status_text = "Encoding animated WebP...";
    } else {
        std::lock_guard<std::mutex> lock(m_status_mutex);
        m_status_text = "Encoding video with H.264 / AAC...";
    }

    std::string ffmpeg_cmd;
    if (config.format == "gif") {
        ffmpeg_cmd = "ffmpeg -y -f rawvideo -vcodec rawvideo -s " + std::to_string(config.width) + "x" + std::to_string(config.height) +
                     " -pix_fmt rgba -r " + std::to_string(config.fps) + " -i - -c:v gif -loop 0 \"" + config.output_path + "\" > /dev/null 2>&1";
    } else if (config.format == "webp") {
        ffmpeg_cmd = "ffmpeg -y -f rawvideo -vcodec rawvideo -s " + std::to_string(config.width) + "x" + std::to_string(config.height) +
                     " -pix_fmt rgba -r " + std::to_string(config.fps) + " -i - -c:v libwebp -loop 0 -pix_fmt yuva420p \"" + config.output_path + "\" > /dev/null 2>&1";
    } else {
        if (audio_rendered) {
            ffmpeg_cmd = "ffmpeg -y -f rawvideo -vcodec rawvideo -s " + std::to_string(config.width) + "x" + std::to_string(config.height) +
                         " -pix_fmt rgba -r " + std::to_string(config.fps) + " -i - -i \"" + temp_audio_path.string() +
                         "\" -c:v libx264 -pix_fmt yuv420p -b:v " + std::to_string(config.video_bitrate) +
                         " -c:a aac -b:a " + std::to_string(config.audio_bitrate) + " -shortest \"" + config.output_path + "\" > /dev/null 2>&1";
        } else {
            ffmpeg_cmd = "ffmpeg -y -f rawvideo -vcodec rawvideo -s " + std::to_string(config.width) + "x" + std::to_string(config.height) +
                         " -pix_fmt rgba -r " + std::to_string(config.fps) + " -i - -c:v libx264 -pix_fmt yuv420p -b:v " +
                         std::to_string(config.video_bitrate) + " \"" + config.output_path + "\" > /dev/null 2>&1";
        }
    }

    FILE* pipe = popen(ffmpeg_cmd.c_str(), "w");
    if (!pipe) {
        std::lock_guard<std::mutex> lock(m_status_mutex);
        m_is_error.store(true);
        m_error_message = "Failed to launch FFmpeg encoder pipeline.";
        m_is_exporting.store(false);
        fs::remove(temp_audio_path);
        return;
    }

    std::map<std::string, std::unique_ptr<DecodedVideoContext>> video_contexts;
    std::vector<uint8_t> frame_buffer(config.width * config.height * 4);
    std::vector<uint8_t> layer_buffer;

    struct SubtitleCacheEntry {
        int w = 0;
        int h = 0;
        std::vector<uint8_t> rgba;
    };
    std::map<std::string, SubtitleCacheEntry> subtitle_cache;

    bool has_solo_video = false;
    for (const auto& track : project_snapshot.tracks) {
        if (track.type == "video" && track.solo) { has_solo_video = true; break; }
    }

    std::vector<const Track*> sorted_video_tracks;
    for (const auto& track : project_snapshot.tracks) {
        if (track.type != "video" || track.muted) continue;
        if (has_solo_video && !track.solo) continue;
        sorted_video_tracks.push_back(&track);
    }
    std::stable_sort(sorted_video_tracks.begin(), sorted_video_tracks.end(), [](const Track* a, const Track* b) {
        return a->depth < b->depth;
    });

    for (int frame_idx = 0; frame_idx < total_frames; ++frame_idx) {
        if (m_cancel_requested.load()) break;

        double current_t = frame_idx / static_cast<double>(config.fps);

        std::fill(frame_buffer.begin(), frame_buffer.end(), 0);
        for (size_t i = 3; i < frame_buffer.size(); i += 4) {
            frame_buffer[i] = 255;
        }

        for (const Track* track_ptr : sorted_video_tracks) {
            const Track& track = *track_ptr;

            for (const auto& clip : track.clips) {
                if (!clip.contains_time(current_t)) continue;

                const VideoAsset* va = project_snapshot.find_video_asset(clip.asset_id);
                if (!va || va->path.empty()) continue;

                if (video_contexts.find(va->path) == video_contexts.end()) {
                    auto ctx = std::make_unique<DecodedVideoContext>();
                    if (ctx->open(va->path)) {
                        video_contexts[va->path] = std::move(ctx);
                    }
                }

                auto it = video_contexts.find(va->path);
                if (it != video_contexts.end()) {
                    double rel_time = clip.clip_relative_time(current_t);
                    double media_time = clip.media_time(current_t);

                    double pos_x   = project_snapshot.get_clip_param(clip, "pos_x", rel_time);
                    double pos_y   = project_snapshot.get_clip_param(clip, "pos_y", rel_time);
                    double rot     = project_snapshot.get_clip_param(clip, "rotation", rel_time);
                    double scale_x = project_snapshot.get_clip_param(clip, "scale_x", rel_time) * va->scale_x;
                    double scale_y = project_snapshot.get_clip_param(clip, "scale_y", rel_time) * va->scale_y;
                    double opacity = project_snapshot.get_clip_param(clip, "opacity", rel_time);
                    opacity = std::clamp(opacity, 0.0, 1.0);

                    bool has_spatial_transform = (std::abs(pos_x) > 1e-3 || std::abs(pos_y) > 1e-3 ||
                                                  std::abs(rot) > 1e-3 || std::abs(scale_x - 1.0) > 1e-3 ||
                                                  std::abs(scale_y - 1.0) > 1e-3);

                    if (va->is_image || has_spatial_transform) {
                        int src_w = it->second->width > 0 ? it->second->width : config.width;
                        int src_h = it->second->height > 0 ? it->second->height : config.height;
                        if (it->second->decode_frame_at(media_time, src_w, src_h, layer_buffer)) {
                            blit_transformed_layer(
                                frame_buffer, config.width, config.height,
                                layer_buffer, src_w, src_h,
                                pos_x, pos_y, rot, scale_x, scale_y, opacity
                            );
                        }
                    } else {
                        if (it->second->decode_frame_at(media_time, config.width, config.height, layer_buffer)) {
                            float alpha = static_cast<float>(opacity);
                            for (size_t i = 0; i < frame_buffer.size(); i += 4) {
                                float src_a = (layer_buffer[i + 3] / 255.0f) * alpha;
                                float inv_a = 1.0f - src_a;

                                frame_buffer[i + 0] = static_cast<uint8_t>(layer_buffer[i + 0] * src_a + frame_buffer[i + 0] * inv_a);
                                frame_buffer[i + 1] = static_cast<uint8_t>(layer_buffer[i + 1] * src_a + frame_buffer[i + 1] * inv_a);
                                frame_buffer[i + 2] = static_cast<uint8_t>(layer_buffer[i + 2] * src_a + frame_buffer[i + 2] * inv_a);
                            }
                        }
                    }
                }
            }
        }

        // Subtitle rendering if not exporting as separate .srt sidecar
        if (!config.export_srt) {
            for (const auto& track : project_snapshot.tracks) {
                if (track.type != "subtitle" || track.muted) continue;
                for (const auto& clip : track.clips) {
                    if (clip.contains_time(current_t) && !clip.subtitle_text.empty()) {
                        auto it = subtitle_cache.find(clip.subtitle_text);
                        if (it == subtitle_cache.end()) {
                            TextConfig sub_cfg;
                            sub_cfg.text = clip.subtitle_text;
                            sub_cfg.font_size = std::clamp(static_cast<float>(config.height) * 0.045f, 18.0f, 64.0f);
                            sub_cfg.use_bg = true;
                            sub_cfg.bg_color[0] = 0.0f; sub_cfg.bg_color[1] = 0.0f; sub_cfg.bg_color[2] = 0.0f; sub_cfg.bg_color[3] = 0.78f;
                            sub_cfg.padding_x = static_cast<int>(sub_cfg.font_size * 0.5f);
                            sub_cfg.padding_y = static_cast<int>(sub_cfg.font_size * 0.25f);
                            sub_cfg.font_color[0] = 1.0f; sub_cfg.font_color[1] = 1.0f; sub_cfg.font_color[2] = 1.0f; sub_cfg.font_color[3] = 1.0f;

                            SubtitleCacheEntry entry;
                            if (TextGenerator::render_to_rgba(sub_cfg, entry.w, entry.h, entry.rgba) && entry.w > 0 && entry.h > 0) {
                                it = subtitle_cache.emplace(clip.subtitle_text, std::move(entry)).first;
                            }
                        }

                        if (it != subtitle_cache.end() && !it->second.rgba.empty()) {
                            const auto& sub = it->second;
                            int sub_x = (config.width - sub.w) / 2;
                            int sub_y = config.height - sub.h - static_cast<int>(config.height * 0.05f);

                            for (int sy = 0; sy < sub.h; ++sy) {
                                int fy = sub_y + sy;
                                if (fy < 0 || fy >= config.height) continue;
                                for (int sx = 0; sx < sub.w; ++sx) {
                                    int fx = sub_x + sx;
                                    if (fx < 0 || fx >= config.width) continue;
                                    size_t s_idx = (sy * sub.w + sx) * 4;
                                    size_t f_idx = (fy * config.width + fx) * 4;
                                    float a = sub.rgba[s_idx + 3] / 255.0f;
                                    float inv_a = 1.0f - a;
                                    frame_buffer[f_idx + 0] = static_cast<uint8_t>(sub.rgba[s_idx + 0] * a + frame_buffer[f_idx + 0] * inv_a);
                                    frame_buffer[f_idx + 1] = static_cast<uint8_t>(sub.rgba[s_idx + 1] * a + frame_buffer[f_idx + 1] * inv_a);
                                    frame_buffer[f_idx + 2] = static_cast<uint8_t>(sub.rgba[s_idx + 2] * a + frame_buffer[f_idx + 2] * inv_a);
                                }
                            }
                        }
                    }
                }
            }
        }

        size_t written = fwrite(frame_buffer.data(), 1, frame_buffer.size(), pipe);
        if (written != frame_buffer.size()) {
            std::cerr << "Warning: Pipe write incomplete on frame " << frame_idx << std::endl;
        }

        m_current_frame.store(frame_idx + 1);
        float v_p = (total_frames > 0) ? (static_cast<float>(frame_idx + 1) / static_cast<float>(total_frames)) : 1.0f;
        m_progress.store(0.25f + v_p * 0.75f);
        if ((frame_idx % 15) == 0 || frame_idx == total_frames - 1) {
            std::lock_guard<std::mutex> lock(m_status_mutex);
            m_status_text = "Encoding video frames: " + std::to_string(frame_idx + 1) + " / " + std::to_string(total_frames);
        }
    }

    pclose(pipe);
    fs::remove(temp_audio_path);

    if (config.export_srt && !m_cancel_requested.load()) {
        fs::path srt_path = fs::path(config.output_path).replace_extension(".srt");
        write_srt_file(srt_path.string(), project_snapshot);
    }

    if (m_cancel_requested.load()) {
        fs::remove(config.output_path);
        std::lock_guard<std::mutex> lock(m_status_mutex);
        m_status_text = "Export cancelled.";
        m_is_exporting.store(false);
    } else {
        std::lock_guard<std::mutex> lock(m_status_mutex);
        m_status_text = "Export completed successfully!";
        m_audio_progress.store(1.0f);
        m_progress.store(1.0f);
        m_is_finished.store(true);
        m_is_exporting.store(false);
    }
}

void ExportManager::render_settings_contents(const std::function<void(const ExportConfig& cfg)>& on_start_export) {
    ImGui::Text("Render and encode timeline to MP4, Animated GIF, or WebP");
    ImGui::Separator();

    // Destination Path with Browse button
    ImGui::Text("Destination File:");
    float input_w = ImGui::GetContentRegionAvail().x - 90.0f;
    if (input_w < 150.0f) input_w = 260.0f;
    ImGui::SetNextItemWidth(input_w);
    ImGui::InputText("##OutputPath", m_output_path_buf, sizeof(m_output_path_buf));
    ImGui::SameLine();
    if (ImGui::Button("Browse...##ExportPath")) {
        if (m_on_browse) m_on_browse();
    }

    ImGui::Spacing();

    // 1. Format Selection
    const char* formats[] = { "MP4 Video (.mp4)", "Animated GIF (.gif)", "Animated WebP (.webp)" };
    int fmt_idx = 0;
    if (m_config.format == "gif") fmt_idx = 1;
    else if (m_config.format == "webp") fmt_idx = 2;

    if (ImGui::Combo("Export Format", &fmt_idx, formats, 3)) {
        if (fmt_idx == 1) m_config.format = "gif";
        else if (fmt_idx == 2) m_config.format = "webp";
        else m_config.format = "mp4";

        // Automatically update destination file extension
        std::string target_ext = "." + m_config.format;
        fs::path p(m_output_path_buf);
        p.replace_extension(target_ext);
        strncpy(m_output_path_buf, p.string().c_str(), sizeof(m_output_path_buf) - 1);
    }

    // 2. Presets Combobox
    if (!m_presets.empty()) {
        std::vector<const char*> preset_labels;
        for (const auto& p : m_presets) {
            preset_labels.push_back(p.name.c_str());
        }

        if (ImGui::Combo("Preset", &m_selected_preset_idx, preset_labels.data(), static_cast<int>(preset_labels.size()))) {
            const auto& sel = m_presets[m_selected_preset_idx];
            m_config.width = sel.width;
            m_config.height = sel.height;
            m_config.fps = sel.fps;
            m_config.format = sel.format;

            std::string target_ext = "." + m_config.format;
            fs::path p(m_output_path_buf);
            p.replace_extension(target_ext);
            strncpy(m_output_path_buf, p.string().c_str(), sizeof(m_output_path_buf) - 1);
        }

        // Preset Management Buttons
        if (ImGui::Button("+ Save Preset...")) {
            m_show_save_preset_dialog = true;
            snprintf(m_new_preset_name_buf, sizeof(m_new_preset_name_buf), "%dx%d Custom", m_config.width, m_config.height);
        }
        ImGui::SameLine();
        bool can_del = (m_selected_preset_idx >= 0 && m_selected_preset_idx < static_cast<int>(m_presets.size()) && m_presets[m_selected_preset_idx].is_custom);
        if (!can_del) ImGui::BeginDisabled();
        if (ImGui::Button("Delete Preset")) {
            delete_format_preset(m_selected_preset_idx);
        }
        if (!can_del) ImGui::EndDisabled();
    }

    ImGui::Spacing();

    // 3. Separate Width and Height Inputs
    ImGui::Text("Dimensions (px):");
    ImGui::SetNextItemWidth(120.0f);
    if (ImGui::InputInt("Width", &m_config.width)) {
        if (m_config.width < 64) m_config.width = 64;
        if (m_config.width > 7680) m_config.width = 7680;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    if (ImGui::InputInt("Height", &m_config.height)) {
        if (m_config.height < 64) m_config.height = 64;
        if (m_config.height > 7680) m_config.height = 7680;
    }

    // Framerate Combo
    const char* fps_options[] = { "30 FPS", "60 FPS", "24 FPS", "15 FPS" };
    int fps_idx = 0;
    if (m_config.fps == 60) fps_idx = 1;
    else if (m_config.fps == 24) fps_idx = 2;
    else if (m_config.fps == 15) fps_idx = 3;

    ImGui::SetNextItemWidth(140.0f);
    if (ImGui::Combo("Framerate", &fps_idx, fps_options, 4)) {
        if (fps_idx == 0) m_config.fps = 30;
        else if (fps_idx == 1) m_config.fps = 60;
        else if (fps_idx == 2) m_config.fps = 24;
        else if (fps_idx == 3) m_config.fps = 15;
    }

    ImGui::Spacing();
    ImGui::Checkbox("Export .srt (External subtitle sidecar file instead of burning subtitles into video)", &m_config.export_srt);

    ImGui::Spacing();
    ImGui::Checkbox("Auto-length (Render to end of assets)", &m_config.auto_length);
    if (!m_config.auto_length) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0f);
        float dur_val = static_cast<float>(m_config.manual_duration);
        if (dur_val <= 0.0f) dur_val = 10.0f;
        if (ImGui::DragFloat("End Time (s)", &dur_val, 0.5f, 0.1f, 3600.0f, "%.2fs")) {
            m_config.manual_duration = std::max(0.1, static_cast<double>(dur_val));
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    if (ImGui::Button("Start Export", ImVec2(120, 28))) {
        // Ensure extension matches chosen format
        fs::path p(m_output_path_buf);
        std::string ext = p.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (m_config.format == "gif" && ext != ".gif") {
            p.replace_extension(".gif");
        } else if (m_config.format == "webp" && ext != ".webp") {
            p.replace_extension(".webp");
        } else if (m_config.format == "mp4" && ext != ".mp4") {
            p.replace_extension(".mp4");
        }
        m_config.output_path = p.string();
        strncpy(m_output_path_buf, p.string().c_str(), sizeof(m_output_path_buf) - 1);

        // Ensure even dimensions for video codecs
        if (m_config.width % 2 != 0) m_config.width++;
        if (m_config.height % 2 != 0) m_config.height++;
        if (on_start_export) {
            on_start_export(m_config);
        }
        m_show_dialog = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(90, 28))) {
        m_show_dialog = false;
    }

    // Modal dialog to save new format preset
    if (m_show_save_preset_dialog) {
        ImGui::OpenPopup("Save Format Preset");
    }
    if (ImGui::BeginPopupModal("Save Format Preset", &m_show_save_preset_dialog, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Enter name for this format preset:");
        ImGui::InputText("##NewPresetName", m_new_preset_name_buf, sizeof(m_new_preset_name_buf));
        ImGui::TextDisabled("Resolution: %dx%d @ %dfps (%s)", m_config.width, m_config.height, m_config.fps, m_config.format.c_str());

        ImGui::Spacing();
        if (ImGui::Button("Save", ImVec2(90, 26))) {
            if (strlen(m_new_preset_name_buf) > 0) {
                save_format_preset(m_new_preset_name_buf, m_config.width, m_config.height, m_config.fps, m_config.format);
            }
            m_show_save_preset_dialog = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel##CancelPresetSave", ImVec2(90, 26))) {
            m_show_save_preset_dialog = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void ExportManager::render_progress_contents() {
    std::string status;
    {
        std::lock_guard<std::mutex> lock(m_status_mutex);
        status = m_status_text;
    }

    ImGui::TextColored(ImVec4(0.24f, 0.68f, 0.91f, 1.0f), "Pipeline: %s", status.c_str());
    ImGui::Spacing();

    // 1. Audio Mixing Progress
    float a_prog = m_audio_progress.load();
    char audio_buf[64];
    snprintf(audio_buf, sizeof(audio_buf), "Audio: %.0f%%", a_prog * 100.0f);
    ImGui::Text("Audio Mixing (Stereo & Keyframing):");
    ImGui::ProgressBar(a_prog, ImVec2(-1, 18), audio_buf);

    // 2. Video Rendering & Encoding Progress
    float v_prog = (m_total_frames.load() > 0) ? (static_cast<float>(m_current_frame.load()) / static_cast<float>(m_total_frames.load())) : 0.0f;
    char video_buf[64];
    snprintf(video_buf, sizeof(video_buf), "Video: %.0f%% (%d / %d frames)", v_prog * 100.0f, m_current_frame.load(), m_total_frames.load());
    ImGui::Text("Video Rendering & Encoding:");
    ImGui::ProgressBar(v_prog, ImVec2(-1, 18), video_buf);

    // 3. Overall Export Progress
    float p = m_progress.load();
    char total_buf[64];
    snprintf(total_buf, sizeof(total_buf), "Total: %.1f%%", p * 100.0f);
    ImGui::Spacing();
    ImGui::ProgressBar(p, ImVec2(-1, 24), total_buf);

    ImGui::Spacing();
    ImGui::Separator();

    if (m_is_exporting.load()) {
        if (ImGui::Button("Cancel Export", ImVec2(120, 26))) {
            cancel_export();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(You can drag this window away and continue editing)");
    } else if (m_is_finished.load()) {
        ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.3f, 1.0f), "Video saved to: %s", m_config.output_path.c_str());
        if (ImGui::Button("Close", ImVec2(100, 26))) {
            m_is_finished.store(false);
        }
    } else if (m_is_error.load()) {
        ImGui::TextColored(ImVec4(0.9f, 0.2f, 0.2f, 1.0f), "Error: %s", m_error_message.c_str());
        if (ImGui::Button("Close", ImVec2(100, 26))) {
            m_is_error.store(false);
        }
    }
}

void ExportManager::render_batch_contents() {
    ImGui::Text("Queue .minicut projects to render multiple videos sequentially");
    ImGui::Separator();

    // Top buttons: + Add Project, Edit Job, Delete Job, Clear Queue
    bool running = m_is_batch_running.load();
    if (running) ImGui::BeginDisabled();

    if (ImGui::Button("+ Add Project...")) {
        m_show_add_batch_dialog = true;
        m_batch_input_project[0] = '\0';
        m_batch_input_output[0] = '\0';
        m_batch_input_width = 1920;
        m_batch_input_height = 1080;
        m_batch_input_fps = 30;
        m_batch_input_fmt_idx = 0;
    }
    ImGui::SameLine();

    bool has_sel = (m_selected_batch_job_id > 0);
    if (!has_sel) ImGui::BeginDisabled();
    if (ImGui::Button("Edit Job")) {
        std::lock_guard<std::mutex> lock(m_batch_mutex);
        for (const auto& job : m_batch_jobs) {
            if (job.id == m_selected_batch_job_id) {
                strncpy(m_batch_input_project, job.project_path.c_str(), sizeof(m_batch_input_project) - 1);
                strncpy(m_batch_input_output, job.output_path.c_str(), sizeof(m_batch_input_output) - 1);
                m_batch_input_width = job.width;
                m_batch_input_height = job.height;
                m_batch_input_fps = job.fps;
                if (job.format == "gif") m_batch_input_fmt_idx = 1;
                else if (job.format == "webp") m_batch_input_fmt_idx = 2;
                else m_batch_input_fmt_idx = 0;
                m_show_edit_batch_dialog = true;
                break;
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete Job")) {
        delete_batch_job(m_selected_batch_job_id);
    }
    if (!has_sel) ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Clear All")) {
        clear_batch_jobs();
    }
    if (running) ImGui::EndDisabled();

    ImGui::Spacing();

    // Table of batch jobs
    static ImGuiTableFlags batch_table_flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg |
                                               ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_Resizable;
    float batch_table_h = ImGui::GetContentRegionAvail().y - 48.0f;
    if (batch_table_h < 100.0f) batch_table_h = 100.0f;

    if (ImGui::BeginTable("BatchJobsTable", 5, batch_table_flags, ImVec2(0, batch_table_h))) {
        ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 36.0f);
        ImGui::TableSetupColumn("Project File", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Output Destination", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Specs", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("Status / Progress", ImGuiTableColumnFlags_WidthFixed, 130.0f);
        ImGui::TableHeadersRow();

        std::lock_guard<std::mutex> lock(m_batch_mutex);
        for (const auto& job : m_batch_jobs) {
            ImGui::TableNextRow();
            bool is_sel = (m_selected_batch_job_id == job.id);

            ImGui::TableSetColumnIndex(0);
            std::string sel_label = "#" + std::to_string(job.id);
            if (ImGui::Selectable(sel_label.c_str(), is_sel, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowItemOverlap)) {
                m_selected_batch_job_id = job.id;
            }

            ImGui::TableSetColumnIndex(1);
            std::string proj_name = fs::path(job.project_path).filename().string();
            ImGui::TextUnformatted(proj_name.empty() ? job.project_path.c_str() : proj_name.c_str());
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", job.project_path.c_str());
            }

            ImGui::TableSetColumnIndex(2);
            std::string out_name = fs::path(job.output_path).filename().string();
            ImGui::TextUnformatted(out_name.empty() ? job.output_path.c_str() : out_name.c_str());
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", job.output_path.c_str());
            }

            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%dx%d %dfps", job.width, job.height, job.fps);

            ImGui::TableSetColumnIndex(4);
            if (job.status == BatchJob::Status::Queued) {
                ImGui::TextDisabled("Queued");
            } else if (job.status == BatchJob::Status::Processing) {
                char p_buf[32];
                snprintf(p_buf, sizeof(p_buf), "%.0f%%", job.progress * 100.0f);
                ImGui::ProgressBar(job.progress, ImVec2(-1, 16), p_buf);
            } else if (job.status == BatchJob::Status::Completed) {
                ImGui::TextColored(ImVec4(0.2f, 0.85f, 0.3f, 1.0f), "Completed");
            } else if (job.status == BatchJob::Status::Failed) {
                ImGui::TextColored(ImVec4(0.9f, 0.25f, 0.25f, 1.0f), "Failed");
                if (ImGui::IsItemHovered() && !job.error_message.empty()) {
                    ImGui::SetTooltip("%s", job.error_message.c_str());
                }
            } else if (job.status == BatchJob::Status::Cancelled) {
                ImGui::TextColored(ImVec4(0.85f, 0.6f, 0.2f, 1.0f), "Cancelled");
            }
        }

        ImGui::EndTable();
    }

    ImGui::Separator();

    // Bottom execution controls
    if (!running) {
        bool can_start = false;
        {
            std::lock_guard<std::mutex> lock(m_batch_mutex);
            for (const auto& j : m_batch_jobs) {
                if (j.status == BatchJob::Status::Queued) {
                    can_start = true;
                    break;
                }
            }
        }
        if (!can_start) ImGui::BeginDisabled();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.35f, 1.0f));
        if (ImGui::Button("Start Batch Processing", ImVec2(180, 28))) {
            start_batch_processing();
        }
        ImGui::PopStyleColor();
        if (!can_start) ImGui::EndDisabled();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.2f, 0.2f, 1.0f));
        if (ImGui::Button("Cancel Batch", ImVec2(120, 28))) {
            cancel_batch_processing();
        }
        ImGui::PopStyleColor();
        ImGui::SameLine();
        float overall_p = get_batch_overall_progress();
        char overall_buf[64];
        snprintf(overall_buf, sizeof(overall_buf), "Overall: %.0f%%", overall_p * 100.0f);
        ImGui::ProgressBar(overall_p, ImVec2(240, 24), overall_buf);
    }

    // Modal: Add Batch Job
    if (m_show_add_batch_dialog) {
        ImGui::OpenPopup("Add Batch Job");
    }
    if (ImGui::BeginPopupModal("Add Batch Job", &m_show_add_batch_dialog, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Enter Project (.minicut) and Output details:");
        ImGui::Spacing();

        ImGui::Text("Project Path:");
        ImGui::SetNextItemWidth(360.0f);
        if (ImGui::InputText("##BatchProjPath", m_batch_input_project, sizeof(m_batch_input_project))) {
            if (strlen(m_batch_input_output) == 0 && strlen(m_batch_input_project) > 0) {
                fs::path p(m_batch_input_project);
                std::string stem = p.stem().string();
                std::string ext = (m_batch_input_fmt_idx == 1) ? ".gif" : (m_batch_input_fmt_idx == 2) ? ".webp" : ".mp4";
                std::string auto_out;
                if (!p.parent_path().empty()) {
                    auto_out = (p.parent_path() / (stem + ext)).string();
                } else {
                    auto_out = stem + ext;
                }
                strncpy(m_batch_input_output, auto_out.c_str(), sizeof(m_batch_input_output) - 1);
            }
        }

        ImGui::Text("Output Destination:");
        ImGui::SetNextItemWidth(360.0f);
        ImGui::InputText("##BatchOutPath", m_batch_input_output, sizeof(m_batch_input_output));

        const char* fmt_opts[] = { "MP4 Video (.mp4)", "Animated GIF (.gif)", "Animated WebP (.webp)" };
        ImGui::Text("Format:");
        ImGui::SetNextItemWidth(200.0f);
        if (ImGui::Combo("##BatchFormat", &m_batch_input_fmt_idx, fmt_opts, 3)) {
            std::string ext = (m_batch_input_fmt_idx == 1) ? ".gif" : (m_batch_input_fmt_idx == 2) ? ".webp" : ".mp4";
            if (strlen(m_batch_input_output) > 0) {
                fs::path p(m_batch_input_output);
                p.replace_extension(ext);
                strncpy(m_batch_input_output, p.string().c_str(), sizeof(m_batch_input_output) - 1);
            }
        }

        ImGui::Text("Resolution:");
        ImGui::SetNextItemWidth(80.0f);
        ImGui::InputInt("W##BatchW", &m_batch_input_width, 0);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80.0f);
        ImGui::InputInt("H##BatchH", &m_batch_input_height, 0);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80.0f);
        ImGui::InputInt("FPS##BatchFPS", &m_batch_input_fps, 0);

        ImGui::Spacing();
        ImGui::Separator();
        if (ImGui::Button("Add to Queue", ImVec2(110, 26))) {
            if (strlen(m_batch_input_project) > 0) {
                std::string fmt = (m_batch_input_fmt_idx == 1) ? "gif" : (m_batch_input_fmt_idx == 2) ? "webp" : "mp4";
                add_batch_job(m_batch_input_project, m_batch_input_output, fmt, m_batch_input_width, m_batch_input_height, m_batch_input_fps);
            }
            m_show_add_batch_dialog = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel##CancelAddBatch", ImVec2(90, 26))) {
            m_show_add_batch_dialog = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Modal: Edit Batch Job
    if (m_show_edit_batch_dialog) {
        ImGui::OpenPopup("Edit Batch Job");
    }
    if (ImGui::BeginPopupModal("Edit Batch Job", &m_show_edit_batch_dialog, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Edit job settings (ID #%d):", m_selected_batch_job_id);
        ImGui::Spacing();

        ImGui::Text("Project Path:");
        ImGui::SetNextItemWidth(360.0f);
        ImGui::InputText("##EditBatchProjPath", m_batch_input_project, sizeof(m_batch_input_project));

        ImGui::Text("Output Destination:");
        ImGui::SetNextItemWidth(360.0f);
        ImGui::InputText("##EditBatchOutPath", m_batch_input_output, sizeof(m_batch_input_output));

        const char* fmt_opts[] = { "MP4 Video (.mp4)", "Animated GIF (.gif)", "Animated WebP (.webp)" };
        ImGui::Text("Format:");
        ImGui::SetNextItemWidth(200.0f);
        ImGui::Combo("##EditBatchFormat", &m_batch_input_fmt_idx, fmt_opts, 3);

        ImGui::Text("Resolution:");
        ImGui::SetNextItemWidth(80.0f);
        ImGui::InputInt("W##EditBatchW", &m_batch_input_width, 0);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80.0f);
        ImGui::InputInt("H##EditBatchH", &m_batch_input_height, 0);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80.0f);
        ImGui::InputInt("FPS##EditBatchFPS", &m_batch_input_fps, 0);

        ImGui::Spacing();
        ImGui::Separator();
        if (ImGui::Button("Save Changes", ImVec2(110, 26))) {
            if (strlen(m_batch_input_project) > 0 && m_selected_batch_job_id > 0) {
                std::string fmt = (m_batch_input_fmt_idx == 1) ? "gif" : (m_batch_input_fmt_idx == 2) ? "webp" : "mp4";
                edit_batch_job(m_selected_batch_job_id, m_batch_input_project, m_batch_input_output, fmt, m_batch_input_width, m_batch_input_height, m_batch_input_fps);
            }
            m_show_edit_batch_dialog = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel##CancelEditBatch", ImVec2(90, 26))) {
            m_show_edit_batch_dialog = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void ExportManager::render_ui(const std::function<void(const ExportConfig& cfg)>& on_start_export, bool standalone_window) {
    if (standalone_window) {
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                                 ImGuiWindowFlags_NoBringToFrontOnFocus;
        if (ImGui::Begin("ExportManagerOSWindow", nullptr, flags)) {
            if (m_is_exporting.load() || m_is_finished.load() || m_is_error.load()) {
                render_progress_contents();
            } else if (m_show_dialog) {
                if (ImGui::BeginTabBar("ExportTabBar")) {
                    if (ImGui::BeginTabItem("Single Export")) {
                        render_settings_contents(on_start_export);
                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("Batch Processing")) {
                        render_batch_contents();
                        ImGui::EndTabItem();
                    }
                    ImGui::EndTabBar();
                }
            }
        }
        ImGui::End();
        return;
    }

    if (m_show_dialog && !m_is_exporting.load()) {
        if (m_request_focus) {
            ImGui::SetNextWindowFocus();
            m_request_focus = false;
        }

        ImVec2 center = ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f);
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(620, 420), ImGuiCond_Appearing);

        if (ImGui::Begin("Export Timeline Media###ExportDialog", &m_show_dialog, ImGuiWindowFlags_NoCollapse)) {
            if (ImGui::BeginTabBar("ExportTabBar")) {
                if (ImGui::BeginTabItem("Single Export")) {
                    render_settings_contents(on_start_export);
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Batch Processing")) {
                    render_batch_contents();
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
        }
        ImGui::End();
    }

    // Modeless Floating Progress Window (allows continuous editing without fading/blocking background)
    if (m_is_exporting.load() || m_is_finished.load() || m_is_error.load()) {
        ImGui::SetNextWindowSize(ImVec2(480, 240), ImGuiCond_Appearing);
        if (ImGui::Begin("Export Progress###ExportProgress", nullptr, ImGuiWindowFlags_NoCollapse)) {
            render_progress_contents();
            ImGui::End();
        }
    }
}
