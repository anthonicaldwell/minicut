#pragma once

#include <string>
#include <vector>
#include <memory>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <GL/glew.h>

#define MINICUT_VERSION "1.4.0"

// Forward declarations
struct VideoAsset;
struct AudioAsset;
struct Keyframe;
struct Clip;
struct Track;
struct Project;

struct VideoAsset {
    std::string id;
    std::string path;
    double duration = 0.0; // In seconds
    int width = 0;
    int height = 0;
    double fps = 30.0;
    bool has_audio = false;
    bool is_image = false;
    GLuint thumbnail_texture = 0;
};

struct AudioAsset {
    std::string id;
    std::string path;
    double duration = 0.0; // In seconds
    int sample_rate = 44100;
    int channels = 2;
};

struct Keyframe {
    std::string clip_id;
    std::string parameter; // "opacity" or "volume"
    double time = 0.0;     // Time relative to clip start (seconds)
    double value = 1.0;    // Opacity [0.0, 1.0], Volume [0.0, 2.0]
};

struct ProjectPrefab {
    std::string id;              // e.g. "pf1"
    std::string name;            // e.g. "IntroSequence"
    std::string path;            // file path
    double duration = 0.0;
    int video_tracks = 0;
    int audio_tracks = 0;
    int total_clips = 0;
    std::string raw_json;        // Full project JSON payload for lossless instantiation
    GLuint thumbnail_texture = 0;
};

struct Clip {
    std::string id;
    std::string asset_id;
    double start_time = 0.0; // Start position on the timeline in seconds
    double duration = 0.0;   // Visible duration on timeline in seconds
    double offset = 0.0;     // Media start offset within asset in seconds

    // Transform default / base properties (when not keyframed)
    double pos_x = 0.0;      // Center X offset in pixels relative to canvas center
    double pos_y = 0.0;      // Center Y offset in pixels relative to canvas center
    double rotation = 0.0;   // Rotation angle in degrees
    double scale = 1.0;      // Scale multiplier (1.0 = 100%)
    double opacity = 1.0;    // Opacity [0.0, 1.0]

    // Prefab membership / provenance
    std::string prefab_id;   // e.g. "pf1" if clip originated from a prefab
    std::string prefab_name; // Display name of origin prefab

    // Subtitle text for subtitle tracks
    std::string subtitle_text;

    double end_time() const { return start_time + duration; }
    bool contains_time(double t) const { return t >= start_time && t < (start_time + duration); }
    double clip_relative_time(double t) const { return t - start_time; }
    double media_time(double t) const { return offset + (t - start_time); }
};

struct Track {
    std::string id;
    std::string name;
    std::string type; // "video", "audio", or "subtitle"
    bool muted = false;
    bool solo = false;
    int depth = 0; // Render depth (0-100; lower renders bottom/background, higher renders top/foreground)
    std::vector<Clip> clips;
};

struct Project {
    std::string version = "1.0";
    std::string name = "Untitled Project";
    std::string file_path;
    std::string created_at;
    std::string updated_at;
    std::vector<VideoAsset> video_assets;
    std::vector<AudioAsset> audio_assets;
    std::vector<Track> tracks;
    std::vector<Keyframe> keyframes;
    std::vector<ProjectPrefab> prefabs;

    double get_total_duration() const {
        double max_time = 10.0; // Minimum default timeline length
        for (const auto& track : tracks) {
            for (const auto& clip : track.clips) {
                if (clip.end_time() > max_time) {
                    max_time = clip.end_time();
                }
            }
        }
        return max_time;
    }

    double get_assets_duration() const {
        double max_time = 0.0;
        for (const auto& track : tracks) {
            for (const auto& clip : track.clips) {
                if (clip.end_time() > max_time) {
                    max_time = clip.end_time();
                }
            }
        }
        return max_time;
    }

    double get_largest_asset_aspect_ratio() const {
        double max_aspect = 0.0;
        // Search timeline clips for active visual assets
        for (const auto& track : tracks) {
            for (const auto& clip : track.clips) {
                const VideoAsset* asset = find_video_asset(clip.asset_id);
                if (asset && asset->width > 0 && asset->height > 0) {
                    double ar = static_cast<double>(asset->width) / static_cast<double>(asset->height);
                    if (ar > max_aspect) {
                        max_aspect = ar;
                    }
                }
            }
        }
        // If no visual clips placed on timeline yet, check project video assets pool
        if (max_aspect <= 0.0) {
            for (const auto& asset : video_assets) {
                if (asset.width > 0 && asset.height > 0) {
                    double ar = static_cast<double>(asset.width) / static_cast<double>(asset.height);
                    if (ar > max_aspect) {
                        max_aspect = ar;
                    }
                }
            }
        }
        // Default standard 16:9 widescreen fallback
        if (max_aspect <= 0.0) {
            max_aspect = 16.0 / 9.0;
        }
        return max_aspect;
    }

    const VideoAsset* find_video_asset(const std::string& id) const {
        for (const auto& a : video_assets) {
            if (a.id == id) return &a;
        }
        return nullptr;
    }

    const AudioAsset* find_audio_asset(const std::string& id) const {
        for (const auto& a : audio_assets) {
            if (a.id == id) return &a;
        }
        return nullptr;
    }

    const ProjectPrefab* find_prefab(const std::string& id) const {
        for (const auto& p : prefabs) {
            if (p.id == id) return &p;
        }
        return nullptr;
    }

    const Clip* find_clip(const std::string& clip_id, int* out_track = nullptr, int* out_idx = nullptr) const {
        for (size_t t = 0; t < tracks.size(); ++t) {
            for (size_t c = 0; c < tracks[t].clips.size(); ++c) {
                if (tracks[t].clips[c].id == clip_id) {
                    if (out_track) *out_track = static_cast<int>(t);
                    if (out_idx) *out_idx = static_cast<int>(c);
                    return &tracks[t].clips[c];
                }
            }
        }
        return nullptr;
    }

    Clip* find_clip_mut(const std::string& clip_id, int* out_track = nullptr, int* out_idx = nullptr) {
        for (size_t t = 0; t < tracks.size(); ++t) {
            for (size_t c = 0; c < tracks[t].clips.size(); ++c) {
                if (tracks[t].clips[c].id == clip_id) {
                    if (out_track) *out_track = static_cast<int>(t);
                    if (out_idx) *out_idx = static_cast<int>(c);
                    return &tracks[t].clips[c];
                }
            }
        }
        return nullptr;
    }

    std::vector<Keyframe> get_clip_keyframes(const std::string& clip_id, const std::string& param) const {
        std::vector<Keyframe> list;
        for (const auto& kf : keyframes) {
            if (kf.clip_id == clip_id && kf.parameter == param) {
                list.push_back(kf);
            }
        }
        std::sort(list.begin(), list.end(), [](const Keyframe& a, const Keyframe& b) {
            return a.time < b.time;
        });
        return list;
    }

    double evaluate_keyframe(const std::string& clip_id, const std::string& param, double clip_rel_time, double default_val = 1.0) const {
        std::vector<Keyframe> kfs = get_clip_keyframes(clip_id, param);
        if (kfs.empty()) {
            return default_val;
        }
        if (kfs.size() == 1 || clip_rel_time <= kfs.front().time) {
            return kfs.front().value;
        }
        if (clip_rel_time >= kfs.back().time) {
            return kfs.back().value;
        }
        for (size_t i = 0; i + 1 < kfs.size(); ++i) {
            if (clip_rel_time >= kfs[i].time && clip_rel_time <= kfs[i + 1].time) {
                double dt = kfs[i + 1].time - kfs[i].time;
                if (dt <= 1e-6) return kfs[i].value;
                double factor = (clip_rel_time - kfs[i].time) / dt;
                return kfs[i].value + factor * (kfs[i + 1].value - kfs[i].value);
            }
        }
        return default_val;
    }

    void set_or_add_keyframe(const std::string& clip_id, const std::string& param, double time, double value) {
        for (auto& kf : keyframes) {
            if (kf.clip_id == clip_id && kf.parameter == param && std::abs(kf.time - time) < 0.05) {
                kf.value = value;
                return;
            }
        }
        keyframes.push_back({clip_id, param, time, value});
    }

    void remove_keyframe(const std::string& clip_id, const std::string& param, size_t index_in_param) {
        size_t current_idx = 0;
        for (auto it = keyframes.begin(); it != keyframes.end(); ++it) {
            if (it->clip_id == clip_id && it->parameter == param) {
                if (current_idx == index_in_param) {
                    keyframes.erase(it);
                    return;
                }
                current_idx++;
            }
        }
    }

    double get_clip_param(const Clip& clip, const std::string& param, double clip_rel_time) const {
        double def_val = 1.0;
        if (param == "pos_x") def_val = clip.pos_x;
        else if (param == "pos_y") def_val = clip.pos_y;
        else if (param == "rotation") def_val = clip.rotation;
        else if (param == "scale") def_val = clip.scale;
        else if (param == "opacity") def_val = clip.opacity;
        else if (param == "volume") def_val = 1.0;
        return evaluate_keyframe(clip.id, param, clip_rel_time, def_val);
    }

    bool has_keyframe_at(const std::string& clip_id, const std::string& param, double time, double tolerance = 0.05) const {
        for (const auto& kf : keyframes) {
            if (kf.clip_id == clip_id && kf.parameter == param && std::abs(kf.time - time) <= tolerance) {
                return true;
            }
        }
        return false;
    }

    bool remove_keyframe_at(const std::string& clip_id, const std::string& param, double time, double tolerance = 0.05) {
        for (auto it = keyframes.begin(); it != keyframes.end(); ++it) {
            if (it->clip_id == clip_id && it->parameter == param && std::abs(it->time - time) <= tolerance) {
                keyframes.erase(it);
                return true;
            }
        }
        return false;
    }

    double find_prev_keyframe_time(const std::string& clip_id, const std::string& param, double current_time) const {
        auto kfs = get_clip_keyframes(clip_id, param);
        double prev_t = -1.0;
        for (const auto& kf : kfs) {
            if (kf.time < (current_time - 0.05)) {
                prev_t = kf.time;
            } else {
                break;
            }
        }
        return prev_t;
    }

    double find_next_keyframe_time(const std::string& clip_id, const std::string& param, double current_time) const {
        auto kfs = get_clip_keyframes(clip_id, param);
        for (const auto& kf : kfs) {
            if (kf.time > (current_time + 0.05)) {
                return kf.time;
            }
        }
        return -1.0;
    }

    void remove_clip_keyframes(const std::string& clip_id) {
        keyframes.erase(std::remove_if(keyframes.begin(), keyframes.end(),
            [&](const Keyframe& kf) { return kf.clip_id == clip_id; }), keyframes.end());
    }
};

// ----------------------------------------------------------------------------
// High-Quality Transformed Layer Blitting (Position, Rotation, Scale, Opacity)
// ----------------------------------------------------------------------------
inline void blit_transformed_layer(
    std::vector<uint8_t>& dst_rgba, int dst_w, int dst_h,
    const std::vector<uint8_t>& src_rgba, int src_w, int src_h,
    double pos_x, double pos_y, double rotation_deg, double scale, double opacity)
{
    if (src_rgba.empty() || src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0) return;
    if (opacity <= 0.001) return;
    if (scale <= 0.001) return;

    // Scale pos_x and pos_y relative to reference 1280x720 coordinates
    double scale_factor_x = static_cast<double>(dst_w) / 1280.0;
    double scale_factor_y = static_cast<double>(dst_h) / 720.0;
    double actual_pos_x = pos_x * scale_factor_x;
    double actual_pos_y = pos_y * scale_factor_y;

    // Base fit scale: fit source into destination canvas while preserving aspect ratio
    double base_scale = std::min(static_cast<double>(dst_w) / src_w, static_cast<double>(dst_h) / src_h);
    double total_scale = base_scale * scale;
    if (total_scale <= 0.0001) return;

    double rad = -rotation_deg * (3.14159265358979323846 / 180.0); // Negative for inverse mapping
    double cos_r = std::cos(rad);
    double sin_r = std::sin(rad);

    double cx = dst_w * 0.5 + actual_pos_x;
    double cy = dst_h * 0.5 + actual_pos_y;

    double half_w = (src_w * total_scale) * 0.5;
    double half_h = (src_h * total_scale) * 0.5;

    // Destination bounding box of the 4 rotated corners
    double f_rad = rotation_deg * (3.14159265358979323846 / 180.0);
    double cos_f = std::cos(f_rad);
    double sin_f = std::sin(f_rad);

    double corners_x[4] = { -half_w,  half_w,  half_w, -half_w };
    double corners_y[4] = { -half_h, -half_h,  half_h,  half_h };

    double min_x = dst_w, max_x = 0;
    double min_y = dst_h, max_y = 0;

    for (int k = 0; k < 4; ++k) {
        double px = cx + corners_x[k] * cos_f - corners_y[k] * sin_f;
        double py = cy + corners_x[k] * sin_f + corners_y[k] * cos_f;
        if (px < min_x) min_x = px;
        if (px > max_x) max_x = px;
        if (py < min_y) min_y = py;
        if (py > max_y) max_y = py;
    }

    int start_x = std::max(0, static_cast<int>(std::floor(min_x)));
    int end_x   = std::min(dst_w - 1, static_cast<int>(std::ceil(max_x)));
    int start_y = std::max(0, static_cast<int>(std::floor(min_y)));
    int end_y   = std::min(dst_h - 1, static_cast<int>(std::ceil(max_y)));

    if (start_x > end_x || start_y > end_y) return;

    float alpha_mod = static_cast<float>(std::clamp(opacity, 0.0, 1.0));
    double inv_scale = 1.0 / total_scale;
    double half_src_w = src_w * 0.5;
    double half_src_h = src_h * 0.5;

    for (int y = start_y; y <= end_y; ++y) {
        double dy = y - cy;
        int dst_row_idx = (y * dst_w + start_x) * 4;

        for (int x = start_x; x <= end_x; ++x, dst_row_idx += 4) {
            double dx = x - cx;

            // Rotate back
            double rx = dx * cos_r - dy * sin_r;
            double ry = dx * sin_r + dy * cos_r;

            // Scale back
            double u = rx * inv_scale + half_src_w;
            double v = ry * inv_scale + half_src_h;

            if (u < 0.0 || u > static_cast<double>(src_w - 1) || v < 0.0 || v > static_cast<double>(src_h - 1)) {
                continue;
            }

            int x0 = static_cast<int>(std::floor(u));
            int y0 = static_cast<int>(std::floor(v));
            int x1 = std::min(x0 + 1, src_w - 1);
            int y1 = std::min(y0 + 1, src_h - 1);

            float fx = static_cast<float>(u - x0);
            float fy = static_cast<float>(v - y0);

            int idx00 = (y0 * src_w + x0) * 4;
            int idx10 = (y0 * src_w + x1) * 4;
            int idx01 = (y1 * src_w + x0) * 4;
            int idx11 = (y1 * src_w + x1) * 4;

            float w00 = (1.0f - fx) * (1.0f - fy);
            float w10 = fx * (1.0f - fy);
            float w01 = (1.0f - fx) * fy;
            float w11 = fx * fy;

            float r = src_rgba[idx00 + 0] * w00 + src_rgba[idx10 + 0] * w10 + src_rgba[idx01 + 0] * w01 + src_rgba[idx11 + 0] * w11;
            float g = src_rgba[idx00 + 1] * w00 + src_rgba[idx10 + 1] * w10 + src_rgba[idx01 + 1] * w01 + src_rgba[idx11 + 1] * w11;
            float b = src_rgba[idx00 + 2] * w00 + src_rgba[idx10 + 2] * w10 + src_rgba[idx01 + 2] * w01 + src_rgba[idx11 + 2] * w11;
            float a = src_rgba[idx00 + 3] * w00 + src_rgba[idx10 + 3] * w10 + src_rgba[idx01 + 3] * w01 + src_rgba[idx11 + 3] * w11;

            float src_a = (a / 255.0f) * alpha_mod;
            if (src_a <= 0.001f) continue;

            float inv_a = 1.0f - src_a;

            dst_rgba[dst_row_idx + 0] = static_cast<uint8_t>(r * src_a + dst_rgba[dst_row_idx + 0] * inv_a);
            dst_rgba[dst_row_idx + 1] = static_cast<uint8_t>(g * src_a + dst_rgba[dst_row_idx + 1] * inv_a);
            dst_rgba[dst_row_idx + 2] = static_cast<uint8_t>(b * src_a + dst_rgba[dst_row_idx + 2] * inv_a);
            dst_rgba[dst_row_idx + 3] = 255;
        }
    }
}

inline std::string format_timecode(double seconds, bool show_subseconds = true) {
    if (seconds < 0.0) seconds = 0.0;
    int total_s = static_cast<int>(seconds);
    int hours = total_s / 3600;
    int minutes = (total_s % 3600) / 60;
    int secs = total_s % 60;
    int millis = static_cast<int>((seconds - total_s) * 100.0);

    std::ostringstream ss;
    ss << std::setfill('0') << std::setw(2) << hours << ":"
       << std::setfill('0') << std::setw(2) << minutes << ":"
       << std::setfill('0') << std::setw(2) << secs;
    if (show_subseconds) {
        ss << "." << std::setfill('0') << std::setw(2) << millis;
    }
    return ss.str();
}

inline std::string current_iso8601() {
    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf;
    gmtime_r(&tt, &tm_buf);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_buf);
    return std::string(buf);
}
