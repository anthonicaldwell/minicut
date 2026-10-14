#pragma once

#include "Common.h"
#include "ProjectSerializer.h"
#include "ClipManager.h"
#include "MediaDecoder.h"
#include "Timeline.h"
#include "KeyframeEditor.h"
#include "ExportManager.h"
#include "AudioEngine.h"
#include "FileBrowser.h"
#include "NativeFileDialog.h"
#include "LocaleManager.h"
#include "TempManager.h"
#include "ScriptEngine.h"
#include "SettingsLogic.h"
#include "SettingsDialog.h"
#include "TTSDialog.h"
#include "AIDialog.h"
#include "render/RenderSystem.h"
#include "TextGenerator.h"

#include <SDL2/SDL.h>
#include <GL/glew.h>
#include <string>
#include <vector>

struct ImGuiContext;

class App {
public:
    App();
    ~App();

    bool init(int argc = 0, char* argv[] = nullptr);
    void run();
    void cleanup();

    // Accessors for ScriptEngine & Subsystems
    Project& get_project() { return m_project; }
    const Project& get_project() const { return m_project; }
    double get_playhead_time() const { return m_playhead_time; }
    void set_playhead_time(double t);
    void push_undo_state();
    void on_project_modified();
    std::string get_selected_clip_id() const { return m_selected_clip_id; }
    void set_selected_clip_id(const std::string& id);
    void set_status_notification(const std::string& msg, double duration = 4.0);
    ClipManager& get_clip_manager() { return m_clip_manager; }
    Timeline& get_timeline() { return m_timeline; }
    AudioEngine& get_audio_engine() { return m_audio_engine; }
    MediaDecoder& get_media_decoder() { return m_media_decoder; }
    ScriptEngine& get_script_engine() { return m_script_engine; }
    FileBrowser& get_file_browser() { return m_file_browser; }
    SettingsLogic& get_settings_logic() { return m_settings_logic; }
    SettingsDialog& get_settings_dialog() { return m_settings_dialog; }
    TTSDialog& get_tts_dialog() { return m_tts_dialog; }
    AIDialog& get_ai_dialog() { return m_ai_dialog; }
    bool get_use_system_file_dialog() const { return m_use_system_file_dialog; }
    void set_use_system_file_dialog(bool val) { m_use_system_file_dialog = val; }

    void open_settings_dialog() { m_settings_dialog.open(); }
    void open_tts_dialog() { m_tts_dialog.open(); }
    void open_ai_dialog() { m_ai_dialog.open(); }

    // Text Generator Dialog
    void open_text_dialog();
    std::string create_text_asset(const std::string& text, const TextConfig& config, const std::string& name = "");
    void add_text_to_timeline(const std::string& asset_id, double insert_time);

    bool is_any_dialog_open() const;
    void update_window_title();
    void set_language(const std::string& code);
    void apply_theme();
    void reload_subsystems();
    void add_subtitle_at_playhead();

    enum class PreviewTransformTool {
        None = 0,
        Transform = 1,
        Select = 1,
        Translate = 1,
        Scale = 1,
        Rotate = 1
    };

    void set_preview_popped_out(bool popped) { m_preview_popped_out = popped; }
    bool is_preview_popped_out() const { return m_preview_popped_out; }
    void set_preview_aspect_idx(int idx) { m_preview_aspect_idx = std::clamp(idx, 0, 6); }
    int get_preview_aspect_idx() const { return m_preview_aspect_idx; }
    void set_preview_transform_enabled(bool enabled) { m_preview_transform_enabled = enabled; }
    bool is_preview_transform_enabled() const { return m_preview_transform_enabled; }
    void set_preview_transform_tool(PreviewTransformTool tool) {
        m_preview_transform_enabled = (tool != PreviewTransformTool::None);
    }
    PreviewTransformTool get_preview_transform_tool() const {
        return m_preview_transform_enabled ? PreviewTransformTool::Transform : PreviewTransformTool::None;
    }

    Clip* find_selected_clip();
    bool is_selected_clip_visual();
    ExportManager& get_export_manager() { return m_export_manager; }

private:
    void apply_kde_plasma_theme();
    void apply_breeze_dark_theme();
    void apply_breeze_light_theme();
    void process_events();
    void update(double delta_time);
    void render();

    // Settings & Localization
    void load_settings();
    void save_settings();
    void cleanup_temp_files();

    // Playback control
    void toggle_playback();

    // Menu Bar & Panels
    void render_menu_bar();
    void render_shelf_dock();
    void render_clip_panel();
    void render_preview_panel();
    void render_timeline_panel();
    void render_context_panel();
    void render_splitters();

    // Dialogs
    void render_about_dialog();
    void render_text_dialog();
    void update_text_preview();

    // FileBrowser Triggers
    void open_project_browser();
    void save_project_as_browser();
    void import_video_browser();
    void import_image_browser();
    void import_audio_browser();
    void import_prefab_browser();
    void export_path_browser();

    // Undo / Redo History
    void undo();
    void redo();

    // Project Actions
    void new_project();
    void open_project(const std::string& path);
    void save_project(const std::string& path = "");
    void add_asset_to_timeline(const std::string& asset_id, bool is_video, double insert_time);

    // SDL & OpenGL state
    SDL_Window* m_window = nullptr;
    SDL_GLContext m_gl_context = nullptr;
    bool m_running = true;
    int m_window_width = 1600;
    int m_window_height = 900;

    // View section split ratios
    float m_split_h = 0.46f;
    float m_split_top_v = 0.28f;
    float m_split_bottom_v = 0.65f;
    bool m_dragging_split_h = false;
    bool m_dragging_split_top_v = false;
    bool m_dragging_split_bottom_v = false;

    // Preview OpenGL Texture
    GLuint m_preview_texture = 0;
    int m_preview_tex_w = 1280;
    int m_preview_tex_h = 720;
    double m_preview_tex_time = 0.0;

    // Core Project & Playhead State
    Project m_project;
    double m_playhead_time = 0.0;
    bool m_is_playing = false;
    bool m_loop_playback = true;
    std::string m_selected_clip_id;

    // Subsystems
    ClipManager m_clip_manager;
    MediaDecoder m_media_decoder;
    Timeline m_timeline;
    KeyframeEditor m_keyframe_editor;
    ExportManager m_export_manager;
    AudioEngine m_audio_engine;
    FileBrowser m_file_browser;
    ScriptEngine m_script_engine;
    SettingsLogic& m_settings_logic = SettingsLogic::instance();
    SettingsDialog m_settings_dialog;
    TTSDialog m_tts_dialog;
    AIDialog m_ai_dialog;

    // Undo / Redo stack
    std::vector<Project> m_undo_stack;
    std::vector<Project> m_redo_stack;

    // Dialog & Notification state
    bool m_show_about = false;
    bool m_use_system_file_dialog = true;
    std::string m_status_notification;
    double m_status_notification_timer = 0.0;
    float m_menu_bar_height = 0.0f;
    GLuint m_settings_icon_texture = 0;
    void generate_settings_gear_icon();

    // Text Asset Dialog state
    bool m_show_text_dialog = false;
    bool m_text_dialog_request_focus = false;
    TextConfig m_text_config;
    char m_text_input_buf[1024] = "Title Text";
    std::string m_last_rendered_text;
    GLuint m_text_preview_texture = 0;
    int m_text_preview_w = 0;
    int m_text_preview_h = 0;
    bool m_text_preview_dirty = true;
    std::string m_ime_composition;
    int m_ime_cursor = 0;
    int m_ime_selection_len = 0;

    // Preview Aspect Ratio (0: Auto / Largest Asset, 1: 16:9, 2: 9:16, 3: 4:3, 4: 1:1, 5: 21:9, 6: 4:5)
    int m_preview_aspect_idx = 0;

    void render_preview_contents(bool is_detached);
    void render_preview_toolbar(bool is_detached);
    void render_preview_viewport();
    void render_preview_transport();
    void render_popout_preview_window();
    void render_export_os_window();
    void generate_preview_toolbar_icons();
    void cleanup_preview_toolbar_icons();

    // Multi-Window ImGui contexts and SDL Windows
    ImGuiContext* m_imgui_context = nullptr;
    bool m_preview_transform_enabled = true;
    bool m_preview_popped_out = false;
    SDL_Window* m_preview_window = nullptr;
    ImGuiContext* m_preview_imgui_ctx = nullptr;

    SDL_Window* m_export_window = nullptr;
    ImGuiContext* m_export_imgui_ctx = nullptr;

    enum class GizmoDragMode {
        None = 0,
        Translate,
        ScaleCorner,
        RotateCorner
    };
    GizmoDragMode m_gizmo_drag_mode = GizmoDragMode::None;
    int m_gizmo_drag_corner = -1;
    ImVec2 m_gizmo_drag_start_mouse{0.0f, 0.0f};
    double m_gizmo_orig_pos_x = 0.0;
    double m_gizmo_orig_pos_y = 0.0;
    double m_gizmo_orig_scale = 1.0;
    double m_gizmo_orig_rotation = 0.0;
    double m_gizmo_orig_angle = 0.0;
    double m_gizmo_orig_dist = 1.0;

    // Generated preview toolbar icons
    GLuint m_icon_tex_transform = 0;
    GLuint m_icon_tex_select = 0;
    GLuint m_icon_tex_translate = 0;
    GLuint m_icon_tex_scale = 0;
    GLuint m_icon_tex_rotate = 0;
    GLuint m_icon_tex_reset = 0;
    GLuint m_icon_tex_popout = 0;
};
