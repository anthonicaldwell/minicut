#include "SettingsDialog.h"
#include "LocaleManager.h"
#include "render/RenderSystem.h"
#include "App.h"
#include "imgui.h"
#include <cstring>

SettingsDialog::SettingsDialog() = default;
SettingsDialog::~SettingsDialog() = default;

void SettingsDialog::open() {
    m_is_open = true;
    m_key_initialized = false;
    m_request_focus = true;
}

void SettingsDialog::close() {
    m_is_open = false;
}

void SettingsDialog::render(SettingsLogic& logic, App& app) {
    if (!m_is_open) return;

    if (!m_key_initialized) {
        std::string key = logic.get_gemini_api_key();
        strncpy(m_api_key_buf, key.c_str(), sizeof(m_api_key_buf) - 1);
        m_api_key_buf[sizeof(m_api_key_buf) - 1] = '\0';
        m_key_initialized = true;
    }

    if (m_request_focus) {
        ImGui::SetNextWindowFocus();
        m_request_focus = false;
    }

    ImVec2 center = ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f);
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(580, 470), ImGuiCond_Appearing);

    if (ImGui::Begin("Settings###SettingsDialog", &m_is_open, ImGuiWindowFlags_NoCollapse)) {
        if (ImGui::BeginTabBar("SettingsTabs")) {
            // Tab 1: General
            if (ImGui::BeginTabItem("General")) {
                ImGui::Spacing();
                ImGui::TextDisabled("Application & Interface Settings");
                ImGui::Separator();
                ImGui::Spacing();

                // Language selection
                std::string cur_lang = logic.get_language();
                auto available_langs = LocaleManager::instance().get_available_languages();
                int current_idx = 0;
                for (size_t i = 0; i < available_langs.size(); ++i) {
                    if (available_langs[i].code == cur_lang) {
                        current_idx = static_cast<int>(i);
                        break;
                    }
                }

                std::vector<const char*> lang_names;
                for (const auto& l : available_langs) {
                    lang_names.push_back(l.name.c_str());
                }

                if (ImGui::Combo("Language", &current_idx, lang_names.data(), static_cast<int>(lang_names.size()))) {
                    std::string new_code = available_langs[current_idx].code;
                    logic.set_language(new_code);
                    app.set_language(new_code);
                }

                ImGui::Spacing();

                // System file dialog checkbox
                bool use_sys = logic.get_use_system_file_dialog();
                if (ImGui::Checkbox("Use System File Dialog (kdialog / zenity)", &use_sys)) {
                    logic.set_use_system_file_dialog(use_sys);
                    app.set_use_system_file_dialog(use_sys);
                    logic.save_to_disk();
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                // Rendering Backend
                ImGui::Text("Rendering Subsystem");
                const char* backends[] = { "OpenGL 3.3 (Default)", "Vulkan" };
                int backend_idx = (logic.get_render_backend() == "Vulkan") ? 1 : 0;
                if (ImGui::Combo("Render Backend", &backend_idx, backends, 2)) {
                    std::string chosen = (backend_idx == 1) ? "Vulkan" : "OpenGL3";
                    logic.set_render_backend(chosen);
                    logic.save_to_disk();

                    RenderBackendType target_type = (backend_idx == 1) ? RenderBackendType::Vulkan : RenderBackendType::OpenGL3;
                    RenderSystem::instance().set_backend(target_type);
                }
                ImGui::TextDisabled("Current Active Backend: %s", RenderSystem::instance().get_backend_name());

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                // Temporary Storage Cleanup
                ImGui::Text("Temporary Storage");
                if (ImGui::Button("Clean Up Temporary Files", ImVec2(220, 26))) {
                    app.cleanup_temp_files();
                }
                ImGui::TextDisabled("Purges temporary render caches and decoded frame scratch buffers.");

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                // Prefab Hot-Reloading
                ImGui::Text("Prefab Project Hot-Reloading");
                bool hot_reload = logic.get_prefab_hot_reload();
                if (ImGui::Checkbox("Enable Prefab Hot-Reload on Change", &hot_reload)) {
                    logic.set_prefab_hot_reload(hot_reload);
                    logic.save_to_disk();
                }
                if (hot_reload) {
                    float interval = logic.get_prefab_reload_interval();
                    if (ImGui::SliderFloat("Reload Check Interval", &interval, 0.5f, 30.0f, "%.1fs")) {
                        logic.set_prefab_reload_interval(interval);
                        logic.save_to_disk();
                    }
                }
                ImGui::TextDisabled("Automatically updates timeline clips when external .minicut prefab files are modified on disk.");

                ImGui::EndTabItem();
            }

            // Tab 2: AI & Gemini Integration
            if (ImGui::BeginTabItem("AI Integration")) {
                ImGui::Spacing();
                ImGui::TextDisabled("Google AI & Personal Account Authentication");
                ImGui::Separator();
                ImGui::Spacing();

                bool ai_enabled = logic.is_ai_enabled();
                if (ImGui::Checkbox("Enable AI Features", &ai_enabled)) {
                    logic.set_ai_enabled(ai_enabled);
                    logic.save_to_disk();
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                ImGui::Text("Personal Gemini API Key (Google AI Studio):");
                ImGui::SetNextItemWidth(380.0f);

                ImGuiInputTextFlags key_flags = m_show_api_key ? 0 : ImGuiInputTextFlags_Password;
                if (ImGui::InputText("##ApiKey", m_api_key_buf, sizeof(m_api_key_buf), key_flags)) {
                    logic.set_gemini_api_key(m_api_key_buf);
                    logic.save_to_disk();
                }

                ImGui::SameLine();
                if (ImGui::Button(m_show_api_key ? "Hide##Key" : "Show##Key")) {
                    m_show_api_key = !m_show_api_key;
                }

                ImGui::Spacing();
                if (ImGui::Button("Test Authentication", ImVec2(160, 26))) {
                    logic.set_gemini_api_key(m_api_key_buf);
                    logic.test_gemini_api_key_async(m_api_key_buf);
                }

                ImGui::SameLine();
                if (ImGui::Button("Get Free Key (Google AI Studio)")) {
                    std::system("xdg-open https://aistudio.google.com/app/apikey >/dev/null 2>&1 &");
                }

                ImGui::Spacing();
                AuthStatus status = logic.get_auth_status();
                std::string msg = logic.get_auth_message();

                if (status == AuthStatus::Testing) {
                    ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.2f, 1.0f), "⏳ Testing...");
                } else if (status == AuthStatus::Valid) {
                    ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.3f, 1.0f), "✔ %s", msg.c_str());
                } else if (status == AuthStatus::Invalid) {
                    ImGui::TextColored(ImVec4(0.9f, 0.2f, 0.2f, 1.0f), "✖ %s", msg.c_str());
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
                ImGui::TextWrapped("Authenticate using a personal Gemini API key from Google AI Studio. This allows you to generate scripts, auto-edits, subtitles, and AI neural speech through your own account at your own quota/expense.");

                ImGui::EndTabItem();
            }

            // Tab 3: Text-to-Speech
            if (ImGui::BeginTabItem("Text-to-Speech")) {
                ImGui::Spacing();
                ImGui::TextDisabled("TTS Synthesis Source");
                ImGui::Separator();
                ImGui::Spacing();

                std::string provider = logic.get_tts_provider();
                int prov_idx = (provider == "gemini") ? 1 : 0;

                if (ImGui::RadioButton("Free Local Synthesis (Speech-Dispatcher / eSpeak-NG)", prov_idx == 0)) {
                    logic.set_tts_provider("system");
                    logic.save_to_disk();
                }
                ImGui::TextDisabled("   100%% offline, free, and uses system speech synthesizers.");

                ImGui::Spacing();

                bool can_use_ai = logic.is_ai_enabled() && !logic.get_active_auth_token_or_key().empty();
                if (!can_use_ai) {
                    ImGui::BeginDisabled();
                }

                if (ImGui::RadioButton("AI Gemini Speech Generation", prov_idx == 1)) {
                    logic.set_tts_provider("gemini");
                    logic.save_to_disk();
                }
                ImGui::TextDisabled("   Uses personal Gemini API authentication for AI-generated speech.");

                if (!can_use_ai) {
                    ImGui::EndDisabled();
                    ImGui::TextColored(ImVec4(0.8f, 0.6f, 0.2f, 1.0f), "   (Enable AI and enter API key in AI Integration tab to activate)");
                }

                // Voice selection
                std::string saved_voice = logic.get_tts_voice();
                if (prov_idx == 1) { // Gemini
                    static const char* gemini_names[] = { "Puck (Natural)", "Charon (Deep)", "Kore (Warm)", "Fenrir (Resonant)", "Aoede (Melodic)" };
                    static const char* gemini_keys[] = { "Puck", "Charon", "Kore", "Fenrir", "Aoede" };
                    int v_idx = 0;
                    for (int i = 0; i < 5; ++i) {
                        if (saved_voice == gemini_keys[i]) { v_idx = i; break; }
                    }
                    if (ImGui::Combo("Default AI Voice", &v_idx, gemini_names, 5)) {
                        logic.set_tts_voice(gemini_keys[v_idx]);
                        logic.save_to_disk();
                    }
                } else { // System
                    static const char* sys_names[] = {
                        "Default (Auto-Detect Language)", "English (en)", "English US (en-us)", "English UK (en-uk)",
                        "Japanese / 日本語 (ja)", "Korean / 한국어 (ko)", "Ukrainian / Українська (uk)", "Arabic / العربية (ar)",
                        "Spanish / Español (es)", "French / Français (fr)", "German / Deutsch (de)", "Chinese / 中文 (cmn)",
                        "Male 1 (en+m1)", "Male 2 (en+m3)", "Female 1 (en+f1)", "Female 2 (en+f3)"
                    };
                    static const char* sys_keys[] = {
                        "default", "en", "en-us", "en-uk", "ja", "ko", "uk", "ar", "es", "fr", "de", "cmn",
                        "en+m1", "en+m3", "en+f1", "en+f3"
                    };
                    int v_idx = 0;
                    for (int i = 0; i < 16; ++i) {
                        if (saved_voice == sys_keys[i]) { v_idx = i; break; }
                    }
                    if (ImGui::Combo("Default System Voice", &v_idx, sys_names, 16)) {
                        logic.set_tts_voice(sys_keys[v_idx]);
                        logic.save_to_disk();
                    }
                }

                ImGui::Spacing();
                float rate = logic.get_tts_rate();
                if (ImGui::SliderFloat("Speech Rate", &rate, 0.5f, 2.0f, "%.2fx")) {
                    logic.set_tts_rate(rate);
                    logic.save_to_disk();
                }

                float pitch = logic.get_tts_pitch();
                if (ImGui::SliderFloat("Speech Pitch", &pitch, 0.5f, 2.0f, "%.2fx")) {
                    logic.set_tts_pitch(pitch);
                    logic.save_to_disk();
                }

                ImGui::EndTabItem();
            }

            // Tab 4: Appearance & Accessibility
            if (ImGui::BeginTabItem("Appearance & Accessibility")) {
                ImGui::Spacing();
                ImGui::TextDisabled("Visual, Appearance & Accessibility Settings");
                ImGui::Separator();
                ImGui::Spacing();

                // Preview Window Background & Padding
                ImGui::Text("Preview Window Background & Padding");
                const char* bg_modes[] = {
                    "Default Dark (Theme)",
                    "Solid Black (Cinema)",
                    "Deep Navy",
                    "Neutral Gray (Colorist)",
                    "Checkerboard (Transparency)",
                    "Custom Color"
                };
                int bg_idx = logic.get_preview_bg_mode();
                if (ImGui::Combo("Preview Background", &bg_idx, bg_modes, 6)) {
                    logic.set_preview_bg_mode(bg_idx);
                    logic.save_to_disk();
                }
                if (bg_idx == 5) {
                    float custom_col[4];
                    const float* src_col = logic.get_preview_bg_custom_color();
                    custom_col[0] = src_col[0];
                    custom_col[1] = src_col[1];
                    custom_col[2] = src_col[2];
                    custom_col[3] = src_col[3];
                    if (ImGui::ColorEdit4("Custom Background Color", custom_col)) {
                        logic.set_preview_bg_custom_color(custom_col[0], custom_col[1], custom_col[2], custom_col[3]);
                        logic.save_to_disk();
                    }
                }
                float pad = logic.get_preview_padding();
                if (ImGui::SliderFloat("Preview Padding Margin", &pad, 0.0f, 40.0f, "%.0f px")) {
                    logic.set_preview_padding(pad);
                    logic.save_to_disk();
                }
                ImGui::TextDisabled("Customizes the padding area and background styling of the video preview window.");

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                // Light Theme Toggle
                bool is_light = logic.get_light_theme();
                if (ImGui::Checkbox("Light Theme (Breeze Light)", &is_light)) {
                    logic.set_light_theme(is_light);
                    logic.save_to_disk();
                    app.apply_theme();
                }
                ImGui::TextDisabled("Toggles between Breeze Dark and Breeze Light interface themes.");

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                // Colorblind Mode
                const char* cb_modes[] = {
                    "None (Default Colors)",
                    "Protanopia (Red-Blind Friendly)",
                    "Deuteranopia (Green-Blind Friendly)",
                    "Tritanopia (Blue-Blind Friendly)",
                    "High Contrast"
                };
                int cb_idx = logic.get_colorblind_mode();
                if (ImGui::Combo("Colorblind Mode", &cb_idx, cb_modes, 5)) {
                    logic.set_colorblind_mode(cb_idx);
                    logic.save_to_disk();
                    app.apply_theme();
                }
                ImGui::TextDisabled("Adapts timeline clip and track lane colors using accessible palettes.");

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                // Menu Font Size
                float menu_font = logic.get_menu_font_size();
                if (ImGui::SliderFloat("Menu Font Size", &menu_font, 11.0f, 26.0f, "%.1f pt")) {
                    logic.set_menu_font_size(menu_font);
                    logic.save_to_disk();
                }
                ImGui::TextDisabled("Controls the text size in the main application menu bar.");

                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Apply", ImVec2(90, 26))) {
            logic.save_to_disk();
            app.reload_subsystems();
        }
        ImGui::SameLine();
        if (ImGui::Button("Reload Subsystems", ImVec2(150, 26))) {
            logic.save_to_disk();
            app.reload_subsystems();
        }
        ImGui::SameLine();
        if (ImGui::Button("Close", ImVec2(90, 26))) {
            close();
        }
    }
    ImGui::End();
}
