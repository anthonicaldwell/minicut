#pragma once

#include "Common.h"
#include <lua.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <filesystem>
#include <functional>
#include <GL/glew.h>

class Timeline;
class ClipManager;

struct ScriptItem {
    std::string id;            // Unique identifier, e.g. "quick_placement"
    std::string filename;      // e.g. "quick_placement.lua"
    std::string path;          // Absolute path
    std::string title;         // Human-friendly title, e.g. "Quick Placement"
    std::string description;   // Tooltip info
    bool is_autorun = false;   // Lives in ~/.config/minicut/autorun/
    bool is_loaded = false;    // Has file been executed into Lua?
    bool is_panel_open = false; // Plugin tool panel currently open
    GLuint icon_texture = 0;   // Texture for shelf dock icon
};

class ScriptEngine {
public:
    ScriptEngine();
    ~ScriptEngine();

    bool init(Project* project = nullptr,
              Timeline* timeline = nullptr,
              ClipManager* clip_manager = nullptr,
              double* playhead_time = nullptr,
              std::string* selected_clip_id = nullptr,
              std::function<void()> on_push_undo = nullptr,
              std::function<void()> on_project_modified = nullptr,
              std::function<void(double)> on_set_playhead = nullptr,
              std::function<void(const std::string&, double)> on_set_status = nullptr,
              std::function<void(const std::string&)> on_select_clip = nullptr,
              std::function<bool(const std::string&)> on_save_project = nullptr,
              std::function<bool(const std::string&)> on_load_project = nullptr);

    void shutdown();

    // Scripts Discovery & Execution
    void reload_scripts();
    bool execute_script(ScriptItem& item);
    bool run_file(const std::string& file_path);
    bool run_string(const std::string& lua_code);

    // Frame update & UI Rendering
    void update(double delta_time);
    void render_shelf_dock(float menu_h = 0.0f);
    void render_scripts_menu();
    void render_script_panels(float total_w, float total_h, float top_offset);

    float get_shelf_height() const { return m_shelf_height; }
    void set_shelf_height(float h) { m_shelf_height = h; }
    bool is_shelf_visible() const { return m_shelf_visible; }
    void set_shelf_visible(bool visible) { m_shelf_visible = visible; }

    const std::vector<ScriptItem>& get_autorun_scripts() const { return m_autorun_scripts; }
    const std::vector<ScriptItem>& get_menu_scripts() const { return m_menu_scripts; }

    std::string get_scripts_dir() const { return m_scripts_dir; }
    std::string get_autorun_dir() const { return m_autorun_dir; }
    std::string get_icons_dir() const { return m_icons_dir; }

    lua_State* get_lua_state() const { return m_L; }

    // Helpers to access bound state
    Project* get_project() { return m_project; }
    double get_playhead_time() const { return m_playhead_time ? *m_playhead_time : 0.0; }
    void set_playhead_time(double t) {
        if (m_playhead_time) *m_playhead_time = t;
        if (m_on_set_playhead) m_on_set_playhead(t);
    }
    void push_undo_state() { if (m_on_push_undo) m_on_push_undo(); }
    void notify_project_modified() { if (m_on_project_modified) m_on_project_modified(); }
    void set_status(const std::string& msg, double dur = 4.0) { if (m_on_set_status) m_on_set_status(msg, dur); }
    std::string get_selected_clip_id() const { return m_selected_clip_id ? *m_selected_clip_id : ""; }
    void set_selected_clip_id(const std::string& id) {
        if (m_selected_clip_id) *m_selected_clip_id = id;
        if (m_on_select_clip) m_on_select_clip(id);
    }
    ClipManager* get_clip_manager() { return m_clip_manager; }
    Timeline* get_timeline() { return m_timeline; }

private:
    void register_lua_api();
    void scan_directories();
    void load_autorun_icons();
    void clear_textures();
    void call_panel_render(ScriptItem& item);

    // Static Lua C-API Dispatchers
    // GUI
    static int lua_gui_text(lua_State* L);
    static int lua_gui_text_colored(lua_State* L);
    static int lua_gui_text_disabled(lua_State* L);
    static int lua_gui_button(lua_State* L);
    static int lua_gui_small_button(lua_State* L);
    static int lua_gui_slider_float(lua_State* L);
    static int lua_gui_slider_int(lua_State* L);
    static int lua_gui_drag_float(lua_State* L);
    static int lua_gui_drag_int(lua_State* L);
    static int lua_gui_input_text(lua_State* L);
    static int lua_gui_input_float(lua_State* L);
    static int lua_gui_checkbox(lua_State* L);
    static int lua_gui_same_line(lua_State* L);
    static int lua_gui_separator(lua_State* L);
    static int lua_gui_spacing(lua_State* L);
    static int lua_gui_tooltip(lua_State* L);
    static int lua_gui_set_next_item_width(lua_State* L);

    // Timeline Placement & Navigation
    static int lua_timeline_get_playhead(lua_State* L);
    static int lua_timeline_set_playhead(lua_State* L);
    static int lua_timeline_get_duration(lua_State* L);
    static int lua_timeline_get_track_count(lua_State* L);
    static int lua_timeline_add_track(lua_State* L);
    static int lua_timeline_place_video(lua_State* L);
    static int lua_timeline_place_audio(lua_State* L);
    static int lua_timeline_place_image(lua_State* L);
    static int lua_timeline_get_selected_clip(lua_State* L);
    static int lua_timeline_select_clip(lua_State* L);
    static int lua_timeline_delete_clip(lua_State* L);
    static int lua_timeline_split_clip(lua_State* L);

    // Keyframing
    static int lua_timeline_add_keyframe(lua_State* L);
    static int lua_timeline_remove_keyframe(lua_State* L);
    static int lua_timeline_clear_keyframes(lua_State* L);
    static int lua_timeline_get_keyframes(lua_State* L);
    static int lua_timeline_evaluate_keyframe(lua_State* L);

    // Clip Transforms & Positioning
    static int lua_clip_get_position(lua_State* L);
    static int lua_clip_set_position(lua_State* L);
    static int lua_clip_get_scale(lua_State* L);
    static int lua_clip_set_scale(lua_State* L);
    static int lua_clip_get_rotation(lua_State* L);
    static int lua_clip_set_rotation(lua_State* L);
    static int lua_clip_get_opacity(lua_State* L);
    static int lua_clip_set_opacity(lua_State* L);
    static int lua_clip_get_volume(lua_State* L);
    static int lua_clip_set_volume(lua_State* L);
    static int lua_clip_get_time(lua_State* L);
    static int lua_clip_set_time(lua_State* L);
    static int lua_clip_get_info(lua_State* L);

    // Project & Minicut
    static int lua_project_save(lua_State* L);
    static int lua_project_load(lua_State* L);
    static int lua_project_import_video(lua_State* L);
    static int lua_project_import_audio(lua_State* L);
    static int lua_project_import_image(lua_State* L);
    static int lua_project_get_name(lua_State* L);
    static int lua_project_set_name(lua_State* L);
    static int lua_minicut_log(lua_State* L);
    static int lua_minicut_set_status(lua_State* L);
    static int lua_minicut_push_undo(lua_State* L);

    // Net API (HTTP requests, file downloads, file uploads)
    static int lua_net_get(lua_State* L);
    static int lua_net_post(lua_State* L);
    static int lua_net_download(lua_State* L);
    static int lua_net_upload(lua_State* L);
    static int lua_net_request(lua_State* L);

    lua_State* m_L = nullptr;

    Project* m_project = nullptr;
    Timeline* m_timeline = nullptr;
    ClipManager* m_clip_manager = nullptr;
    double* m_playhead_time = nullptr;
    std::string* m_selected_clip_id = nullptr;

    std::function<void()> m_on_push_undo;
    std::function<void()> m_on_project_modified;
    std::function<void(double)> m_on_set_playhead;
    std::function<void(const std::string&, double)> m_on_set_status;
    std::function<void(const std::string&)> m_on_select_clip;
    std::function<bool(const std::string&)> m_on_save_project;
    std::function<bool(const std::string&)> m_on_load_project;

    std::vector<ScriptItem> m_autorun_scripts;
    std::vector<ScriptItem> m_menu_scripts;

    std::string m_scripts_dir;
    std::string m_autorun_dir;
    std::string m_icons_dir;

    float m_shelf_height = 44.0f;
    bool m_shelf_visible = true;

    std::string m_current_render_script_id;
};
