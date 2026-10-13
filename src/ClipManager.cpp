#include "ClipManager.h"
#include "LocaleManager.h"
#include "MediaDecoder.h"
#include "AudioEngine.h"
#include "ProjectSerializer.h"
#include <imgui.h>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <map>

namespace fs = std::filesystem;

ClipManager::ClipManager() {}

ClipManager::~ClipManager() {}

bool ClipManager::import_video(Project& project, const std::string& filepath) {
    if (!fs::exists(filepath)) return false;

    m_import_progress.filename = fs::path(filepath).filename().string();
    m_import_progress.active.store(true);
    m_import_progress.set(0.15f, "Probing video format and streams...");

    VideoAsset va;
    if (!MediaDecoder::probe_video_file(filepath, va)) {
        std::cerr << "Failed to probe video file: " << filepath << std::endl;
        m_import_progress.active.store(false);
        return false;
    }

    m_import_progress.set(0.45f, "Decoding frame for thumbnail preview...");
    // Assign ID
    int next_num = 1;
    for (const auto& existing : project.video_assets) {
        if (existing.id.rfind("v", 0) == 0) {
            try {
                int n = std::stoi(existing.id.substr(1));
                if (n >= next_num) next_num = n + 1;
            } catch (...) {}
        }
    }
    va.id = "v" + std::to_string(next_num);
    va.thumbnail_texture = MediaDecoder::create_thumbnail_texture(filepath, std::min(1.0, va.duration * 0.2));

    project.video_assets.push_back(va);

    if (va.has_audio && m_audio_engine) {
        m_import_progress.set(0.75f, "Demuxing and preloading audio stream...");
        m_audio_engine->preload_asset_audio(filepath);
    }
    m_import_progress.set(1.0f, "Import complete!");
    m_import_progress.active.store(false);
    return true;
}

bool ClipManager::import_image(Project& project, const std::string& filepath, double default_duration) {
    if (!fs::exists(filepath)) return false;

    m_import_progress.filename = fs::path(filepath).filename().string();
    m_import_progress.active.store(true);
    m_import_progress.set(0.2f, "Probing image metadata...");

    VideoAsset va;
    if (!MediaDecoder::probe_video_file(filepath, va)) {
        std::cerr << "Failed to probe image file: " << filepath << std::endl;
        m_import_progress.active.store(false);
        return false;
    }

    va.is_image = true;
    va.duration = default_duration;

    m_import_progress.set(0.6f, "Creating image thumbnail preview...");
    int next_num = 1;
    for (const auto& existing : project.video_assets) {
        if (existing.id.rfind("v", 0) == 0) {
            try {
                int n = std::stoi(existing.id.substr(1));
                if (n >= next_num) next_num = n + 1;
            } catch (...) {}
        }
    }
    va.id = "v" + std::to_string(next_num);
    va.thumbnail_texture = MediaDecoder::create_thumbnail_texture(filepath, 0.0);

    project.video_assets.push_back(va);
    m_import_progress.set(1.0f, "Import complete!");
    m_import_progress.active.store(false);
    return true;
}

bool ClipManager::import_audio(Project& project, const std::string& filepath) {
    if (!fs::exists(filepath)) return false;

    m_import_progress.filename = fs::path(filepath).filename().string();
    m_import_progress.active.store(true);
    m_import_progress.set(0.2f, "Probing audio stream format...");

    AudioAsset aa;
    if (!MediaDecoder::probe_audio_file(filepath, aa)) {
        std::cerr << "Failed to probe audio file: " << filepath << std::endl;
        m_import_progress.active.store(false);
        return false;
    }

    int next_num = 1;
    for (const auto& existing : project.audio_assets) {
        if (existing.id.rfind("a", 0) == 0) {
            try {
                int n = std::stoi(existing.id.substr(1));
                if (n >= next_num) next_num = n + 1;
            } catch (...) {}
        }
    }
    aa.id = "a" + std::to_string(next_num);
    project.audio_assets.push_back(aa);

    if (m_audio_engine) {
        m_import_progress.set(0.65f, "Decoding and caching PCM audio waveforms...");
        m_audio_engine->preload_asset_audio(filepath);
    }
    m_import_progress.set(1.0f, "Import complete!");
    m_import_progress.active.store(false);
    return true;
}

void ClipManager::start_async_import_video(const std::string& filepath, std::function<void()> on_complete) {
    if (!fs::exists(filepath)) return;
    m_import_progress.filename = fs::path(filepath).filename().string();
    m_import_progress.active.store(true);
    m_import_progress.set(0.1f, "Probing video format and streams...");

    std::thread([this, filepath, on_complete]() {
        AsyncImportResult res;
        res.type = AsyncImportResult::Type::Video;
        res.filepath = filepath;
        res.on_complete = on_complete;

        m_import_progress.set(0.25f, "Inspecting video tracks...");
        if (MediaDecoder::probe_video_file(filepath, res.video_asset)) {
            m_import_progress.set(0.55f, "Decoding thumbnail frame...");
            MediaDecoder::decode_thumbnail_rgba(filepath, std::min(1.0, res.video_asset.duration * 0.2),
                                                res.thumb_w, res.thumb_h, res.thumb_rgba);

            if (res.video_asset.has_audio && m_audio_engine) {
                m_import_progress.set(0.75f, "Preloading and caching audio track...");
                m_audio_engine->preload_asset_audio(filepath);
            }
            m_import_progress.set(0.95f, "Finalizing import...");
            res.success = true;
        } else {
            res.success = false;
        }

        {
            std::lock_guard<std::mutex> lock(m_async_mutex);
            m_completed_async_results.push_back(std::move(res));
        }
    }).detach();
}

void ClipManager::start_async_import_image(const std::string& filepath, double default_duration, std::function<void()> on_complete) {
    if (!fs::exists(filepath)) return;
    m_import_progress.filename = fs::path(filepath).filename().string();
    m_import_progress.active.store(true);
    m_import_progress.set(0.15f, "Probing image metadata...");

    std::thread([this, filepath, default_duration, on_complete]() {
        AsyncImportResult res;
        res.type = AsyncImportResult::Type::Image;
        res.filepath = filepath;
        res.on_complete = on_complete;

        if (MediaDecoder::probe_video_file(filepath, res.video_asset)) {
            res.video_asset.is_image = true;
            res.video_asset.duration = default_duration;
            m_import_progress.set(0.6f, "Decoding image thumbnail...");
            MediaDecoder::decode_thumbnail_rgba(filepath, 0.0, res.thumb_w, res.thumb_h, res.thumb_rgba);
            m_import_progress.set(0.95f, "Finalizing import...");
            res.success = true;
        } else {
            res.success = false;
        }

        {
            std::lock_guard<std::mutex> lock(m_async_mutex);
            m_completed_async_results.push_back(std::move(res));
        }
    }).detach();
}

void ClipManager::start_async_import_audio(const std::string& filepath, std::function<void()> on_complete) {
    if (!fs::exists(filepath)) return;
    m_import_progress.filename = fs::path(filepath).filename().string();
    m_import_progress.active.store(true);
    m_import_progress.set(0.15f, "Probing audio stream...");

    std::thread([this, filepath, on_complete]() {
        AsyncImportResult res;
        res.type = AsyncImportResult::Type::Audio;
        res.filepath = filepath;
        res.on_complete = on_complete;

        if (MediaDecoder::probe_audio_file(filepath, res.audio_asset)) {
            if (m_audio_engine) {
                m_import_progress.set(0.65f, "Decoding and caching PCM audio waveforms...");
                m_audio_engine->preload_asset_audio(filepath);
            }
            m_import_progress.set(0.95f, "Finalizing import...");
            res.success = true;
        } else {
            res.success = false;
        }

        {
            std::lock_guard<std::mutex> lock(m_async_mutex);
            m_completed_async_results.push_back(std::move(res));
        }
    }).detach();
}

void ClipManager::start_async_import_prefab(const std::string& filepath, std::function<void()> on_complete) {
    if (!fs::exists(filepath)) return;
    m_import_progress.filename = fs::path(filepath).filename().string();
    m_import_progress.active.store(true);
    m_import_progress.set(0.15f, "Reading prefab project JSON...");

    std::thread([this, filepath, on_complete]() {
        AsyncImportResult res;
        res.type = AsyncImportResult::Type::Prefab;
        res.filepath = filepath;
        res.on_complete = on_complete;

        std::ifstream ifs(filepath);
        if (!ifs.is_open()) {
            res.success = false;
        } else {
            std::string json_str((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
            Project sub_p;
            if (ProjectSerializer::deserialize_json(sub_p, json_str)) {
                res.prefab_block.name = sub_p.name.empty() ? fs::path(filepath).stem().string() : sub_p.name;
                res.prefab_block.path = filepath;
                res.prefab_block.duration = sub_p.get_total_duration();
                res.prefab_block.raw_json = json_str;

                m_import_progress.set(0.5f, "Decoding prefab thumbnail preview...");
                if (!sub_p.video_assets.empty()) {
                    MediaDecoder::decode_thumbnail_rgba(sub_p.video_assets[0].path, 0.5,
                                                        res.thumb_w, res.thumb_h, res.thumb_rgba);
                }

                if (m_audio_engine) {
                    m_import_progress.set(0.75f, "Preloading prefab audio assets...");
                    for (const auto& aa : sub_p.audio_assets) {
                        m_audio_engine->preload_asset_audio(aa.path);
                    }
                    for (const auto& va : sub_p.video_assets) {
                        if (va.has_audio && !va.is_image) {
                            m_audio_engine->preload_asset_audio(va.path);
                        }
                    }
                }
                m_import_progress.set(0.95f, "Finalizing import...");
                res.success = true;
            } else {
                res.success = false;
            }
        }

        {
            std::lock_guard<std::mutex> lock(m_async_mutex);
            m_completed_async_results.push_back(std::move(res));
        }
    }).detach();
}

void ClipManager::poll_async_imports(Project& project) {
    std::vector<AsyncImportResult> results;
    {
        std::lock_guard<std::mutex> lock(m_async_mutex);
        if (m_completed_async_results.empty()) return;
        results.swap(m_completed_async_results);
    }

    for (auto& res : results) {
        if (!res.success) {
            continue;
        }

        if (res.type == AsyncImportResult::Type::Video) {
            int next_num = 1;
            for (const auto& existing : project.video_assets) {
                if (existing.id.rfind("v", 0) == 0) {
                    try {
                        int n = std::stoi(existing.id.substr(1));
                        if (n >= next_num) next_num = n + 1;
                    } catch (...) {}
                }
            }
            res.video_asset.id = "v" + std::to_string(next_num);
            if (!res.thumb_rgba.empty()) {
                res.video_asset.thumbnail_texture = MediaDecoder::upload_rgba_texture(res.thumb_w, res.thumb_h, res.thumb_rgba);
            }
            project.video_assets.push_back(res.video_asset);
            select_asset(res.video_asset.id, true, false);
        } else if (res.type == AsyncImportResult::Type::Image) {
            int next_num = 1;
            for (const auto& existing : project.video_assets) {
                if (existing.id.rfind("v", 0) == 0) {
                    try {
                        int n = std::stoi(existing.id.substr(1));
                        if (n >= next_num) next_num = n + 1;
                    } catch (...) {}
                }
            }
            res.video_asset.id = "v" + std::to_string(next_num);
            if (!res.thumb_rgba.empty()) {
                res.video_asset.thumbnail_texture = MediaDecoder::upload_rgba_texture(res.thumb_w, res.thumb_h, res.thumb_rgba);
            }
            project.video_assets.push_back(res.video_asset);
            select_asset(res.video_asset.id, true, false);
        } else if (res.type == AsyncImportResult::Type::Audio) {
            int next_num = 1;
            for (const auto& existing : project.audio_assets) {
                if (existing.id.rfind("a", 0) == 0) {
                    try {
                        int n = std::stoi(existing.id.substr(1));
                        if (n >= next_num) next_num = n + 1;
                    } catch (...) {}
                }
            }
            res.audio_asset.id = "a" + std::to_string(next_num);
            project.audio_assets.push_back(res.audio_asset);
            select_asset(res.audio_asset.id, false, false);
        } else if (res.type == AsyncImportResult::Type::Prefab) {
            int next_num = 1;
            for (const auto& existing : project.prefabs) {
                if (existing.id.rfind("pf", 0) == 0) {
                    try {
                        int n = std::stoi(existing.id.substr(2));
                        if (n >= next_num) next_num = n + 1;
                    } catch (...) {}
                }
            }
            res.prefab_block.id = "pf" + std::to_string(next_num);
            if (!res.thumb_rgba.empty()) {
                res.prefab_block.thumbnail_texture = MediaDecoder::upload_rgba_texture(res.thumb_w, res.thumb_h, res.thumb_rgba);
            }
            project.prefabs.push_back(res.prefab_block);
            select_asset(res.prefab_block.id, false, true);
        }

        if (res.on_complete) {
            res.on_complete();
        }
    }

    m_import_progress.set(1.0f, "Import complete!");
    m_import_progress.active.store(false);
}

bool ClipManager::import_project_as_prefab(Project& project, const std::string& filepath) {
    if (!fs::exists(filepath)) return false;

    std::ifstream ifs(filepath);
    if (!ifs.is_open()) return false;
    std::string json_str((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

    Project sub_p;
    if (!ProjectSerializer::deserialize_json(sub_p, json_str)) {
        std::cerr << "Failed to deserialize project file for prefab: " << filepath << std::endl;
        return false;
    }

    int next_num = 1;
    for (const auto& existing : project.prefabs) {
        if (existing.id.rfind("pf", 0) == 0) {
            try {
                int n = std::stoi(existing.id.substr(2));
                if (n >= next_num) next_num = n + 1;
            } catch (...) {}
        }
    }

    ProjectPrefab pf;
    pf.id = "pf" + std::to_string(next_num);
    pf.path = filepath;
    pf.name = (sub_p.name.empty() || sub_p.name == "Untitled Project")
        ? fs::path(filepath).stem().string()
        : sub_p.name;
    pf.duration = sub_p.get_total_duration();
    pf.raw_json = json_str;

    for (const auto& t : sub_p.tracks) {
        if (t.type == "video") pf.video_tracks++;
        else pf.audio_tracks++;
        pf.total_clips += static_cast<int>(t.clips.size());
    }

    // Attempt thumbnail generation from the first video/image in the subproject
    for (const auto& va : sub_p.video_assets) {
        if (fs::exists(va.path)) {
            pf.thumbnail_texture = MediaDecoder::create_thumbnail_texture(va.path, 0.5);
            if (pf.thumbnail_texture) break;
        }
    }

    // Preload any audio assets from the prefab
    if (m_audio_engine) {
        for (const auto& aa : sub_p.audio_assets) {
            if (fs::exists(aa.path)) {
                m_audio_engine->preload_asset_audio(aa.path);
            }
        }
        for (const auto& va : sub_p.video_assets) {
            if (va.has_audio && fs::exists(va.path)) {
                m_audio_engine->preload_asset_audio(va.path);
            }
        }
    }

    project.prefabs.push_back(pf);
    return true;
}

void ClipManager::instantiate_prefab_on_timeline(Project& project, const std::string& prefab_id, double insert_time) {
    const ProjectPrefab* pf = project.find_prefab(prefab_id);
    if (!pf) return;

    Project sub_p;
    if (!ProjectSerializer::deserialize_json(sub_p, pf->raw_json)) {
        return;
    }

    // 1. Remap assets into the host project
    std::map<std::string, std::string> asset_id_map;

    int next_v_num = 1;
    for (const auto& existing : project.video_assets) {
        if (existing.id.rfind("v", 0) == 0) {
            try {
                int n = std::stoi(existing.id.substr(1));
                if (n >= next_v_num) next_v_num = n + 1;
            } catch (...) {}
        }
    }

    for (const auto& sub_va : sub_p.video_assets) {
        const VideoAsset* existing_host_va = nullptr;
        for (const auto& host_va : project.video_assets) {
            if (host_va.path == sub_va.path) {
                existing_host_va = &host_va;
                break;
            }
        }

        if (existing_host_va) {
            asset_id_map[sub_va.id] = existing_host_va->id;
        } else {
            VideoAsset new_va = sub_va;
            new_va.id = "v" + std::to_string(next_v_num++);
            if (fs::exists(new_va.path)) {
                new_va.thumbnail_texture = MediaDecoder::create_thumbnail_texture(new_va.path, 0.5);
                if (new_va.has_audio && m_audio_engine) {
                    m_audio_engine->preload_asset_audio(new_va.path);
                }
            }
            project.video_assets.push_back(new_va);
            asset_id_map[sub_va.id] = new_va.id;
        }
    }

    int next_a_num = 1;
    for (const auto& existing : project.audio_assets) {
        if (existing.id.rfind("a", 0) == 0) {
            try {
                int n = std::stoi(existing.id.substr(1));
                if (n >= next_a_num) next_a_num = n + 1;
            } catch (...) {}
        }
    }

    for (const auto& sub_aa : sub_p.audio_assets) {
        const AudioAsset* existing_host_aa = nullptr;
        for (const auto& host_aa : project.audio_assets) {
            if (host_aa.path == sub_aa.path) {
                existing_host_aa = &host_aa;
                break;
            }
        }

        if (existing_host_aa) {
            asset_id_map[sub_aa.id] = existing_host_aa->id;
        } else {
            AudioAsset new_aa = sub_aa;
            new_aa.id = "a" + std::to_string(next_a_num++);
            if (fs::exists(new_aa.path) && m_audio_engine) {
                m_audio_engine->preload_asset_audio(new_aa.path);
            }
            project.audio_assets.push_back(new_aa);
            asset_id_map[sub_aa.id] = new_aa.id;
        }
    }

    // 2. Align and insert tracks/clips
    std::vector<Track*> host_video_tracks;
    std::vector<Track*> host_audio_tracks;
    for (auto& t : project.tracks) {
        if (t.type == "video") host_video_tracks.push_back(&t);
        else host_audio_tracks.push_back(&t);
    }

    std::vector<const Track*> sub_video_tracks;
    std::vector<const Track*> sub_audio_tracks;
    for (const auto& t : sub_p.tracks) {
        if (t.type == "video") sub_video_tracks.push_back(&t);
        else sub_audio_tracks.push_back(&t);
    }

    int track_counter = static_cast<int>(project.tracks.size()) + 1;
    while (host_video_tracks.size() < sub_video_tracks.size()) {
        Track new_vt;
        new_vt.type = "video";
        new_vt.id = "track_v" + std::to_string(track_counter++);
        new_vt.name = "Video " + std::to_string(host_video_tracks.size() + 1);

        size_t insert_pos = 0;
        for (size_t i = 0; i < project.tracks.size(); ++i) {
            if (project.tracks[i].type == "video") insert_pos = i + 1;
        }
        project.tracks.insert(project.tracks.begin() + insert_pos, new_vt);

        host_video_tracks.clear();
        host_audio_tracks.clear();
        for (auto& t : project.tracks) {
            if (t.type == "video") host_video_tracks.push_back(&t);
            else host_audio_tracks.push_back(&t);
        }
    }

    while (host_audio_tracks.size() < sub_audio_tracks.size()) {
        Track new_at;
        new_at.type = "audio";
        new_at.id = "track_a" + std::to_string(track_counter++);
        new_at.name = "Audio " + std::to_string(host_audio_tracks.size() + 1);
        project.tracks.push_back(new_at);
        host_audio_tracks.push_back(&project.tracks.back());
    }

    int host_clip_num = 1;
    for (const auto& t : project.tracks) {
        for (const auto& c : t.clips) {
            try {
                size_t pos = c.id.find_last_of('_');
                if (pos != std::string::npos) {
                    int n = std::stoi(c.id.substr(pos + 1));
                    if (n >= host_clip_num) host_clip_num = n + 1;
                }
            } catch (...) {}
        }
    }

    for (size_t i = 0; i < sub_video_tracks.size(); ++i) {
        Track* host_vt = host_video_tracks[i];
        for (const auto& sub_clip : sub_video_tracks[i]->clips) {
            Clip new_clip = sub_clip;
            new_clip.id = "clip_pf_" + pf->id + "_" + std::to_string(host_clip_num++);
            if (asset_id_map.find(sub_clip.asset_id) != asset_id_map.end()) {
                new_clip.asset_id = asset_id_map[sub_clip.asset_id];
            }
            new_clip.start_time = insert_time + sub_clip.start_time;
            new_clip.prefab_id = pf->id;
            new_clip.prefab_name = pf->name;
            host_vt->clips.push_back(new_clip);

            for (const auto& kf : sub_p.keyframes) {
                if (kf.clip_id == sub_clip.id) {
                    Keyframe new_kf = kf;
                    new_kf.clip_id = new_clip.id;
                    project.keyframes.push_back(new_kf);
                }
            }
        }
    }

    for (size_t i = 0; i < sub_audio_tracks.size(); ++i) {
        Track* host_at = host_audio_tracks[i];
        for (const auto& sub_clip : sub_audio_tracks[i]->clips) {
            Clip new_clip = sub_clip;
            new_clip.id = "clip_pf_" + pf->id + "_" + std::to_string(host_clip_num++);
            if (asset_id_map.find(sub_clip.asset_id) != asset_id_map.end()) {
                new_clip.asset_id = asset_id_map[sub_clip.asset_id];
            }
            new_clip.start_time = insert_time + sub_clip.start_time;
            new_clip.prefab_id = pf->id;
            new_clip.prefab_name = pf->name;
            host_at->clips.push_back(new_clip);

            for (const auto& kf : sub_p.keyframes) {
                if (kf.clip_id == sub_clip.id) {
                    Keyframe new_kf = kf;
                    new_kf.clip_id = new_clip.id;
                    project.keyframes.push_back(new_kf);
                }
            }
        }
    }
}

void ClipManager::remove_prefab(Project& project, const std::string& prefab_id, bool remove_from_timeline) {
    if (remove_from_timeline) {
        std::vector<std::string> removed_clip_ids;
        for (auto& track : project.tracks) {
            auto it = std::remove_if(track.clips.begin(), track.clips.end(),
                [&](const Clip& c) {
                    if (c.prefab_id == prefab_id) {
                        removed_clip_ids.push_back(c.id);
                        return true;
                    }
                    return false;
                });
            track.clips.erase(it, track.clips.end());
        }
        for (const auto& cid : removed_clip_ids) {
            auto kit = std::remove_if(project.keyframes.begin(), project.keyframes.end(),
                [&](const Keyframe& k) { return k.clip_id == cid; });
            project.keyframes.erase(kit, project.keyframes.end());
        }
    }

    auto it = std::remove_if(project.prefabs.begin(), project.prefabs.end(),
        [&](const ProjectPrefab& pf) {
            if (pf.id == prefab_id) {
                if (pf.thumbnail_texture) glDeleteTextures(1, &pf.thumbnail_texture);
                return true;
            }
            return false;
        });
    project.prefabs.erase(it, project.prefabs.end());

    if (m_selected_asset_id == prefab_id) {
        m_selected_asset_id.clear();
        m_selected_is_prefab = false;
    }
}

int ClipManager::get_asset_usage_count(const Project& project, const std::string& asset_id) {
    int count = 0;
    for (const auto& track : project.tracks) {
        for (const auto& clip : track.clips) {
            if (clip.asset_id == asset_id) {
                count++;
            }
        }
    }
    return count;
}

void ClipManager::remove_asset(Project& project, const std::string& asset_id, bool is_video, bool remove_from_timeline) {
    if (remove_from_timeline) {
        std::vector<std::string> removed_clip_ids;
        for (auto& track : project.tracks) {
            auto it = std::remove_if(track.clips.begin(), track.clips.end(),
                [&](const Clip& c) {
                    if (c.asset_id == asset_id) {
                        removed_clip_ids.push_back(c.id);
                        return true;
                    }
                    return false;
                });
            track.clips.erase(it, track.clips.end());
        }
        for (const auto& cid : removed_clip_ids) {
            auto kit = std::remove_if(project.keyframes.begin(), project.keyframes.end(),
                [&](const Keyframe& k) { return k.clip_id == cid; });
            project.keyframes.erase(kit, project.keyframes.end());
        }
    }

    if (is_video) {
        auto it = std::remove_if(project.video_assets.begin(), project.video_assets.end(),
            [&](const VideoAsset& v) {
                if (v.id == asset_id) {
                    if (v.thumbnail_texture) glDeleteTextures(1, &v.thumbnail_texture);
                    return true;
                }
                return false;
            });
        project.video_assets.erase(it, project.video_assets.end());
    } else {
        auto it = std::remove_if(project.audio_assets.begin(), project.audio_assets.end(),
            [&](const AudioAsset& a) { return a.id == asset_id; });
        project.audio_assets.erase(it, project.audio_assets.end());
    }

    if (m_selected_asset_id == asset_id) {
        m_selected_asset_id.clear();
    }
}

int ClipManager::delete_unused_assets(Project& project) {
    int removed_count = 0;
    std::vector<std::string> unused_videos;
    for (const auto& v : project.video_assets) {
        if (get_asset_usage_count(project, v.id) == 0) {
            unused_videos.push_back(v.id);
        }
    }
    for (const auto& vid : unused_videos) {
        remove_asset(project, vid, true, false);
        removed_count++;
    }

    std::vector<std::string> unused_audios;
    for (const auto& a : project.audio_assets) {
        if (get_asset_usage_count(project, a.id) == 0) {
            unused_audios.push_back(a.id);
        }
    }
    for (const auto& aid : unused_audios) {
        remove_asset(project, aid, false, false);
        removed_count++;
    }
    return removed_count;
}

void ClipManager::render_ui(Project& project, double playhead_time,
                            const std::function<void(const std::string& asset_id, bool is_video)>& on_add_to_timeline,
                            const std::function<void(const std::string& prefab_id, double insert_time)>& on_add_prefab_to_timeline,
                            const std::function<void()>& on_project_changed,
                            const std::function<void()>& on_import_video,
                            const std::function<void()>& on_import_image,
                            const std::function<void()>& on_import_audio,
                            const std::function<void()>& on_import_prefab,
                            const std::function<void()>& on_add_text) {
    poll_async_imports(project);

    // Keyboard shortcut deletion and enter-to-add
    bool is_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    if (is_focused && !m_selected_asset_id.empty()) {
        if (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace)) {
            if (m_selected_is_prefab) {
                remove_prefab(project, m_selected_asset_id, true);
            } else {
                remove_asset(project, m_selected_asset_id, m_selected_is_video, true);
            }
            if (on_project_changed) on_project_changed();
        } else if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) {
            if (m_selected_is_prefab) {
                if (on_add_prefab_to_timeline) on_add_prefab_to_timeline(m_selected_asset_id, playhead_time);
            } else {
                if (on_add_to_timeline) on_add_to_timeline(m_selected_asset_id, m_selected_is_video);
            }
        }
    }

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5, 3));

    // Top import action buttons
    if (ImGui::Button(_TR("project_import_video"))) {
        if (on_import_video) on_import_video();
    }
    ImGui::SameLine();
    if (ImGui::Button(_TR("project_import_image"))) {
        if (on_import_image) on_import_image();
    }
    ImGui::SameLine();
    if (ImGui::Button(_TR("project_import_audio"))) {
        if (on_import_audio) on_import_audio();
    }
    ImGui::SameLine();
    if (ImGui::Button(_TR("project_import_prefab"))) {
        if (on_import_prefab) on_import_prefab();
    }
    ImGui::SameLine();
    if (ImGui::Button(_TR("text_add_asset"))) {
        if (on_add_text) on_add_text();
    }

    // Active Import Progress Banner
    if (m_import_progress.active.load()) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.18f, 0.22f, 0.28f, 1.0f));
        ImGui::BeginChild("##import_progress_panel", ImVec2(0, 52), true);
        float p = m_import_progress.progress.load();
        std::string pct = std::to_string(static_cast<int>(p * 100.0f)) + "%";
        ImGui::Text("Importing: %s", m_import_progress.filename.c_str());
        ImGui::ProgressBar(p, ImVec2(-1, 14), pct.c_str());
        ImGui::TextDisabled("%s", get_import_status().c_str());
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::Spacing();
    }

    // Filter bar
    ImGui::TextDisabled("Filter:");
    ImGui::SameLine();
    ImGui::RadioButton("All", &m_filter_type, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Video", &m_filter_type, 1);
    ImGui::SameLine();
    ImGui::RadioButton("Image", &m_filter_type, 2);
    ImGui::SameLine();
    ImGui::RadioButton("Audio", &m_filter_type, 3);
    ImGui::SameLine();
    ImGui::RadioButton("Prefabs", &m_filter_type, 4);

    ImGui::InputTextWithHint("##search", "Search assets...", m_search_filter, sizeof(m_search_filter));

    ImGui::PopStyleVar();
    ImGui::Separator();

    // Table of assets
    static ImGuiTableFlags table_flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg |
                                         ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_Resizable;

    float table_height = ImGui::GetContentRegionAvail().y - 42.0f;
    if (table_height < 60.0f) table_height = 60.0f;

    bool item_was_right_clicked = false;

    if (ImGui::BeginTable("AssetTable", 5, table_flags, ImVec2(0, table_height))) {
        ImGui::TableSetupColumn("Thumb", ImGuiTableColumnFlags_WidthFixed, 48.0f);
        ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 36.0f);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Duration", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("Specs", ImGuiTableColumnFlags_WidthFixed, 95.0f);
        ImGui::TableHeadersRow();

        std::string search_str = m_search_filter;
        std::transform(search_str.begin(), search_str.end(), search_str.begin(), ::tolower);

        // 1. Videos & Images
        if (m_filter_type == 0 || m_filter_type == 1 || m_filter_type == 2) {
            for (const auto& v : project.video_assets) {
                if (m_filter_type == 1 && v.is_image) continue;
                if (m_filter_type == 2 && !v.is_image) continue;

                std::string filename = fs::path(v.path).filename().string();
                std::string filename_lower = filename;
                std::transform(filename_lower.begin(), filename_lower.end(), filename_lower.begin(), ::tolower);
                if (!search_str.empty() && filename_lower.find(search_str) == std::string::npos) {
                    continue;
                }

                const float row_height = 36.0f;
                ImGui::TableNextRow(ImGuiTableRowFlags_None, row_height);
                bool is_selected = (m_selected_asset_id == v.id && m_selected_is_video && !m_selected_is_prefab);

                ImGui::TableSetColumnIndex(0);
                std::string sel_id = "##sel_v_" + v.id;
                if (ImGui::Selectable(sel_id.c_str(), is_selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowItemOverlap, ImVec2(0, row_height))) {
                    m_selected_asset_id = v.id;
                    m_selected_is_video = true;
                    m_selected_is_prefab = false;
                }

                // Handle double-click and right-click
                if (ImGui::IsItemHovered()) {
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        m_selected_asset_id = v.id;
                        m_selected_is_video = true;
                        m_selected_is_prefab = false;
                        if (on_add_to_timeline) {
                            on_add_to_timeline(v.id, true);
                        }
                    }
                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                        m_selected_asset_id = v.id;
                        m_selected_is_video = true;
                        m_selected_is_prefab = false;
                        m_context_asset_id = v.id;
                        m_context_is_video = true;
                        m_context_is_prefab = false;
                        m_open_asset_context = true;
                        item_was_right_clicked = true;
                    }
                }

                // Drag & Drop Source
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                    ImGui::SetDragDropPayload("MINICUT_ASSET_VIDEO", v.id.c_str(), v.id.size() + 1);
                    ImGui::Text("Add video %s to timeline", filename.c_str());
                    ImGui::EndDragDropSource();
                }

                ImGui::SameLine(0.0f, 4.0f);
                if (v.thumbnail_texture) {
                    ImGui::Image((ImTextureID)(intptr_t)v.thumbnail_texture, ImVec2(40, 24));
                } else if (v.is_image) {
                    ImGui::TextColored(ImVec4(0.9f, 0.65f, 0.2f, 1.0f), "[IMG]");
                } else {
                    ImGui::TextColored(ImVec4(0.3f, 0.7f, 1.0f, 1.0f), "[VID]");
                }

                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%s", v.id.c_str());

                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(filename.c_str());

                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%s", format_timecode(v.duration, false).c_str());

                ImGui::TableSetColumnIndex(4);
                ImGui::Text("%dx%d", v.width, v.height);
            }
        }

        // 2. Audio
        if (m_filter_type == 0 || m_filter_type == 3) {
            for (const auto& a : project.audio_assets) {
                std::string filename = fs::path(a.path).filename().string();
                std::string filename_lower = filename;
                std::transform(filename_lower.begin(), filename_lower.end(), filename_lower.begin(), ::tolower);
                if (!search_str.empty() && filename_lower.find(search_str) == std::string::npos) {
                    continue;
                }

                const float row_height = 36.0f;
                ImGui::TableNextRow(ImGuiTableRowFlags_None, row_height);
                bool is_selected = (m_selected_asset_id == a.id && !m_selected_is_video && !m_selected_is_prefab);

                ImGui::TableSetColumnIndex(0);
                std::string sel_id = "##sel_a_" + a.id;
                if (ImGui::Selectable(sel_id.c_str(), is_selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowItemOverlap, ImVec2(0, row_height))) {
                    m_selected_asset_id = a.id;
                    m_selected_is_video = false;
                    m_selected_is_prefab = false;
                }

                // Handle double-click and right-click
                if (ImGui::IsItemHovered()) {
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        m_selected_asset_id = a.id;
                        m_selected_is_video = false;
                        m_selected_is_prefab = false;
                        if (on_add_to_timeline) {
                            on_add_to_timeline(a.id, false);
                        }
                    }
                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                        m_selected_asset_id = a.id;
                        m_selected_is_video = false;
                        m_selected_is_prefab = false;
                        m_context_asset_id = a.id;
                        m_context_is_video = false;
                        m_context_is_prefab = false;
                        m_open_asset_context = true;
                        item_was_right_clicked = true;
                    }
                }

                // Drag & Drop Source
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                    ImGui::SetDragDropPayload("MINICUT_ASSET_AUDIO", a.id.c_str(), a.id.size() + 1);
                    ImGui::Text("Add audio %s to timeline", filename.c_str());
                    ImGui::EndDragDropSource();
                }

                ImGui::SameLine(0.0f, 4.0f);
                ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.5f, 1.0f), "[AUD]");

                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%s", a.id.c_str());

                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(filename.c_str());

                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%s", format_timecode(a.duration, false).c_str());

                ImGui::TableSetColumnIndex(4);
                ImGui::Text("%dHz %dch", a.sample_rate, a.channels);
            }
        }

        // 3. Project Prefabs
        if (m_filter_type == 0 || m_filter_type == 4) {
            for (const auto& pf : project.prefabs) {
                std::string pf_lower = pf.name;
                std::transform(pf_lower.begin(), pf_lower.end(), pf_lower.begin(), ::tolower);
                if (!search_str.empty() && pf_lower.find(search_str) == std::string::npos) {
                    continue;
                }

                const float row_height = 36.0f;
                ImGui::TableNextRow(ImGuiTableRowFlags_None, row_height);
                bool is_selected = (m_selected_asset_id == pf.id && m_selected_is_prefab);

                ImGui::TableSetColumnIndex(0);
                std::string sel_id = "##sel_pf_" + pf.id;
                if (ImGui::Selectable(sel_id.c_str(), is_selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowItemOverlap, ImVec2(0, row_height))) {
                    m_selected_asset_id = pf.id;
                    m_selected_is_prefab = true;
                }

                // Double click & right-click
                if (ImGui::IsItemHovered()) {
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        m_selected_asset_id = pf.id;
                        m_selected_is_prefab = true;
                        if (on_add_prefab_to_timeline) {
                            on_add_prefab_to_timeline(pf.id, playhead_time);
                        }
                    }
                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                        m_selected_asset_id = pf.id;
                        m_selected_is_prefab = true;
                        m_context_asset_id = pf.id;
                        m_context_is_prefab = true;
                        m_open_asset_context = true;
                        item_was_right_clicked = true;
                    }
                }

                // Drag & Drop Source
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                    ImGui::SetDragDropPayload("MINICUT_ASSET_PREFAB", pf.id.c_str(), pf.id.size() + 1);
                    ImGui::Text("Insert prefab %s at playhead", pf.name.c_str());
                    ImGui::EndDragDropSource();
                }

                ImGui::SameLine(0.0f, 4.0f);
                if (pf.thumbnail_texture) {
                    ImGui::Image((ImTextureID)(intptr_t)pf.thumbnail_texture, ImVec2(40, 24));
                } else {
                    ImGui::TextColored(ImVec4(0.85f, 0.45f, 1.0f, 1.0f), "[PF]");
                }

                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%s", pf.id.c_str());

                ImGui::TableSetColumnIndex(2);
                std::string display_name = "[Prefab] " + pf.name;
                ImGui::TextUnformatted(display_name.c_str());

                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%s", format_timecode(pf.duration, false).c_str());

                ImGui::TableSetColumnIndex(4);
                ImGui::Text("%dV %dA (%dc)", pf.video_tracks, pf.audio_tracks, pf.total_clips);
            }
        }

        ImGui::EndTable();
    }

    // Right click on empty space of the window or table
    if (!item_was_right_clicked && !m_open_asset_context &&
        ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows) &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        ImGui::OpenPopup("ClipPoolBackgroundContext");
    }

    // Open context popup if triggered
    if (m_open_asset_context) {
        ImGui::OpenPopup("AssetItemContextMenu");
        m_open_asset_context = false;
    }

    // Context menu for individual asset or prefab
    if (ImGui::BeginPopup("AssetItemContextMenu")) {
        const std::string& cid = m_context_asset_id;

        if (m_context_is_prefab) {
            const auto* pf = project.find_prefab(cid);
            if (pf) {
                ImGui::TextColored(ImVec4(0.85f, 0.45f, 1.0f, 1.0f), "[%s] Project Prefab: %s", pf->id.c_str(), pf->name.c_str());
                ImGui::Text("Duration: %s | Tracks: %dV %dA | Total Clips: %d",
                            format_timecode(pf->duration, false).c_str(),
                            pf->video_tracks, pf->audio_tracks, pf->total_clips);
                ImGui::TextDisabled("Path: %s", pf->path.c_str());
                ImGui::Separator();

                if (ImGui::MenuItem("+ Add Prefab to Timeline", "Enter / Dbl-Click")) {
                    if (on_add_prefab_to_timeline) on_add_prefab_to_timeline(pf->id, playhead_time);
                }

                if (ImGui::MenuItem("Copy Project Path")) {
                    ImGui::SetClipboardText(pf->path.c_str());
                }

                ImGui::Separator();

                if (ImGui::MenuItem("- Delete Prefab from Pool", "Del")) {
                    remove_prefab(project, pf->id, false);
                    if (on_project_changed) on_project_changed();
                }

                if (ImGui::MenuItem("- Delete Prefab from Pool and Timeline")) {
                    remove_prefab(project, pf->id, true);
                    if (on_project_changed) on_project_changed();
                }
            }
        } else {
            bool is_vid = m_context_is_video;
            std::string file_path;
            std::string file_name;
            int usages = get_asset_usage_count(project, cid);

            if (is_vid) {
                const auto* va = project.find_video_asset(cid);
                if (va) {
                    file_path = va->path;
                    file_name = fs::path(va->path).filename().string();
                    ImGui::TextColored(ImVec4(0.3f, 0.75f, 1.0f, 1.0f), "[%s] %s", va->id.c_str(), va->is_image ? "Image" : "Video");
                    ImGui::Text("%dx%d | %s", va->width, va->height, format_timecode(va->duration, false).c_str());
                }
            } else {
                const auto* aa = project.find_audio_asset(cid);
                if (aa) {
                    file_path = aa->path;
                    file_name = fs::path(aa->path).filename().string();
                    ImGui::TextColored(ImVec4(0.2f, 0.85f, 0.5f, 1.0f), "[%s] Audio", aa->id.c_str());
                    ImGui::Text("%dHz %dch | %s", aa->sample_rate, aa->channels, format_timecode(aa->duration, false).c_str());
                }
            }
            if (!file_name.empty()) {
                ImGui::TextUnformatted(file_name.c_str());
            }
            if (usages > 0) {
                ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.2f, 1.0f), "Timeline instances: %d", usages);
            } else {
                ImGui::TextDisabled("Status: Unused on timeline");
            }
            ImGui::Separator();

            if (ImGui::MenuItem("+ Add to Timeline", "Enter / Dbl-Click")) {
                if (on_add_to_timeline) on_add_to_timeline(cid, is_vid);
            }

            if (ImGui::MenuItem("Copy File Path")) {
                if (!file_path.empty()) ImGui::SetClipboardText(file_path.c_str());
            }

            ImGui::Separator();

            if (ImGui::MenuItem("- Delete from Clip Pool", "Del")) {
                remove_asset(project, cid, is_vid, true);
                if (on_project_changed) on_project_changed();
            }

            if (usages > 0) {
                if (ImGui::MenuItem("- Delete from Pool Only (keep timeline clips)")) {
                    remove_asset(project, cid, is_vid, false);
                    if (on_project_changed) on_project_changed();
                }
            }

            ImGui::Separator();
            if (ImGui::MenuItem("Delete All Unused Assets")) {
                int removed = delete_unused_assets(project);
                if (removed > 0 && on_project_changed) on_project_changed();
            }
        }

        ImGui::EndPopup();
    }

    // Context menu for pool background
    if (ImGui::BeginPopup("ClipPoolBackgroundContext")) {
        ImGui::TextDisabled("Clip Pool Options");
        ImGui::Separator();
        if (ImGui::MenuItem("+ Import Video...")) {
            if (on_import_video) on_import_video();
        }
        if (ImGui::MenuItem("+ Import Image...")) {
            if (on_import_image) on_import_image();
        }
        if (ImGui::MenuItem("+ Import Audio...")) {
            if (on_import_audio) on_import_audio();
        }
        if (ImGui::MenuItem("+ Import Project as Prefab...")) {
            if (on_import_prefab) on_import_prefab();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Delete All Unused Assets")) {
            int removed = delete_unused_assets(project);
            if (removed > 0 && on_project_changed) on_project_changed();
        }
        if (ImGui::MenuItem("Clear Selection")) {
            m_selected_asset_id.clear();
            m_selected_is_prefab = false;
        }
        ImGui::EndPopup();
    }

    ImGui::Separator();

    // Bottom Action buttons
    if (ImGui::Button("+ Add to Timeline", ImVec2(120, 26))) {
        if (!m_selected_asset_id.empty()) {
            if (m_selected_is_prefab) {
                if (on_add_prefab_to_timeline) on_add_prefab_to_timeline(m_selected_asset_id, playhead_time);
            } else {
                if (on_add_to_timeline) on_add_to_timeline(m_selected_asset_id, m_selected_is_video);
            }
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Add selected asset or prefab to timeline at playhead position (or double-click)");
    }

    ImGui::SameLine();
    bool has_selection = !m_selected_asset_id.empty();
    if (!has_selection) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Delete Asset [Del]", ImVec2(120, 26))) {
        if (has_selection) {
            if (m_selected_is_prefab) {
                remove_prefab(project, m_selected_asset_id, true);
            } else {
                remove_asset(project, m_selected_asset_id, m_selected_is_video, true);
            }
            if (on_project_changed) on_project_changed();
        }
    }
    if (!has_selection) {
        ImGui::EndDisabled();
    }
    if (ImGui::IsItemHovered() && has_selection) {
        ImGui::SetTooltip("Remove selected asset or prefab from the clip pool and timeline");
    }

    ImGui::SameLine();
    if (ImGui::Button("Delete Unused", ImVec2(100, 26))) {
        int count = delete_unused_assets(project);
        if (count > 0 && on_project_changed) {
            on_project_changed();
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Remove all assets that are not placed on the timeline");
    }

    ImGui::SameLine();
    ImGui::TextDisabled("(Del to delete, Dbl-click to add)");
}
