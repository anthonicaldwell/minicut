#pragma once

#include "Common.h"
#include <lua.hpp>
#include "duktape.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <filesystem>
#include <functional>
#include <atomic>
#include <thread>
#include <GL/glew.h>

class Timeline;
class ClipManager;
class App;

struct ScriptItem {
    std::string id;            // Unique identifier, e.g. "quick_placement"
    std::string filename;      // e.g. "quick_placement.lua" or "quick_placement.js"
    std::string path;          // Absolute path
    std::string title;         // Human-friendly title, e.g. "Quick Placement"
    std::string description;   // Tooltip info
    bool is_autorun = false;   // Lives in ~/.config/minicut/autorun/
    bool is_loaded = false;    // Has file been executed?
    bool is_panel_open = false; // Plugin tool panel currently open
    bool is_javascript = false; // True if .js file
    GLuint icon_texture = 0;   // Texture for dock icon
    bool icon_checked = false; // Has icon search been performed?
};

struct ScriptFrameBuffer {
    int id = 0;
    int width = 0;
    int height = 0;
    int channels = 4;
    double time = 0.0;
    std::string clip_id;
    std::vector<uint8_t> rgba;
    std::string applied_shader;
    std::unordered_map<std::string, float> applied_uniforms;
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
    bool run_js_file(const std::string& file_path);
    bool run_js_string(const std::string& js_code);
    duk_context* get_js_context() const { return m_ctx; }

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
    void set_app(App* app) { m_app = app; }
    App* get_app() { return m_app; }

    void set_mock_ai_response(const std::string& resp) {
        m_mock_ai_response = resp;
        m_has_mock_ai_response = true;
    }
    void clear_mock_ai_response() {
        m_mock_ai_response.clear();
        m_has_mock_ai_response = false;
    }
    bool has_mock_ai_response() const { return m_has_mock_ai_response; }
    std::string get_mock_ai_response() const { return m_mock_ai_response; }
    std::unordered_map<int, ScriptFrameBuffer>& get_frame_buffers() { return m_frame_buffers; }
    std::unordered_map<int, std::string>& get_shaders() { return m_shaders; }

    bool is_rendering_shader() const { return m_is_rendering_shader.load(); }
    float get_shader_progress() const { return m_shader_progress.load(); }
    void set_rendering_shader(bool active, float p = 0.0f) {
        m_is_rendering_shader.store(active);
        m_shader_progress.store(p);
    }
    void update_shader_progress(float p) {
        m_shader_progress.store(p);
    }

    bool render_shader_across_clip(Clip* clip,
                                  const std::string& shader_src,
                                  const std::unordered_map<std::string, float>& uniforms,
                                  bool create_new,
                                  int target_track,
                                  double new_start_time,
                                  std::string& out_clip_id);
    void poll_shader_rendering();

private:
    void register_lua_api();
    void scan_directories();
    void load_autorun_icons();
    void clear_textures();
    void call_panel_render(ScriptItem& item);

public:
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
    static int lua_gui_file_dialog(lua_State* L);
    static int lua_gui_native_file_dialog(lua_State* L);
    static int lua_gui_builtin_file_dialog(lua_State* L);
    static int lua_gui_popout_preview(lua_State* L);
    static int lua_gui_is_preview_popped_out(lua_State* L);
    static int lua_gui_set_preview_aspect_mode(lua_State* L);
    static int lua_gui_get_preview_aspect_mode(lua_State* L);
    static int lua_gui_set_transform_tool(lua_State* L);
    static int lua_gui_get_transform_tool(lua_State* L);

    // Timeline Placement & Navigation
    static int lua_timeline_get_playhead(lua_State* L);
    static int lua_timeline_set_playhead(lua_State* L);
    static int lua_timeline_get_duration(lua_State* L);
    static int lua_timeline_get_track_count(lua_State* L);
    static int lua_timeline_add_track(lua_State* L);
    static int lua_timeline_place_video(lua_State* L);
    static int lua_timeline_place_audio(lua_State* L);
    static int lua_timeline_place_image(lua_State* L);
    static int lua_timeline_place_text_clip(lua_State* L);
    static int lua_timeline_get_selected_clip(lua_State* L);
    static int lua_timeline_get_selected_clips(lua_State* L);
    static int lua_timeline_select_clip(lua_State* L);
    static int lua_timeline_select_clips(lua_State* L);
    static int lua_timeline_clear_selection(lua_State* L);
    static int lua_timeline_select_all(lua_State* L);
    static int lua_timeline_delete_clip(lua_State* L);
    static int lua_timeline_split_clip(lua_State* L);

    // Track Reordering & Renaming
    static int lua_timeline_move_track_up(lua_State* L);
    static int lua_timeline_move_track_down(lua_State* L);
    static int lua_timeline_move_track(lua_State* L);
    static int lua_timeline_swap_tracks(lua_State* L);
    static int lua_timeline_set_track_name(lua_State* L);
    static int lua_timeline_get_track_name(lua_State* L);

    // Assets API
    static int lua_assets_create_text(lua_State* L);
    static int lua_assets_get_scale(lua_State* L);
    static int lua_assets_get_scale_x(lua_State* L);
    static int lua_assets_get_scale_y(lua_State* L);
    static int lua_assets_set_scale(lua_State* L);
    static int lua_assets_set_scale_x(lua_State* L);
    static int lua_assets_set_scale_y(lua_State* L);

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
    static int lua_clip_get_scale_xy(lua_State* L);
    static int lua_clip_get_scale_x(lua_State* L);
    static int lua_clip_get_scale_y(lua_State* L);
    static int lua_clip_set_scale(lua_State* L);
    static int lua_clip_set_scale_x(lua_State* L);
    static int lua_clip_set_scale_y(lua_State* L);
    static int lua_clip_get_rotation(lua_State* L);
    static int lua_clip_set_rotation(lua_State* L);
    static int lua_clip_get_opacity(lua_State* L);
    static int lua_clip_set_opacity(lua_State* L);
    static int lua_clip_get_volume(lua_State* L);
    static int lua_clip_set_volume(lua_State* L);
    static int lua_clip_get_time(lua_State* L);
    static int lua_clip_set_time(lua_State* L);
    static int lua_clip_get_info(lua_State* L);
    static int lua_clip_get_transform(lua_State* L);
    static int lua_clip_set_transform(lua_State* L);
    static int lua_clip_reset_transform(lua_State* L);

    // Raw Frame Data, Shaders & SFX Pipeline API
    static int lua_clip_get_frame(lua_State* L);
    static int lua_clip_set_frame(lua_State* L);
    static int lua_clip_create_from_frame(lua_State* L);
    static int lua_shader_create(lua_State* L);
    static int lua_shader_set_uniform(lua_State* L);
    static int lua_shader_apply(lua_State* L);

    // Localization API
    static int lua_locale_get(lua_State* L);
    static int lua_locale_get_language(lua_State* L);
    static int lua_locale_set_language(lua_State* L);
    static int lua_locale_is_rtl(lua_State* L);
    static int lua_locale_register(lua_State* L);

    // AI Library
    static int lua_ai_is_available(lua_State* L);
    static int lua_ai_is_enabled(lua_State* L);
    static int lua_ai_set_enabled(lua_State* L);
    static int lua_ai_has_api_key(lua_State* L);
    static int lua_ai_set_api_key(lua_State* L);
    static int lua_ai_is_busy(lua_State* L);
    static int lua_ai_get_status(lua_State* L);
    static int lua_ai_set_mock_response(lua_State* L);
    static int lua_ai_clear_mock_response(lua_State* L);
    static int lua_ai_prompt(lua_State* L);
    static int lua_ai_prompt_json(lua_State* L);
    static int lua_ai_parse_json(lua_State* L);
    static int lua_ai_to_json(lua_State* L);
    static int lua_ai_struct(lua_State* L);

    // Export API
    static int lua_export_get_audio_progress(lua_State* L);
    static int lua_export_get_progress(lua_State* L);
    static int lua_export_is_exporting(lua_State* L);
    static int lua_export_get_status(lua_State* L);

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

    // Static JS C-API Dispatchers
    // GUI
    static duk_ret_t js_gui_text(duk_context* ctx);
    static duk_ret_t js_gui_text_colored(duk_context* ctx);
    static duk_ret_t js_gui_text_disabled(duk_context* ctx);
    static duk_ret_t js_gui_button(duk_context* ctx);
    static duk_ret_t js_gui_small_button(duk_context* ctx);
    static duk_ret_t js_gui_slider_float(duk_context* ctx);
    static duk_ret_t js_gui_slider_int(duk_context* ctx);
    static duk_ret_t js_gui_drag_float(duk_context* ctx);
    static duk_ret_t js_gui_drag_int(duk_context* ctx);
    static duk_ret_t js_gui_input_text(duk_context* ctx);
    static duk_ret_t js_gui_checkbox(duk_context* ctx);
    static duk_ret_t js_gui_same_line(duk_context* ctx);
    static duk_ret_t js_gui_separator(duk_context* ctx);
    static duk_ret_t js_gui_spacing(duk_context* ctx);
    static duk_ret_t js_gui_tooltip(duk_context* ctx);
    static duk_ret_t js_gui_set_next_item_width(duk_context* ctx);
    static duk_ret_t js_gui_file_dialog(duk_context* ctx);
    static duk_ret_t js_gui_native_file_dialog(duk_context* ctx);
    static duk_ret_t js_gui_builtin_file_dialog(duk_context* ctx);
    static duk_ret_t js_gui_popout_preview(duk_context* ctx);
    static duk_ret_t js_gui_is_preview_popped_out(duk_context* ctx);
    static duk_ret_t js_gui_set_preview_aspect_mode(duk_context* ctx);
    static duk_ret_t js_gui_get_preview_aspect_mode(duk_context* ctx);
    static duk_ret_t js_gui_set_transform_tool(duk_context* ctx);
    static duk_ret_t js_gui_get_transform_tool(duk_context* ctx);

    // Timeline
    static duk_ret_t js_timeline_get_playhead(duk_context* ctx);
    static duk_ret_t js_timeline_set_playhead(duk_context* ctx);
    static duk_ret_t js_timeline_get_duration(duk_context* ctx);
    static duk_ret_t js_timeline_get_track_count(duk_context* ctx);
    static duk_ret_t js_timeline_add_track(duk_context* ctx);
    static duk_ret_t js_timeline_place_video(duk_context* ctx);
    static duk_ret_t js_timeline_place_audio(duk_context* ctx);
    static duk_ret_t js_timeline_place_image(duk_context* ctx);
    static duk_ret_t js_timeline_place_text_clip(duk_context* ctx);
    static duk_ret_t js_timeline_get_selected_clip(duk_context* ctx);
    static duk_ret_t js_timeline_get_selected_clips(duk_context* ctx);
    static duk_ret_t js_timeline_select_clip(duk_context* ctx);
    static duk_ret_t js_timeline_select_clips(duk_context* ctx);
    static duk_ret_t js_timeline_clear_selection(duk_context* ctx);
    static duk_ret_t js_timeline_select_all(duk_context* ctx);
    static duk_ret_t js_timeline_delete_clip(duk_context* ctx);
    static duk_ret_t js_timeline_split_clip(duk_context* ctx);
    static duk_ret_t js_timeline_move_track_up(duk_context* ctx);
    static duk_ret_t js_timeline_move_track_down(duk_context* ctx);
    static duk_ret_t js_timeline_move_track(duk_context* ctx);
    static duk_ret_t js_timeline_swap_tracks(duk_context* ctx);
    static duk_ret_t js_timeline_set_track_name(duk_context* ctx);
    static duk_ret_t js_timeline_get_track_name(duk_context* ctx);
    static duk_ret_t js_timeline_add_keyframe(duk_context* ctx);
    static duk_ret_t js_timeline_remove_keyframe(duk_context* ctx);
    static duk_ret_t js_timeline_clear_keyframes(duk_context* ctx);
    static duk_ret_t js_timeline_get_keyframes(duk_context* ctx);
    static duk_ret_t js_timeline_evaluate_keyframe(duk_context* ctx);

    // Clip
    static duk_ret_t js_clip_get_position(duk_context* ctx);
    static duk_ret_t js_clip_set_position(duk_context* ctx);
    static duk_ret_t js_clip_get_scale(duk_context* ctx);
    static duk_ret_t js_clip_get_scale_xy(duk_context* ctx);
    static duk_ret_t js_clip_get_scale_x(duk_context* ctx);
    static duk_ret_t js_clip_get_scale_y(duk_context* ctx);
    static duk_ret_t js_clip_set_scale(duk_context* ctx);
    static duk_ret_t js_clip_set_scale_x(duk_context* ctx);
    static duk_ret_t js_clip_set_scale_y(duk_context* ctx);
    static duk_ret_t js_clip_get_rotation(duk_context* ctx);
    static duk_ret_t js_clip_set_rotation(duk_context* ctx);
    static duk_ret_t js_clip_get_opacity(duk_context* ctx);
    static duk_ret_t js_clip_set_opacity(duk_context* ctx);
    static duk_ret_t js_clip_get_volume(duk_context* ctx);
    static duk_ret_t js_clip_set_volume(duk_context* ctx);
    static duk_ret_t js_clip_get_time(duk_context* ctx);
    static duk_ret_t js_clip_set_time(duk_context* ctx);
    static duk_ret_t js_clip_get_info(duk_context* ctx);
    static duk_ret_t js_clip_get_transform(duk_context* ctx);
    static duk_ret_t js_clip_set_transform(duk_context* ctx);
    static duk_ret_t js_clip_reset_transform(duk_context* ctx);

    // Raw Frame Data, Shaders & SFX Pipeline API
    static duk_ret_t js_clip_get_frame(duk_context* ctx);
    static duk_ret_t js_clip_set_frame(duk_context* ctx);
    static duk_ret_t js_clip_create_from_frame(duk_context* ctx);
    static duk_ret_t js_shader_create(duk_context* ctx);
    static duk_ret_t js_shader_set_uniform(duk_context* ctx);
    static duk_ret_t js_shader_apply(duk_context* ctx);

    // Localization API
    static duk_ret_t js_locale_get(duk_context* ctx);
    static duk_ret_t js_locale_get_language(duk_context* ctx);
    static duk_ret_t js_locale_set_language(duk_context* ctx);
    static duk_ret_t js_locale_is_rtl(duk_context* ctx);
    static duk_ret_t js_locale_register(duk_context* ctx);

    // AI Library
    static duk_ret_t js_ai_is_available(duk_context* ctx);
    static duk_ret_t js_ai_is_enabled(duk_context* ctx);
    static duk_ret_t js_ai_set_enabled(duk_context* ctx);
    static duk_ret_t js_ai_has_api_key(duk_context* ctx);
    static duk_ret_t js_ai_set_api_key(duk_context* ctx);
    static duk_ret_t js_ai_is_busy(duk_context* ctx);
    static duk_ret_t js_ai_get_status(duk_context* ctx);
    static duk_ret_t js_ai_set_mock_response(duk_context* ctx);
    static duk_ret_t js_ai_clear_mock_response(duk_context* ctx);
    static duk_ret_t js_ai_prompt(duk_context* ctx);
    static duk_ret_t js_ai_prompt_json(duk_context* ctx);
    static duk_ret_t js_ai_parse_json(duk_context* ctx);
    static duk_ret_t js_ai_to_json(duk_context* ctx);
    static duk_ret_t js_ai_struct(duk_context* ctx);

    // JS Export API
    static duk_ret_t js_export_get_audio_progress(duk_context* ctx);
    static duk_ret_t js_export_get_progress(duk_context* ctx);
    static duk_ret_t js_export_is_exporting(duk_context* ctx);
    static duk_ret_t js_export_get_status(duk_context* ctx);

    // Project & Assets
    static duk_ret_t js_project_save(duk_context* ctx);
    static duk_ret_t js_project_load(duk_context* ctx);
    static duk_ret_t js_project_import_video(duk_context* ctx);
    static duk_ret_t js_project_import_audio(duk_context* ctx);
    static duk_ret_t js_project_import_image(duk_context* ctx);
    static duk_ret_t js_project_get_name(duk_context* ctx);
    static duk_ret_t js_project_set_name(duk_context* ctx);
    static duk_ret_t js_assets_create_text(duk_context* ctx);
    static duk_ret_t js_assets_get_scale(duk_context* ctx);
    static duk_ret_t js_assets_get_scale_x(duk_context* ctx);
    static duk_ret_t js_assets_get_scale_y(duk_context* ctx);
    static duk_ret_t js_assets_set_scale(duk_context* ctx);
    static duk_ret_t js_assets_set_scale_x(duk_context* ctx);
    static duk_ret_t js_assets_set_scale_y(duk_context* ctx);

    // Minicut & Utilities
    static duk_ret_t js_minicut_log(duk_context* ctx);
    static duk_ret_t js_minicut_set_status(duk_context* ctx);
    static duk_ret_t js_minicut_push_undo(duk_context* ctx);

    // Net
    static duk_ret_t js_net_get(duk_context* ctx);
    static duk_ret_t js_net_post(duk_context* ctx);
    static duk_ret_t js_net_download(duk_context* ctx);
    static duk_ret_t js_net_upload(duk_context* ctx);
    static duk_ret_t js_net_request(duk_context* ctx);

    void register_js_api();
    bool execute_js_script(ScriptItem& item);
    void call_js_panel_render(ScriptItem& item);

    lua_State* m_L = nullptr;
    duk_context* m_ctx = nullptr;

    Project* m_project = nullptr;
    Timeline* m_timeline = nullptr;
    ClipManager* m_clip_manager = nullptr;
    double* m_playhead_time = nullptr;
    std::string* m_selected_clip_id = nullptr;
    App* m_app = nullptr;

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

    // Frame Buffers, SFX & Shader caches
    std::unordered_map<int, ScriptFrameBuffer> m_frame_buffers;
    int m_next_frame_id = 1;
    std::unordered_map<int, std::string> m_shaders;
    int m_next_shader_id = 1;

    // AI Mock testing support
    std::string m_mock_ai_response;
    bool m_has_mock_ai_response = false;

    // Shader rendering across clip & progress
    std::atomic<bool> m_is_rendering_shader{false};
    std::atomic<float> m_shader_progress{0.0f};
    std::atomic<bool> m_shader_task_done{false};
    std::thread* m_shader_thread = nullptr;

    std::string m_pending_asset_id;
    std::string m_pending_video_path;
    double m_pending_fps = 30.0;
    double m_pending_duration = 0.0;
    int m_pending_total_frames = 0;
};
