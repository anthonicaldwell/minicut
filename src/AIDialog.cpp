#include "AIDialog.h"
#include "SettingsLogic.h"
#include "App.h"
#include "imgui.h"
#include <cstring>
#include <chrono>

AIDialog::AIDialog() = default;
AIDialog::~AIDialog() = default;

void AIDialog::open() {
    m_is_open = true;
    m_request_focus = true;
    m_status_msg.clear();
    m_output_buf[0] = '\0';
    if (m_prompt_buf[0] == '\0') {
        strncpy(m_prompt_buf, "Create a button that fades all timeline clips in and out", sizeof(m_prompt_buf) - 1);
    }
}

void AIDialog::close() {
    m_is_open = false;
}

void AIDialog::render(App& app) {
    if (!m_is_open) return;

    if (m_request_focus) {
        ImGui::SetNextWindowFocus();
        m_request_focus = false;
    }

    ImVec2 center = ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f);
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(620, 500), ImGuiCond_Appearing);

    const char* title = "AI Script Generator###AIDialog";

    if (ImGui::Begin(title, &m_is_open, ImGuiWindowFlags_NoCollapse)) {
        ImGui::Text("Generate Duktape JavaScript scripts and tool shelf plugins using Gemini AI.");
        ImGui::Spacing();

        ImGui::Text("Script Prompt / Request:");
        ImGui::InputTextMultiline("##AIPrompt", m_prompt_buf, sizeof(m_prompt_buf), ImVec2(-1, 80));

        ImGui::Spacing();
        ImGui::Text("AI Output / Logs:");
        ImGui::InputTextMultiline("##AIOutput", m_output_buf, sizeof(m_output_buf), ImVec2(-1, 160), ImGuiInputTextFlags_ReadOnly);

        if (!m_status_msg.empty()) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "%s", m_status_msg.c_str());
        }

        std::string auth_token = SettingsLogic::instance().get_active_auth_token_or_key();
        bool is_ai_on = SettingsLogic::instance().is_ai_enabled();
        if (!is_ai_on || auth_token.empty()) {
            ImGui::TextColored(ImVec4(0.95f, 0.5f, 0.2f, 1.0f), "Warning: AI is not authenticated. Open Settings (Gear Icon) -> AI Integration to configure.");
        } else {
            std::string masked = auth_token.substr(0, std::min<size_t>(auth_token.length(), 8)) + "...";
            ImGui::TextColored(ImVec4(0.3f, 0.85f, 0.45f, 1.0f), "Using: Gemini API Key (%s)", masked.c_str());
        }

        ImGui::Spacing();
        ImGui::Separator();

        bool can_run = (!m_is_running && strlen(m_prompt_buf) > 0);
        if (!can_run) ImGui::BeginDisabled();

        if (ImGui::Button("Confirm & Generate", ImVec2(160, 28))) {
            m_is_running = true;
            m_status_msg = "Querying Gemini AI...";
            strncpy(m_output_buf, "Connecting to Gemini AI...\n", sizeof(m_output_buf) - 1);
            std::string prompt = m_prompt_buf;

            AICore::instance().generate_script_async(prompt, auth_token, [this, &app](bool ok, const std::string& path, const std::string& msg) {
                m_is_running = false;
                m_status_msg = msg;
                std::string out_text = AICore::instance().get_last_output();
                strncpy(m_output_buf, out_text.c_str(), sizeof(m_output_buf) - 1);
                if (ok) {
                    app.get_script_engine().reload_scripts();
                }
            });
        }

        if (!can_run) ImGui::EndDisabled();

        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(90, 28))) {
            close();
        }
    }
    ImGui::End();
}
