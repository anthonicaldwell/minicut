#pragma once

#include "Common.h"
#include <imgui.h>
#include <string>
#include <functional>

class Timeline {
public:
    Timeline();
    ~Timeline();

    void render_ui(Project& project, double& playhead_time,
                   const std::string& selected_clip_id,
                   const std::function<void(const std::string& clip_id)>& on_select_clip,
                   const std::function<void(double new_time)>& on_scrub,
                   const std::function<void()>& on_project_changed,
                   const std::function<void(const std::string& prefab_id, double insert_time)>& on_add_prefab = nullptr);

    // Timeline editing actions (for Menu & Shortcuts)
    bool split_selected_clip(Project& project, double playhead_time);
    bool split_selected_clips(Project& project, double playhead_time);
    bool split_clip_at(Project& project, const std::string& clip_id, double playhead_time);
    bool delete_selected_clip(Project& project);
    bool delete_selected_clips(Project& project);
    void copy_selected_clip(const Project& project);
    void copy_selected_clips(const Project& project);
    void cut_selected_clip(Project& project);
    void cut_selected_clips(Project& project);
    bool paste_clip(Project& project, double target_time, int target_track_idx = -1);

    // Multi-Selection support
    const std::vector<std::string>& get_selected_clip_ids() const { return m_selected_clip_ids; }
    bool is_clip_selected(const std::string& clip_id) const;
    void select_clip(const std::string& clip_id, bool toggle = false, bool add_to_selection = false);
    void deselect_clip(const std::string& clip_id);
    void clear_selection();
    void select_all(const Project& project);

    // Track management & reordering
    void add_track(Project& project, const std::string& type);
    std::string add_subtitle_clip(Project& project, double start_time, double duration = 3.0, const std::string& text = "New Subtitle");
    void delete_track(Project& project, int track_idx);
    void move_track_up(Project& project, int track_idx);
    void move_track_down(Project& project, int track_idx);
    void swap_tracks(Project& project, int idx1, int idx2);
    void reorder_track(Project& project, int from_idx, int to_idx);

    // Track renaming
    void set_track_name(Project& project, int track_idx, const std::string& name);
    std::string get_track_name(const Project& project, int track_idx) const;
    static std::string get_display_track_name(const Track& track);

    int get_active_track_idx() const { return m_active_track_idx; }
    void set_active_track_idx(int idx) { m_active_track_idx = idx; }
    int get_hovered_track_idx() const { return m_hovered_track_idx; }
    int get_hovered_or_active_track() const { return (m_hovered_track_idx >= 0) ? m_hovered_track_idx : m_active_track_idx; }

    double get_mouse_hover_time() const { return m_mouse_hover_time; }

    // Scroll and zoom state
    float get_scroll_sec() const { return m_scroll_sec; }
    void set_scroll_sec(float s) { m_scroll_sec = s; }
    float get_scroll_track_y() const { return m_scroll_track_y; }
    void set_scroll_track_y(float y) { m_scroll_track_y = y; }
    float get_zoom() const { return m_pixels_per_sec; }
    void set_zoom(float z) { m_pixels_per_sec = z; }

    // Track control header width (resizable)
    float get_track_header_width() const { return m_track_header_w; }
    void set_track_header_width(float w) { m_track_header_w = std::clamp(w, 80.0f, 400.0f); }

    bool is_rename_modal_open() const { return m_open_rename_modal; }

private:
    float m_track_header_w = 145.0f;
    bool m_dragging_track_header = false;
    float m_pixels_per_sec = 60.0f;
    float m_scroll_sec = 0.0f;
    float m_scroll_track_y = 0.0f;
    bool m_snap_enabled = true;
    int m_active_track_idx = 0;
    int m_hovered_track_idx = -1;
    double m_mouse_hover_time = 0.0;

    // Multi-Selection State
    std::vector<std::string> m_selected_clip_ids;
    bool m_is_box_selecting = false;
    ImVec2 m_box_select_start = ImVec2(0, 0);
    ImVec2 m_box_select_cur = ImVec2(0, 0);

    // Track rename modal state
    int m_rename_track_idx = -1;
    char m_rename_track_buf[128] = {0};
    bool m_open_rename_modal = false;

    // Scrollbar and pan state
    bool m_dragging_scrollbar = false;
    float m_scrollbar_drag_orig_scroll = 0.0f;
    float m_scrollbar_drag_start_mouse_x = 0.0f;

    // Clipboard
    struct ClipboardItem {
        Clip clip;
        int track_offset = 0;
        double time_offset = 0.0;
        std::vector<Keyframe> keyframes;
    };
    bool m_has_clipboard = false;
    std::vector<ClipboardItem> m_clipboard_items;

    // Drag / Trim states
    enum class DragMode { None, MoveClip, TrimLeft, TrimRight, ScrubPlayhead, BoxSelect };
    DragMode m_drag_mode = DragMode::None;
    std::string m_drag_clip_id;
    int m_drag_track_idx = -1;
    double m_drag_start_mouse_time = 0.0;
    double m_drag_orig_start_time = 0.0;
    double m_drag_orig_duration = 0.0;
    double m_drag_orig_offset = 0.0;
    std::unordered_map<std::string, double> m_drag_multi_orig_times;

    double snap_time(double t, const Project& project, const std::string& exclude_clip_id);
    void render_rename_modal(Project& project, const std::function<void()>& on_project_changed);
};
