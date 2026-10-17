#pragma once

#include <string>
#include <mutex>
#include <atomic>
#include <functional>
#include <vector>
#include <algorithm>

enum class AuthStatus {
    Idle,
    Testing,
    Valid,
    Invalid
};

class SettingsLogic {
public:
    SettingsLogic();
    ~SettingsLogic();

    static SettingsLogic& instance();

    void load_from_disk();
    void save_to_disk();
    void reset_to_defaults();

    // General Settings
    bool get_use_system_file_dialog() const { return m_use_system_file_dialog; }
    void set_use_system_file_dialog(bool val) { m_use_system_file_dialog = val; }

    std::string get_language() const { return m_language; }
    void set_language(const std::string& lang) { m_language = lang; }

    std::string get_render_backend() const { return m_render_backend; }
    void set_render_backend(const std::string& backend) { m_render_backend = backend; }

    bool get_prefab_hot_reload() const { return m_prefab_hot_reload; }
    void set_prefab_hot_reload(bool val) { m_prefab_hot_reload = val; }

    float get_prefab_reload_interval() const { return m_prefab_reload_interval; }
    void set_prefab_reload_interval(float val) { m_prefab_reload_interval = std::clamp(val, 0.5f, 60.0f); }

    // Accessibility Settings
    bool get_light_theme() const { return m_light_theme; }
    void set_light_theme(bool val) { m_light_theme = val; }

    int get_colorblind_mode() const { return m_colorblind_mode; }
    void set_colorblind_mode(int mode) { m_colorblind_mode = mode; }

    float get_menu_font_size() const { return m_menu_font_size; }
    void set_menu_font_size(float sz) { m_menu_font_size = sz; }

    // Preview Window Background & Padding Settings
    int get_preview_bg_mode() const { return m_preview_bg_mode; }
    void set_preview_bg_mode(int mode) { m_preview_bg_mode = mode; }

    const float* get_preview_bg_custom_color() const { return m_preview_bg_color; }
    void set_preview_bg_custom_color(float r, float g, float b, float a = 1.0f) {
        m_preview_bg_color[0] = r;
        m_preview_bg_color[1] = g;
        m_preview_bg_color[2] = b;
        m_preview_bg_color[3] = a;
    }

    float get_preview_padding() const { return m_preview_padding; }
    void set_preview_padding(float pad) { m_preview_padding = pad; }

    // Layout Splitters & Timeline Dimensions
    float get_split_h() const { return m_split_h; }
    void set_split_h(float val) { m_split_h = val; }

    float get_split_top_v() const { return m_split_top_v; }
    void set_split_top_v(float val) { m_split_top_v = val; }

    float get_split_bottom_v() const { return m_split_bottom_v; }
    void set_split_bottom_v(float val) { m_split_bottom_v = val; }

    float get_track_header_w() const { return m_track_header_w; }
    void set_track_header_w(float val) { m_track_header_w = val; }

    // AI & Gemini Settings
    bool is_ai_enabled() const { return m_ai_enabled; }
    void set_ai_enabled(bool enabled) { m_ai_enabled = enabled; }

    std::string get_gemini_api_key() const;
    void set_gemini_api_key(const std::string& key);

    std::string get_active_auth_token_or_key() const { return get_gemini_api_key(); }
    bool is_authenticated() const;

    AuthStatus get_auth_status() const { return m_auth_status.load(); }
    std::string get_auth_message() const;

    void test_gemini_api_key_async(const std::string& key, std::function<void(bool success, const std::string& message)> on_complete = nullptr);

    // Text-to-Speech Settings
    std::string get_tts_provider() const { return m_tts_provider; }
    void set_tts_provider(const std::string& provider) { m_tts_provider = provider; }

    std::string get_tts_voice() const { return m_tts_voice; }
    void set_tts_voice(const std::string& voice) { m_tts_voice = voice; }

    float get_tts_rate() const { return m_tts_rate; }
    void set_tts_rate(float rate) { m_tts_rate = rate; }

    float get_tts_pitch() const { return m_tts_pitch; }
    void set_tts_pitch(float pitch) { m_tts_pitch = pitch; }

    // Audio Preview Settings
    float get_preview_volume_db() const { return m_preview_volume_db; }
    void set_preview_volume_db(float db) { m_preview_volume_db = db; }

private:
    float m_preview_volume_db = 0.0f;
    bool m_use_system_file_dialog = true;
    std::string m_language = "en";
    std::string m_render_backend = "OpenGL3";
    bool m_prefab_hot_reload = true;
    float m_prefab_reload_interval = 3.0f;
    bool m_light_theme = false;
    int m_colorblind_mode = 0; // 0=None, 1=Protanopia, 2=Deuteranopia, 3=Tritanopia, 4=High Contrast
    float m_menu_font_size = 15.0f;
    int m_preview_bg_mode = 0; // 0=Theme Dark, 1=Black, 2=Deep Navy, 3=Neutral Gray, 4=Checkerboard, 5=Custom
    float m_preview_bg_color[4] = { 0.10f, 0.10f, 0.12f, 1.0f };
    float m_preview_padding = 8.0f;

    float m_split_h = 0.46f;
    float m_split_top_v = 0.28f;
    float m_split_bottom_v = 0.65f;
    float m_track_header_w = 145.0f;

    bool m_ai_enabled = false;
    std::string m_gemini_api_key = "";

    std::atomic<AuthStatus> m_auth_status{AuthStatus::Idle};
    mutable std::mutex m_auth_mutex;
    std::string m_auth_message = "";

    std::string m_tts_provider = "system"; // "system" (free) or "gemini"
    std::string m_tts_voice = "default";
    float m_tts_rate = 1.0f;
    float m_tts_pitch = 1.0f;
};
