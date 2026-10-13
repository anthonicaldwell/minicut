#include "ExportManager.h"
#include "MediaDecoder.h"
#include "TempManager.h"
#include <imgui.h>
#include <filesystem>
#include <iostream>
#include <vector>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <cstring>

namespace fs = std::filesystem;

ExportManager::ExportManager() {}

ExportManager::~ExportManager() {
    cancel_export();
    if (m_worker_thread.joinable()) {
        m_worker_thread.join();
    }
}

void ExportManager::set_output_path(const std::string& path) {
    strncpy(m_output_path_buf, path.c_str(), sizeof(m_output_path_buf) - 1);
    m_config.output_path = path;
}

bool ExportManager::start_export(const Project& project, const ExportConfig& config) {
    if (m_is_exporting.load()) return false;

    m_config = config;
    m_progress.store(0.0f);
    m_current_frame.store(0);
    m_total_frames.store(0);
    m_is_finished.store(false);
    m_is_error.store(false);
    m_cancel_requested.store(false);
    m_is_exporting.store(true);

    {
        std::lock_guard<std::mutex> lock(m_status_mutex);
        m_status_text = "Preparing export snapshot...";
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
    double total_duration = project_snapshot.get_total_duration();
    if (total_duration <= 0.1) total_duration = 5.0;

    int total_frames = std::max(1, static_cast<int>(std::ceil(total_duration * config.fps)));
    m_total_frames.store(total_frames);

    fs::path temp_dir = TempManager::get_temp_dir();
    fs::path temp_audio_path = temp_dir / ("minicut_audio_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) + ".wav");

    // ------------------------------------------------------------------------
    // Step 1: Render and Mix Timeline Audio
    // ------------------------------------------------------------------------
    {
        std::lock_guard<std::mutex> lock(m_status_mutex);
        m_status_text = "Mixing timeline audio...";
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
                    if (v && v->has_audio) path = v->path;
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

        for (int64_t i = 0; i < total_samples; i += 2) {
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
                        if (v && v->has_audio) path = v->path;
                    }

                    if (path.empty() || audio_buffers.find(path) == audio_buffers.end()) continue;

                    const auto& buf = audio_buffers[path];
                    double media_sec = clip.media_time(current_t);
                    int64_t s_idx = static_cast<int64_t>(media_sec * 48000.0) * 2;
                    if (s_idx + 1 < static_cast<int64_t>(buf.size())) {
                        double gain = project_snapshot.evaluate_keyframe(clip.id, "volume", clip.clip_relative_time(current_t), 1.0);
                        gain = std::clamp(gain, 0.0, 2.0);
                        mix_l += buf[s_idx + 0] * static_cast<float>(gain);
                        mix_r += buf[s_idx + 1] * static_cast<float>(gain);
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
    }

    if (m_cancel_requested.load()) {
        fs::remove(temp_audio_path);
        m_is_exporting.store(false);
        return;
    }

    // ------------------------------------------------------------------------
    // Step 2: Render Composited Video Frames and Encode
    // ------------------------------------------------------------------------
    {
        std::lock_guard<std::mutex> lock(m_status_mutex);
        m_status_text = "Encoding video with H.264 / AAC...";
    }

    std::string ffmpeg_cmd;
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

                    double pos_x = project_snapshot.get_clip_param(clip, "pos_x", rel_time);
                    double pos_y = project_snapshot.get_clip_param(clip, "pos_y", rel_time);
                    double rot   = project_snapshot.get_clip_param(clip, "rotation", rel_time);
                    double scale = project_snapshot.get_clip_param(clip, "scale", rel_time);
                    double opacity = project_snapshot.get_clip_param(clip, "opacity", rel_time);
                    opacity = std::clamp(opacity, 0.0, 1.0);

                    bool has_spatial_transform = (std::abs(pos_x) > 1e-3 || std::abs(pos_y) > 1e-3 ||
                                                  std::abs(rot) > 1e-3 || std::abs(scale - 1.0) > 1e-3);

                    if (va->is_image || has_spatial_transform) {
                        int src_w = it->second->width > 0 ? it->second->width : config.width;
                        int src_h = it->second->height > 0 ? it->second->height : config.height;
                        if (it->second->decode_frame_at(media_time, src_w, src_h, layer_buffer)) {
                            blit_transformed_layer(
                                frame_buffer, config.width, config.height,
                                layer_buffer, src_w, src_h,
                                pos_x, pos_y, rot, scale, opacity
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

        size_t written = fwrite(frame_buffer.data(), 1, frame_buffer.size(), pipe);
        if (written != frame_buffer.size()) {
            std::cerr << "Warning: Pipe write incomplete on frame " << frame_idx << std::endl;
        }

        m_current_frame.store(frame_idx + 1);
        m_progress.store(static_cast<float>(frame_idx + 1) / total_frames);
    }

    pclose(pipe);
    fs::remove(temp_audio_path);

    if (m_cancel_requested.load()) {
        fs::remove(config.output_path);
        std::lock_guard<std::mutex> lock(m_status_mutex);
        m_status_text = "Export cancelled.";
        m_is_exporting.store(false);
    } else {
        std::lock_guard<std::mutex> lock(m_status_mutex);
        m_status_text = "Export completed successfully!";
        m_is_finished.store(true);
        m_is_exporting.store(false);
    }
}

void ExportManager::render_ui(const std::function<void(const ExportConfig& cfg)>& on_start_export) {
    if (m_show_dialog && !m_is_exporting.load()) {
        ImVec2 center = ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f);
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(520, 260), ImGuiCond_Appearing);

        if (ImGui::Begin("Export Video (.mp4)###ExportDialog", &m_show_dialog, ImGuiWindowFlags_NoCollapse)) {
            ImGui::Text("Render timeline to H.264 / AAC .mp4");
            ImGui::Separator();

            // Output Path with Browse button
            ImGui::Text("Destination File:");
            float input_w = ImGui::GetContentRegionAvail().x - 90.0f;
            if (input_w < 150.0f) input_w = 260.0f;
            ImGui::SetNextItemWidth(input_w);
            ImGui::InputText("##OutputPath", m_output_path_buf, sizeof(m_output_path_buf));
            ImGui::SameLine();
            if (ImGui::Button("Browse...##ExportPath")) {
                if (m_on_browse) m_on_browse();
            }

            const char* res_options[] = { "1280x720 (720p HD)", "1920x1080 (1080p Full HD)", "854x480 (480p SD)" };
            static int res_idx = 0;
            if (ImGui::Combo("Resolution", &res_idx, res_options, 3)) {
                if (res_idx == 0) { m_config.width = 1280; m_config.height = 720; }
                else if (res_idx == 1) { m_config.width = 1920; m_config.height = 1080; }
                else if (res_idx == 2) { m_config.width = 854; m_config.height = 480; }
            }

            const char* fps_options[] = { "30 FPS", "60 FPS", "24 FPS" };
            static int fps_idx = 0;
            if (ImGui::Combo("Framerate", &fps_idx, fps_options, 3)) {
                if (fps_idx == 0) m_config.fps = 30;
                else if (fps_idx == 1) m_config.fps = 60;
                else if (fps_idx == 2) m_config.fps = 24;
            }

            ImGui::Separator();
            if (ImGui::Button("Start Export", ImVec2(120, 28))) {
                m_config.output_path = m_output_path_buf;
                if (on_start_export) {
                    on_start_export(m_config);
                }
                m_show_dialog = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(90, 28))) {
                m_show_dialog = false;
            }
        }
        ImGui::End();
    }

    // Modal Progress Bar Dialog
    if (m_is_exporting.load() || m_is_finished.load() || m_is_error.load()) {
        ImGui::OpenPopup("Export Progress");
    }

    if (ImGui::BeginPopupModal("Export Progress", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        std::string status;
        {
            std::lock_guard<std::mutex> lock(m_status_mutex);
            status = m_status_text;
        }

        ImGui::Text("%s", status.c_str());
        ImGui::Spacing();

        float p = m_progress.load();
        char buf[64];
        snprintf(buf, sizeof(buf), "%.1f%% (%d / %d frames)", p * 100.0f, m_current_frame.load(), m_total_frames.load());
        ImGui::ProgressBar(p, ImVec2(360, 26), buf);

        ImGui::Spacing();
        ImGui::Separator();

        if (m_is_exporting.load()) {
            if (ImGui::Button("Cancel Export", ImVec2(120, 26))) {
                cancel_export();
            }
        } else if (m_is_finished.load()) {
            ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.3f, 1.0f), "Video saved to: %s", m_config.output_path.c_str());
            if (ImGui::Button("Close", ImVec2(100, 26))) {
                m_is_finished.store(false);
                ImGui::CloseCurrentPopup();
            }
        } else if (m_is_error.load()) {
            ImGui::TextColored(ImVec4(0.9f, 0.2f, 0.2f, 1.0f), "Error: %s", m_error_message.c_str());
            if (ImGui::Button("Close", ImVec2(100, 26))) {
                m_is_error.store(false);
                ImGui::CloseCurrentPopup();
            }
        }

        ImGui::EndPopup();
    }
}
