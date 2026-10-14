#pragma once

#include "Common.h"
#include <string>
#include <vector>
#include <functional>

class KeyframeEditor {
public:
    KeyframeEditor();
    ~KeyframeEditor();

    void set_selected_clip(const std::string& clip_id) { m_selected_clip_id = clip_id; }
    const std::string& get_selected_clip() const { return m_selected_clip_id; }

    void render_ui(Project& project, double playhead_time,
                   const std::function<void(double new_playhead)>& on_scrub,
                   const std::function<void()>& on_project_changed);

private:
    void render_empty_context(Project& project);
    void render_image_tools(Project& project, Clip* clip, const Track& track, double playhead_time,
                            const std::function<void(double)>& on_scrub,
                            const std::function<void()>& on_project_changed);
    void render_video_tools(Project& project, Clip* clip, const Track& track, double playhead_time,
                            const std::function<void(double)>& on_scrub,
                            const std::function<void()>& on_project_changed);
    void render_audio_tools(Project& project, Clip* clip, const Track& track, double playhead_time,
                            const std::function<void(double)>& on_scrub,
                            const std::function<void()>& on_project_changed);
    void render_subtitle_tools(Project& project, Clip* clip, const Track& track, double playhead_time,
                               const std::function<void()>& on_project_changed);

    void render_parameter_row(Project& project, Clip* clip, const std::string& param, const char* label,
                              double rel_playhead, double default_val, float min_val, float max_val,
                              float speed, const char* format,
                              const std::function<void(double)>& on_scrub,
                              const std::function<void()>& on_project_changed);

    void render_curve_graph(Project& project, Clip* clip, const std::string& current_param,
                            float min_val, float max_val,
                            double playhead_time,
                            const std::function<void(double)>& on_scrub,
                            const std::function<void()>& on_project_changed);

    std::string m_selected_clip_id;
    int m_selected_param_idx = 0;
    int m_selected_kf_idx = -1;

    // Interaction state for curve graph
    bool m_dragging_node = false;
    int m_drag_node_idx = -1;
};
