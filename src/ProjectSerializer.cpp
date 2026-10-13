#include "ProjectSerializer.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>

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
        vj["duration"] = v.duration;
        videos_arr.push_back(vj);
    }
    assets_obj["videos"] = videos_arr;

    json audio_arr = json::array();
    for (const auto& a : project.audio_assets) {
        json aj;
        aj["id"] = a.id;
        aj["path"] = a.path;
        aj["duration"] = a.duration;
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
            cj["opacity"] = clip.opacity;
            if (!clip.prefab_id.empty()) {
                cj["prefab_id"] = clip.prefab_id;
                cj["prefab_name"] = clip.prefab_name;
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
        pfj["duration"] = pf.duration;
        pfj["video_tracks"] = pf.video_tracks;
        pfj["audio_tracks"] = pf.audio_tracks;
        pfj["total_clips"] = pf.total_clips;
        pfj["raw_json"] = pf.raw_json;
        prefabs_arr.push_back(pfj);
    }
    root["prefabs"] = prefabs_arr;

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

        if (root.contains("assets") && root["assets"].is_object()) {
            const auto& assets_obj = root["assets"];
            if (assets_obj.contains("videos") && assets_obj["videos"].is_array()) {
                for (const auto& vj : assets_obj["videos"]) {
                    VideoAsset va;
                    va.id = vj.value("id", "");
                    va.path = vj.value("path", "");
                    va.duration = vj.value("duration", 0.0);
                    project.video_assets.push_back(va);
                }
            }
            if (assets_obj.contains("audio") && assets_obj["audio"].is_array()) {
                for (const auto& aj : assets_obj["audio"]) {
                    AudioAsset aa;
                    aa.id = aj.value("id", "");
                    aa.path = aj.value("path", "");
                    aa.duration = aj.value("duration", 0.0);
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
                            c.opacity = cj.value("opacity", 1.0);
                            c.prefab_id = cj.value("prefab_id", "");
                            c.prefab_name = cj.value("prefab_name", "");
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
        if (deserialize_json(project, json_str)) {
            project.file_path = filepath;
            return true;
        }
        return false;
    } catch (const std::exception& e) {
        std::cerr << "Exception in load_from_file: " << e.what() << std::endl;
        return false;
    }
}
