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

#include <SDL2/SDL.h>
#include <GL/glew.h>
#include <string>
#include <vector>

class App {
public:
    App();
    ~App();

    bool init(int argc = 0, char* argv[] = nullptr);
    void run();
    void cleanup();

private:
    void apply_kde_plasma_theme();
    void process_events();
    void update(double delta_time);
    void render();

    // Settings & Localization
    void load_settings();
    void save_settings();
    void set_language(const std::string& code);
    void cleanup_temp_files();

    // Playback control
    void toggle_playback();

    // Menu Bar & Panels
    void render_menu_bar();
    void render_clip_panel();
    void render_preview_panel();
    void render_timeline_panel();
    void render_context_panel();
    void render_splitters();

    // Dialogs
    void render_about_dialog();

    // FileBrowser Triggers
    void open_project_browser();
    void save_project_as_browser();
    void import_video_browser();
    void import_image_browser();
    void import_audio_browser();
    void import_prefab_browser();
    void export_path_browser();

    // Undo / Redo History
    void push_undo_state();
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

    // Undo / Redo stack
    std::vector<Project> m_undo_stack;
    std::vector<Project> m_redo_stack;

    // Dialog & Notification state
    bool m_show_about = false;
    bool m_use_system_file_dialog = true;
    std::string m_status_notification;
    double m_status_notification_timer = 0.0;
};
