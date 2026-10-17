#include "SettingsLogic.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <algorithm>
#include <curl/curl.h>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

SettingsLogic::SettingsLogic() {
    load_from_disk();
}

SettingsLogic::~SettingsLogic() = default;

SettingsLogic& SettingsLogic::instance() {
    static SettingsLogic s_instance;
    return s_instance;
}

std::string SettingsLogic::get_gemini_api_key() const {
    std::lock_guard<std::mutex> lock(m_auth_mutex);
    return m_gemini_api_key;
}

void SettingsLogic::set_gemini_api_key(const std::string& key) {
    std::lock_guard<std::mutex> lock(m_auth_mutex);
    m_gemini_api_key = key;
}

std::string SettingsLogic::get_auth_message() const {
    std::lock_guard<std::mutex> lock(m_auth_mutex);
    return m_auth_message;
}

void SettingsLogic::load_from_disk() {
    const char* home = std::getenv("HOME");
    if (!home) return;

    fs::path cfg_path = fs::path(home) / ".config" / "minicut" / "settings.json";
    std::error_code ec;
    if (!fs::exists(cfg_path, ec)) return;

    try {
        std::ifstream ifs(cfg_path);
        if (ifs.is_open()) {
            nlohmann::json j;
            ifs >> j;

            m_use_system_file_dialog = j.value("use_system_file_dialog", true);
            m_language = j.value("language", "en");
            m_render_backend = j.value("render_backend", "OpenGL3");
            m_prefab_hot_reload = j.value("prefab_hot_reload", true);
            m_prefab_reload_interval = std::clamp(j.value("prefab_reload_interval", 3.0f), 0.5f, 60.0f);
            m_light_theme = j.value("light_theme", false);
            m_colorblind_mode = j.value("colorblind_mode", 0);
            m_menu_font_size = std::clamp(j.value("menu_font_size", 15.0f), 10.0f, 32.0f);
            m_preview_bg_mode = std::clamp(j.value("preview_bg_mode", 0), 0, 5);
            m_preview_padding = std::clamp(j.value("preview_padding", 8.0f), 0.0f, 60.0f);
            if (j.contains("preview_bg_color") && j["preview_bg_color"].is_array() && j["preview_bg_color"].size() >= 3) {
                m_preview_bg_color[0] = j["preview_bg_color"][0];
                m_preview_bg_color[1] = j["preview_bg_color"][1];
                m_preview_bg_color[2] = j["preview_bg_color"][2];
                if (j["preview_bg_color"].size() >= 4) {
                    m_preview_bg_color[3] = j["preview_bg_color"][3];
                }
            }

            m_split_h = std::clamp(j.value("split_h", 0.46f), 0.15f, 0.85f);
            m_split_top_v = std::clamp(j.value("split_top_v", 0.28f), 0.15f, 0.75f);
            m_split_bottom_v = std::clamp(j.value("split_bottom_v", 0.65f), 0.20f, 0.85f);
            m_track_header_w = std::clamp(j.value("track_header_w", 145.0f), 80.0f, 400.0f);

            m_ai_enabled = j.value("ai_enabled", false);
            {
                std::lock_guard<std::mutex> lock(m_auth_mutex);
                m_gemini_api_key = j.value("gemini_api_key", "");
                if (!m_gemini_api_key.empty()) {
                    m_auth_status.store(AuthStatus::Valid);
                    m_auth_message = "Gemini API key configured";
                }
            }

            m_tts_provider = j.value("tts_provider", "system");
            m_tts_voice = j.value("tts_voice", "default");
            m_tts_rate = j.value("tts_rate", 1.0f);
            m_tts_pitch = j.value("tts_pitch", 1.0f);
            m_preview_volume_db = std::clamp(j.value("preview_volume_db", 0.0f), -60.0f, 6.0f);
        }
    } catch (const std::exception& e) {
        std::cerr << "SettingsLogic: Error loading settings: " << e.what() << std::endl;
    }
}

void SettingsLogic::save_to_disk() {
    const char* home = std::getenv("HOME");
    if (!home) return;

    fs::path cfg_dir = fs::path(home) / ".config" / "minicut";
    std::error_code ec;
    fs::create_directories(cfg_dir, ec);
    fs::path cfg_path = cfg_dir / "settings.json";

    nlohmann::json j;
    j["use_system_file_dialog"] = m_use_system_file_dialog;
    j["language"] = m_language;
    j["render_backend"] = m_render_backend;
    j["prefab_hot_reload"] = m_prefab_hot_reload;
    j["prefab_reload_interval"] = m_prefab_reload_interval;
    j["light_theme"] = m_light_theme;
    j["colorblind_mode"] = m_colorblind_mode;
    j["menu_font_size"] = m_menu_font_size;
    j["preview_bg_mode"] = m_preview_bg_mode;
    j["preview_padding"] = m_preview_padding;
    j["preview_bg_color"] = { m_preview_bg_color[0], m_preview_bg_color[1], m_preview_bg_color[2], m_preview_bg_color[3] };

    j["split_h"] = m_split_h;
    j["split_top_v"] = m_split_top_v;
    j["split_bottom_v"] = m_split_bottom_v;
    j["track_header_w"] = m_track_header_w;

    j["ai_enabled"] = m_ai_enabled;
    {
        std::lock_guard<std::mutex> lock(m_auth_mutex);
        j["gemini_api_key"] = m_gemini_api_key;
    }

    j["tts_provider"] = m_tts_provider;
    j["tts_voice"] = m_tts_voice;
    j["tts_rate"] = m_tts_rate;
    j["tts_pitch"] = m_tts_pitch;
    j["preview_volume_db"] = m_preview_volume_db;

    try {
        std::ofstream ofs(cfg_path);
        if (ofs.is_open()) {
            ofs << j.dump(2);
        }
    } catch (const std::exception& e) {
        std::cerr << "SettingsLogic: Error saving settings: " << e.what() << std::endl;
    }
}

void SettingsLogic::reset_to_defaults() {
    m_use_system_file_dialog = true;
    m_language = "en";
    m_render_backend = "OpenGL3";
    m_prefab_hot_reload = true;
    m_prefab_reload_interval = 3.0f;
    m_light_theme = false;
    m_colorblind_mode = 0;
    m_menu_font_size = 15.0f;
    m_preview_bg_mode = 0;
    m_preview_bg_color[0] = 0.10f;
    m_preview_bg_color[1] = 0.10f;
    m_preview_bg_color[2] = 0.12f;
    m_preview_bg_color[3] = 1.0f;
    m_preview_padding = 8.0f;
    m_split_h = 0.46f;
    m_split_top_v = 0.28f;
    m_split_bottom_v = 0.65f;
    m_track_header_w = 145.0f;
    m_ai_enabled = false;
    {
        std::lock_guard<std::mutex> lock(m_auth_mutex);
        m_gemini_api_key.clear();
        m_auth_status.store(AuthStatus::Idle);
        m_auth_message.clear();
    }
    m_tts_provider = "system";
    m_tts_voice = "default";
    m_tts_rate = 1.0f;
    m_tts_pitch = 1.0f;
    m_preview_volume_db = 0.0f;

    const char* home = std::getenv("HOME");
    if (home) {
        fs::path cfg_dir = fs::path(home) / ".config" / "minicut";
        std::error_code ec;
        fs::remove(cfg_dir / "settings.json", ec);
        fs::remove(cfg_dir / "imgui.ini", ec);
    }
    save_to_disk();
}

static size_t curl_discard_write(void* ptr, size_t size, size_t nmemb, void* userdata) {
    (void)ptr;
    (void)userdata;
    return size * nmemb;
}

void SettingsLogic::test_gemini_api_key_async(const std::string& key,
                                              std::function<void(bool success, const std::string& message)> on_complete) {
    if (key.empty()) {
        m_auth_status.store(AuthStatus::Invalid);
        {
            std::lock_guard<std::mutex> lock(m_auth_mutex);
            m_auth_message = "API key cannot be empty.";
        }
        if (on_complete) on_complete(false, "API key cannot be empty.");
        return;
    }

    m_auth_status.store(AuthStatus::Testing);
    {
        std::lock_guard<std::mutex> lock(m_auth_mutex);
        m_auth_message = "Testing Gemini authentication...";
    }

    std::thread([this, key, on_complete]() {
        CURL* curl = curl_easy_init();
        if (!curl) {
            m_auth_status.store(AuthStatus::Invalid);
            {
                std::lock_guard<std::mutex> lock(m_auth_mutex);
                m_auth_message = "Failed to initialize network client.";
            }
            if (on_complete) on_complete(false, "Failed to initialize network client.");
            return;
        }

        char* escaped = curl_easy_escape(curl, key.c_str(), static_cast<int>(key.length()));
        std::string url = "https://generativelanguage.googleapis.com/v1beta/models?key=" + std::string(escaped);
        curl_free(escaped);

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_discard_write);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "minicut-editor/1.4");

        CURLcode res = curl_easy_perform(curl);
        long response_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
        curl_easy_cleanup(curl);

        bool success = (res == CURLE_OK && response_code == 200);
        std::string message;

        if (success) {
            m_auth_status.store(AuthStatus::Valid);
            message = "Gemini API authenticated successfully!";
            {
                std::lock_guard<std::mutex> lock(m_auth_mutex);
                m_gemini_api_key = key;
                m_auth_message = message;
            }
            save_to_disk();
        } else {
            m_auth_status.store(AuthStatus::Invalid);
            if (res != CURLE_OK) {
                message = std::string("Network error: ") + curl_easy_strerror(res);
            } else if (response_code == 400 || response_code == 403) {
                message = "Invalid API key or unauthorized project.";
            } else {
                message = "Authentication error (HTTP " + std::to_string(response_code) + ").";
            }
            {
                std::lock_guard<std::mutex> lock(m_auth_mutex);
                m_auth_message = message;
            }
        }

        if (on_complete) {
            on_complete(success, message);
        }
    }).detach();
}

bool SettingsLogic::is_authenticated() const {
    std::lock_guard<std::mutex> lock(m_auth_mutex);
    return !m_gemini_api_key.empty();
}
