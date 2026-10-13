#include "Timeline.h"
#include "LocaleManager.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <filesystem>

namespace fs = std::filesystem;

Timeline::Timeline() {}
Timeline::~Timeline() {}

double Timeline::snap_time(double t, const Project& project, const std::string& exclude_clip_id) {
    if (!m_snap_enabled) return t;

    double snap_threshold_sec = 8.0 / m_pixels_per_sec;
    double best_time = t;
    double min_diff = snap_threshold_sec;

    // Snap to integer seconds
    double round_sec = std::round(t);
    if (std::abs(t - round_sec) < min_diff) {
        min_diff = std::abs(t - round_sec);
        best_time = round_sec;
    }

    // Snap to other clip start/end boundaries
    for (const auto& track : project.tracks) {
        for (const auto& clip : track.clips) {
            if (clip.id == exclude_clip_id) continue;

            double diff_start = std::abs(t - clip.start_time);
            if (diff_start < min_diff) {
                min_diff = diff_start;
                best_time = clip.start_time;
            }

            double diff_end = std::abs(t - clip.end_time());
            if (diff_end < min_diff) {
                min_diff = diff_end;
                best_time = clip.end_time();
            }
        }
    }

    return best_time;
}

void Timeline::add_track(Project& project, const std::string& type) {
    int next_num = 1;
    for (const auto& trk : project.tracks) {
        if (trk.type == type) next_num++;
    }

    Track t;
    t.type = type;
    t.name = (type == "video" ? "Video " : "Audio ") + std::to_string(next_num);
    t.id = "track_" + type + "_" + std::to_string(next_num);
    project.tracks.push_back(t);
}

void Timeline::delete_track(Project& project, int track_idx) {
    if (track_idx >= 0 && track_idx < static_cast<int>(project.tracks.size())) {
        for (const auto& clip : project.tracks[track_idx].clips) {
            project.remove_clip_keyframes(clip.id);
        }
        project.tracks.erase(project.tracks.begin() + track_idx);
        if (m_active_track_idx >= static_cast<int>(project.tracks.size())) {
            m_active_track_idx = std::max(0, static_cast<int>(project.tracks.size()) - 1);
        }
    }
}

void Timeline::move_track_up(Project& project, int track_idx) {
    if (track_idx > 0 && track_idx < static_cast<int>(project.tracks.size())) {
        std::swap(project.tracks[track_idx], project.tracks[track_idx - 1]);
        if (m_active_track_idx == track_idx) m_active_track_idx--;
        else if (m_active_track_idx == track_idx - 1) m_active_track_idx++;
    }
}

void Timeline::move_track_down(Project& project, int track_idx) {
    if (track_idx >= 0 && track_idx + 1 < static_cast<int>(project.tracks.size())) {
        std::swap(project.tracks[track_idx], project.tracks[track_idx + 1]);
        if (m_active_track_idx == track_idx) m_active_track_idx++;
        else if (m_active_track_idx == track_idx + 1) m_active_track_idx--;
    }
}

void Timeline::swap_tracks(Project& project, int idx1, int idx2) {
    if (idx1 >= 0 && idx1 < static_cast<int>(project.tracks.size()) &&
        idx2 >= 0 && idx2 < static_cast<int>(project.tracks.size()) && idx1 != idx2) {
        std::swap(project.tracks[idx1], project.tracks[idx2]);
    }
}

void Timeline::reorder_track(Project& project, int from_idx, int to_idx) {
    if (from_idx < 0 || from_idx >= static_cast<int>(project.tracks.size()) ||
        to_idx < 0 || to_idx >= static_cast<int>(project.tracks.size()) || from_idx == to_idx) return;
    Track trk = std::move(project.tracks[from_idx]);
    project.tracks.erase(project.tracks.begin() + from_idx);
    project.tracks.insert(project.tracks.begin() + to_idx, std::move(trk));
    m_active_track_idx = to_idx;
}

void Timeline::set_track_name(Project& project, int track_idx, const std::string& name) {
    if (track_idx >= 0 && track_idx < static_cast<int>(project.tracks.size())) {
        project.tracks[track_idx].name = name;
    }
}

std::string Timeline::get_track_name(const Project& project, int track_idx) const {
    if (track_idx >= 0 && track_idx < static_cast<int>(project.tracks.size())) {
        return project.tracks[track_idx].name;
    }
    return "";
}

std::string Timeline::get_display_track_name(const Track& track) {
    if (track.type == "video" && track.name.rfind("Video ", 0) == 0) {
        std::string num = track.name.substr(6);
        return std::string(_TR("track_video")) + " " + num;
    }
    if (track.type == "audio" && track.name.rfind("Audio ", 0) == 0) {
        std::string num = track.name.substr(6);
        return std::string(_TR("track_audio")) + " " + num;
    }
    return track.name;
}

bool Timeline::is_clip_selected(const std::string& clip_id) const {
    if (clip_id.empty()) return false;
    if (clip_id == m_drag_clip_id) return true;
    return std::find(m_selected_clip_ids.begin(), m_selected_clip_ids.end(), clip_id) != m_selected_clip_ids.end();
}

void Timeline::select_clip(const std::string& clip_id, bool toggle, bool add_to_selection) {
    if (clip_id.empty()) {
        clear_selection();
        return;
    }
    if (toggle) {
        auto it = std::find(m_selected_clip_ids.begin(), m_selected_clip_ids.end(), clip_id);
        if (it != m_selected_clip_ids.end()) {
            m_selected_clip_ids.erase(it);
            if (m_drag_clip_id == clip_id) {
                m_drag_clip_id = m_selected_clip_ids.empty() ? "" : m_selected_clip_ids.back();
            }
        } else {
            m_selected_clip_ids.push_back(clip_id);
            m_drag_clip_id = clip_id;
        }
    } else if (add_to_selection) {
        if (std::find(m_selected_clip_ids.begin(), m_selected_clip_ids.end(), clip_id) == m_selected_clip_ids.end()) {
            m_selected_clip_ids.push_back(clip_id);
        }
        m_drag_clip_id = clip_id;
    } else {
        m_selected_clip_ids.clear();
        m_selected_clip_ids.push_back(clip_id);
        m_drag_clip_id = clip_id;
    }
}

void Timeline::deselect_clip(const std::string& clip_id) {
    auto it = std::find(m_selected_clip_ids.begin(), m_selected_clip_ids.end(), clip_id);
    if (it != m_selected_clip_ids.end()) {
        m_selected_clip_ids.erase(it);
    }
    if (m_drag_clip_id == clip_id) {
        m_drag_clip_id = m_selected_clip_ids.empty() ? "" : m_selected_clip_ids.back();
    }
}

void Timeline::clear_selection() {
    m_selected_clip_ids.clear();
    m_drag_clip_id.clear();
}

void Timeline::select_all(const Project& project) {
    m_selected_clip_ids.clear();
    for (const auto& trk : project.tracks) {
        for (const auto& c : trk.clips) {
            m_selected_clip_ids.push_back(c.id);
        }
    }
    if (!m_selected_clip_ids.empty()) {
        m_drag_clip_id = m_selected_clip_ids.front();
    }
}

bool Timeline::split_clip_at(Project& project, const std::string& clip_id, double playhead_time) {
    int t_idx = -1, c_idx = -1;
    if (!project.find_clip(clip_id, &t_idx, &c_idx) || t_idx < 0 || c_idx < 0) return false;

    Clip& orig = project.tracks[t_idx].clips[c_idx];
    if (!orig.contains_time(playhead_time)) return false;

    double split_rel = playhead_time - orig.start_time;
    if (split_rel <= 0.05 || split_rel >= (orig.duration - 0.05)) {
        return false; // Too close to edges
    }

    // New second clip
    Clip second;
    second.id = "clip_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count() % 1000000);
    second.asset_id = orig.asset_id;
    second.start_time = playhead_time;
    second.duration = orig.duration - split_rel;
    second.offset = orig.offset + split_rel;
    second.pos_x = orig.pos_x;
    second.pos_y = orig.pos_y;
    second.rotation = orig.rotation;
    second.scale = orig.scale;
    second.opacity = orig.opacity;
    second.prefab_id = orig.prefab_id;
    second.prefab_name = orig.prefab_name;

    // Adjust first clip duration
    orig.duration = split_rel;

    // Partition keyframes
    std::vector<Keyframe> old_kfs = project.keyframes;
    project.remove_clip_keyframes(orig.id);

    for (const auto& kf : old_kfs) {
        if (kf.clip_id == orig.id) {
            if (kf.time <= split_rel) {
                project.keyframes.push_back(kf);
            } else {
                Keyframe new_kf = kf;
                new_kf.clip_id = second.id;
                new_kf.time = kf.time - split_rel;
                project.keyframes.push_back(new_kf);
            }
        }
    }

    m_drag_clip_id = orig.id;
    m_active_track_idx = t_idx;
    project.tracks[t_idx].clips.insert(project.tracks[t_idx].clips.begin() + c_idx + 1, second);
    return true;
}

bool Timeline::split_selected_clip(Project& project, double playhead_time) {
    // 1. If a clip is explicitly selected and under playhead, slice it
    if (!m_drag_clip_id.empty()) {
        if (split_clip_at(project, m_drag_clip_id, playhead_time)) {
            return true;
        }
    }

    // 2. If an active track is selected, slice clip on that track under playhead
    if (m_active_track_idx >= 0 && m_active_track_idx < static_cast<int>(project.tracks.size())) {
        for (const auto& clip : project.tracks[m_active_track_idx].clips) {
            if (clip.contains_time(playhead_time)) {
                if (split_clip_at(project, clip.id, playhead_time)) {
                    return true;
                }
            }
        }
    }

    // 3. Fallback: Check any track containing playhead_time
    for (const auto& track : project.tracks) {
        for (const auto& clip : track.clips) {
            if (clip.contains_time(playhead_time)) {
                if (split_clip_at(project, clip.id, playhead_time)) {
                    return true;
                }
            }
        }
    }
    return false;
}

bool Timeline::delete_selected_clips(Project& project) {
    if (m_selected_clip_ids.empty() && m_drag_clip_id.empty()) return false;
    std::vector<std::string> to_delete = m_selected_clip_ids;
    if (!m_drag_clip_id.empty() && std::find(to_delete.begin(), to_delete.end(), m_drag_clip_id) == to_delete.end()) {
        to_delete.push_back(m_drag_clip_id);
    }

    bool deleted_any = false;
    for (const auto& cid : to_delete) {
        for (auto& trk : project.tracks) {
            auto it = std::remove_if(trk.clips.begin(), trk.clips.end(),
                [&](const Clip& c) { return c.id == cid; });
            if (it != trk.clips.end()) {
                trk.clips.erase(it, trk.clips.end());
                deleted_any = true;
            }
        }
        project.remove_clip_keyframes(cid);
    }
    clear_selection();
    return deleted_any;
}

bool Timeline::delete_selected_clip(Project& project) {
    return delete_selected_clips(project);
}

void Timeline::copy_selected_clips(const Project& project) {
    m_clipboard_clips.clear();
    m_clipboard_keyframes.clear();
    std::vector<std::string> targets = m_selected_clip_ids;
    if (targets.empty() && !m_drag_clip_id.empty()) {
        targets.push_back(m_drag_clip_id);
    }
    for (const auto& cid : targets) {
        const Clip* c = project.find_clip(cid);
        if (c) {
            m_clipboard_clips.push_back(*c);
            for (const auto& kf : project.keyframes) {
                if (kf.clip_id == c->id) {
                    m_clipboard_keyframes.push_back(kf);
                }
            }
        }
    }
    if (!m_clipboard_clips.empty()) {
        m_clipboard_clip = m_clipboard_clips.front();
        m_has_clipboard = true;
    }
}

void Timeline::copy_selected_clip(const Project& project) {
    copy_selected_clips(project);
}

void Timeline::cut_selected_clips(Project& project) {
    copy_selected_clips(project);
    delete_selected_clips(project);
}

void Timeline::cut_selected_clip(Project& project) {
    cut_selected_clips(project);
}

bool Timeline::paste_clip(Project& project, double target_time, int target_track_idx) {
    if (!m_has_clipboard || project.tracks.empty()) return false;

    int dest_track = target_track_idx;
    if (dest_track < 0 || dest_track >= static_cast<int>(project.tracks.size())) {
        dest_track = m_hovered_track_idx;
    }
    if (dest_track < 0 || dest_track >= static_cast<int>(project.tracks.size())) {
        dest_track = m_active_track_idx;
    }
    if (dest_track < 0 || dest_track >= static_cast<int>(project.tracks.size())) {
        dest_track = 0;
    }

    m_active_track_idx = dest_track;

    Clip new_clip = m_clipboard_clip;
    new_clip.id = "clip_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count() % 1000000);
    new_clip.start_time = std::max(0.0, target_time);

    project.tracks[dest_track].clips.push_back(new_clip);

    for (auto kf : m_clipboard_keyframes) {
        kf.clip_id = new_clip.id;
        project.keyframes.push_back(kf);
    }
    m_drag_clip_id = new_clip.id;
    return true;
}

void Timeline::render_ui(Project& project, double& playhead_time,
                         const std::string& selected_clip_id,
                         const std::function<void(const std::string& clip_id)>& on_select_clip,
                         const std::function<void(double new_time)>& on_scrub,
                         const std::function<void()>& on_project_changed,
                         const std::function<void(const std::string& prefab_id, double insert_time)>& on_add_prefab) {
    if (!selected_clip_id.empty()) {
        m_drag_clip_id = selected_clip_id;
    }

    // Toolbar header
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5, 3));

    if (ImGui::Button(_TR("timeline_add_video"))) {
        add_track(project, "video");
        if (on_project_changed) on_project_changed();
    }
    ImGui::SameLine();
    if (ImGui::Button(_TR("timeline_add_audio"))) {
        add_track(project, "audio");
        if (on_project_changed) on_project_changed();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Snap", &m_snap_enabled);

    ImGui::SameLine();
    ImGui::Text("Zoom:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80.0f);
    ImGui::SliderFloat("##zoom", &m_pixels_per_sec, 15.0f, 300.0f, "%.0f px/s");

    ImGui::SameLine();
    if (ImGui::SmallButton("Fit")) {
        double dur = project.get_total_duration();
        float avail_w = ImGui::GetContentRegionAvail().x - 120.0f;
        if (dur > 0.0 && avail_w > 100.0f) {
            m_pixels_per_sec = std::clamp(static_cast<float>(avail_w / dur), 15.0f, 300.0f);
            m_scroll_sec = 0.0f;
        }
    }

    float total_proj_dur = static_cast<float>(std::max(10.0, std::max(project.get_total_duration(), playhead_time)));
    float avail_canvas_w = ImGui::GetContentRegionAvail().x - m_track_header_w;
    float max_scroll_sec = std::max(0.0f, total_proj_dur + 5.0f - (avail_canvas_w * 0.25f / m_pixels_per_sec));
    m_scroll_sec = std::clamp(m_scroll_sec, 0.0f, max_scroll_sec);

    ImGui::SameLine();
    ImGui::Text("Scroll:");
    ImGui::SameLine();
    if (ImGui::SmallButton("|<")) {
        m_scroll_sec = 0.0f;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(">|")) {
        m_scroll_sec = max_scroll_sec;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(85.0f);
    ImGui::SliderFloat("##hscroll_slider", &m_scroll_sec, 0.0f, max_scroll_sec, "%.1fs");

    ImGui::SameLine();
    ImGui::TextDisabled("| Cursor: %s", format_timecode(m_mouse_hover_time, true).c_str());

    ImGui::PopStyleVar();
    ImGui::Separator();

    // Timeline Canvas setup
    ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
    ImVec2 canvas_size = ImGui::GetContentRegionAvail();
    if (canvas_size.y < 120.0f) canvas_size.y = 120.0f;

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImGuiIO& io = ImGui::GetIO();

    const float track_header_w = m_track_header_w;
    const float ruler_h = 24.0f;
    const float track_h = 44.0f;
    const float scrollbar_h = 16.0f;
    float content_x = canvas_pos.x + track_header_w;
    float content_w = canvas_size.x - track_header_w;
    float tracks_bottom_y = canvas_pos.y + canvas_size.y - scrollbar_h;
    float visible_track_area_h = tracks_bottom_y - (canvas_pos.y + ruler_h);
    float total_tracks_h = project.tracks.size() * track_h;
    float max_track_scroll_y = std::max(0.0f, total_tracks_h - visible_track_area_h);
    m_scroll_track_y = std::clamp(m_scroll_track_y, 0.0f, max_track_scroll_y);

    auto time_to_screen_x = [&](double t) -> float {
        return content_x + static_cast<float>((t - m_scroll_sec) * m_pixels_per_sec);
    };

    auto screen_x_to_time = [&](float sx) -> double {
        return m_scroll_sec + (sx - content_x) / m_pixels_per_sec;
    };

    ImVec2 mouse_pos = io.MousePos;

    // Mouse Canvas Hover & Pan / Wheel Interactions
    bool is_canvas_hovered = (mouse_pos.x >= canvas_pos.x && mouse_pos.x <= canvas_pos.x + canvas_size.x &&
                              mouse_pos.y >= canvas_pos.y && mouse_pos.y <= canvas_pos.y + canvas_size.y);
    if (is_canvas_hovered) {
        // Ctrl + MouseWheel: Zoom centered on cursor time
        if (io.KeyCtrl && io.MouseWheel != 0.0f) {
            float zoom_factor = (io.MouseWheel > 0) ? 1.2f : 0.83f;
            double mouse_t = screen_x_to_time(mouse_pos.x);
            m_pixels_per_sec = std::clamp(m_pixels_per_sec * zoom_factor, 15.0f, 300.0f);
            m_scroll_sec = std::clamp(static_cast<float>(mouse_t - (mouse_pos.x - content_x) / m_pixels_per_sec), 0.0f, max_scroll_sec);
        }
        // Shift + Wheel or horizontal wheel: Horizontal Scroll
        else if (io.KeyShift && io.MouseWheel != 0.0f) {
            m_scroll_sec = std::clamp(m_scroll_sec - (io.MouseWheel * 60.0f / m_pixels_per_sec), 0.0f, max_scroll_sec);
        }
        else if (io.MouseWheelH != 0.0f) {
            m_scroll_sec = std::clamp(m_scroll_sec + (io.MouseWheelH * 60.0f / m_pixels_per_sec), 0.0f, max_scroll_sec);
        }
        // Standard vertical wheel: Vertical Track Scroll
        else if (!io.KeyShift && !io.KeyCtrl && io.MouseWheel != 0.0f) {
            m_scroll_track_y = std::clamp(m_scroll_track_y - (io.MouseWheel * track_h * 0.8f), 0.0f, max_track_scroll_y);
        }

        // Middle Mouse Button or Alt + Left Click: Panning
        if (ImGui::IsMouseDown(2) || (io.KeyAlt && ImGui::IsMouseDown(0))) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
            if (io.MouseDelta.x != 0.0f) {
                m_scroll_sec = std::clamp(m_scroll_sec - (io.MouseDelta.x / m_pixels_per_sec), 0.0f, max_scroll_sec);
            }
            if (io.MouseDelta.y != 0.0f) {
                m_scroll_track_y = std::clamp(m_scroll_track_y - io.MouseDelta.y, 0.0f, max_track_scroll_y);
            }
        }
    }

    if (mouse_pos.x >= content_x && mouse_pos.x <= (canvas_pos.x + canvas_size.x)) {
        m_mouse_hover_time = std::max(0.0, screen_x_to_time(mouse_pos.x));
    }

    // Interactive Resizing for Track Control Header (Splitter)
    float splitter_x = content_x;
    bool is_header_splitter_hovered = (is_canvas_hovered &&
                                       mouse_pos.x >= splitter_x - 4.0f && mouse_pos.x <= splitter_x + 4.0f &&
                                       mouse_pos.y >= canvas_pos.y && mouse_pos.y <= tracks_bottom_y);

    if (m_dragging_track_header) {
        if (!io.MouseDown[0]) {
            m_dragging_track_header = false;
            if (on_project_changed) on_project_changed();
        } else {
            m_track_header_w = std::clamp(mouse_pos.x - canvas_pos.x, 80.0f, 400.0f);
        }
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    } else if (is_header_splitter_hovered) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        if (ImGui::IsMouseClicked(0)) {
            m_dragging_track_header = true;
        }
    }

    // Backgrounds
    draw_list->AddRectFilled(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, tracks_bottom_y),
                             IM_COL32(24, 27, 31, 255));
    // Header sidebar background
    draw_list->AddRectFilled(canvas_pos, ImVec2(content_x, tracks_bottom_y),
                             IM_COL32(31, 35, 41, 255));
    // Ruler background
    draw_list->AddRectFilled(ImVec2(content_x, canvas_pos.y), ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + ruler_h),
                             IM_COL32(35, 39, 46, 255));
    // Boundary line / Resizable splitter line
    ImU32 splitter_line_col = (m_dragging_track_header || is_header_splitter_hovered)
                              ? IM_COL32(61, 174, 233, 255)
                              : IM_COL32(55, 62, 72, 255);
    float splitter_thickness = (m_dragging_track_header || is_header_splitter_hovered) ? 2.5f : 1.0f;
    draw_list->AddLine(ImVec2(content_x, canvas_pos.y), ImVec2(content_x, tracks_bottom_y),
                       splitter_line_col, splitter_thickness);
    draw_list->AddLine(ImVec2(content_x, canvas_pos.y + ruler_h), ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + ruler_h),
                       IM_COL32(55, 62, 72, 255));

    // Draw Ruler ticks
    double visible_start_t = std::max(0.0, static_cast<double>(m_scroll_sec));
    double visible_end_t = screen_x_to_time(canvas_pos.x + canvas_size.x) + 2.0;

    int tick_step_sec = 1;
    if (m_pixels_per_sec < 30.0f) tick_step_sec = 5;
    else if (m_pixels_per_sec < 60.0f) tick_step_sec = 2;

    int start_tick = static_cast<int>(visible_start_t / tick_step_sec) * tick_step_sec;
    for (int t = start_tick; t <= static_cast<int>(visible_end_t); t += tick_step_sec) {
        float tx = time_to_screen_x(t);
        if (tx < content_x || tx > canvas_pos.x + canvas_size.x) continue;

        draw_list->AddLine(ImVec2(tx, canvas_pos.y + ruler_h - 10), ImVec2(tx, canvas_pos.y + ruler_h),
                           IM_COL32(180, 190, 205, 255));

        std::string tc = format_timecode(t, false);
        draw_list->AddText(ImVec2(tx + 4, canvas_pos.y + 3), IM_COL32(190, 200, 215, 255), tc.c_str());

        // Vertical faint grid across tracks
        draw_list->AddLine(ImVec2(tx, canvas_pos.y + ruler_h), ImVec2(tx, tracks_bottom_y),
                           IM_COL32(38, 43, 50, 255));
    }

    // Ruler scrubbing
    bool in_ruler = (mouse_pos.x >= content_x && mouse_pos.x <= (canvas_pos.x + canvas_size.x) &&
                     mouse_pos.y >= canvas_pos.y && mouse_pos.y <= (canvas_pos.y + ruler_h));

    if (in_ruler && ImGui::IsMouseClicked(0)) {
        m_drag_mode = DragMode::ScrubPlayhead;
    }

    if (m_drag_mode == DragMode::ScrubPlayhead && ImGui::IsMouseDown(0)) {
        double raw_t = std::max(0.0, screen_x_to_time(mouse_pos.x));
        playhead_time = snap_time(raw_t, project, "");
        if (on_scrub) on_scrub(playhead_time);
    }

    // Tracks rendering with vertical clipping
    draw_list->PushClipRect(ImVec2(canvas_pos.x, canvas_pos.y + ruler_h), ImVec2(canvas_pos.x + canvas_size.x, tracks_bottom_y), true);

    float start_track_y = canvas_pos.y + ruler_h - m_scroll_track_y;
    std::string clip_to_select = "";

    for (size_t t = 0; t < project.tracks.size(); ++t) {
        Track& track = project.tracks[t];
        float track_y = start_track_y + (t * track_h);

        // Cull offscreen tracks
        if (track_y + track_h < canvas_pos.y + ruler_h || track_y > tracks_bottom_y) {
            continue;
        }

        bool is_active_track = (static_cast<int>(t) == m_active_track_idx);

        // Header Background
        ImU32 header_bg = is_active_track ? IM_COL32(45, 52, 63, 255) : IM_COL32(33, 38, 45, 255);
        draw_list->AddRectFilled(ImVec2(canvas_pos.x, track_y), ImVec2(content_x, track_y + track_h), header_bg);
        draw_list->AddLine(ImVec2(canvas_pos.x, track_y + track_h), ImVec2(canvas_pos.x + canvas_size.x, track_y + track_h),
                           IM_COL32(45, 50, 58, 255));

        // Clip header controls so they NEVER overflow or overlap the timeline lanes!
        draw_list->PushClipRect(ImVec2(canvas_pos.x, track_y), ImVec2(content_x - 1.0f, track_y + track_h), true);

        // Track Name & selection
        std::string badge = (track.type == "video") ? "[V]" : "[A]";
        ImU32 badge_col = (track.type == "video") ? IM_COL32(61, 174, 233, 255) : IM_COL32(46, 204, 113, 255);
        draw_list->AddText(ImVec2(canvas_pos.x + 6, track_y + 4), badge_col, badge.c_str());
        std::string display_name = get_display_track_name(track);
        draw_list->AddText(ImVec2(canvas_pos.x + 28, track_y + 4), IM_COL32(230, 235, 245, 255), display_name.c_str());

        // Mute / Solo mini buttons in header
        ImGui::SetCursorScreenPos(ImVec2(canvas_pos.x + 6, track_y + 20));
        std::string mute_id = (track.muted ? "M##" : "m##") + std::to_string(t);
        if (ImGui::SmallButton(mute_id.c_str())) {
            track.muted = !track.muted;
            if (on_project_changed) on_project_changed();
        }
        ImGui::SameLine();
        std::string solo_id = (track.solo ? "S##" : "s##") + std::to_string(t);
        if (ImGui::SmallButton(solo_id.c_str())) {
            track.solo = !track.solo;
            if (on_project_changed) on_project_changed();
        }
        ImGui::SameLine();
        std::string del_id = "X##" + std::to_string(t);
        if (ImGui::SmallButton(del_id.c_str())) {
            delete_track(project, static_cast<int>(t));
            if (on_project_changed) on_project_changed();
            draw_list->PopClipRect();
            break;
        }

        // Reorder buttons (Move Up / Move Down)
        ImGui::SameLine();
        std::string up_id = "^##" + std::to_string(t);
        if (ImGui::SmallButton(up_id.c_str())) {
            if (t > 0) {
                move_track_up(project, static_cast<int>(t));
                if (on_project_changed) on_project_changed();
                draw_list->PopClipRect();
                break;
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", _TR("timeline_move_up"));

        ImGui::SameLine();
        std::string down_id = "v##" + std::to_string(t);
        if (ImGui::SmallButton(down_id.c_str())) {
            if (t + 1 < project.tracks.size()) {
                move_track_down(project, static_cast<int>(t));
                if (on_project_changed) on_project_changed();
                draw_list->PopClipRect();
                break;
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", _TR("timeline_move_down"));

        // Render Depth control for video tracks (0 to 100)
        if (track.type == "video") {
            ImGui::SameLine();
            float cur_x = ImGui::GetCursorScreenPos().x;
            float rem_w = std::max(20.0f, content_x - cur_x - 4.0f);
            float item_w = std::min(45.0f, rem_w);
            ImGui::SetNextItemWidth(item_w);
            std::string depth_id = "##depth_" + std::to_string(t);
            if (ImGui::DragInt(depth_id.c_str(), &track.depth, 1.0f, 0, 100, "Z:%d")) {
                if (on_project_changed) on_project_changed();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Render Depth / Z-Order (0 to 100)\nHigher numbers render on top");
            }
        }

        draw_list->PopClipRect();

        bool is_timeline_window_hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);

        // Header click, double-click (rename), and right-click (context menu)
        bool in_header = (is_timeline_window_hovered &&
                          mouse_pos.x >= canvas_pos.x && mouse_pos.x < content_x &&
                          mouse_pos.y >= track_y && mouse_pos.y < track_y + track_h);
        if (in_header) {
            if (ImGui::IsMouseClicked(0)) {
                m_active_track_idx = static_cast<int>(t);
            }
            if (ImGui::IsMouseDoubleClicked(0)) {
                m_rename_track_idx = static_cast<int>(t);
                strncpy(m_rename_track_buf, track.name.c_str(), sizeof(m_rename_track_buf) - 1);
                m_rename_track_buf[sizeof(m_rename_track_buf) - 1] = '\0';
                m_open_rename_modal = true;
            }
            if (ImGui::IsMouseClicked(1)) {
                m_active_track_idx = static_cast<int>(t);
                std::string hdr_ctx = "TrackHdrCtx_" + std::to_string(t);
                ImGui::OpenPopup(hdr_ctx.c_str());
            }
        }

        // Header context popup
        std::string hdr_ctx_id = "TrackHdrCtx_" + std::to_string(t);
        if (ImGui::BeginPopup(hdr_ctx_id.c_str())) {
            ImGui::TextDisabled("%s", display_name.c_str());
            ImGui::Separator();
            if (ImGui::MenuItem(_TR("timeline_rename_track"))) {
                m_rename_track_idx = static_cast<int>(t);
                strncpy(m_rename_track_buf, track.name.c_str(), sizeof(m_rename_track_buf) - 1);
                m_rename_track_buf[sizeof(m_rename_track_buf) - 1] = '\0';
                m_open_rename_modal = true;
            }
            if (ImGui::MenuItem(_TR("timeline_move_up"), nullptr, false, t > 0)) {
                move_track_up(project, static_cast<int>(t));
                if (on_project_changed) on_project_changed();
            }
            if (ImGui::MenuItem(_TR("timeline_move_down"), nullptr, false, t + 1 < project.tracks.size())) {
                move_track_down(project, static_cast<int>(t));
                if (on_project_changed) on_project_changed();
            }
            ImGui::Separator();
            if (ImGui::MenuItem(_TR("edit_delete"))) {
                delete_track(project, static_cast<int>(t));
                if (on_project_changed) on_project_changed();
            }
            ImGui::EndPopup();
        }

        // Detect hover over this track lane
        if (is_timeline_window_hovered && mouse_pos.y >= track_y && mouse_pos.y < track_y + track_h &&
            mouse_pos.x >= canvas_pos.x && mouse_pos.x <= (canvas_pos.x + canvas_size.x)) {
            m_hovered_track_idx = static_cast<int>(t);
        }

        // Invisible button over track lane for Drag-Drop target
        ImGui::SetCursorScreenPos(ImVec2(content_x, track_y));
        std::string lane_btn = "##lane_" + std::to_string(t);
        ImGui::InvisibleButton(lane_btn.c_str(), ImVec2(content_w, track_h));

        if (ImGui::BeginDragDropTarget()) {
            if (track.type == "video") {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("MINICUT_ASSET_VIDEO")) {
                    std::string asset_id = static_cast<const char*>(payload->Data);
                    const VideoAsset* va = project.find_video_asset(asset_id);
                    if (va) {
                        double drop_time = std::max(0.0, screen_x_to_time(mouse_pos.x));
                        double dur = va->duration > 0 ? va->duration : 5.0;
                        std::string ts = std::to_string(std::chrono::system_clock::now().time_since_epoch().count() % 1000000);

                        Clip c;
                        c.id = "clip_v_" + ts;
                        c.asset_id = asset_id;
                        c.start_time = drop_time;
                        c.duration = dur;
                        c.offset = 0.0;
                        track.clips.push_back(c);
                        clip_to_select = c.id;

                        // Separate audio onto first audio track if video has audio
                        if (va->has_audio && !va->is_image) {
                            Track* audio_track = nullptr;
                            for (auto& tr : project.tracks) {
                                if (tr.type == "audio") {
                                    audio_track = &tr;
                                    break;
                                }
                            }
                            if (!audio_track) {
                                Track at;
                                at.type = "audio";
                                at.name = "Audio 1";
                                at.id = "track_a" + std::to_string(project.tracks.size() + 1);
                                project.tracks.push_back(at);
                                audio_track = &project.tracks.back();
                            }
                            Clip ac;
                            ac.id = "clip_a_" + ts;
                            ac.asset_id = asset_id;
                            ac.start_time = drop_time;
                            ac.duration = dur;
                            ac.offset = 0.0;
                            audio_track->clips.push_back(ac);
                        }

                        if (on_project_changed) on_project_changed();
                    }
                }
            } else if (track.type == "audio") {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("MINICUT_ASSET_AUDIO")) {
                    std::string asset_id = static_cast<const char*>(payload->Data);
                    const AudioAsset* aa = project.find_audio_asset(asset_id);
                    if (aa) {
                        Clip c;
                        c.id = "clip_a_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count() % 1000000);
                        c.asset_id = asset_id;
                        c.start_time = std::max(0.0, screen_x_to_time(mouse_pos.x));
                        c.duration = aa->duration > 0 ? aa->duration : 5.0;
                        c.offset = 0.0;
                        track.clips.push_back(c);
                        clip_to_select = c.id;
                        if (on_project_changed) on_project_changed();
                    }
                } else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("MINICUT_ASSET_VIDEO")) {
                    std::string asset_id = static_cast<const char*>(payload->Data);
                    const VideoAsset* va = project.find_video_asset(asset_id);
                    if (va && va->has_audio) {
                        Clip c;
                        c.id = "clip_a_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count() % 1000000);
                        c.asset_id = asset_id;
                        c.start_time = std::max(0.0, screen_x_to_time(mouse_pos.x));
                        c.duration = va->duration > 0 ? va->duration : 5.0;
                        c.offset = 0.0;
                        track.clips.push_back(c);
                        clip_to_select = c.id;
                        if (on_project_changed) on_project_changed();
                    }
                }
            }
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("MINICUT_ASSET_PREFAB")) {
                std::string pf_id = static_cast<const char*>(payload->Data);
                double drop_time = std::max(0.0, screen_x_to_time(mouse_pos.x));
                if (on_add_prefab) {
                    on_add_prefab(pf_id, drop_time);
                }
            }
            ImGui::EndDragDropTarget();
        }

        // Right-click context menu on lane background
        std::string lane_ctx_id = "LaneContext_" + std::to_string(t);
        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(1)) {
            m_active_track_idx = static_cast<int>(t);
            ImGui::OpenPopup(lane_ctx_id.c_str());
        }
        if (ImGui::BeginPopup(lane_ctx_id.c_str())) {
            double click_time = screen_x_to_time(mouse_pos.x);
            if (ImGui::MenuItem(_TR("timeline_paste_here"), "Ctrl+V", false, m_has_clipboard)) {
                paste_clip(project, click_time, static_cast<int>(t));
                if (on_project_changed) on_project_changed();
            }
            ImGui::Separator();
            if (ImGui::MenuItem(_TR("timeline_rename_track"))) {
                m_rename_track_idx = static_cast<int>(t);
                strncpy(m_rename_track_buf, track.name.c_str(), sizeof(m_rename_track_buf) - 1);
                m_rename_track_buf[sizeof(m_rename_track_buf) - 1] = '\0';
                m_open_rename_modal = true;
            }
            if (ImGui::MenuItem(_TR("timeline_move_up"), nullptr, false, t > 0)) {
                move_track_up(project, static_cast<int>(t));
                if (on_project_changed) on_project_changed();
            }
            if (ImGui::MenuItem(_TR("timeline_move_down"), nullptr, false, t + 1 < project.tracks.size())) {
                move_track_down(project, static_cast<int>(t));
                if (on_project_changed) on_project_changed();
            }
            if (track.type == "video") {
                ImGui::Separator();
                ImGui::Text("%s: %d", _TR("timeline_depth"), track.depth);
                if (ImGui::MenuItem(_TR("timeline_move_front"))) {
                    track.depth = 100;
                    if (on_project_changed) on_project_changed();
                }
                if (ImGui::MenuItem(_TR("timeline_move_back"))) {
                    track.depth = 0;
                    if (on_project_changed) on_project_changed();
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem(_TR("timeline_add_video"))) {
                Track vt; vt.type = "video"; vt.name = "Video " + std::to_string(project.tracks.size() + 1); vt.id = "track_v" + std::to_string(project.tracks.size() + 1);
                project.tracks.push_back(vt);
                if (on_project_changed) on_project_changed();
            }
            if (ImGui::MenuItem(_TR("timeline_add_audio"))) {
                Track at; at.type = "audio"; at.name = "Audio " + std::to_string(project.tracks.size() + 1); at.id = "track_a" + std::to_string(project.tracks.size() + 1);
                project.tracks.push_back(at);
                if (on_project_changed) on_project_changed();
            }
            ImGui::EndPopup();
        }

        float canvas_right = canvas_pos.x + canvas_size.x;
        float canvas_top = canvas_pos.y + ruler_h;
        float canvas_bottom = tracks_bottom_y;
        bool in_timeline_content = is_timeline_window_hovered &&
                                   (mouse_pos.x >= content_x && mouse_pos.x <= canvas_right &&
                                    mouse_pos.y >= canvas_top && mouse_pos.y <= canvas_bottom);

        // Render Clips on this track
        for (size_t c = 0; c < track.clips.size(); ++c) {
            Clip& clip = track.clips[c];
            float clip_x0 = time_to_screen_x(clip.start_time);
            float clip_x1 = time_to_screen_x(clip.end_time());
            float clip_y0 = track_y + 4.0f;
            float clip_y1 = track_y + track_h - 4.0f;

            if (clip_x1 < content_x || clip_x0 > canvas_right) {
                continue; // Off screen
            }

            float vis_clip_x0 = std::max(content_x, clip_x0);
            float vis_clip_x1 = std::min(canvas_right, clip_x1);
            if (vis_clip_x0 >= vis_clip_x1) {
                continue;
            }

            bool is_selected = is_clip_selected(clip.id);
            bool is_hovered = in_timeline_content &&
                              (mouse_pos.x >= vis_clip_x0 && mouse_pos.x <= vis_clip_x1 &&
                               mouse_pos.y >= clip_y0 && mouse_pos.y <= clip_y1);

            // Clip block colors
            ImU32 fill_col, border_col;
            if (!clip.prefab_id.empty()) {
                fill_col = is_hovered ? IM_COL32(110, 50, 150, 230) : IM_COL32(85, 35, 125, 220);
                border_col = is_selected ? IM_COL32(241, 196, 15, 255) : IM_COL32(180, 100, 255, 220);
            } else if (track.type == "video") {
                fill_col = is_hovered ? IM_COL32(41, 128, 185, 230) : IM_COL32(31, 98, 145, 220);
                border_col = is_selected ? IM_COL32(241, 196, 15, 255) : IM_COL32(61, 174, 233, 200);
            } else {
                fill_col = is_hovered ? IM_COL32(39, 174, 96, 230) : IM_COL32(30, 130, 72, 220);
                border_col = is_selected ? IM_COL32(241, 196, 15, 255) : IM_COL32(46, 204, 113, 200);
            }

            draw_list->AddRectFilled(ImVec2(clip_x0, clip_y0), ImVec2(clip_x1, clip_y1), fill_col, 4.0f);
            draw_list->AddRect(ImVec2(clip_x0, clip_y0), ImVec2(clip_x1, clip_y1), border_col, 4.0f, 0, is_selected ? 2.5f : 1.0f);

            // Left/Right Trim Handle visualization
            if (is_hovered || is_selected) {
                draw_list->AddRectFilled(ImVec2(clip_x0, clip_y0), ImVec2(clip_x0 + 4, clip_y1), IM_COL32(255, 255, 255, 120), 2.0f);
                draw_list->AddRectFilled(ImVec2(clip_x1 - 4, clip_y0), ImVec2(clip_x1, clip_y1), IM_COL32(255, 255, 255, 120), 2.0f);
            }

            // Clip label text
            std::string label;
            if (!clip.prefab_name.empty()) {
                label = "[PF: " + clip.prefab_name + "] ";
            }
            if (track.type == "video") {
                const auto* va = project.find_video_asset(clip.asset_id);
                if (va) label += fs::path(va->path).filename().string();
                else label += clip.id;
            } else {
                const auto* aa = project.find_audio_asset(clip.asset_id);
                if (aa) {
                    label += fs::path(aa->path).filename().string();
                } else {
                    const auto* va = project.find_video_asset(clip.asset_id);
                    if (va) label += fs::path(va->path).filename().string() + " [Audio]";
                    else label += clip.id;
                }
            }
            label += " (" + format_timecode(clip.duration, false) + ")";

            draw_list->PushClipRect(ImVec2(clip_x0 + 4, clip_y0), ImVec2(clip_x1 - 4, clip_y1), true);
            draw_list->AddText(ImVec2(clip_x0 + 6, clip_y0 + 4), IM_COL32(255, 255, 255, 240), label.c_str());
            draw_list->PopClipRect();

            // Right-click context menu on clip
            std::string clip_ctx_id = "ClipContext_" + clip.id;
            if (is_hovered && ImGui::IsMouseClicked(1)) {
                clip_to_select = clip.id;
                m_drag_clip_id = clip.id;
                m_active_track_idx = static_cast<int>(t);
                ImGui::OpenPopup(clip_ctx_id.c_str());
            }

            if (ImGui::BeginPopup(clip_ctx_id.c_str())) {
                ImGui::TextDisabled("Clip: %s", label.c_str());
                ImGui::Separator();

                if (track.type == "video") {
                    const VideoAsset* va = project.find_video_asset(clip.asset_id);
                    if (va && va->has_audio && !va->is_image) {
                        if (ImGui::MenuItem(_TR("timeline_separate_audio"))) {
                            Track* at = nullptr;
                            for (auto& tr : project.tracks) {
                                if (tr.type == "audio") { at = &tr; break; }
                            }
                            if (!at) {
                                Track new_at; new_at.type = "audio"; new_at.name = "Audio 1"; new_at.id = "track_a" + std::to_string(project.tracks.size() + 1);
                                project.tracks.push_back(new_at);
                                at = &project.tracks.back();
                            }
                            Clip ac;
                            ac.id = "clip_a_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count() % 1000000);
                            ac.asset_id = clip.asset_id;
                            ac.start_time = clip.start_time;
                            ac.duration = clip.duration;
                            ac.offset = clip.offset;
                            at->clips.push_back(ac);
                            if (on_project_changed) on_project_changed();
                        }
                    }
                }

                if (ImGui::MenuItem(_TR("timeline_split_playhead"), "S")) {
                    split_clip_at(project, clip.id, playhead_time);
                    if (on_project_changed) on_project_changed();
                }
                if (ImGui::MenuItem(_TR("edit_cut"), "Ctrl+X")) {
                    cut_selected_clip(project);
                    clip_to_select = "";
                    if (on_project_changed) on_project_changed();
                }
                if (ImGui::MenuItem(_TR("edit_copy"), "Ctrl+C")) {
                    copy_selected_clips(project);
                }
                std::string del_label = (m_selected_clip_ids.size() > 1)
                    ? (std::string(_TR("edit_delete")) + " (" + std::to_string(m_selected_clip_ids.size()) + ")")
                    : std::string(_TR("edit_delete"));
                if (ImGui::MenuItem(del_label.c_str(), "Del")) {
                    delete_selected_clips(project);
                    clip_to_select = "";
                    if (on_project_changed) on_project_changed();
                }
                ImGui::Separator();
                if (ImGui::MenuItem(_TR("timeline_reset_transforms"))) {
                    clip.pos_x = 0; clip.pos_y = 0; clip.rotation = 0; clip.scale = 1.0; clip.opacity = 1.0;
                    project.remove_clip_keyframes(clip.id);
                    if (on_project_changed) on_project_changed();
                }

                if (!clip.prefab_id.empty()) {
                    ImGui::Separator();
                    std::string del_pf_label = std::string("- ") + _TR("timeline_delete_prefab") + " (" + clip.prefab_name + ")";
                    if (ImGui::MenuItem(del_pf_label.c_str())) {
                        std::string pf_id = clip.prefab_id;
                        std::vector<std::string> rem_cids;
                        for (auto& trk : project.tracks) {
                            auto it = std::remove_if(trk.clips.begin(), trk.clips.end(),
                                [&](const Clip& c) {
                                    if (c.prefab_id == pf_id) {
                                        rem_cids.push_back(c.id);
                                        return true;
                                    }
                                    return false;
                                });
                            trk.clips.erase(it, trk.clips.end());
                        }
                        for (const auto& cid : rem_cids) {
                            auto kit = std::remove_if(project.keyframes.begin(), project.keyframes.end(),
                                [&](const Keyframe& k) { return k.clip_id == cid; });
                            project.keyframes.erase(kit, project.keyframes.end());
                        }
                        clip_to_select = "";
                        if (on_project_changed) on_project_changed();
                    }
                    if (ImGui::MenuItem(_TR("timeline_dissolve_prefab"))) {
                        std::string pf_id = clip.prefab_id;
                        for (auto& trk : project.tracks) {
                            for (auto& c : trk.clips) {
                                if (c.prefab_id == pf_id) {
                                    c.prefab_id.clear();
                                    c.prefab_name.clear();
                                }
                            }
                        }
                        if (on_project_changed) on_project_changed();
                    }
                }

                ImGui::EndPopup();
            }

            // Hover cursor & interaction detection
            if (is_hovered && m_drag_mode == DragMode::None) {
                bool near_left = (std::abs(mouse_pos.x - clip_x0) <= 8.0f) && (clip_x0 >= content_x && clip_x0 <= canvas_right);
                bool near_right = (std::abs(mouse_pos.x - clip_x1) <= 8.0f) && (clip_x1 >= content_x && clip_x1 <= canvas_right);
                if (near_left) {
                    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
                    if (ImGui::IsMouseClicked(0)) {
                        m_drag_mode = DragMode::TrimLeft;
                        m_drag_clip_id = clip.id;
                        m_drag_track_idx = static_cast<int>(t);
                        m_drag_start_mouse_time = screen_x_to_time(mouse_pos.x);
                        m_drag_orig_start_time = clip.start_time;
                        m_drag_orig_duration = clip.duration;
                        m_drag_orig_offset = clip.offset;
                        clip_to_select = clip.id;
                    }
                } else if (near_right) {
                    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
                    if (ImGui::IsMouseClicked(0)) {
                        m_drag_mode = DragMode::TrimRight;
                        m_drag_clip_id = clip.id;
                        m_drag_track_idx = static_cast<int>(t);
                        m_drag_start_mouse_time = screen_x_to_time(mouse_pos.x);
                        m_drag_orig_duration = clip.duration;
                        clip_to_select = clip.id;
                    }
                } else {
                    if (ImGui::IsMouseClicked(0)) {
                        if (io.KeyCtrl) {
                            select_clip(clip.id, true, false);
                        } else if (io.KeyShift) {
                            select_clip(clip.id, false, true);
                        } else {
                            if (!is_clip_selected(clip.id)) {
                                select_clip(clip.id, false, false);
                            }
                        }
                        m_drag_mode = DragMode::MoveClip;
                        m_drag_clip_id = clip.id;
                        m_drag_track_idx = static_cast<int>(t);
                        m_drag_start_mouse_time = screen_x_to_time(mouse_pos.x);
                        m_drag_orig_start_time = clip.start_time;

                        m_drag_multi_orig_times.clear();
                        for (const auto& cid : m_selected_clip_ids) {
                            const Clip* sc = project.find_clip(cid);
                            if (sc) m_drag_multi_orig_times[cid] = sc->start_time;
                        }
                        clip_to_select = clip.id;
                    }
                }
            }
        }

        track_y += track_h;
    }

    draw_list->PopClipRect();

    // Box Selection on empty area
    float canvas_right = canvas_pos.x + canvas_size.x;
    float canvas_top = canvas_pos.y + ruler_h;
    float canvas_bottom = tracks_bottom_y;
    bool is_timeline_window_hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
    bool in_timeline_content = is_timeline_window_hovered &&
                               (mouse_pos.x >= content_x && mouse_pos.x <= canvas_right &&
                                mouse_pos.y >= canvas_top && mouse_pos.y <= canvas_bottom);


    if (in_timeline_content && ImGui::IsMouseClicked(0) && m_drag_mode == DragMode::None) {
        if (!io.KeyCtrl && !io.KeyShift) {
            clear_selection();
        }
        m_drag_mode = DragMode::BoxSelect;
        m_box_select_start = mouse_pos;
        m_box_select_cur = mouse_pos;
    }


    // Vertical Track Scroll Indicator if tracks overflow canvas
    if (max_track_scroll_y > 0.0f) {
        float vsb_w = 5.0f;
        float vsb_x0 = canvas_pos.x + canvas_size.x - vsb_w - 2.0f;
        float vsb_x1 = vsb_x0 + vsb_w;
        float vsb_ratio = m_scroll_track_y / max_track_scroll_y;
        float vsb_thumb_h = std::max(20.0f, visible_track_area_h * (visible_track_area_h / total_tracks_h));
        float vsb_y0 = canvas_pos.y + ruler_h + vsb_ratio * (visible_track_area_h - vsb_thumb_h);
        draw_list->AddRectFilled(ImVec2(vsb_x0, vsb_y0), ImVec2(vsb_x1, vsb_y0 + vsb_thumb_h), IM_COL32(110, 120, 135, 180), 2.0f);
    }

    if (!clip_to_select.empty() && on_select_clip) {
        on_select_clip(clip_to_select);
    }

    // Process Active Drag Actions
    if (m_drag_mode != DragMode::None && ImGui::IsMouseDown(0)) {
        double current_mouse_t = screen_x_to_time(mouse_pos.x);
        double delta_t = current_mouse_t - m_drag_start_mouse_time;

        if (m_drag_mode == DragMode::BoxSelect) {
            m_box_select_cur = mouse_pos;
            float bx0 = std::min(m_box_select_start.x, m_box_select_cur.x);
            float bx1 = std::max(m_box_select_start.x, m_box_select_cur.x);
            float by0 = std::min(m_box_select_start.y, m_box_select_cur.y);
            float by1 = std::max(m_box_select_start.y, m_box_select_cur.y);

            draw_list->AddRectFilled(ImVec2(bx0, by0), ImVec2(bx1, by1), IM_COL32(61, 174, 233, 40));
            draw_list->AddRect(ImVec2(bx0, by0), ImVec2(bx1, by1), IM_COL32(61, 174, 233, 200), 0.0f, 0, 1.5f);
        } else {
            Clip* target_clip = project.find_clip_mut(m_drag_clip_id);
            if (target_clip) {
                if (m_drag_mode == DragMode::MoveClip) {
                    double new_start = std::max(0.0, m_drag_orig_start_time + delta_t);
                    double snapped = snap_time(new_start, project, target_clip->id);
                    double actual_dt = snapped - m_drag_orig_start_time;
                    target_clip->start_time = snapped;

                    for (const auto& cid : m_selected_clip_ids) {
                        if (cid == target_clip->id) continue;
                        Clip* other_clip = project.find_clip_mut(cid);
                        if (other_clip && m_drag_multi_orig_times.find(cid) != m_drag_multi_orig_times.end()) {
                            other_clip->start_time = std::max(0.0, m_drag_multi_orig_times[cid] + actual_dt);
                        }
                    }
                    if (on_project_changed) on_project_changed();
                } else if (m_drag_mode == DragMode::TrimLeft) {
                    double new_start = m_drag_orig_start_time + delta_t;
                    new_start = snap_time(new_start, project, target_clip->id);
                    new_start = std::max(0.0, new_start);

                    double dt = new_start - m_drag_orig_start_time;
                    double new_dur = m_drag_orig_duration - dt;
                    double new_off = m_drag_orig_offset + dt;

                    if (new_dur >= 0.2 && new_off >= 0.0) {
                        target_clip->start_time = new_start;
                        target_clip->duration = new_dur;
                        target_clip->offset = new_off;
                        if (on_project_changed) on_project_changed();
                    }
                } else if (m_drag_mode == DragMode::TrimRight) {
                    double new_dur = m_drag_orig_duration + delta_t;
                    double new_end = snap_time(target_clip->start_time + new_dur, project, target_clip->id);
                    new_dur = new_end - target_clip->start_time;
                    if (new_dur >= 0.2) {
                        target_clip->duration = new_dur;
                        if (on_project_changed) on_project_changed();
                    }
                }
            }
        }
    }

    if (m_drag_mode != DragMode::None && !ImGui::IsMouseDown(0)) {
        if (m_drag_mode == DragMode::BoxSelect) {
            float bx0 = std::min(m_box_select_start.x, m_box_select_cur.x);
            float bx1 = std::max(m_box_select_start.x, m_box_select_cur.x);
            float by0 = std::min(m_box_select_start.y, m_box_select_cur.y);
            float by1 = std::max(m_box_select_start.y, m_box_select_cur.y);

            if (std::abs(bx1 - bx0) > 4.0f || std::abs(by1 - by0) > 4.0f) {
                float start_trk_y = canvas_pos.y + ruler_h - m_scroll_track_y;
                for (size_t t = 0; t < project.tracks.size(); ++t) {
                    float ty0 = start_trk_y + (t * track_h);
                    float ty1 = ty0 + track_h;
                    if (ty1 < by0 || ty0 > by1) continue;

                    for (const auto& c : project.tracks[t].clips) {
                        float cx0 = time_to_screen_x(c.start_time);
                        float cx1 = time_to_screen_x(c.end_time());
                        if (cx1 >= bx0 && cx0 <= bx1) {
                            if (std::find(m_selected_clip_ids.begin(), m_selected_clip_ids.end(), c.id) == m_selected_clip_ids.end()) {
                                m_selected_clip_ids.push_back(c.id);
                            }
                        }
                    }
                }
                if (!m_selected_clip_ids.empty()) {
                    m_drag_clip_id = m_selected_clip_ids.front();
                    if (on_select_clip) on_select_clip(m_drag_clip_id);
                }
            }
        }
        m_drag_mode = DragMode::None;
    }

    // Playhead Line across visible tracks height
    float playhead_x = time_to_screen_x(playhead_time);
    if (playhead_x >= content_x && playhead_x <= (canvas_pos.x + canvas_size.x)) {
        draw_list->AddLine(ImVec2(playhead_x, canvas_pos.y), ImVec2(playhead_x, tracks_bottom_y),
                           IM_COL32(235, 77, 75, 255), 2.0f);

        // Top triangle handle on ruler
        ImVec2 p0(playhead_x - 6.0f, canvas_pos.y);
        ImVec2 p1(playhead_x + 6.0f, canvas_pos.y);
        ImVec2 p2(playhead_x, canvas_pos.y + 12.0f);
        draw_list->AddTriangleFilled(p0, p1, p2, IM_COL32(235, 77, 75, 255));
    }

    // Bottom Horizontal Scrollbar
    float sb_x0 = content_x;
    float sb_x1 = canvas_pos.x + canvas_size.x;
    float sb_y0 = tracks_bottom_y;
    float sb_y1 = canvas_pos.y + canvas_size.y;
    float sb_w = sb_x1 - sb_x0;

    // Scrollbar track background
    draw_list->AddRectFilled(ImVec2(canvas_pos.x, sb_y0), ImVec2(canvas_pos.x + canvas_size.x, sb_y1), IM_COL32(20, 23, 27, 255));
    draw_list->AddLine(ImVec2(canvas_pos.x, sb_y0), ImVec2(canvas_pos.x + canvas_size.x, sb_y0), IM_COL32(48, 54, 63, 255));
    draw_list->AddText(ImVec2(canvas_pos.x + 8, sb_y0 + 1), IM_COL32(130, 140, 155, 255), "TIMELINE");

    // Scrollbar Thumb calculation
    float visible_duration = content_w / m_pixels_per_sec;
    float total_range = std::max(total_proj_dur + 5.0f, visible_duration);
    float thumb_w = std::clamp(sb_w * (visible_duration / total_range), 32.0f, sb_w);
    float thumb_avail = sb_w - thumb_w;
    float thumb_ratio = (max_scroll_sec > 0.001f) ? (m_scroll_sec / max_scroll_sec) : 0.0f;
    float thumb_x0 = sb_x0 + thumb_ratio * thumb_avail;
    float thumb_x1 = thumb_x0 + thumb_w;

    bool mouse_in_sb = (mouse_pos.x >= sb_x0 && mouse_pos.x <= sb_x1 && mouse_pos.y >= sb_y0 && mouse_pos.y <= sb_y1);
    bool mouse_in_thumb = (mouse_pos.x >= thumb_x0 && mouse_pos.x <= thumb_x1 && mouse_pos.y >= sb_y0 && mouse_pos.y <= sb_y1);

    if (mouse_in_thumb && ImGui::IsMouseClicked(0)) {
        m_dragging_scrollbar = true;
        m_scrollbar_drag_start_mouse_x = mouse_pos.x;
        m_scrollbar_drag_orig_scroll = m_scroll_sec;
    }
    if (m_dragging_scrollbar && ImGui::IsMouseDown(0)) {
        float dx = mouse_pos.x - m_scrollbar_drag_start_mouse_x;
        if (thumb_avail > 0.001f) {
            float dscroll = (dx / thumb_avail) * max_scroll_sec;
            m_scroll_sec = std::clamp(m_scrollbar_drag_orig_scroll + dscroll, 0.0f, max_scroll_sec);
        }
    }
    if (!ImGui::IsMouseDown(0)) {
        m_dragging_scrollbar = false;
    }
    if (mouse_in_sb && !mouse_in_thumb && !m_dragging_scrollbar && ImGui::IsMouseClicked(0)) {
        if (thumb_avail > 0.001f) {
            float target_ratio = std::clamp((mouse_pos.x - sb_x0 - thumb_w * 0.5f) / thumb_avail, 0.0f, 1.0f);
            m_scroll_sec = std::clamp(target_ratio * max_scroll_sec, 0.0f, max_scroll_sec);
        }
    }

    ImU32 thumb_col = (m_dragging_scrollbar || mouse_in_thumb) ? IM_COL32(95, 110, 130, 255) : IM_COL32(65, 75, 88, 255);
    draw_list->AddRectFilled(ImVec2(thumb_x0, sb_y0 + 2), ImVec2(thumb_x1, sb_y1 - 2), thumb_col, 3.0f);

    render_rename_modal(project, on_project_changed);
}

void Timeline::render_rename_modal(Project& project, const std::function<void()>& on_project_changed) {
    if (m_open_rename_modal) {
        ImGui::OpenPopup("Rename Track##Modal");
        m_open_rename_modal = false;
    }

    if (ImGui::BeginPopupModal("Rename Track##Modal", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("%s:", _TR("timeline_track_name"));
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
        }
        bool confirmed = ImGui::InputText("##track_name_input", m_rename_track_buf, sizeof(m_rename_track_buf), ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::Spacing();
        if (ImGui::Button(_TR("dialog_ok"), ImVec2(100, 0)) || confirmed) {
            if (m_rename_track_idx >= 0 && m_rename_track_idx < static_cast<int>(project.tracks.size())) {
                project.tracks[m_rename_track_idx].name = m_rename_track_buf;
                if (on_project_changed) on_project_changed();
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(_TR("dialog_cancel"), ImVec2(100, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}
