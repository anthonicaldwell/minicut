#include "KeyframeEditor.h"
#include "LocaleManager.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <filesystem>

namespace fs = std::filesystem;

KeyframeEditor::KeyframeEditor() {}
KeyframeEditor::~KeyframeEditor() {}

static void draw_diamond(ImDrawList* draw_list, ImVec2 center, float size, ImU32 col_fill, ImU32 col_border) {
    ImVec2 p0(center.x, center.y - size);
    ImVec2 p1(center.x + size, center.y);
    ImVec2 p2(center.x, center.y + size);
    ImVec2 p3(center.x - size, center.y);

    draw_list->AddQuadFilled(p0, p1, p2, p3, col_fill);
    draw_list->AddQuad(p0, p1, p2, p3, col_border, 1.5f);
}

void KeyframeEditor::render_ui(Project& project, double playhead_time,
                              const std::function<void(double new_playhead)>& on_scrub,
                              const std::function<void()>& on_project_changed) {
    if (m_selected_clip_id.empty()) {
        render_empty_context(project);
        return;
    }

    int track_idx = -1;
    int clip_idx = -1;
    Clip* clip = project.find_clip_mut(m_selected_clip_id, &track_idx, &clip_idx);
    if (!clip || track_idx < 0) {
        render_empty_context(project);
        return;
    }

    const Track& track = project.tracks[track_idx];
    if (track.type == "audio") {
        render_audio_tools(project, clip, track, playhead_time, on_scrub, on_project_changed);
    } else {
        const VideoAsset* va = project.find_video_asset(clip->asset_id);
        if (va && va->is_image) {
            render_image_tools(project, clip, track, playhead_time, on_scrub, on_project_changed);
        } else {
            render_video_tools(project, clip, track, playhead_time, on_scrub, on_project_changed);
        }
    }
}

void KeyframeEditor::render_empty_context(Project& project) {
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.24f, 0.68f, 0.91f, 1.0f), "[ Context Tools & Inspector ]");
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextDisabled("No timeline clip currently selected.");
    ImGui::TextWrapped("Click any image, video, or audio clip on the timeline to inspect and edit its properties, transforms, and keyframes.");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextDisabled("Project Summary:");
    ImGui::BulletText("Project: %s", project.name.c_str());
    ImGui::BulletText("Total Duration: %s", format_timecode(project.get_total_duration()).c_str());
    int v_tracks = 0, a_tracks = 0;
    for (const auto& t : project.tracks) {
        if (t.type == "video") v_tracks++; else a_tracks++;
    }
    ImGui::BulletText("Tracks: %d Video, %d Audio", v_tracks, a_tracks);
    ImGui::BulletText("Total Media Assets: %zu (Videos/Images: %zu, Audio: %zu)",
                      project.video_assets.size() + project.audio_assets.size(),
                      project.video_assets.size(), project.audio_assets.size());
}

void KeyframeEditor::render_parameter_row(
    Project& project, Clip* clip, const std::string& param, const char* label,
    double rel_playhead, double default_val, float min_val, float max_val,
    float speed, const char* format,
    const std::function<void(double)>& on_scrub,
    const std::function<void()>& on_project_changed)
{
    ImGui::PushID(param.c_str());

    bool has_kf = project.has_keyframe_at(clip->id, param, rel_playhead);
    double cur_val = project.get_clip_param(*clip, param, rel_playhead);
    double prev_kf = project.find_prev_keyframe_time(clip->id, param, rel_playhead);
    double next_kf = project.find_next_keyframe_time(clip->id, param, rel_playhead);

    // Label
    ImGui::Text("%-11s", label);
    ImGui::SameLine();

    // Previous Keyframe Jump
    if (prev_kf >= 0.0) {
        if (ImGui::SmallButton("<")) {
            if (on_scrub) on_scrub(clip->start_time + prev_kf);
        }
    } else {
        ImGui::BeginDisabled();
        ImGui::SmallButton("<");
        ImGui::EndDisabled();
    }
    ImGui::SameLine();

    // Keyframe Toggle Diamond
    if (has_kf) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.65f, 0.10f, 0.8f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        if (ImGui::SmallButton(has_kf ? "[#]" : "[ ]")) {
            project.remove_keyframe_at(clip->id, param, rel_playhead);
            if (on_project_changed) on_project_changed();
        }
        ImGui::PopStyleColor(2);
    } else {
        if (ImGui::SmallButton("[+]")) {
            project.set_or_add_keyframe(clip->id, param, rel_playhead, cur_val);
            if (on_project_changed) on_project_changed();
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(has_kf ? "Remove keyframe at playhead" : "Add keyframe at playhead");
    }
    ImGui::SameLine();

    // Next Keyframe Jump
    if (next_kf >= 0.0) {
        if (ImGui::SmallButton(">")) {
            if (on_scrub) on_scrub(clip->start_time + next_kf);
        }
    } else {
        ImGui::BeginDisabled();
        ImGui::SmallButton(">");
        ImGui::EndDisabled();
    }
    ImGui::SameLine();

    // Numerical Input DragFloat
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 30.0f);
    float val_f = static_cast<float>(cur_val);
    if (ImGui::DragFloat("##val", &val_f, speed, min_val, max_val, format)) {
        auto kfs = project.get_clip_keyframes(clip->id, param);
        if (has_kf || !kfs.empty()) {
            project.set_or_add_keyframe(clip->id, param, rel_playhead, val_f);
        } else {
            // Update base clip property
            if (param == "pos_x") clip->pos_x = val_f;
            else if (param == "pos_y") clip->pos_y = val_f;
            else if (param == "rotation") clip->rotation = val_f;
            else if (param == "scale") clip->scale = val_f;
            else if (param == "opacity") clip->opacity = val_f;
        }
        if (on_project_changed) on_project_changed();
    }
    ImGui::SameLine();

    // Reset Button
    if (ImGui::SmallButton("R")) {
        auto kfs = project.get_clip_keyframes(clip->id, param);
        if (has_kf || !kfs.empty()) {
            project.set_or_add_keyframe(clip->id, param, rel_playhead, default_val);
        } else {
            if (param == "pos_x") clip->pos_x = default_val;
            else if (param == "pos_y") clip->pos_y = default_val;
            else if (param == "rotation") clip->rotation = default_val;
            else if (param == "scale") clip->scale = default_val;
            else if (param == "opacity") clip->opacity = default_val;
        }
        if (on_project_changed) on_project_changed();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Reset to default value");
    }

    ImGui::PopID();
}

void KeyframeEditor::render_image_tools(
    Project& project, Clip* clip, const Track& track, double playhead_time,
    const std::function<void(double)>& on_scrub,
    const std::function<void()>& on_project_changed)
{
    const VideoAsset* va = project.find_video_asset(clip->asset_id);
    std::string filename = va ? fs::path(va->path).filename().string() : clip->id;

    // Header info
    ImGui::TextColored(ImVec4(0.24f, 0.68f, 0.91f, 1.0f), "Image Tools: %s", filename.c_str());
    ImGui::TextDisabled("Track: %s | Time: %.2fs - %.2fs (Dur: %.2fs)",
                        track.name.c_str(), clip->start_time, clip->end_time(), clip->duration);
    if (va && va->width > 0) {
        ImGui::TextDisabled("Resolution: %dx%d px", va->width, va->height);
    }
    ImGui::Separator();

    double rel_playhead = std::clamp(playhead_time - clip->start_time, 0.0, clip->duration);

    // Animation & Transform Presets
    ImGui::TextDisabled("Presets:");
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset All")) {
        clip->pos_x = 0.0; clip->pos_y = 0.0; clip->rotation = 0.0; clip->scale = 1.0; clip->opacity = 1.0;
        project.remove_clip_keyframes(clip->id);
        if (on_project_changed) on_project_changed();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Fade In")) {
        project.set_or_add_keyframe(clip->id, "opacity", 0.0, 0.0);
        project.set_or_add_keyframe(clip->id, "opacity", std::min(1.0, clip->duration), 1.0);
        if (on_project_changed) on_project_changed();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Fade Out")) {
        double sf = std::max(0.0, clip->duration - 1.0);
        project.set_or_add_keyframe(clip->id, "opacity", sf, 1.0);
        project.set_or_add_keyframe(clip->id, "opacity", clip->duration, 0.0);
        if (on_project_changed) on_project_changed();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Ken Burns (In)")) {
        project.set_or_add_keyframe(clip->id, "scale", 0.0, 1.0);
        project.set_or_add_keyframe(clip->id, "scale", clip->duration, 1.25);
        if (on_project_changed) on_project_changed();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Ken Burns (Out)")) {
        project.set_or_add_keyframe(clip->id, "scale", 0.0, 1.25);
        project.set_or_add_keyframe(clip->id, "scale", clip->duration, 1.0);
        if (on_project_changed) on_project_changed();
    }

    ImGui::Spacing();
    ImGui::Separator();

    // 2D Transform Controls
    ImGui::Text("Transform Properties (Keyframeable):");
    render_parameter_row(project, clip, "pos_x", _TR("inspector_position_x"), rel_playhead, 0.0, -1920.0f, 1920.0f, 1.0f, "%.0f px", on_scrub, on_project_changed);
    render_parameter_row(project, clip, "pos_y", _TR("inspector_position_y"), rel_playhead, 0.0, -1080.0f, 1080.0f, 1.0f, "%.0f px", on_scrub, on_project_changed);
    render_parameter_row(project, clip, "rotation", _TR("inspector_rotation"), rel_playhead, 0.0, -360.0f, 360.0f, 0.5f, "%.1f deg", on_scrub, on_project_changed);
    render_parameter_row(project, clip, "scale", _TR("inspector_scale"), rel_playhead, 1.0, 0.01f, 5.0f, 0.01f, "%.2fx", on_scrub, on_project_changed);
    render_parameter_row(project, clip, "opacity", _TR("inspector_opacity"), rel_playhead, 1.0, 0.0f, 1.0f, 0.01f, "%.2f", on_scrub, on_project_changed);

    int trk_idx = -1;
    project.find_clip(clip->id, &trk_idx);
    if (trk_idx >= 0 && trk_idx < static_cast<int>(project.tracks.size())) {
        Track& trk = project.tracks[trk_idx];
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::DragInt(_TR("inspector_track_depth"), &trk.depth, 1.0f, 0, 100, "%d")) {
            if (on_project_changed) on_project_changed();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(Higher renders on top)");
    }

    ImGui::Spacing();
    ImGui::Separator();

    // Curve Graph Selector
    const char* params[] = { "pos_x", "pos_y", "rotation", "scale", "opacity" };
    const char* param_labels[] = { "Position X Curve", "Position Y Curve", "Rotation Curve", "Scale Curve", "Opacity Curve" };
    if (m_selected_param_idx < 0 || m_selected_param_idx > 4) m_selected_param_idx = 4; // Default to Opacity

    ImGui::Text("Keyframe Curve Graph:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(180.0f);
    if (ImGui::Combo("##ParamCombo", &m_selected_param_idx, param_labels, 5)) {
        m_selected_kf_idx = -1;
    }

    std::string sel_param = params[m_selected_param_idx];
    float min_v = 0.0f, max_v = 1.0f;
    if (sel_param == "pos_x") { min_v = -640.0f; max_v = 640.0f; }
    else if (sel_param == "pos_y") { min_v = -360.0f; max_v = 360.0f; }
    else if (sel_param == "rotation") { min_v = -180.0f; max_v = 180.0f; }
    else if (sel_param == "scale") { min_v = 0.0f; max_v = 3.0f; }
    else if (sel_param == "opacity") { min_v = 0.0f; max_v = 1.0f; }

    render_curve_graph(project, clip, sel_param, min_v, max_v, playhead_time, on_scrub, on_project_changed);
}

void KeyframeEditor::render_video_tools(
    Project& project, Clip* clip, const Track& track, double playhead_time,
    const std::function<void(double)>& on_scrub,
    const std::function<void()>& on_project_changed)
{
    const VideoAsset* va = project.find_video_asset(clip->asset_id);
    std::string filename = va ? fs::path(va->path).filename().string() : clip->id;

    ImGui::TextColored(ImVec4(0.24f, 0.68f, 0.91f, 1.0f), "Video Tools: %s", filename.c_str());
    ImGui::TextDisabled("Track: %s | Time: %.2fs - %.2fs (Dur: %.2fs)",
                        track.name.c_str(), clip->start_time, clip->end_time(), clip->duration);
    if (va && va->width > 0) {
        ImGui::TextDisabled("Resolution: %dx%d px | FPS: %.1f", va->width, va->height, va->fps);
    }
    ImGui::Separator();

    double rel_playhead = std::clamp(playhead_time - clip->start_time, 0.0, clip->duration);

    // Presets
    ImGui::TextDisabled("Presets:");
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset All")) {
        clip->pos_x = 0.0; clip->pos_y = 0.0; clip->rotation = 0.0; clip->scale = 1.0; clip->opacity = 1.0;
        project.remove_clip_keyframes(clip->id);
        if (on_project_changed) on_project_changed();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Fade In")) {
        project.set_or_add_keyframe(clip->id, "opacity", 0.0, 0.0);
        project.set_or_add_keyframe(clip->id, "opacity", std::min(1.0, clip->duration), 1.0);
        if (on_project_changed) on_project_changed();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Fade Out")) {
        double sf = std::max(0.0, clip->duration - 1.0);
        project.set_or_add_keyframe(clip->id, "opacity", sf, 1.0);
        project.set_or_add_keyframe(clip->id, "opacity", clip->duration, 0.0);
        if (on_project_changed) on_project_changed();
    }

    ImGui::Spacing();
    ImGui::Separator();

    // Transform controls
    ImGui::Text("Video Transform & Opacity:");
    render_parameter_row(project, clip, "pos_x", _TR("inspector_position_x"), rel_playhead, 0.0, -1920.0f, 1920.0f, 1.0f, "%.0f px", on_scrub, on_project_changed);
    render_parameter_row(project, clip, "pos_y", _TR("inspector_position_y"), rel_playhead, 0.0, -1080.0f, 1080.0f, 1.0f, "%.0f px", on_scrub, on_project_changed);
    render_parameter_row(project, clip, "rotation", _TR("inspector_rotation"), rel_playhead, 0.0, -360.0f, 360.0f, 0.5f, "%.1f deg", on_scrub, on_project_changed);
    render_parameter_row(project, clip, "scale", _TR("inspector_scale"), rel_playhead, 1.0, 0.01f, 5.0f, 0.01f, "%.2fx", on_scrub, on_project_changed);
    render_parameter_row(project, clip, "opacity", _TR("inspector_opacity"), rel_playhead, 1.0, 0.0f, 1.0f, 0.01f, "%.2f", on_scrub, on_project_changed);

    int trk_idx = -1;
    project.find_clip(clip->id, &trk_idx);
    if (trk_idx >= 0 && trk_idx < static_cast<int>(project.tracks.size())) {
        Track& trk = project.tracks[trk_idx];
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::DragInt(_TR("inspector_track_depth"), &trk.depth, 1.0f, 0, 100, "%d")) {
            if (on_project_changed) on_project_changed();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(Higher renders on top)");
    }

    ImGui::Spacing();
    ImGui::Separator();

    // Curve Graph Selector
    const char* params[] = { "opacity", "scale", "rotation", "pos_x", "pos_y" };
    const char* param_labels[] = { "Opacity Curve", "Scale Curve", "Rotation Curve", "Position X Curve", "Position Y Curve" };
    if (m_selected_param_idx < 0 || m_selected_param_idx > 4) m_selected_param_idx = 0;

    ImGui::Text("Keyframe Curve Graph:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(180.0f);
    if (ImGui::Combo("##ParamCombo", &m_selected_param_idx, param_labels, 5)) {
        m_selected_kf_idx = -1;
    }

    std::string sel_param = params[m_selected_param_idx];
    float min_v = 0.0f, max_v = 1.0f;
    if (sel_param == "pos_x") { min_v = -640.0f; max_v = 640.0f; }
    else if (sel_param == "pos_y") { min_v = -360.0f; max_v = 360.0f; }
    else if (sel_param == "rotation") { min_v = -180.0f; max_v = 180.0f; }
    else if (sel_param == "scale") { min_v = 0.0f; max_v = 3.0f; }
    else if (sel_param == "opacity") { min_v = 0.0f; max_v = 1.0f; }

    render_curve_graph(project, clip, sel_param, min_v, max_v, playhead_time, on_scrub, on_project_changed);
}

void KeyframeEditor::render_audio_tools(
    Project& project, Clip* clip, const Track& track, double playhead_time,
    const std::function<void(double)>& on_scrub,
    const std::function<void()>& on_project_changed)
{
    const AudioAsset* aa = project.find_audio_asset(clip->asset_id);
    const VideoAsset* va = aa ? nullptr : project.find_video_asset(clip->asset_id);
    std::string filename;
    if (aa) filename = fs::path(aa->path).filename().string();
    else if (va) filename = fs::path(va->path).filename().string() + " [Audio]";
    else filename = clip->id;

    ImGui::TextColored(ImVec4(0.18f, 0.80f, 0.44f, 1.0f), "Audio Tools: %s", filename.c_str());
    ImGui::TextDisabled("Track: %s | Time: %.2fs - %.2fs (Dur: %.2fs)",
                        track.name.c_str(), clip->start_time, clip->end_time(), clip->duration);
    if (aa) {
        ImGui::TextDisabled("Sample Rate: %d Hz | Channels: %d", aa->sample_rate, aa->channels);
    } else if (va) {
        ImGui::TextDisabled("Extracted Audio from Video (%s)", fs::path(va->path).filename().string().c_str());
    }
    ImGui::Separator();

    double rel_playhead = std::clamp(playhead_time - clip->start_time, 0.0, clip->duration);

    // Audio Presets
    ImGui::TextDisabled("Presets:");
    ImGui::SameLine();
    if (ImGui::SmallButton("Fade In")) {
        project.set_or_add_keyframe(clip->id, "volume", 0.0, 0.0);
        project.set_or_add_keyframe(clip->id, "volume", std::min(1.0, clip->duration), 1.0);
        if (on_project_changed) on_project_changed();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Fade Out")) {
        double sf = std::max(0.0, clip->duration - 1.0);
        project.set_or_add_keyframe(clip->id, "volume", sf, 1.0);
        project.set_or_add_keyframe(clip->id, "volume", clip->duration, 0.0);
        if (on_project_changed) on_project_changed();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Duck (20%)")) {
        double d = clip->duration;
        project.set_or_add_keyframe(clip->id, "volume", 0.0, 1.0);
        project.set_or_add_keyframe(clip->id, "volume", d * 0.2, 0.2);
        project.set_or_add_keyframe(clip->id, "volume", d * 0.8, 0.2);
        project.set_or_add_keyframe(clip->id, "volume", d, 1.0);
        if (on_project_changed) on_project_changed();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset 100%")) {
        project.remove_clip_keyframes(clip->id);
        project.set_or_add_keyframe(clip->id, "volume", 0.0, 1.0);
        if (on_project_changed) on_project_changed();
    }

    ImGui::Spacing();
    ImGui::Separator();

    ImGui::Text("Volume & Gain (Keyframeable):");
    render_parameter_row(project, clip, "volume", _TR("inspector_volume"), rel_playhead, 1.0, 0.0f, 2.0f, 0.02f, "%.2fx", on_scrub, on_project_changed);

    ImGui::Spacing();
    ImGui::Separator();

    ImGui::Text("Audio Gain Curve Graph:");
    render_curve_graph(project, clip, "volume", 0.0f, 2.0f, playhead_time, on_scrub, on_project_changed);
}

void KeyframeEditor::render_curve_graph(
    Project& project, Clip* clip, const std::string& current_param,
    float min_val, float max_val,
    double playhead_time,
    const std::function<void(double)>& on_scrub,
    const std::function<void()>& on_project_changed)
{
    ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
    ImVec2 canvas_size = ImGui::GetContentRegionAvail();
    canvas_size.y -= 36.0f;
    if (canvas_size.y < 80.0f) canvas_size.y = 80.0f;
    if (canvas_size.x < 120.0f) canvas_size.x = 120.0f;

    ImGui::InvisibleButton("KeyframeCanvas", canvas_size);
    bool is_hovered = ImGui::IsItemHovered();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    // Dark canvas styling
    ImVec2 canvas_end(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y);
    draw_list->AddRectFilled(canvas_pos, canvas_end, IM_COL32(28, 31, 36, 255));
    draw_list->AddRect(canvas_pos, canvas_end, IM_COL32(50, 55, 65, 255));

    float margin_left = 40.0f;
    float margin_bottom = 18.0f;
    float plot_x = canvas_pos.x + margin_left;
    float plot_y = canvas_pos.y + 8.0f;
    float plot_w = canvas_size.x - margin_left - 10.0f;
    float plot_h = canvas_size.y - margin_bottom - 16.0f;

    float val_range = max_val - min_val;
    if (val_range <= 0.001f) val_range = 1.0f;

    auto time_to_x = [&](double t) -> float {
        if (clip->duration <= 0.0) return plot_x;
        return plot_x + static_cast<float>(t / clip->duration) * plot_w;
    };

    auto val_to_y = [&](double v) -> float {
        float norm = (static_cast<float>(v) - min_val) / val_range;
        return (plot_y + plot_h) - norm * plot_h;
    };

    auto x_to_time = [&](float x) -> double {
        if (plot_w <= 0.0f) return 0.0;
        return std::clamp(static_cast<double>((x - plot_x) / plot_w) * clip->duration, 0.0, clip->duration);
    };

    auto y_to_val = [&](float y) -> double {
        if (plot_h <= 0.0f) return min_val;
        float norm = ((plot_y + plot_h) - y) / plot_h;
        return std::clamp(static_cast<double>(min_val + norm * val_range), static_cast<double>(min_val), static_cast<double>(max_val));
    };

    // Horizontal Grid Lines
    int grid_steps = 4;
    for (int i = 0; i <= grid_steps; ++i) {
        float norm = static_cast<float>(i) / grid_steps;
        float gy = (plot_y + plot_h) - norm * plot_h;
        draw_list->AddLine(ImVec2(plot_x, gy), ImVec2(plot_x + plot_w, gy), IM_COL32(42, 47, 56, 255));

        char label[16];
        snprintf(label, sizeof(label), "%.1f", min_val + norm * val_range);
        draw_list->AddText(ImVec2(canvas_pos.x + 4, gy - 6), IM_COL32(130, 140, 155, 255), label);
    }

    // Vertical Grid Lines (Time ticks)
    int time_steps = static_cast<int>(std::ceil(clip->duration));
    if (time_steps < 1) time_steps = 1;
    if (time_steps > 10) time_steps = 8;
    for (int i = 0; i <= time_steps; ++i) {
        double t = (static_cast<double>(i) / time_steps) * clip->duration;
        float gx = time_to_x(t);
        draw_list->AddLine(ImVec2(gx, plot_y), ImVec2(gx, plot_y + plot_h), IM_COL32(42, 47, 56, 255));

        char label[16];
        snprintf(label, sizeof(label), "%.1fs", t);
        draw_list->AddText(ImVec2(gx - 8, plot_y + plot_h + 2), IM_COL32(130, 140, 155, 255), label);
    }

    // Draw Curve
    std::vector<Keyframe> kfs = project.get_clip_keyframes(clip->id, current_param);
    const int curve_samples = 100;
    std::vector<ImVec2> curve_pts;
    curve_pts.reserve(curve_samples + 1);

    for (int i = 0; i <= curve_samples; ++i) {
        double t = (static_cast<double>(i) / curve_samples) * clip->duration;
        double v = project.get_clip_param(*clip, current_param, t);
        curve_pts.push_back(ImVec2(time_to_x(t), val_to_y(v)));
    }

    ImU32 col_glow = (current_param == "volume") ? IM_COL32(46, 204, 113, 60) : IM_COL32(0, 180, 255, 60);
    ImU32 col_line = (current_param == "volume") ? IM_COL32(46, 204, 113, 255) : IM_COL32(0, 210, 255, 255);

    draw_list->AddPolyline(curve_pts.data(), static_cast<int>(curve_pts.size()), col_glow, 0, 4.0f);
    draw_list->AddPolyline(curve_pts.data(), static_cast<int>(curve_pts.size()), col_line, 0, 2.0f);

    // Playhead line
    double rel_playhead = playhead_time - clip->start_time;
    if (rel_playhead >= 0.0 && rel_playhead <= clip->duration) {
        float px = time_to_x(rel_playhead);
        draw_list->AddLine(ImVec2(px, plot_y), ImVec2(px, plot_y + plot_h), IM_COL32(235, 77, 75, 220), 2.0f);
    }

    // Keyframe Nodes
    ImVec2 mouse_pos = ImGui::GetIO().MousePos;
    int hovered_node = -1;

    for (size_t i = 0; i < kfs.size(); ++i) {
        ImVec2 node_pos(time_to_x(kfs[i].time), val_to_y(kfs[i].value));
        float dist = std::hypot(mouse_pos.x - node_pos.x, mouse_pos.y - node_pos.y);
        if (dist <= 8.0f) {
            hovered_node = static_cast<int>(i);
        }

        bool is_node_selected = (static_cast<int>(i) == m_selected_kf_idx);
        ImU32 fill_col = is_node_selected ? IM_COL32(255, 235, 59, 255) :
                         (hovered_node == static_cast<int>(i) ? IM_COL32(255, 180, 0, 255) : IM_COL32(243, 156, 18, 255));
        ImU32 border_col = is_node_selected ? IM_COL32(255, 255, 255, 255) : IM_COL32(30, 30, 30, 255);

        draw_diamond(draw_list, node_pos, is_node_selected ? 7.0f : 5.5f, fill_col, border_col);
    }

    // Mouse Interactions
    if (is_hovered && ImGui::IsMouseClicked(0)) {
        if (hovered_node >= 0) {
            m_selected_kf_idx = hovered_node;
            m_dragging_node = true;
            m_drag_node_idx = hovered_node;
        } else {
            double clicked_time = x_to_time(mouse_pos.x);
            if (on_scrub) on_scrub(clip->start_time + clicked_time);
        }
    }

    // Double click to add keyframe
    if (is_hovered && ImGui::IsMouseDoubleClicked(0)) {
        double new_t = x_to_time(mouse_pos.x);
        double new_v = y_to_val(mouse_pos.y);
        project.set_or_add_keyframe(clip->id, current_param, new_t, new_v);
        if (on_project_changed) on_project_changed();

        kfs = project.get_clip_keyframes(clip->id, current_param);
        for (size_t i = 0; i < kfs.size(); ++i) {
            if (std::abs(kfs[i].time - new_t) < 0.05) {
                m_selected_kf_idx = static_cast<int>(i);
                break;
            }
        }
    }

    // Node Dragging
    if (m_dragging_node && ImGui::IsMouseDown(0) && m_drag_node_idx >= 0 && m_drag_node_idx < static_cast<int>(kfs.size())) {
        double new_t = x_to_time(mouse_pos.x);
        double new_v = y_to_val(mouse_pos.y);

        size_t count = 0;
        for (auto& kf : project.keyframes) {
            if (kf.clip_id == clip->id && kf.parameter == current_param) {
                if (static_cast<int>(count) == m_drag_node_idx) {
                    kf.time = new_t;
                    kf.value = new_v;
                    break;
                }
                count++;
            }
        }
        if (on_project_changed) on_project_changed();
    }

    if (m_dragging_node && !ImGui::IsMouseDown(0)) {
        m_dragging_node = false;
        m_drag_node_idx = -1;
    }

    // Controls below canvas
    ImGui::Separator();
    if (ImGui::Button("+ Add Keyframe", ImVec2(120, 22))) {
        double current_val = project.get_clip_param(*clip, current_param, rel_playhead);
        project.set_or_add_keyframe(clip->id, current_param, std::clamp(rel_playhead, 0.0, clip->duration), current_val);
        if (on_project_changed) on_project_changed();
    }
    ImGui::SameLine();
    if (ImGui::Button("- Delete Keyframe", ImVec2(130, 22))) {
        if (m_selected_kf_idx >= 0 && m_selected_kf_idx < static_cast<int>(kfs.size())) {
            project.remove_keyframe(clip->id, current_param, static_cast<size_t>(m_selected_kf_idx));
            m_selected_kf_idx = -1;
            if (on_project_changed) on_project_changed();
        }
    }

    // Selected Node Details
    if (m_selected_kf_idx >= 0 && m_selected_kf_idx < static_cast<int>(kfs.size())) {
        Keyframe& cur_kf = kfs[m_selected_kf_idx];
        ImGui::SameLine();
        ImGui::TextDisabled("Node:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(70.0f);
        float t_val = static_cast<float>(cur_kf.time);
        if (ImGui::DragFloat("Time##node", &t_val, 0.05f, 0.0f, static_cast<float>(clip->duration), "%.2fs")) {
            cur_kf.time = t_val;
            project.set_or_add_keyframe(clip->id, current_param, cur_kf.time, cur_kf.value);
            if (on_project_changed) on_project_changed();
        }

        ImGui::SameLine();
        ImGui::SetNextItemWidth(70.0f);
        float v_val = static_cast<float>(cur_kf.value);
        if (ImGui::DragFloat("Val##node", &v_val, 0.02f, min_val, max_val, "%.2f")) {
            cur_kf.value = v_val;
            project.set_or_add_keyframe(clip->id, current_param, cur_kf.time, cur_kf.value);
            if (on_project_changed) on_project_changed();
        }
    }
}
