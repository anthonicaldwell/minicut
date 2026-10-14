#include "TTSDialog.h"
#include "TTSManager.h"
#include "SettingsLogic.h"
#include "TempManager.h"
#include "App.h"
#include "imgui.h"
#include <chrono>

TTSDialog::TTSDialog() = default;
TTSDialog::~TTSDialog() = default;

void TTSDialog::open() {
    m_is_open = true;
    m_request_focus = true;
    m_status_msg.clear();

    std::string saved_voice = SettingsLogic::instance().get_tts_voice();
    static const std::vector<std::string> s_gemini_voices = { "Puck", "Charon", "Kore", "Fenrir", "Aoede" };
    for (size_t i = 0; i < s_gemini_voices.size(); ++i) {
        if (s_gemini_voices[i] == saved_voice) {
            m_gemini_voice_idx = static_cast<int>(i);
            break;
        }
    }
    static const std::vector<std::string> s_system_voices = {
        "default", "en", "en-us", "en-uk", "ja", "ko", "uk", "ar", "es", "fr", "de", "cmn",
        "en+m1", "en+m3", "en+f1", "en+f3"
    };
    for (size_t i = 0; i < s_system_voices.size(); ++i) {
        if (s_system_voices[i] == saved_voice) {
            m_system_voice_idx = static_cast<int>(i);
            break;
        }
    }
}

void TTSDialog::close() {
    m_is_open = false;
}

void TTSDialog::render(App& app) {
    if (!m_is_open) return;

    if (m_request_focus) {
        ImGui::SetNextWindowFocus();
        m_request_focus = false;
    }

    ImVec2 center = ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f);
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(560, 410), ImGuiCond_Appearing);

    if (ImGui::Begin("Text-to-Speech (TTS) Source###TTSDialog", &m_is_open, ImGuiWindowFlags_NoCollapse)) {
        ImGui::Text("Enter text to synthesize spoken audio for Assets and Timeline:");
        ImGui::Spacing();

        ImGui::InputTextMultiline("##TTSText", m_text_buf, sizeof(m_text_buf), ImVec2(-1, 100));

        ImGui::Spacing();
        std::string prov = SettingsLogic::instance().get_tts_provider();
        bool is_ai = (prov == "gemini" && SettingsLogic::instance().is_ai_enabled());
        if (is_ai) {
            ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "Mode: AI Gemini Speech Synthesis (Personal API Key)");
        } else {
            ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.5f, 1.0f), "Mode: Free Local System Synthesis (eSpeak-NG / Speech-Dispatcher)");
        }

        ImGui::Spacing();
        static const std::vector<std::string> s_gemini_voices = { "Puck", "Charon", "Kore", "Fenrir", "Aoede" };
        static const std::vector<std::string> s_system_voices = {
            "default", "en", "en-us", "en-uk", "ja", "ko", "uk", "ar", "es", "fr", "de", "cmn",
            "en+m1", "en+m3", "en+f1", "en+f3"
        };
        std::string chosen_voice;

        if (is_ai) {
            static const char* gemini_names[] = { "Puck (Natural)", "Charon (Deep)", "Kore (Warm)", "Fenrir (Resonant)", "Aoede (Melodic)" };
            ImGui::SetNextItemWidth(240.0f);
            if (ImGui::Combo("Voice Selection", &m_gemini_voice_idx, gemini_names, 5)) {
                SettingsLogic::instance().set_tts_voice(s_gemini_voices[m_gemini_voice_idx]);
                SettingsLogic::instance().save_to_disk();
            }
            chosen_voice = s_gemini_voices[m_gemini_voice_idx];
        } else {
            static const char* system_names[] = {
                "Default (Auto-Detect Language)", "English (en)", "English US (en-us)", "English UK (en-uk)",
                "Japanese / 日本語 (ja)", "Korean / 한국어 (ko)", "Ukrainian / Українська (uk)", "Arabic / العربية (ar)",
                "Spanish / Español (es)", "French / Français (fr)", "German / Deutsch (de)", "Chinese / 中文 (cmn)",
                "Male 1 (en+m1)", "Male 2 (en+m3)", "Female 1 (en+f1)", "Female 2 (en+f3)"
            };
            ImGui::SetNextItemWidth(240.0f);
            if (ImGui::Combo("Voice Selection", &m_system_voice_idx, system_names, 16)) {
                SettingsLogic::instance().set_tts_voice(s_system_voices[m_system_voice_idx]);
                SettingsLogic::instance().save_to_disk();
            }
            chosen_voice = s_system_voices[m_system_voice_idx];
        }

        ImGui::SameLine();
        ImGui::SetNextItemWidth(110.0f);
        ImGui::SliderFloat("Rate", &m_rate, 0.5f, 2.0f, "%.2fx");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110.0f);
        ImGui::SliderFloat("Pitch", &m_pitch, 0.5f, 2.0f, "%.2fx");

        if (!m_status_msg.empty()) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.2f, 1.0f), "%s", m_status_msg.c_str());
        }

        ImGui::Spacing();
        ImGui::Separator();

        bool can_generate = (strlen(m_text_buf) > 0 && !m_is_generating);
        if (!can_generate) ImGui::BeginDisabled();

        if (ImGui::Button("Add to Assets", ImVec2(130, 28))) {
            m_is_generating = true;
            std::string text = m_text_buf;
            float r = m_rate;
            float p = m_pitch;
            std::string voice_to_use = chosen_voice;
            std::string temp_dir = TempManager::get_temp_dir();
            uint64_t ts = static_cast<uint64_t>(std::chrono::system_clock::now().time_since_epoch().count());
            std::string wav_path = temp_dir + "/tts_" + std::to_string(ts) + ".wav";

            TTSManager::instance().synthesize_async(text, wav_path, r, p, voice_to_use, [this, &app, wav_path](bool success, const std::string& path, const std::string& err) {
                m_is_generating = false;
                if (success) {
                    m_status_msg = "Speech synthesized and added to assets!";
                    app.push_undo_state();
                    app.get_clip_manager().import_audio(app.get_project(), wav_path);
                    app.get_audio_engine().update_project_snapshot(app.get_project());
                } else {
                    m_status_msg = "Synthesis error: " + err;
                }
            });
        }

        ImGui::SameLine();
        if (ImGui::Button("Add to Timeline", ImVec2(140, 28))) {
            m_is_generating = true;
            std::string text = m_text_buf;
            float r = m_rate;
            float p = m_pitch;
            std::string voice_to_use = chosen_voice;
            std::string temp_dir = TempManager::get_temp_dir();
            uint64_t ts = static_cast<uint64_t>(std::chrono::system_clock::now().time_since_epoch().count());
            std::string wav_path = temp_dir + "/tts_" + std::to_string(ts) + ".wav";

            TTSManager::instance().synthesize_async(text, wav_path, r, p, voice_to_use, [this, &app, wav_path](bool success, const std::string& path, const std::string& err) {
                m_is_generating = false;
                if (success) {
                    app.push_undo_state();
                    bool imported = app.get_clip_manager().import_audio(app.get_project(), wav_path);
                    if (!imported || app.get_project().audio_assets.empty()) {
                        m_status_msg = "Failed to import synthesized speech audio.";
                        return;
                    }
                    std::string asset_id = app.get_project().audio_assets.back().id;
                    app.get_audio_engine().update_project_snapshot(app.get_project());

                    // Place clip on active or first audio track at playhead
                    double playhead = app.get_playhead_time();
                    const AudioAsset* aa = app.get_project().find_audio_asset(asset_id);
                    double duration = aa ? aa->duration : 3.0;
                    if (duration <= 0.1) duration = 3.0;

                    int target_track = -1;
                    for (size_t i = 0; i < app.get_project().tracks.size(); ++i) {
                        if (app.get_project().tracks[i].type == "audio") {
                            target_track = static_cast<int>(i);
                            break;
                        }
                    }
                    if (target_track < 0) {
                        // Add audio track
                        Track new_trk;
                        new_trk.id = "track_a_" + std::to_string(app.get_project().tracks.size());
                        new_trk.name = "Audio " + std::to_string(app.get_project().tracks.size() + 1);
                        new_trk.type = "audio";
                        app.get_project().tracks.push_back(new_trk);
                        target_track = static_cast<int>(app.get_project().tracks.size() - 1);
                    }

                    Clip clip;
                    clip.id = "clip_tts_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
                    clip.asset_id = asset_id;
                    clip.start_time = playhead;
                    clip.duration = duration;
                    clip.offset = 0.0;
                    app.get_project().tracks[target_track].clips.push_back(clip);
                    app.get_audio_engine().update_project_snapshot(app.get_project());
                } else {
                    m_status_msg = "Synthesis error: " + err;
                }
            });
        }

        if (!can_generate) ImGui::EndDisabled();

        ImGui::SameLine();
        if (ImGui::Button("Close", ImVec2(90, 28))) {
            close();
        }
    }
    ImGui::End();
}
