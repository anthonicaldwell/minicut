#include "ProjectSerializer.h"
#include "MediaDecoder.h"
#include "TextGenerator.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <filesystem>

namespace fs = std::filesystem;
using json = nlohmann::json;

std::string ProjectSerializer::serialize_json(Project& project) {
    if (project.created_at.empty()) {
        project.created_at = current_iso8601();
    }
    project.updated_at = current_iso8601();

    json root;
    root["version"] = project.version.empty() ? "1.0" : project.version;
    root["name"] = project.name.empty() ? "Untitled Project" : project.name;
    root["created_at"] = project.created_at;
    root["updated_at"] = project.updated_at;

    // Assets
    json assets_obj;
    json videos_arr = json::array();
    for (const auto& v : project.video_assets) {
        json vj;
        vj["id"] = v.id;
        vj["path"] = v.path;
        vj["folder"] = v.folder;
        if (!project.file_path.empty()) {
            std::error_code ec;
            fs::path proj_dir = fs::path(project.file_path).parent_path();
            fs::path rel = fs::relative(v.path, proj_dir, ec);
            if (!ec && !rel.empty()) {
                vj["rel_path"] = rel.string();
            }
        }
        vj["duration"] = v.duration;
        vj["scale"] = v.scale;
        vj["scale_x"] = v.scale_x;
        vj["scale_y"] = v.scale_y;
        vj["width"] = v.width;
        vj["height"] = v.height;
        vj["fps"] = v.fps;
        vj["has_audio"] = v.has_audio;
        vj["is_image"] = v.is_image;
        if (!v.text_content.empty()) {
            vj["text_content"] = v.text_content;
            vj["text_font_path"] = v.text_font_path;
            vj["text_font_size"] = v.text_font_size;
            vj["text_font_color"] = { v.text_font_color[0], v.text_font_color[1], v.text_font_color[2], v.text_font_color[3] };
            vj["text_use_bg"] = v.text_use_bg;
            vj["text_bg_color"] = { v.text_bg_color[0], v.text_bg_color[1], v.text_bg_color[2], v.text_bg_color[3] };
            vj["text_use_outline"] = v.text_use_outline;
            vj["text_outline_color"] = { v.text_outline_color[0], v.text_outline_color[1], v.text_outline_color[2], v.text_outline_color[3] };
            vj["text_outline_thickness"] = v.text_outline_thickness;
            vj["text_letter_spacing"] = v.text_letter_spacing;
            vj["text_line_spacing"] = v.text_line_spacing;
            vj["text_justification"] = v.text_justification;
        }
        videos_arr.push_back(vj);
    }
    assets_obj["videos"] = videos_arr;

    json audio_arr = json::array();
    for (const auto& a : project.audio_assets) {
        json aj;
        aj["id"] = a.id;
        aj["path"] = a.path;
        aj["folder"] = a.folder;
        if (!project.file_path.empty()) {
            std::error_code ec;
            fs::path proj_dir = fs::path(project.file_path).parent_path();
            fs::path rel = fs::relative(a.path, proj_dir, ec);
            if (!ec && !rel.empty()) {
                aj["rel_path"] = rel.string();
            }
        }
        aj["duration"] = a.duration;
        aj["sample_rate"] = a.sample_rate;
        aj["channels"] = a.channels;
        audio_arr.push_back(aj);
    }
    assets_obj["audio"] = audio_arr;
    root["assets"] = assets_obj;

    // Timeline
    json timeline_obj;
    json tracks_arr = json::array();
    for (const auto& track : project.tracks) {
        json tj;
        tj["id"] = track.id;
        tj["name"] = track.name;
        tj["type"] = track.type;
        tj["depth"] = track.depth;
        json clips_arr = json::array();
        for (const auto& clip : track.clips) {
            json cj;
            cj["id"] = clip.id;
            cj["asset_id"] = clip.asset_id;
            cj["start_time"] = clip.start_time;
            cj["duration"] = clip.duration;
            cj["offset"] = clip.offset;
            cj["pos_x"] = clip.pos_x;
            cj["pos_y"] = clip.pos_y;
            cj["rotation"] = clip.rotation;
            cj["scale"] = clip.scale;
            cj["scale_x"] = clip.scale_x;
            cj["scale_y"] = clip.scale_y;
            cj["opacity"] = clip.opacity;
            if (!clip.prefab_id.empty()) {
                cj["prefab_id"] = clip.prefab_id;
                cj["prefab_name"] = clip.prefab_name;
            }
            if (!clip.subtitle_text.empty()) {
                cj["subtitle_text"] = clip.subtitle_text;
            }
            clips_arr.push_back(cj);
        }
        tj["clips"] = clips_arr;
        tracks_arr.push_back(tj);
    }
    timeline_obj["tracks"] = tracks_arr;

    json keyframes_arr = json::array();
    for (const auto& kf : project.keyframes) {
        json kfj;
        kfj["clip_id"] = kf.clip_id;
        kfj["parameter"] = kf.parameter;
        kfj["time"] = kf.time;
        kfj["value"] = kf.value;
        keyframes_arr.push_back(kfj);
    }
    timeline_obj["keyframes"] = keyframes_arr;
    root["timeline"] = timeline_obj;

    // Prefabs
    json prefabs_arr = json::array();
    for (const auto& pf : project.prefabs) {
        json pfj;
        pfj["id"] = pf.id;
        pfj["name"] = pf.name;
        pfj["path"] = pf.path;
        pfj["folder"] = pf.folder;
        if (!project.file_path.empty()) {
            std::error_code ec;
            fs::path proj_dir = fs::path(project.file_path).parent_path();
            fs::path rel = fs::relative(pf.path, proj_dir, ec);
            if (!ec && !rel.empty()) {
                pfj["rel_path"] = rel.string();
            }
        }
        pfj["duration"] = pf.duration;
        pfj["video_tracks"] = pf.video_tracks;
        pfj["audio_tracks"] = pf.audio_tracks;
        pfj["total_clips"] = pf.total_clips;
        pfj["raw_json"] = pf.raw_json;
        prefabs_arr.push_back(pfj);
    }
    root["prefabs"] = prefabs_arr;

    // Asset Folders
    json folders_arr = json::array();
    for (const auto& f : project.asset_folders) {
        folders_arr.push_back(f);
    }
    root["asset_folders"] = folders_arr;

    return root.dump(2);
}

bool ProjectSerializer::save_to_file(Project& project, const std::string& filepath) {
    try {
        std::string json_str = serialize_json(project);
        std::ofstream ofs(filepath);
        if (!ofs.is_open()) {
            std::cerr << "Failed to open file for writing: " << filepath << std::endl;
            return false;
        }
        ofs << json_str;
        project.file_path = filepath;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Exception in save_to_file: " << e.what() << std::endl;
        return false;
    }
}

bool ProjectSerializer::deserialize_json(Project& project, const std::string& json_str) {
    try {
        json root = json::parse(json_str);

        if (root.contains("version") && root["version"].is_string()) {
            project.version = root["version"].get<std::string>();
        }
        if (root.contains("name") && root["name"].is_string()) {
            project.name = root["name"].get<std::string>();
        }
        if (root.contains("created_at") && root["created_at"].is_string()) {
            project.created_at = root["created_at"].get<std::string>();
        }
        if (root.contains("updated_at") && root["updated_at"].is_string()) {
            project.updated_at = root["updated_at"].get<std::string>();
        }

        project.video_assets.clear();
        project.audio_assets.clear();
        project.tracks.clear();
        project.keyframes.clear();
        project.asset_folders.clear();

        if (root.contains("asset_folders") && root["asset_folders"].is_array()) {
            for (const auto& fj : root["asset_folders"]) {
                if (fj.is_string()) {
                    std::string f_str = fj.get<std::string>();
                    if (!f_str.empty()) project.add_asset_folder(f_str);
                }
            }
        }

        if (root.contains("assets") && root["assets"].is_object()) {
            const auto& assets_obj = root["assets"];
            if (assets_obj.contains("videos") && assets_obj["videos"].is_array()) {
                for (const auto& vj : assets_obj["videos"]) {
                    VideoAsset va;
                    va.id = vj.value("id", "");
                    va.path = vj.value("path", "");
                    va.folder = vj.value("folder", "");
                    if (!va.folder.empty()) {
                        project.add_asset_folder(va.folder);
                    }
                    if (vj.contains("rel_path") && vj["rel_path"].is_string() && !project.file_path.empty()) {
                        std::string rel_p = vj["rel_path"].get<std::string>();
                        std::error_code ec;
                        fs::path resolved = fs::path(project.file_path).parent_path() / rel_p;
                        if (fs::exists(resolved, ec)) {
                            va.path = resolved.lexically_normal().string();
                        }
                    }
                    va.duration = vj.value("duration", 0.0);
                    va.scale = vj.value("scale", 1.0);
                    va.scale_x = vj.value("scale_x", va.scale);
                    va.scale_y = vj.value("scale_y", va.scale);
                    va.width = vj.value("width", 0);
                    va.height = vj.value("height", 0);
                    va.fps = vj.value("fps", 30.0);
                    va.has_audio = vj.value("has_audio", false);
                    va.is_image = vj.value("is_image", false);

                    if (vj.contains("text_content") && vj["text_content"].is_string()) {
                        va.text_content = vj["text_content"].get<std::string>();
                        va.text_font_path = vj.value("text_font_path", "");
                        va.text_font_size = vj.value("text_font_size", 48.0f);
                        if (vj.contains("text_font_color") && vj["text_font_color"].is_array() && vj["text_font_color"].size() >= 4) {
                            for (int i = 0; i < 4; ++i) va.text_font_color[i] = vj["text_font_color"][i].get<float>();
                        }
                        va.text_use_bg = vj.value("text_use_bg", false);
                        if (vj.contains("text_bg_color") && vj["text_bg_color"].is_array() && vj["text_bg_color"].size() >= 4) {
                            for (int i = 0; i < 4; ++i) va.text_bg_color[i] = vj["text_bg_color"][i].get<float>();
                        }
                        va.text_use_outline = vj.value("text_use_outline", false);
                        if (vj.contains("text_outline_color") && vj["text_outline_color"].is_array() && vj["text_outline_color"].size() >= 4) {
                            for (int i = 0; i < 4; ++i) va.text_outline_color[i] = vj["text_outline_color"][i].get<float>();
                        }
                        va.text_outline_thickness = vj.value("text_outline_thickness", 2.5f);
                        va.text_letter_spacing = vj.value("text_letter_spacing", 0.0f);
                        va.text_line_spacing = vj.value("text_line_spacing", 1.25f);
                        va.text_justification = vj.value("text_justification", 0);
                        va.is_image = true;
                    }

                    // Dynamic re-rendering for generated text assets if PNG was a purged temporary file or missing
                    if (!va.text_content.empty() && (va.path.empty() || !fs::exists(va.path))) {
                        const char* home = std::getenv("HOME");
                        std::string temp_dir = home ? (std::string(home) + "/.config/minicut/temp") : "/tmp";
                        fs::create_directories(temp_dir);
                        std::string safe_id = va.id.empty() ? "text" : va.id;
                        std::string full_path = (fs::path(temp_dir) / (safe_id + ".png")).string();

                        TextConfig cfg;
                        cfg.text = va.text_content;
                        cfg.font_path = va.text_font_path;
                        cfg.font_size = va.text_font_size;
                        for (int i = 0; i < 4; ++i) cfg.font_color[i] = va.text_font_color[i];
                        cfg.use_bg = va.text_use_bg;
                        for (int i = 0; i < 4; ++i) cfg.bg_color[i] = va.text_bg_color[i];
                        cfg.use_outline = va.text_use_outline;
                        for (int i = 0; i < 4; ++i) cfg.outline_color[i] = va.text_outline_color[i];
                        cfg.outline_thickness = va.text_outline_thickness;
                        cfg.letter_spacing = va.text_letter_spacing;
                        cfg.line_spacing = va.text_line_spacing;
                        cfg.justification = va.text_justification;

                        int rw = 0, rh = 0;
                        if (TextGenerator::save_to_png(cfg, full_path, rw, rh)) {
                            va.path = full_path;
                            va.width = rw;
                            va.height = rh;
                            va.is_image = true;
                        }
                    }

                    // Re-extract metadata and audio stream presence if file exists on disk
                    if (!va.path.empty() && fs::exists(va.path)) {
                        if (!va.is_image && (va.width <= 0 || va.height <= 0 || !va.has_audio || va.duration <= 0.0)) {
                            VideoAsset probed;
                            if (MediaDecoder::probe_video_file(va.path, probed)) {
                                if (va.width <= 0) va.width = probed.width;
                                if (va.height <= 0) va.height = probed.height;
                                if (va.fps <= 0.0) va.fps = probed.fps;
                                if (va.duration <= 0.0) va.duration = probed.duration;
                                if (probed.has_audio) va.has_audio = true;
                                if (probed.is_image) va.is_image = true;
                            }
                        }
                    }
                    project.video_assets.push_back(va);
                }
            }
            if (assets_obj.contains("audio") && assets_obj["audio"].is_array()) {
                for (const auto& aj : assets_obj["audio"]) {
                    AudioAsset aa;
                    aa.id = aj.value("id", "");
                    aa.path = aj.value("path", "");
                    aa.folder = aj.value("folder", "");
                    if (!aa.folder.empty()) {
                        project.add_asset_folder(aa.folder);
                    }
                    if (aj.contains("rel_path") && aj["rel_path"].is_string() && !project.file_path.empty()) {
                        std::string rel_p = aj["rel_path"].get<std::string>();
                        std::error_code ec;
                        fs::path resolved = fs::path(project.file_path).parent_path() / rel_p;
                        if (fs::exists(resolved, ec)) {
                            aa.path = resolved.lexically_normal().string();
                        }
                    }
                    aa.duration = aj.value("duration", 0.0);
                    aa.sample_rate = aj.value("sample_rate", 44100);
                    aa.channels = aj.value("channels", 2);

                    if (!aa.path.empty() && fs::exists(aa.path)) {
                        if (aa.duration <= 0.0 || aa.sample_rate <= 0) {
                            AudioAsset probed;
                            if (MediaDecoder::probe_audio_file(aa.path, probed)) {
                                if (aa.duration <= 0.0) aa.duration = probed.duration;
                                aa.sample_rate = probed.sample_rate;
                                aa.channels = probed.channels;
                            }
                        }
                    }
                    project.audio_assets.push_back(aa);
                }
            }
        }

        int clip_counter = 1;
        if (root.contains("timeline") && root["timeline"].is_object()) {
            const auto& timeline_obj = root["timeline"];
            if (timeline_obj.contains("tracks") && timeline_obj["tracks"].is_array()) {
                int track_idx = 1;
                for (const auto& tj : timeline_obj["tracks"]) {
                    Track t;
                    t.type = tj.value("type", "video");
                    t.depth = tj.value("depth", 0);
                    std::string def_name = (t.type == "video" ? "Video " : "Audio ") + std::to_string(track_idx);
                    t.name = tj.value("name", def_name);
                    t.id = tj.value("id", "track_" + std::to_string(track_idx));
                    track_idx++;
                    if (tj.contains("clips") && tj["clips"].is_array()) {
                        for (const auto& cj : tj["clips"]) {
                            Clip c;
                            c.asset_id = cj.value("asset_id", "");
                            c.start_time = cj.value("start_time", 0.0);
                            c.duration = cj.value("duration", 0.0);
                            c.offset = cj.value("offset", 0.0);
                            c.id = cj.value("id", "clip_" + std::to_string(clip_counter++));
                            c.pos_x = cj.value("pos_x", 0.0);
                            c.pos_y = cj.value("pos_y", 0.0);
                            c.rotation = cj.value("rotation", 0.0);
                            c.scale = cj.value("scale", 1.0);
                            c.scale_x = cj.value("scale_x", c.scale);
                            c.scale_y = cj.value("scale_y", c.scale);
                            c.opacity = cj.value("opacity", 1.0);
                            c.prefab_id = cj.value("prefab_id", "");
                            c.prefab_name = cj.value("prefab_name", "");
                            c.subtitle_text = cj.value("subtitle_text", "");
                            t.clips.push_back(c);
                        }
                    }
                    project.tracks.push_back(t);
                }
            }

            if (timeline_obj.contains("keyframes") && timeline_obj["keyframes"].is_array()) {
                for (const auto& kfj : timeline_obj["keyframes"]) {
                    Keyframe kf;
                    kf.clip_id = kfj.value("clip_id", "");
                    kf.parameter = kfj.value("parameter", "opacity");
                    kf.time = kfj.value("time", 0.0);
                    kf.value = kfj.value("value", 1.0);
                    project.keyframes.push_back(kf);
                }
            }
        }

        // Prefabs
        project.prefabs.clear();
        if (root.contains("prefabs") && root["prefabs"].is_array()) {
            for (const auto& pfj : root["prefabs"]) {
                ProjectPrefab pf;
                pf.id = pfj.value("id", "");
                pf.name = pfj.value("name", "");
                pf.path = pfj.value("path", "");
                pf.folder = pfj.value("folder", "");
                if (!pf.folder.empty()) {
                    project.add_asset_folder(pf.folder);
                }
                if (pfj.contains("rel_path") && pfj["rel_path"].is_string() && !project.file_path.empty()) {
                    std::string rel_p = pfj["rel_path"].get<std::string>();
                    std::error_code ec;
                    fs::path resolved = fs::path(project.file_path).parent_path() / rel_p;
                    if (fs::exists(resolved, ec)) {
                        pf.path = resolved.lexically_normal().string();
                    }
                }
                pf.duration = pfj.value("duration", 0.0);
                pf.video_tracks = pfj.value("video_tracks", 0);
                pf.audio_tracks = pfj.value("audio_tracks", 0);
                pf.total_clips = pfj.value("total_clips", 0);
                pf.raw_json = pfj.value("raw_json", "");
                project.prefabs.push_back(pf);
            }
        }

        // If no tracks exist, initialize standard default tracks
        if (project.tracks.empty()) {
            Track v1; v1.type = "video"; v1.name = "Video 1"; v1.id = "track_v1";
            Track v2; v2.type = "video"; v2.name = "Video 2"; v2.id = "track_v2";
            Track a1; a1.type = "audio"; a1.name = "Audio 1"; a1.id = "track_a1";
            Track a2; a2.type = "audio"; a2.name = "Audio 2"; a2.id = "track_a2";
            project.tracks.push_back(v1);
            project.tracks.push_back(v2);
            project.tracks.push_back(a1);
            project.tracks.push_back(a2);
        }

        return true;
    } catch (const std::exception& e) {
        std::cerr << "Exception in deserialize_json: " << e.what() << std::endl;
        return false;
    }
}

bool ProjectSerializer::load_from_file(Project& project, const std::string& filepath) {
    try {
        std::ifstream ifs(filepath);
        if (!ifs.is_open()) {
            std::cerr << "Failed to open project file: " << filepath << std::endl;
            return false;
        }
        std::string json_str((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        project.file_path = filepath;
        if (deserialize_json(project, json_str)) {
            return true;
        }
        return false;
    } catch (const std::exception& e) {
        std::cerr << "Exception in load_from_file: " << e.what() << std::endl;
        return false;
    }
}
