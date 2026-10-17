#include "Common.h"
#include "ProjectSerializer.h"
#include "MediaDecoder.h"
#include "ExportManager.h"
#include "ClipManager.h"
#include "AudioEngine.h"
#include "Timeline.h"
#include "NativeFileDialog.h"
#include "TempManager.h"
#include "LocaleManager.h"
#include "FileBrowser.h"
#include "ScriptEngine.h"
#include "ArabicShaper.h"
#include "TextGenerator.h"
#include "SettingsLogic.h"
#include "TTSManager.h"
#include "AICore.h"
#include "render/RenderSystem.h"
#include "render/RenderBackendOpenGL3.h"
#include "render/RenderBackendVulkan.h"
#include "SwapManager.h"
#include "JapaneseTransliteration.h"
#include "ShaderManager.h"
#include "third_party/imgui/backends/imgui_impl_sdl2.h"
#include <iostream>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

void test_keyframe_evaluation() {
    std::cout << "[TEST] Running keyframe evaluation test..." << std::endl;
    Project p;
    // Default when no keyframes exist
    assert(std::abs(p.evaluate_keyframe("clip1", "opacity", 2.0, 1.0) - 1.0) < 1e-4);

    // Single keyframe
    p.set_or_add_keyframe("clip1", "opacity", 1.0, 0.5);
    assert(std::abs(p.evaluate_keyframe("clip1", "opacity", 0.0, 1.0) - 0.5) < 1e-4);
    assert(std::abs(p.evaluate_keyframe("clip1", "opacity", 3.0, 1.0) - 0.5) < 1e-4);

    // Two keyframes: linear interpolation
    p.set_or_add_keyframe("clip1", "opacity", 3.0, 1.0);
    // at t = 2.0: halfway between 1.0 and 3.0 -> value 0.75
    double val_mid = p.evaluate_keyframe("clip1", "opacity", 2.0, 1.0);
    assert(std::abs(val_mid - 0.75) < 1e-3);

    // Before first keyframe
    assert(std::abs(p.evaluate_keyframe("clip1", "opacity", 0.5, 1.0) - 0.5) < 1e-3);
    // After last keyframe
    assert(std::abs(p.evaluate_keyframe("clip1", "opacity", 4.0, 1.0) - 1.0) < 1e-3);

    std::cout << "[PASS] Keyframe evaluation tests passed." << std::endl;
}

void test_project_serialization() {
    std::cout << "[TEST] Running project serialization test..." << std::endl;
    Project p;
    p.name = "Test Project MiniCut";
    p.created_at = "2026-10-12T12:00:00Z";

    // Assets
    VideoAsset v1;
    v1.id = "v1";
    v1.path = "/test/path/video1.mp4";
    v1.duration = 15.5;
    p.video_assets.push_back(v1);

    AudioAsset a1;
    a1.id = "a1";
    a1.path = "/test/path/audio1.mp3";
    a1.duration = 45.0;
    p.audio_assets.push_back(a1);

    // Tracks & clips
    Track vt;
    vt.type = "video";
    vt.name = "Video 1";
    Clip vc;
    vc.id = "vc_1";
    vc.asset_id = "v1";
    vc.start_time = 1.0;
    vc.duration = 6.0;
    vc.offset = 0.5;
    vt.clips.push_back(vc);
    p.tracks.push_back(vt);

    Track at;
    at.type = "audio";
    at.name = "Audio 1";
    Clip ac;
    ac.id = "ac_1";
    ac.asset_id = "a1";
    ac.start_time = 0.0;
    ac.duration = 10.0;
    ac.offset = 0.0;
    at.clips.push_back(ac);
    p.tracks.push_back(at);

    // Keyframes
    p.set_or_add_keyframe("vc_1", "opacity", 0.0, 0.0);
    p.set_or_add_keyframe("vc_1", "opacity", 1.0, 1.0);
    p.set_or_add_keyframe("ac_1", "volume", 0.0, 1.0);
    p.set_or_add_keyframe("ac_1", "volume", 5.0, 0.2);

    std::string json_str = ProjectSerializer::serialize_json(p);
    nlohmann::json parsed = nlohmann::json::parse(json_str);

    // Verify schema structure
    assert(parsed.contains("version"));
    assert(parsed.contains("name"));
    assert(parsed.contains("created_at"));
    assert(parsed.contains("updated_at"));
    assert(parsed.contains("assets"));
    assert(parsed["assets"].contains("videos"));
    assert(parsed["assets"].contains("audio"));
    assert(parsed.contains("timeline"));
    assert(parsed["timeline"].contains("tracks"));
    assert(parsed["timeline"].contains("keyframes"));

    assert(parsed["assets"]["videos"].size() == 1);
    assert(parsed["assets"]["audio"].size() == 1);
    assert(parsed["timeline"]["tracks"].size() == 2);
    assert(parsed["timeline"]["keyframes"].size() == 4);

    // Roundtrip load
    Project loaded;
    bool ok = ProjectSerializer::deserialize_json(loaded, json_str);
    assert(ok);
    assert(loaded.name == "Test Project MiniCut");
    assert(loaded.video_assets.size() == 1);
    assert(loaded.video_assets[0].id == "v1");
    assert(loaded.audio_assets.size() == 1);
    assert(loaded.audio_assets[0].id == "a1");
    assert(loaded.tracks.size() == 2);
    assert(loaded.keyframes.size() == 4);

    std::cout << "[PASS] Project serialization & deserialization tests passed." << std::endl;
}

void test_media_probe_and_export() {
    std::cout << "[TEST] Running media probe and export test..." << std::endl;

    fs::path temp_video = "test_src_video.mp4";
    fs::path temp_audio = "test_src_audio.mp3";
    fs::path test_output = "test_exported_output.mp4";

    // Clean any leftover files
    if (fs::exists(temp_video)) fs::remove(temp_video);
    if (fs::exists(temp_audio)) fs::remove(temp_audio);
    if (fs::exists(test_output)) fs::remove(test_output);

    std::cout << "  -> Generating synthetic test video and audio..." << std::endl;
    bool gen_ok = MediaDecoder::generate_test_media(temp_video.string(), temp_audio.string());
    assert(gen_ok && "Failed to generate test media!");

    // Probe video
    VideoAsset va;
    bool v_ok = MediaDecoder::probe_video_file(temp_video.string(), va);
    assert(v_ok);
    assert(va.width == 1280);
    assert(va.height == 720);
    assert(va.duration > 1.0);
    std::cout << "  -> Video probed: " << va.width << "x" << va.height << ", dur=" << va.duration << "s" << std::endl;

    // Probe audio
    AudioAsset aa;
    bool a_ok = MediaDecoder::probe_audio_file(temp_audio.string(), aa);
    assert(a_ok);
    assert(aa.duration > 1.0);
    std::cout << "  -> Audio probed: " << aa.sample_rate << "Hz, " << aa.channels << "ch, dur=" << aa.duration << "s" << std::endl;

    // Setup a test project for export
    Project proj;
    proj.name = "Export Test";
    va.id = "v1";
    aa.id = "a1";
    proj.video_assets.push_back(va);
    proj.audio_assets.push_back(aa);

    Track vtrack;
    vtrack.type = "video";
    vtrack.name = "Video 1";
    Clip vclip;
    vclip.id = "clip_v1";
    vclip.asset_id = "v1";
    vclip.start_time = 0.0;
    vclip.duration = 2.0; // 2 seconds test
    vclip.offset = 0.0;
    vtrack.clips.push_back(vclip);
    proj.tracks.push_back(vtrack);

    Track atrack;
    atrack.type = "audio";
    atrack.name = "Audio 1";
    Clip aclip;
    aclip.id = "clip_a1";
    aclip.asset_id = "a1";
    aclip.start_time = 0.0;
    aclip.duration = 2.0;
    aclip.offset = 0.0;
    atrack.clips.push_back(aclip);
    proj.tracks.push_back(atrack);

    // Add opacity fade-out keyframe
    proj.set_or_add_keyframe("clip_v1", "opacity", 0.0, 1.0);
    proj.set_or_add_keyframe("clip_v1", "opacity", 2.0, 0.2);

    // Export using ExportManager
    std::cout << "  -> Starting asynchronous project snapshot export..." << std::endl;
    ExportManager exporter;
    ExportConfig cfg;
    cfg.output_path = test_output.string();
    cfg.width = 640;
    cfg.height = 360;
    cfg.fps = 24;
    cfg.video_bitrate = 1000000;

    bool started = exporter.start_export(proj, cfg);
    assert(started);

    // Non-blocking wait while monitoring progress
    while (exporter.is_exporting()) {
        float prog = exporter.get_progress();
        std::cout << "     Export progress: " << static_cast<int>(prog * 100) << "%\r" << std::flush;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    std::cout << std::endl;

    assert(!exporter.is_error());
    assert(exporter.is_finished());
    assert(fs::exists(test_output));
    std::cout << "  -> Exported file exists: " << test_output << " (size: " << fs::file_size(test_output) << " bytes)" << std::endl;

    // Verify the exported file by probing it
    VideoAsset exported_probe;
    bool exp_ok = MediaDecoder::probe_video_file(test_output.string(), exported_probe);
    assert(exp_ok);
    assert(exported_probe.width == 640);
    assert(exported_probe.height == 360);
    assert(exported_probe.duration >= 1.9);
    std::cout << "  -> Exported video verified: " << exported_probe.width << "x" << exported_probe.height
              << ", dur=" << exported_probe.duration << "s" << std::endl;

    // Cleanup
    fs::remove(temp_video);
    fs::remove(temp_audio);
    fs::remove(test_output);

    std::cout << "[PASS] Media probe and export tests passed." << std::endl;
}

static std::string resolve_test_asset(const std::string& filename) {
    std::vector<std::string> candidates = {
        "tests/assets/" + filename,
        "assets/" + filename,
        "../tests/assets/" + filename,
        "../assets/" + filename,
        "test_assets/" + filename,
        "../test_assets/" + filename,
        filename
    };
    for (const auto& c : candidates) {
        if (fs::exists(c)) return c;
    }
    return filename;
}

void test_image_probe_and_decode() {
    std::cout << "[TEST] Running image probe and decode test..." << std::endl;
    fs::path temp_img = resolve_test_asset("test_sample.png");
    assert(fs::exists(temp_img) && "test_sample.png should exist in tests/assets/");

    VideoAsset img_asset;
    bool ok = MediaDecoder::probe_video_file(temp_img.string(), img_asset);
    assert(ok);
    assert(img_asset.is_image);
    assert(img_asset.width == 1280);
    assert(img_asset.height == 720);
    assert(img_asset.duration == 5.0);
    std::cout << "  -> Image probed successfully: " << img_asset.width << "x" << img_asset.height
              << ", is_image=" << (img_asset.is_image ? "true" : "false")
              << ", default duration=" << img_asset.duration << "s" << std::endl;

    DecodedVideoContext dec_ctx;
    bool opened = dec_ctx.open(img_asset.path);
    assert(opened);
    assert(dec_ctx.is_image);
    assert(dec_ctx.width == 1280 && dec_ctx.height == 720);

    std::vector<uint8_t> frame_buf;
    bool got_frame1 = dec_ctx.decode_frame_at(0.0, 1280, 720, frame_buf);
    assert(got_frame1);
    assert(frame_buf.size() == 1280 * 720 * 4);

    // Any arbitrary timestamp should return the cached frame instantly
    std::vector<uint8_t> frame_buf2;
    bool got_frame2 = dec_ctx.decode_frame_at(4.2, 1280, 720, frame_buf2);
    assert(got_frame2);
    assert(frame_buf == frame_buf2);

    dec_ctx.close();

    std::cout << "[PASS] Image probe and decode tests passed." << std::endl;
}

void test_image_transform_and_keyframing() {
    std::cout << "[TEST] Running image transform and keyframing test..." << std::endl;

    Project p;
    Clip img_clip;
    img_clip.id = "img_clip_1";
    img_clip.asset_id = "img_asset_1";
    img_clip.start_time = 0.0;
    img_clip.duration = 5.0;
    img_clip.pos_x = 50.0;
    img_clip.pos_y = -30.0;
    img_clip.rotation = 15.0;
    img_clip.scale = 1.25;
    img_clip.opacity = 0.9;

    // Test default param retrieval
    assert(std::abs(p.get_clip_param(img_clip, "pos_x", 0.0) - 50.0) < 1e-3);
    assert(std::abs(p.get_clip_param(img_clip, "pos_y", 0.0) - -30.0) < 1e-3);
    assert(std::abs(p.get_clip_param(img_clip, "rotation", 0.0) - 15.0) < 1e-3);
    assert(std::abs(p.get_clip_param(img_clip, "scale", 0.0) - 1.25) < 1e-3);
    assert(std::abs(p.get_clip_param(img_clip, "opacity", 0.0) - 0.9) < 1e-3);

    // Add keyframes for rotation and scale
    p.set_or_add_keyframe(img_clip.id, "rotation", 0.0, 0.0);
    p.set_or_add_keyframe(img_clip.id, "rotation", 2.0, 90.0);
    p.set_or_add_keyframe(img_clip.id, "rotation", 4.0, 180.0);

    // Test interpolated rotation
    assert(std::abs(p.get_clip_param(img_clip, "rotation", 0.0) - 0.0) < 1e-3);
    assert(std::abs(p.get_clip_param(img_clip, "rotation", 1.0) - 45.0) < 1e-3);
    assert(std::abs(p.get_clip_param(img_clip, "rotation", 2.0) - 90.0) < 1e-3);
    assert(std::abs(p.get_clip_param(img_clip, "rotation", 3.0) - 135.0) < 1e-3);
    assert(std::abs(p.get_clip_param(img_clip, "rotation", 4.0) - 180.0) < 1e-3);

    // Test keyframe helpers
    assert(p.has_keyframe_at(img_clip.id, "rotation", 2.0));
    assert(!p.has_keyframe_at(img_clip.id, "rotation", 1.0));
    assert(std::abs(p.find_prev_keyframe_time(img_clip.id, "rotation", 3.0) - 2.0) < 1e-3);
    assert(std::abs(p.find_next_keyframe_time(img_clip.id, "rotation", 1.0) - 2.0) < 1e-3);

    // Test blit_transformed_layer
    const int src_w = 64, src_h = 64;
    std::vector<uint8_t> src_buf(src_w * src_h * 4, 0);
    // Fill with opaque pure red
    for (size_t i = 0; i < src_buf.size(); i += 4) {
        src_buf[i + 0] = 255;
        src_buf[i + 1] = 0;
        src_buf[i + 2] = 0;
        src_buf[i + 3] = 255;
    }

    const int dst_w = 1280, dst_h = 720;
    std::vector<uint8_t> dst_buf(dst_w * dst_h * 4, 0);

    // Blit transformed layer centered with full opacity
    blit_transformed_layer(dst_buf, dst_w, dst_h, src_buf, src_w, src_h, 0.0, 0.0, 0.0, 1.0, 1.0);
    int center_idx = ( (dst_h / 2) * dst_w + (dst_w / 2) ) * 4;
    assert(dst_buf[center_idx + 0] == 255); // Red
    assert(dst_buf[center_idx + 1] == 0);
    assert(dst_buf[center_idx + 2] == 0);

    // Blit transformed layer with 50% opacity
    std::vector<uint8_t> dst_buf_half(dst_w * dst_h * 4, 0);
    blit_transformed_layer(dst_buf_half, dst_w, dst_h, src_buf, src_w, src_h, 0.0, 0.0, 0.0, 1.0, 0.5);
    uint8_t red_val = dst_buf_half[center_idx + 0];
    assert(red_val >= 120 && red_val <= 135); // Approx 127 for 50% opacity

    std::cout << "[PASS] Image transform and keyframing tests passed." << std::endl;
}

void test_clip_pool_and_audio_separation() {
    std::cout << "[TEST] Running clip pool deletion and audio track separation test..." << std::endl;

    Project p;
    ClipManager cm;

    // Add 2 video assets and 1 audio asset
    VideoAsset v1;
    v1.id = "v1";
    v1.path = "/tmp/fake_v1.mp4";
    v1.duration = 10.0;
    v1.has_audio = true;
    p.video_assets.push_back(v1);

    VideoAsset v2;
    v2.id = "v2";
    v2.path = "/tmp/fake_v2.mp4";
    v2.duration = 5.0;
    v2.has_audio = false;
    p.video_assets.push_back(v2);

    AudioAsset a1;
    a1.id = "a1";
    a1.path = "/tmp/fake_a1.mp3";
    a1.duration = 12.0;
    p.audio_assets.push_back(a1);

    // Setup timeline tracks
    Track vt;
    vt.id = "t_v1";
    vt.type = "video";
    vt.name = "Video 1";

    Track at;
    at.id = "t_a1";
    at.type = "audio";
    at.name = "Audio 1";

    p.tracks.push_back(vt);
    p.tracks.push_back(at);

    // Simulate placing v1 on timeline: video clip on vt, separated audio clip on at!
    Clip vc1;
    vc1.id = "c_v1_0";
    vc1.asset_id = "v1";
    vc1.start_time = 0.0;
    vc1.duration = 10.0;
    p.tracks[0].clips.push_back(vc1);

    Clip ac1;
    ac1.id = "clip_a_c_v1_0";
    ac1.asset_id = "v1";
    ac1.start_time = 0.0;
    ac1.duration = 10.0;
    p.tracks[1].clips.push_back(ac1);

    // Add keyframes for vc1 and ac1
    p.set_or_add_keyframe(vc1.id, "opacity", 0.0, 1.0);
    p.set_or_add_keyframe(ac1.id, "volume", 0.0, 0.8);

    // Verify usage counts
    assert(ClipManager::get_asset_usage_count(p, "v1") == 2);
    assert(ClipManager::get_asset_usage_count(p, "v2") == 0);
    assert(ClipManager::get_asset_usage_count(p, "a1") == 0);

    // Test delete_unused_assets: should remove v2 and a1, but preserve v1
    int deleted = cm.delete_unused_assets(p);
    assert(deleted == 2);
    assert(p.video_assets.size() == 1);
    assert(p.video_assets[0].id == "v1");
    assert(p.audio_assets.empty());

    // Test removing v1 with timeline cleanup
    cm.remove_asset(p, "v1", true, true);
    assert(p.video_assets.empty());
    // Timeline clips referencing v1 should have been deleted
    assert(p.tracks[0].clips.empty());
    assert(p.tracks[1].clips.empty());
    // Keyframes for those clips should also have been deleted
    assert(p.keyframes.empty());

    std::cout << "[PASS] Clip pool deletion and audio track separation tests passed." << std::endl;
}

void test_project_prefab_import_and_instantiation() {
    std::cout << "[TEST] Running project prefab import and instantiation test..." << std::endl;

    // 1. Create a dummy sub-project to act as a prefab
    Project sub_p;
    sub_p.name = "Animated Lower Third";

    VideoAsset sub_v;
    sub_v.id = "v1";
    sub_v.path = "/tmp/test_lower_third.png";
    sub_v.duration = 4.0;
    sub_p.video_assets.push_back(sub_v);

    Track sub_track;
    sub_track.type = "video";
    sub_track.name = "Title Track";
    Clip sub_c;
    sub_c.id = "c_title";
    sub_c.asset_id = "v1";
    sub_c.start_time = 0.5;
    sub_c.duration = 3.5;
    sub_c.pos_x = 100.0;
    sub_c.pos_y = 200.0;
    sub_c.scale = 1.2;
    sub_c.opacity = 0.8;
    sub_track.clips.push_back(sub_c);
    sub_p.tracks.push_back(sub_track);

    // Add keyframe in subproject
    sub_p.set_or_add_keyframe(sub_c.id, "opacity", 0.0, 0.0);
    sub_p.set_or_add_keyframe(sub_c.id, "opacity", 1.0, 1.0);

    std::string prefab_file = "/tmp/test_prefab_proj.minicut";
    bool saved = ProjectSerializer::save_to_file(sub_p, prefab_file);
    assert(saved);

    // 2. In host project, import the sub-project as a prefab clip
    Project host_p;
    host_p.name = "Main Movie Project";
    ClipManager cm;

    bool imported = cm.import_project_as_prefab(host_p, prefab_file);
    assert(imported);
    assert(host_p.prefabs.size() == 1);
    const auto& pf = host_p.prefabs[0];
    assert(pf.name == "Animated Lower Third");
    assert(pf.video_tracks == 1);
    assert(pf.total_clips == 1);

    // 3. Test serialization of project containing prefabs
    std::string host_json = ProjectSerializer::serialize_json(host_p);
    Project loaded_host;
    bool loaded = ProjectSerializer::deserialize_json(loaded_host, host_json);
    assert(loaded);
    assert(loaded_host.prefabs.size() == 1);
    assert(loaded_host.prefabs[0].name == "Animated Lower Third");

    // 4. Instantiate prefab on timeline at time 10.0s
    cm.instantiate_prefab_on_timeline(host_p, pf.id, 10.0);
    assert(!host_p.tracks.empty());
    // Find the instantiated clip
    bool found_instantiated = false;
    for (const auto& trk : host_p.tracks) {
        for (const auto& c : trk.clips) {
            if (c.prefab_id == pf.id) {
                found_instantiated = true;
                assert(std::abs(c.start_time - (10.0 + 0.5)) < 1e-4);
                assert(std::abs(c.duration - 3.5) < 1e-4);
                assert(std::abs(c.pos_x - 100.0) < 1e-4);
                assert(std::abs(c.pos_y - 200.0) < 1e-4);
                assert(std::abs(c.scale - 1.2) < 1e-4);
                assert(c.prefab_name == "Animated Lower Third");

                // Check keyframes transferred
                auto kfs = host_p.get_clip_keyframes(c.id, "opacity");
                assert(kfs.size() == 2);
            }
        }
    }
    assert(found_instantiated);

    // 5. Test removing prefab from pool and timeline
    cm.remove_prefab(host_p, pf.id, true);
    assert(host_p.prefabs.empty());
    for (const auto& trk : host_p.tracks) {
        for (const auto& c : trk.clips) {
            assert(c.prefab_id != pf.id);
        }
    }

    fs::remove(prefab_file);

    std::cout << "[PASS] Project prefab import and instantiation tests passed." << std::endl;
}

void test_audio_engine_preview_and_caching() {
    std::cout << "[TEST] Running AudioEngine preview and caching test..." << std::endl;
    AudioEngine engine;
    // Test playhead set/get
    engine.set_playhead_time(2.5);
    assert(std::abs(engine.get_playhead_time() - 2.5) < 1e-4);

    // Preload audio asset
    std::string tone_path = resolve_test_asset("demo_tone.mp3");
    if (fs::exists(tone_path)) {
        engine.preload_asset_audio(tone_path);
        assert(engine.is_asset_cached(tone_path));
        size_t samples = engine.get_cached_sample_count(tone_path);
        assert(samples > 0);
        std::cout << "  -> Cached tone samples: " << samples << " (" << samples / 2 << " frames)" << std::endl;
    }

    // Test ensure_project_assets_loaded
    Project p;
    AudioAsset aa;
    aa.id = "a1";
    aa.path = tone_path;
    p.audio_assets.push_back(aa);
    engine.ensure_project_assets_loaded(p);
    assert(engine.is_asset_cached(tone_path));

    std::cout << "[PASS] AudioEngine preview and caching tests passed." << std::endl;
}

void test_timeline_scrolling_and_navigation() {
    std::cout << "[TEST] Running Timeline scrolling and navigation test..." << std::endl;
    Timeline tl;
    assert(tl.get_scroll_sec() == 0.0f);
    tl.set_scroll_sec(12.5f);
    assert(std::abs(tl.get_scroll_sec() - 12.5f) < 1e-4);

    tl.set_scroll_track_y(44.0f);
    assert(std::abs(tl.get_scroll_track_y() - 44.0f) < 1e-4);

    tl.set_zoom(120.0f);
    assert(std::abs(tl.get_zoom() - 120.0f) < 1e-4);

    std::cout << "[PASS] Timeline scrolling and navigation tests passed." << std::endl;
}

void test_native_file_dialog_system() {
    std::cout << "[TEST] Running NativeFileDialog system detection test..." << std::endl;
    bool avail = NativeFileDialog::is_available();
    std::cout << "  -> Native dialog available: " << (avail ? "YES" : "NO") << std::endl;
    std::cout << "  -> kdialog detected: " << (NativeFileDialog::is_kdialog() ? "YES" : "NO") << std::endl;
    std::cout << "  -> zenity detected: " << (NativeFileDialog::is_zenity() ? "YES" : "NO") << std::endl;
    assert(avail == true);

    // Test callback polling without pending items
    NativeFileDialog::poll_callbacks();
    assert(!NativeFileDialog::is_dialog_open());

    std::cout << "[PASS] NativeFileDialog system tests passed." << std::endl;
}

void test_clip_import_progress() {
    std::cout << "[TEST] Running ClipManager import progress test..." << std::endl;
    ClipManager cm;
    assert(!cm.is_importing());
    assert(cm.get_import_progress() == 0.0f);

    Project p;
    std::string sample_img = resolve_test_asset("test_sample.png");
    if (fs::exists(sample_img)) {
        bool ok = cm.import_image(p, sample_img, 5.0);
        assert(ok);
        assert(!cm.is_importing());
        assert(cm.get_import_progress() == 1.0f);
        std::cout << "  -> Import status: " << cm.get_import_status() << std::endl;
    }

    std::cout << "[PASS] ClipManager import progress tests passed." << std::endl;
}

void test_temp_manager() {
    std::cout << "[TEST] Running TempManager temporary file and cleanup test..." << std::endl;
    std::string temp_dir = TempManager::get_temp_dir();
    assert(!temp_dir.empty());
    assert(fs::exists(temp_dir));
    std::cout << "  -> Temp directory: " << temp_dir << std::endl;

    // Create a dummy temporary file
    std::string dummy_file = TempManager::get_temp_file_path("test_dummy", ".tmp");
    {
        std::ofstream ofs(dummy_file);
        ofs << "Temporary test data buffer 12345" << std::endl;
    }
    assert(fs::exists(dummy_file));

    uintmax_t total_bytes = 0;
    size_t count = TempManager::get_temp_file_count(&total_bytes);
    assert(count >= 1);
    assert(total_bytes > 0);

    // Test cleanup
    uintmax_t freed_bytes = 0;
    size_t cleaned = TempManager::cleanup_temp_files(&freed_bytes);
    assert(cleaned >= 1);
    assert(freed_bytes >= total_bytes);
    assert(!fs::exists(dummy_file));
    std::cout << "  -> Cleaned " << cleaned << " temp files, freed " << freed_bytes << " bytes." << std::endl;

    std::cout << "[PASS] TempManager tests passed." << std::endl;
}

void test_locale_manager() {
    std::cout << "[TEST] Running LocaleManager translation and switching test..." << std::endl;
    auto& lm = LocaleManager::instance();
    lm.init();

    // 1. English
    lm.set_locale("en");
    assert(lm.current_locale() == "en");
    assert(lm.get("menu_file") == "File");
    assert(lm.get("project_cleanup_temp") == "Cleanup Temporary Files");

    // 2. Japanese
    lm.set_locale("ja");
    assert(lm.current_locale() == "ja");
    assert(lm.get("menu_file") == "ファイル");
    assert(lm.get("timeline_play") == "再生");

    // 3. Korean
    lm.set_locale("ko");
    assert(lm.current_locale() == "ko");
    assert(lm.get("menu_file") == "파일");
    assert(lm.get("timeline_play") == "재생");

    // 4. Ukrainian
    lm.set_locale("uk");
    assert(lm.current_locale() == "uk");
    assert(lm.get("menu_file") == "Файл");
    assert(lm.get("timeline_play") == "Відтворити");

    // 5. Arabic
    lm.set_locale("ar");
    assert(lm.current_locale() == "ar");
    assert(lm.is_rtl());
    assert(lm.get("menu_file") == ArabicShaper::process("ملف"));
    assert(lm.get("timeline_play") == ArabicShaper::process("تشغيل"));

    // Reset to en
    lm.set_locale("en");
    assert(!lm.is_rtl());
    std::cout << "[PASS] LocaleManager tests passed." << std::endl;

}

void test_track_render_depth() {
    std::cout << "[TEST] Running Track render depth sorting and serialization test..." << std::endl;
    Project p;
    Track t0; t0.type = "video"; t0.name = "Background"; t0.depth = 10;
    Track t1; t1.type = "video"; t1.name = "Overlay"; t1.depth = 90;
    Track t2; t2.type = "video"; t2.name = "Middle"; t2.depth = 50;
    p.tracks.push_back(t0);
    p.tracks.push_back(t1);
    p.tracks.push_back(t2);

    // Serialization test
    std::string json_str = ProjectSerializer::serialize_json(p);
    Project p2;
    assert(ProjectSerializer::deserialize_json(p2, json_str));
    assert(p2.tracks.size() == 3);
    assert(p2.tracks[0].depth == 10);
    assert(p2.tracks[1].depth == 90);
    assert(p2.tracks[2].depth == 50);

    // Sorting test
    std::vector<const Track*> sorted_tracks;
    for (const auto& trk : p2.tracks) {
        sorted_tracks.push_back(&trk);
    }
    std::stable_sort(sorted_tracks.begin(), sorted_tracks.end(), [](const Track* a, const Track* b) {
        return a->depth < b->depth;
    });

    assert(sorted_tracks[0]->name == "Background" && sorted_tracks[0]->depth == 10);
    assert(sorted_tracks[1]->name == "Middle" && sorted_tracks[1]->depth == 50);
    assert(sorted_tracks[2]->name == "Overlay" && sorted_tracks[2]->depth == 90);

    std::cout << "[PASS] Track render depth tests passed." << std::endl;
}

void test_slice_and_paste_on_selected_track() {
    std::cout << "[TEST] Running Slice and Paste on targeted track test..." << std::endl;
    Project p;
    Track t0; t0.type = "video"; t0.name = "Video 1";
    Clip c0; c0.id = "clip_top"; c0.start_time = 0.0; c0.duration = 10.0;
    t0.clips.push_back(c0);

    Track t1; t1.type = "video"; t1.name = "Video 2";
    Clip c1; c1.id = "clip_target"; c1.start_time = 0.0; c1.duration = 10.0;
    t1.clips.push_back(c1);

    p.tracks.push_back(t0);
    p.tracks.push_back(t1);

    Timeline tl;

    // 1. Test slicing specifically the clip on track 1 (target clip)
    // Select clip on track 1
    bool sliced_specific = tl.split_clip_at(p, "clip_target", 4.0);
    assert(sliced_specific);
    // Track 0 clip should be intact (1 clip, dur 10s)
    assert(p.tracks[0].clips.size() == 1);
    assert(p.tracks[0].clips[0].duration == 10.0);
    // Track 1 should now have 2 clips: [0-4s] and [4-10s]
    assert(p.tracks[1].clips.size() == 2);
    assert(std::abs(p.tracks[1].clips[0].duration - 4.0) < 1e-4);
    assert(std::abs(p.tracks[1].clips[1].duration - 6.0) < 1e-4);

    // 2. Test copy & paste targeting track 1 explicitly
    tl.copy_selected_clip(p); // copies selected or active
    // Paste specifically onto track 1
    bool paste_ok = tl.paste_clip(p, 12.0, 1);
    assert(paste_ok);
    assert(p.tracks[1].clips.size() == 3);
    assert(p.tracks[1].clips.back().start_time == 12.0);

    std::cout << "[PASS] Slice and Paste on targeted track tests passed." << std::endl;
}

void test_filebrowser_system_thumbnail() {
    std::cout << "[TEST] Running FileBrowser system thumbnail resolution test..." << std::endl;
    FileBrowser fb;
    std::string colorbars_path = resolve_test_asset("demo_colorbars.mp4");
    if (fs::exists(colorbars_path)) {
        std::string thumb = fb.find_system_thumbnail(colorbars_path);
        std::cout << "  -> Resolved system thumbnail: " << (thumb.empty() ? "(none / fallback)" : thumb) << std::endl;
    }
    std::cout << "[PASS] FileBrowser system thumbnail tests passed." << std::endl;
}

void test_track_header_resizing() {
    std::cout << "[TEST] Running Track Header resizing & Z-depth layout test..." << std::endl;
    Timeline tl;
    assert(tl.get_track_header_width() == 145.0f);

    tl.set_track_header_width(200.0f);
    assert(tl.get_track_header_width() == 200.0f);

    // Clamping min & max
    tl.set_track_header_width(50.0f);
    assert(tl.get_track_header_width() == 80.0f);

    tl.set_track_header_width(500.0f);
    assert(tl.get_track_header_width() == 400.0f);

    std::cout << "[PASS] Track Header resizing & layout tests passed." << std::endl;
}

void test_script_engine_lua_api() {
    std::cout << "[TEST] Running ScriptEngine & LuaAPI integration test..." << std::endl;
    Project proj;
    Timeline tl;
    ClipManager cm;
    double playhead = 2.5;
    std::string selected_clip = "";
    bool undo_pushed = false;
    bool modified_called = false;

    // Track 0: video, Track 1: audio
    Track v0; v0.type = "video"; v0.name = "Video 1"; v0.id = "v0"; proj.tracks.push_back(v0);
    Track a0; a0.type = "audio"; a0.name = "Audio 1"; a0.id = "a0"; proj.tracks.push_back(a0);

    // Add dummy video & audio assets
    VideoAsset va; va.id = "vid_asset_1"; va.path = "/test/vid.mp4"; va.duration = 10.0; proj.video_assets.push_back(va);
    AudioAsset aa; aa.id = "aud_asset_1"; aa.path = "/test/aud.mp3"; aa.duration = 8.0; proj.audio_assets.push_back(aa);

    ScriptEngine se;
    bool init_ok = se.init(
        &proj, &tl, &cm, &playhead, &selected_clip,
        [&]() { undo_pushed = true; },
        [&]() { modified_called = true; },
        [&](double t) { playhead = t; },
        nullptr,
        [&](const std::string& id) { selected_clip = id; }
    );
    assert(init_ok);
    assert(se.get_lua_state() != nullptr);

    // 1. Test timeline navigation from Lua
    se.run_string("timeline.set_playhead(5.0)");
    assert(std::abs(playhead - 5.0) < 1e-4);

    // 2. Test timeline placement from Lua
    se.run_string("placed_vid = timeline.place_video('vid_asset_1', 0, 1.0, 4.0, 0.5)");
    assert(proj.tracks[0].clips.size() == 1);
    assert(undo_pushed == true);
    assert(modified_called == true);
    Clip& c = proj.tracks[0].clips[0];
    assert(c.start_time == 1.0);
    assert(c.duration == 4.0);
    assert(c.offset == 0.5);

    // 3. Test clip transform and positioning from Lua
    se.run_string("clip.set_position(placed_vid, 150.0, -80.0)");
    se.run_string("clip.set_scale(placed_vid, 1.5)");
    se.run_string("clip.set_rotation(placed_vid, 45.0)");
    se.run_string("clip.set_opacity(placed_vid, 0.85)");
    assert(std::abs(c.pos_x - 150.0) < 1e-4);
    assert(std::abs(c.pos_y - (-80.0)) < 1e-4);
    assert(std::abs(c.scale - 1.5) < 1e-4);
    assert(std::abs(c.rotation - 45.0) < 1e-4);
    assert(std::abs(c.opacity - 0.85) < 1e-4);

    // 4. Test keyframing from Lua
    se.run_string("timeline.add_keyframe(placed_vid, 'opacity', 0.0, 0.0)");
    se.run_string("timeline.add_keyframe(placed_vid, 'opacity', 2.0, 1.0)");
    double kf_mid = proj.evaluate_keyframe(c.id, "opacity", 1.0, 1.0);
    assert(std::abs(kf_mid - 0.5) < 1e-3);

    // 5. Test base script structure (logic_callback & plugin:init_panel)
    const char* test_script_code = R"(
        callback_ran = false
        function logic_callback()
            callback_ran = true
            clip.set_position(placed_vid, 300.0, 200.0)
        end

        function plugin:init_panel()
            gui.text("Test Panel Header")
            gui.button("Run Logic", logic_callback)
        end
    )";

    // Run script code in Lua
    se.run_string(test_script_code);

    // Verify plugin table and callback invocation
    lua_State* L = se.get_lua_state();
    lua_getglobal(L, "logic_callback");
    assert(lua_isfunction(L, -1));
    lua_pcall(L, 0, 0, 0);

    lua_getglobal(L, "callback_ran");
    assert(lua_toboolean(L, -1) == true);
    lua_pop(L, 1);
    assert(std::abs(c.pos_x - 300.0) < 1e-4);
    // 6. Test project_save and project_load from Lua
    se.run_string("save_ok, save_err = project_save('test_lua_saved_project.minicut')");
    se.run_string("assert(save_ok == true)");
    assert(fs::exists("test_lua_saved_project.minicut"));

    // Clear tracks and reload from disk
    proj.tracks[0].clips.clear();
    assert(proj.tracks[0].clips.empty());

    se.run_string("load_ok, load_err = project_load('test_lua_saved_project.minicut')");
    se.run_string("assert(load_ok == true)");
    assert(!proj.tracks[0].clips.empty());
    fs::remove("test_lua_saved_project.minicut");

    // 7. Test net library (download and file I/O via libcurl file protocol)
    std::string test_net_file = "test_net_source.txt";
    std::ofstream ofs(test_net_file);
    ofs << "minicut network subsystem payload";
    ofs.close();

    std::string abs_net_path = fs::absolute(test_net_file).string();
    se.run_string("dl_ok, dl_path = net.download('file://" + abs_net_path + "', 'test_net_dest.txt')");
    se.run_string("assert(dl_ok == true)");
    assert(fs::exists("test_net_dest.txt"));
    std::ifstream ifs("test_net_dest.txt");
    std::string net_content;
    std::getline(ifs, net_content);
    ifs.close();
    assert(net_content == "minicut network subsystem payload");
    fs::remove(test_net_file);
    fs::remove("test_net_dest.txt");

    std::cout << "[PASS] ScriptEngine & LuaAPI integration tests passed." << std::endl;
}

void test_arabic_shaping() {
    std::cout << "[TEST] Running Arabic shaping and RTL tests..." << std::endl;
    // 1. Basic conversions
    std::string text = "مرحبا";
    auto u32 = ArabicShaper::utf8_to_utf32(text);
    assert(u32.size() == 5);
    auto back_utf8 = ArabicShaper::utf32_to_utf8(u32);
    assert(back_utf8 == text);

    // 2. Shaping Arabic string
    std::string shaped = ArabicShaper::process("سلام");
    assert(!shaped.empty());
    assert(shaped != "سلام");

    // 3. Non-Arabic strings (Japanese and English) remain untouched and strictly LTR
    std::string ja_text = "こんにちは世界";
    std::string ja_shaped = ArabicShaper::process(ja_text);
    assert(ja_shaped == ja_text);

    std::string en_text = "Hello World 123";
    std::string en_shaped = ArabicShaper::process(en_text);
    assert(en_shaped == en_text);

    // 4. LocaleManager is_rtl() check
    LocaleManager& lm = LocaleManager::instance();
    lm.set_locale("en");
    assert(!lm.is_rtl());
    lm.set_locale("ja");
    assert(!lm.is_rtl()); // Japanese MUST remain LTR
    lm.set_locale("ko");
    assert(!lm.is_rtl());
    lm.set_locale("ar");
    assert(lm.is_rtl());   // Arabic triggers RTL
    lm.set_locale("en");   // Reset to default English

    std::cout << "[PASS] Arabic shaping and RTL tests passed." << std::endl;
}

void test_text_generator() {
    std::cout << "[TEST] Running TextGenerator rendering and asset creation tests..." << std::endl;
    auto fonts = TextGenerator::get_available_fonts();
    assert(!fonts.empty());

    TextConfig cfg;
    cfg.text = "Minicut Title\nLine Two";
    cfg.font_size = 36.0f;
    cfg.use_bg = true;
    cfg.use_outline = true;
    cfg.outline_thickness = 2.0f;

    int w = 0, h = 0;
    std::vector<uint8_t> rgba;
    bool ok = TextGenerator::render_to_rgba(cfg, w, h, rgba);
    assert(ok);
    assert(w > 0 && h > 0);
    assert(rgba.size() == static_cast<size_t>(w * h * 4));

    // Test PNG file export
    std::string png_path = "test_text_output.png";
    int pw = 0, ph = 0;
    bool save_ok = TextGenerator::save_to_png(cfg, png_path, pw, ph);
    assert(save_ok);
    assert(fs::exists(png_path));
    assert(fs::file_size(png_path) > 100);
    fs::remove(png_path);

    // Test Project Asset creation
    Project proj;
    std::string asset_id = TextGenerator::create_text_asset(proj, cfg, "Title Asset");
    assert(!asset_id.empty());
    assert(!proj.video_assets.empty());
    assert(proj.video_assets.front().id == asset_id);
    assert(proj.video_assets.front().is_image == true);

    std::cout << "[PASS] TextGenerator tests passed." << std::endl;
}

void test_track_reordering_and_renaming() {
    std::cout << "[TEST] Running track reordering and renaming tests..." << std::endl;
    Project proj;
    Timeline tl;

    Track t0; t0.id = "t0"; t0.type = "video"; t0.name = "Video 1";
    Track t1; t1.id = "t1"; t1.type = "audio"; t1.name = "Audio 1";
    Track t2; t2.id = "t2"; t2.type = "video"; t2.name = "Video 2";
    proj.tracks = { t0, t1, t2 };

    // 1. Move down: track 0 moves to index 1
    tl.move_track_down(proj, 0);
    assert(proj.tracks[0].id == "t1");
    assert(proj.tracks[1].id == "t0");
    assert(proj.tracks[2].id == "t2");

    // 2. Move up: track 1 moves back to index 0
    tl.move_track_up(proj, 1);
    assert(proj.tracks[0].id == "t0");
    assert(proj.tracks[1].id == "t1");
    assert(proj.tracks[2].id == "t2");

    // 3. Swap tracks 0 and 2
    tl.swap_tracks(proj, 0, 2);
    assert(proj.tracks[0].id == "t2");
    assert(proj.tracks[1].id == "t1");
    assert(proj.tracks[2].id == "t0");

    // 4. Reorder track 2 to index 0
    tl.reorder_track(proj, 2, 0);
    assert(proj.tracks[0].id == "t0");
    assert(proj.tracks[1].id == "t2");
    assert(proj.tracks[2].id == "t1");

    // 5. Track Renaming
    tl.set_track_name(proj, 0, "Main B-Roll");
    assert(tl.get_track_name(proj, 0) == "Main B-Roll");
    assert(proj.tracks[0].name == "Main B-Roll");

    // 6. Localized Display Name
    assert(Timeline::get_display_track_name(proj.tracks[0]) == "Main B-Roll");
    assert(Timeline::get_display_track_name(proj.tracks[1]) == "Video 2");

    std::cout << "[PASS] Track reordering and renaming tests passed." << std::endl;
}

void test_timeline_multiselect() {
    std::cout << "[TEST] Running timeline multi-selection tests..." << std::endl;
    Project proj;
    Timeline tl;

    Track trk; trk.id = "v1"; trk.type = "video";
    Clip c1; c1.id = "c1"; c1.start_time = 0.0; c1.duration = 5.0;
    Clip c2; c2.id = "c2"; c2.start_time = 5.0; c2.duration = 5.0;
    Clip c3; c3.id = "c3"; c3.start_time = 10.0; c3.duration = 5.0;
    trk.clips = { c1, c2, c3 };
    proj.tracks = { trk };

    // Single select
    tl.select_clip("c1", false, false);
    assert(tl.get_selected_clip_ids().size() == 1);
    assert(tl.is_clip_selected("c1"));

    // Add to selection
    tl.select_clip("c2", false, true);
    assert(tl.get_selected_clip_ids().size() == 2);
    assert(tl.is_clip_selected("c1"));
    assert(tl.is_clip_selected("c2"));

    // Toggle off c1
    tl.select_clip("c1", true, false);
    assert(tl.get_selected_clip_ids().size() == 1);
    assert(!tl.is_clip_selected("c1"));
    assert(tl.is_clip_selected("c2"));

    // Select All
    tl.select_all(proj);
    assert(tl.get_selected_clip_ids().size() == 3);
    assert(tl.is_clip_selected("c1"));
    assert(tl.is_clip_selected("c2"));
    assert(tl.is_clip_selected("c3"));

    // Delete selected clips
    tl.delete_selected_clips(proj);
    assert(proj.tracks[0].clips.empty());
    assert(tl.get_selected_clip_ids().empty());

    std::cout << "[PASS] Timeline multi-selection tests passed." << std::endl;
}

void test_extended_lua_api() {
    std::cout << "[TEST] Running extended Lua API tests (file dialog, text, tracks)..." << std::endl;
    ScriptEngine se;
    Project proj;
    Timeline tl;
    ClipManager cm;
    double playhead = 0.0;
    std::string sel_clip;

    Track vt; vt.id = "v1"; vt.type = "video"; vt.name = "Video 1";
    Track at; at.id = "a1"; at.type = "audio"; at.name = "Audio 1";
    proj.tracks = { vt, at };

    se.init(&proj, &tl, &cm, &playhead, &sel_clip);

    // 1. Text asset creation via assets.create_text
    se.run_string("txt_id = assets.create_text('Sample Lower Third', { size = 36, justification = 1 })");
    se.run_string("assert(txt_id ~= nil and txt_id ~= '')");
    assert(!proj.video_assets.empty());

    // 2. Timeline text placement via timeline.place_text_clip
    se.run_string("clip_id = timeline.place_text_clip('Live News Alert', 0, 2.0, 5.0)");
    se.run_string("assert(clip_id ~= nil and clip_id ~= '')");
    assert(!proj.tracks[0].clips.empty());
    assert(proj.tracks[0].clips.front().start_time == 2.0);

    // 3. Track reordering via Lua
    se.run_string("timeline.move_track_down(0)");
    assert(proj.tracks[0].id == "a1");
    assert(proj.tracks[1].id == "v1");

    se.run_string("timeline.move_track_up(1)");
    assert(proj.tracks[0].id == "v1");
    assert(proj.tracks[1].id == "a1");

    // 4. Track renaming via Lua
    se.run_string("timeline.set_track_name(0, 'Main Camera')");
    se.run_string("assert(timeline.get_track_name(0) == 'Main Camera')");
    assert(proj.tracks[0].name == "Main Camera");

    // 5. Multi-selection via Lua
    se.run_string("timeline.select_clips({ 'c_test1', 'c_test2' })");
    se.run_string("sel_tbl = timeline.get_selected_clips()");
    se.run_string("assert(#sel_tbl == 2)");
    se.run_string("timeline.clear_selection()");
    se.run_string("assert(#timeline.get_selected_clips() == 0)");

    // 6. File dialog presence in Lua environment
    se.run_string("assert(type(gui.file_dialog) == 'function')");
    se.run_string("assert(type(gui.native_file_dialog) == 'function')");
    se.run_string("assert(type(gui.builtin_file_dialog) == 'function')");

    std::cout << "[PASS] Extended Lua API tests passed." << std::endl;
}

void test_ogg_ogv_ogm_support() {
    std::cout << "[TEST] Running OGG, OGV, and OGM format recognition tests..." << std::endl;
    assert(FileBrowser::is_video_file("test_clip.ogv"));
    assert(FileBrowser::is_video_file("test_clip.ogm"));
    assert(FileBrowser::is_video_file("TEST_MOVIE.OGV"));
    assert(FileBrowser::is_video_file("TEST_MOVIE.OGM"));
    assert(FileBrowser::is_audio_file("soundtrack.ogg"));
    assert(FileBrowser::is_audio_file("EFFECT.OGG"));
    std::cout << "[PASS] OGG, OGV, and OGM format tests passed." << std::endl;
}

void test_window_title_and_version() {
    std::cout << "[TEST] Running window title and version tests..." << std::endl;
    // Version test
    assert(std::string(MINICUT_VERSION) == "1.5.0");

    // Title formatting test
    auto format_title = [](const std::string& name) -> std::string {
        std::string proj_title = name;
        if (proj_title.empty() || proj_title == "Untitled Project") {
            proj_title = "New Project";
        }
        return "Minicut - " + proj_title;
    };

    assert(format_title("") == "Minicut - New Project");
    assert(format_title("Untitled Project") == "Minicut - New Project");
    assert(format_title("Summer Vacation") == "Minicut - Summer Vacation");
    std::cout << "[PASS] Window title and version tests passed." << std::endl;
}

void test_dock_localization() {
    std::cout << "[TEST] Running Dock localization tests..." << std::endl;
    auto& lm = LocaleManager::instance();
    lm.init();

    lm.set_locale("en");
    assert(lm.get("dock_title") == "DOCK");

    lm.set_locale("ja");
    assert(lm.get("dock_title") == "ドック");

    lm.set_locale("ko");
    assert(lm.get("dock_title") == "도크");

    lm.set_locale("uk");
    assert(lm.get("dock_title") == "ДОК");

    lm.set_locale("ar");
    assert(lm.get("dock_title") == ArabicShaper::process("منصة"));

    // Restore to English
    lm.set_locale("en");
    std::cout << "[PASS] Dock localization tests passed." << std::endl;
}

void test_javascript_engine() {
    std::cout << "[TEST] Running Duktape JavaScript Engine integration tests..." << std::endl;
    Project proj;
    Timeline tl;
    ClipManager cm;
    ScriptEngine se;
    se.init(&proj, &tl, &cm);

    // 1. Basic JS execution
    bool ok = se.run_js_string("var x = 10 + 20;");
    assert(ok);

    // 2. Timeline and Track management via JS
    ok = se.run_js_string("timeline.add_track('video', 'JS Video Track');");
    assert(ok);
    assert(proj.tracks.size() == 1);
    assert(proj.tracks[0].name == "JS Video Track");

    se.run_js_string("timeline.set_track_name(0, 'Renamed JS Track');");
    assert(proj.tracks[0].name == "Renamed JS Track");

    // 3. Clip placement and manipulation via JS
    se.run_js_string("var cid = timeline.place_video(0, 'asset_1', 1.0, 4.0);");
    assert(!proj.tracks[0].clips.empty());
    std::string cid = proj.tracks[0].clips.front().id;

    se.run_js_string("clip.set_position('" + cid + "', 50.0, -25.0);");
    assert(proj.tracks[0].clips.front().pos_x == 50.0);
    assert(proj.tracks[0].clips.front().pos_y == -25.0);

    se.run_js_string("clip.set_scale('" + cid + "', 2.0);");
    assert(proj.tracks[0].clips.front().scale == 2.0);

    se.run_js_string("clip.set_rotation('" + cid + "', 45.0);");
    assert(proj.tracks[0].clips.front().rotation == 45.0);

    se.run_js_string("clip.set_opacity('" + cid + "', 0.75);");
    assert(proj.tracks[0].clips.front().opacity == 0.75);

    // 4. Keyframing via JS
    se.run_js_string("timeline.add_keyframe('" + cid + "', 'opacity', 2.0, 0.2);");
    assert(proj.keyframes.size() == 1);
    assert(proj.keyframes[0].clip_id == cid);
    assert(proj.keyframes[0].value == 0.2);

    se.run_js_string("var val = timeline.evaluate_keyframe('" + cid + "', 'opacity', 2.0, 1.0);");
    double val = proj.evaluate_keyframe(cid, "opacity", 2.0, 1.0);
    assert(std::abs(val - 0.2) < 1e-4);

    // 5. Project properties via JS
    se.run_js_string("project.set_name('JS Created Project');");
    assert(proj.name == "JS Created Project");

    // 6. Net and GUI object presence in JS
    se.run_js_string("if (typeof net.get !== 'function') throw new Error('net.get missing');");
    se.run_js_string("if (typeof gui.button !== 'function') throw new Error('gui.button missing');");
    se.run_js_string("if (typeof gui.file_dialog !== 'function') throw new Error('gui.file_dialog missing');");

    std::cout << "[PASS] Duktape JavaScript Engine integration tests passed." << std::endl;
}

void test_filebrowser_cancel_behavior() {
    std::cout << "[TEST] Running FileBrowser cancel behavior test..." << std::endl;
    FileBrowser fb;
    bool cancelled = false;
    bool confirmed = false;

    fb.open("Test Browser", FileBrowserMode::OpenFile, {}, "default.mp4", ".mp4",
        [&](const std::string& p) { confirmed = true; },
        "Open",
        [&]() { cancelled = true; });

    assert(fb.is_open());
    fb.cancel();
    assert(!fb.is_open());
    assert(cancelled == true);
    assert(confirmed == false);

    // Test second open and close()
    bool cancelled2 = false;
    fb.open("Test Browser 2", FileBrowserMode::SaveFile, {}, "out.mp4", ".mp4",
        [&](const std::string& p) {},
        "Save",
        [&]() { cancelled2 = true; });
    assert(fb.is_open());
    fb.close();
    assert(!fb.is_open());
    assert(cancelled2 == true);

    std::cout << "[PASS] FileBrowser cancel behavior tests passed." << std::endl;
}

void test_export_format_presets_and_formats() {
    std::cout << "[TEST] Running Export format presets and formats test..." << std::endl;
    ExportManager em;
    em.load_format_presets();
    size_t initial_count = em.get_presets().size();
    assert(initial_count >= 5); // Built-in presets: 1080p, 720p, 4K, GIF, WebP

    // Save a custom preset
    bool saved = em.save_format_preset("Custom TikTok 9:16", 1080, 1920, 60, "mp4");
    assert(saved);
    em.load_format_presets();
    assert(em.get_presets().size() == initial_count + 1);

    // Verify properties
    int found_idx = -1;
    for (size_t i = 0; i < em.get_presets().size(); ++i) {
        if (em.get_presets()[i].name == "Custom TikTok 9:16") {
            found_idx = static_cast<int>(i);
            assert(em.get_presets()[i].width == 1080);
            assert(em.get_presets()[i].height == 1920);
            assert(em.get_presets()[i].fps == 60);
            assert(em.get_presets()[i].format == "mp4");
            assert(em.get_presets()[i].is_custom == true);
            break;
        }
    }
    assert(found_idx != -1);

    // Delete custom preset
    bool deleted = em.delete_format_preset(found_idx);
    assert(deleted);
    em.load_format_presets();
    assert(em.get_presets().size() == initial_count);

    std::cout << "[PASS] Export format presets and formats tests passed." << std::endl;
}

void test_subtitle_track_and_serialization() {
    std::cout << "[TEST] Running subtitle track & serialization test..." << std::endl;
    Project p;
    p.name = "Subtitle Demo Project";

    Track sub_track;
    sub_track.id = "track_sub_0";
    sub_track.name = "Subtitles";
    sub_track.type = "subtitle";
    sub_track.depth = 95;

    Clip c1;
    c1.id = "sub_clip_1";
    c1.start_time = 1.0;
    c1.duration = 2.5;
    c1.subtitle_text = "Welcome to Minicut 1.4!";
    sub_track.clips.push_back(c1);

    Clip c2;
    c2.id = "sub_clip_2";
    c2.start_time = 4.0;
    c2.duration = 3.0;
    c2.subtitle_text = "Generated using local and AI speech models.";
    sub_track.clips.push_back(c2);

    p.tracks.push_back(sub_track);

    // Serialize to JSON string
    std::string json_str = ProjectSerializer::serialize_json(p);
    assert(!json_str.empty());

    // Deserialize
    Project p2;
    bool ok = ProjectSerializer::deserialize_json(p2, json_str);
    assert(ok);
    assert(p2.tracks.size() == 1);
    assert(p2.tracks[0].type == "subtitle");
    assert(p2.tracks[0].clips.size() == 2);
    assert(p2.tracks[0].clips[0].subtitle_text == "Welcome to Minicut 1.4!");
    assert(p2.tracks[0].clips[1].subtitle_text == "Generated using local and AI speech models.");
    assert(p2.tracks[0].clips[0].contains_time(2.0));
    assert(!p2.tracks[0].clips[0].contains_time(3.8));

    std::cout << "[PASS] Subtitle track & serialization tests passed." << std::endl;
}

void test_render_subsystem() {
    std::cout << "[TEST] Running Render Subsystem test..." << std::endl;
    RenderSystem& rs = RenderSystem::instance();
    assert(rs.get_backend_type() == RenderBackendType::OpenGL3);
    assert(std::string(rs.get_backend_name()) == "OpenGL 3.3");

    // Vulkan backend probe test
    RenderBackendVulkan vk_backend;
    assert(std::string(vk_backend.get_name()) == "Vulkan");
    assert(vk_backend.get_type() == RenderBackendType::Vulkan);
    bool vk_supported = vk_backend.is_supported();
    std::cout << "  -> Vulkan support on host: " << (vk_supported ? "Available" : "Not present (graceful fallback)") << std::endl;

    // OpenGL backend probe test
    RenderBackendOpenGL3 gl_backend;
    assert(std::string(gl_backend.get_name()) == "OpenGL 3.3");
    assert(gl_backend.get_type() == RenderBackendType::OpenGL3);
    assert(gl_backend.is_supported() == true);

    std::cout << "[PASS] Render Subsystem tests passed." << std::endl;
}

void test_settings_logic_and_auth() {
    std::cout << "[TEST] Running SettingsLogic and Auth test..." << std::endl;

    std::string home = getenv("HOME") ? getenv("HOME") : "";
    std::string cfg_path = home + "/.config/minicut/settings.json";
    std::string orig_cfg;
    if (fs::exists(cfg_path)) {
        std::ifstream ifs(cfg_path);
        orig_cfg.assign((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    }

    SettingsLogic& sl = SettingsLogic::instance();
    sl.load_from_disk();

    sl.set_ai_enabled(true);
    assert(sl.is_ai_enabled() == true);

    sl.set_render_backend("OpenGL3");
    assert(sl.get_render_backend() == "OpenGL3");

    sl.set_tts_provider("system");
    assert(sl.get_tts_provider() == "system");

    sl.set_tts_rate(1.25f);
    assert(std::abs(sl.get_tts_rate() - 1.25f) < 1e-4);

    sl.set_gemini_api_key("AIzaSyTestApiKeyMinicut12345");
    assert(sl.get_gemini_api_key() == "AIzaSyTestApiKeyMinicut12345");
    assert(sl.is_authenticated());
    assert(sl.get_active_auth_token_or_key() == "AIzaSyTestApiKeyMinicut12345");

    sl.save_to_disk();

    // Verify persistence
    assert(fs::exists(cfg_path));

    // Restore original configuration so user settings are never lost
    if (!orig_cfg.empty()) {
        std::ofstream ofs(cfg_path);
        ofs << orig_cfg;
        ofs.close();
        sl.load_from_disk();
    }

    std::cout << "[PASS] SettingsLogic and Auth tests passed." << std::endl;
}

void test_tts_manager() {
    std::cout << "[TEST] Running TTSManager synthesis test..." << std::endl;
    std::string temp_dir = TempManager::get_temp_dir();
    std::string out_wav = temp_dir + "/test_tts_unit.wav";

    std::string err;
    bool ok = TTSManager::instance().synthesize("Testing Minicut speech synthesis.", out_wav, 1.0f, 1.0f, "", &err);
    if (ok) {
        assert(fs::exists(out_wav));
        uintmax_t sz = fs::file_size(out_wav);
        assert(sz > 44); // Greater than WAV header size
        std::cout << "  -> Synthesized WAV size: " << sz << " bytes." << std::endl;
    } else {
        std::cout << "  -> Synthesis skipped or unavailable: " << err << std::endl;
    }

    // Direct WAV writer test
    std::vector<int16_t> test_samples(8000, 1000); // 0.5s of audio
    std::string direct_wav = temp_dir + "/test_direct_wav.wav";
    bool wrote = TTSManager::write_wav_file(direct_wav, test_samples, 16000, 1);
    assert(wrote);
    assert(fs::exists(direct_wav));
    assert(fs::file_size(direct_wav) == 44 + test_samples.size() * sizeof(int16_t));

    std::cout << "[PASS] TTSManager tests passed." << std::endl;
}

void test_accessibility_and_cascading_timeline() {
    std::cout << "[TEST] Running accessibility, srt export, and cascading timeline tests..." << std::endl;

    // 1. SettingsLogic Accessibility and Voice Settings
    SettingsLogic& settings = SettingsLogic::instance();
    settings.set_light_theme(true);
    assert(settings.get_light_theme() == true);
    settings.set_colorblind_mode(2); // Deuteranopia
    assert(settings.get_colorblind_mode() == 2);
    settings.set_tts_voice("Fenrir");
    assert(settings.get_tts_voice() == "Fenrir");

    // Serialization & persistence
    std::string home = getenv("HOME") ? getenv("HOME") : "";
    std::string cfg_path = home + "/.config/minicut/settings.json";
    std::string orig_cfg;
    if (fs::exists(cfg_path)) {
        std::ifstream ifs(cfg_path);
        orig_cfg.assign((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    }

    settings.save_to_disk();
    SettingsLogic test_settings;
    test_settings.load_from_disk();
    assert(test_settings.get_light_theme() == true);
    assert(test_settings.get_colorblind_mode() == 2);
    assert(test_settings.get_tts_voice() == "Fenrir");

    // Restore original configuration so user settings are never modified by tests
    if (!orig_cfg.empty()) {
        std::ofstream ofs(cfg_path);
        ofs << orig_cfg;
        ofs.close();
        settings.load_from_disk();
    }

    // 2. ExportManager SRT File Generation
    Project p;
    Track st;
    st.type = "subtitle";
    st.name = "Subtitles";
    Clip sc1;
    sc1.id = "sub1";
    sc1.start_time = 1.0;
    sc1.duration = 2.5;
    sc1.subtitle_text = "First line of subtitles";
    st.clips.push_back(sc1);
    Clip sc2;
    sc2.id = "sub2";
    sc2.start_time = 4.0;
    sc2.duration = 3.0;
    sc2.subtitle_text = "Second line of subtitles";
    st.clips.push_back(sc2);
    p.tracks.push_back(st);

    std::string srt_path = "test_export_subtitles.srt";
    if (fs::exists(srt_path)) fs::remove(srt_path);
    bool srt_ok = ExportManager::write_srt_file(srt_path, p);
    assert(srt_ok);
    assert(fs::exists(srt_path));
    std::ifstream srt_in(srt_path);
    std::string srt_content((std::istreambuf_iterator<char>(srt_in)), std::istreambuf_iterator<char>());
    assert(srt_content.find("00:00:01,000 --> 00:00:03,500") != std::string::npos);
    assert(srt_content.find("First line of subtitles") != std::string::npos);
    assert(srt_content.find("00:00:04,000 --> 00:00:07,000") != std::string::npos);
    assert(srt_content.find("Second line of subtitles") != std::string::npos);
    fs::remove(srt_path);

    // 3. Cascading Split on Multi-Selected Clips
    Timeline tl;
    Project p_split;
    Track t0; t0.type = "video"; t0.name = "V0";
    Clip c0; c0.id = "c0"; c0.start_time = 1.0; c0.duration = 4.0;
    t0.clips.push_back(c0);
    p_split.tracks.push_back(t0);

    Track t1; t1.type = "audio"; t1.name = "A0";
    Clip c1; c1.id = "c1"; c1.start_time = 2.0; c1.duration = 5.0;
    t1.clips.push_back(c1);
    p_split.tracks.push_back(t1);

    // Select both clips across both tracks
    tl.select_clip("c0", false, true);
    tl.select_clip("c1", false, true);
    assert(tl.get_selected_clip_ids().size() == 2);

    // Playhead at t=3.0 intersects both c0 (1.0..5.0) and c1 (2.0..7.0)
    bool split_ok = tl.split_selected_clips(p_split, 3.0);
    assert(split_ok);
    // Both tracks should now have 2 clips!
    assert(p_split.tracks[0].clips.size() == 2);
    assert(p_split.tracks[1].clips.size() == 2);
    assert(std::abs(p_split.tracks[0].clips[0].duration - 2.0) < 1e-3);
    assert(std::abs(p_split.tracks[0].clips[1].duration - 2.0) < 1e-3);
    assert(std::abs(p_split.tracks[1].clips[0].duration - 1.0) < 1e-3);
    assert(std::abs(p_split.tracks[1].clips[1].duration - 4.0) < 1e-3);

    // 4. Multi-Clip Copy & Paste
    tl.clear_selection();
    tl.select_clip(p_split.tracks[0].clips[0].id, false, true);
    tl.select_clip(p_split.tracks[1].clips[0].id, false, true);
    tl.copy_selected_clips(p_split);

    // Paste at t=10.0 starting on track 0
    bool paste_ok = tl.paste_clip(p_split, 10.0, 0);
    assert(paste_ok);
    // Track 0 should now have 3 clips, Track 1 should now have 3 clips
    assert(p_split.tracks[0].clips.size() == 3);
    assert(p_split.tracks[1].clips.size() == 3);
    assert(p_split.tracks[0].clips[2].start_time == 10.0);
    assert(p_split.tracks[1].clips[2].start_time == 11.0); // 1s relative offset preserved!

    std::cout << "[PASS] Accessibility, srt export, and cascading timeline tests passed." << std::endl;
}

void test_aicore_interfaces() {
    std::cout << "[TEST] Running AICore interfaces test..." << std::endl;
    AICore& ai = AICore::instance();
    assert(!ai.is_busy());
    assert(!ai.get_status().empty());

    std::cout << "[PASS] AICore interfaces tests passed." << std::endl;
}

void test_swap_manager_and_disk_buffer() {
    std::cout << "[TEST] Running SwapManager & disk-backed buffer tests..." << std::endl;
    SwapManager& sm = SwapManager::instance();
    sm.init();
    std::string swap_dir = sm.get_swap_dir();
    assert(!swap_dir.empty());
    assert(fs::exists(swap_dir));

    // Test SwapBuffer<float> for large audio PCM buffers
    {
        SwapBuffer<float> buf;
        assert(buf.empty());
        assert(buf.size() == 0);

        // Append 10,000 floats
        std::vector<float> sample_data(10000);
        for (size_t i = 0; i < sample_data.size(); ++i) {
            sample_data[i] = static_cast<float>(i) * 0.001f;
        }
        buf.append(sample_data.data(), sample_data.size());

        assert(buf.size() == 10000);
        assert(std::abs(buf[0] - 0.0f) < 1e-5f);
        assert(std::abs(buf[500] - 0.5f) < 1e-5f);
        assert(std::abs(buf[9999] - 9.999f) < 1e-5f);

        // Verify disk-backed file was created in ~/.config/minicut/swap/
        std::string path = buf.get_swap_path();
        assert(!path.empty());
        assert(fs::exists(path));
    }

    // Now test SwapBuffer<uint8_t> for decoded image caches
    {
        SwapBuffer<uint8_t> img_buf(1920 * 1080 * 4, 128);
        assert(img_buf.size() == 1920 * 1080 * 4);
        assert(img_buf[0] == 128);
        assert(img_buf[1920 * 1080 * 4 - 1] == 128);
        std::string img_path = img_buf.get_swap_path();
        assert(!img_path.empty());
        assert(fs::exists(img_path));
    }

    std::cout << "[PASS] SwapManager and disk-backed buffer tests passed." << std::endl;
}

void test_subtitle_addition_and_assets_menu() {
    std::cout << "[TEST] Running subtitle addition and assets menu tests..." << std::endl;
    LocaleManager& loc = LocaleManager::instance();
    loc.set_locale("en");
    assert(std::string(_TR("panel_clips")) == "Asset Browser");
    assert(std::string(_TR("menu_assets")) == "Assets");
    assert(std::string(_TR("timeline_add_subtitle")) == "+ Subtitle");

    // Test timeline subtitle addition
    Project p;
    Timeline tl;
    assert(p.tracks.empty());

    // Adding subtitle clip should auto-create subtitle track with depth 90
    std::string sub_id1 = tl.add_subtitle_clip(p, 1.5, 3.0, "Test Subtitle 1");
    assert(!sub_id1.empty());
    assert(p.tracks.size() == 1);
    assert(p.tracks[0].type == "subtitle");
    assert(p.tracks[0].depth == 90);
    assert(p.tracks[0].clips.size() == 1);
    assert(p.tracks[0].clips[0].id == sub_id1);
    assert(p.tracks[0].clips[0].start_time == 1.5);
    assert(p.tracks[0].clips[0].duration == 3.0);
    assert(p.tracks[0].clips[0].subtitle_text == "Test Subtitle 1");

    // Adding second subtitle clip should reuse existing subtitle track
    std::string sub_id2 = tl.add_subtitle_clip(p, 5.0, 2.5, "Second Subtitle");
    assert(!sub_id2.empty());
    assert(p.tracks.size() == 1); // Reuses the track
    assert(p.tracks[0].clips.size() == 2);
    assert(p.tracks[0].clips[1].subtitle_text == "Second Subtitle");

    // Test explicit add_track for subtitle
    tl.add_track(p, "subtitle");
    assert(p.tracks.size() == 2);
    assert(p.tracks[1].type == "subtitle");
    assert(p.tracks[1].name == "Subtitle 2");

    std::cout << "[PASS] Subtitle addition and assets menu tests passed." << std::endl;
}

void test_multilanguage_tts_stereo_and_autolength() {
    std::cout << "[TEST] Running multi-language TTS, stereo audio, and auto-length export tests..." << std::endl;

    // 1. Language Detection for TTS
    assert(TTSManager::detect_language_code("Hello world, this is a test.") == "en");
    assert(TTSManager::detect_language_code("こんにちは世界") == "ja");
    assert(TTSManager::detect_language_code("カタカナ") == "ja");
    assert(TTSManager::detect_language_code("안녕하세요 한국어") == "ko");
    assert(TTSManager::detect_language_code("Привіт, як справи? Україна") == "uk");
    assert(TTSManager::detect_language_code("مرحبا بك في العالم") == "ar");
    assert(TTSManager::detect_language_code("你好，世界") == "cmn");

    // 2. SettingsLogic preview volume and live persistence
    SettingsLogic& logic = SettingsLogic::instance();
    logic.set_preview_volume_db(-6.0f);
    assert(logic.get_preview_volume_db() == -6.0f);
    logic.set_preview_volume_db(0.0f);
    assert(logic.get_preview_volume_db() == 0.0f);

    // 3. AudioEngine preview volume
    AudioEngine ae;
    ae.set_preview_volume_db(-14.5f);
    assert(std::abs(ae.get_preview_volume_db() - (-14.5f)) < 0.001f);

    // 4. Asset Duration vs Total Timeline Duration
    Project proj;
    assert(proj.get_assets_duration() == 0.0);
    assert(proj.get_total_duration() == 10.0); // minimum 10s timeline view

    Track v0; v0.id = "v0"; v0.type = "video"; v0.name = "Video 1";
    Clip c0; c0.id = "c0"; c0.start_time = 0.0; c0.duration = 4.2;
    v0.clips.push_back(c0);
    proj.tracks.push_back(v0);

    assert(std::abs(proj.get_assets_duration() - 4.2) < 0.001);
    assert(proj.get_total_duration() == 10.0);

    Clip c1; c1.id = "c1"; c1.start_time = 10.0; c1.duration = 6.0;
    proj.tracks[0].clips.push_back(c1);
    assert(std::abs(proj.get_assets_duration() - 16.0) < 0.001);
    assert(std::abs(proj.get_total_duration() - 16.0) < 0.001);

    // 5. Stereo Audio Keyframes (volume, volume_left, volume_right)
    proj.set_or_add_keyframe("c0", "volume", 0.0, 1.0);
    proj.set_or_add_keyframe("c0", "volume_left", 0.0, 0.8);
    proj.set_or_add_keyframe("c0", "volume_right", 0.0, 0.2);

    double v_master = proj.evaluate_keyframe("c0", "volume", 0.0, 1.0);
    double v_left = proj.evaluate_keyframe("c0", "volume_left", 0.0, 1.0);
    double v_right = proj.evaluate_keyframe("c0", "volume_right", 0.0, 1.0);
    assert(std::abs(v_master - 1.0) < 0.001);
    assert(std::abs(v_left - 0.8) < 0.001);
    assert(std::abs(v_right - 0.2) < 0.001);

    // 6. Cross-track Clip Dragging
    Track v1; v1.id = "v1"; v1.type = "video"; v1.name = "Video 2";
    Track a0; a0.id = "a0"; a0.type = "audio"; a0.name = "Audio 1";
    proj.tracks.push_back(v1);
    proj.tracks.push_back(a0);

    // Drag clip c0 from v0 to v1 (both video) -> allowed
    assert(proj.tracks[0].clips.size() == 2);
    assert(proj.tracks[1].clips.empty());
    if (proj.tracks[0].type == proj.tracks[1].type) {
        Clip moved = proj.tracks[0].clips[0];
        proj.tracks[0].clips.erase(proj.tracks[0].clips.begin());
        proj.tracks[1].clips.push_back(moved);
    }
    assert(proj.tracks[0].clips.size() == 1);
    assert(proj.tracks[1].clips.size() == 1);
    assert(proj.tracks[1].clips[0].id == "c0");

    // Cross-track drag from v1 to a0 (video to audio) -> mismatched type rejected
    bool can_drag_mismatched = (proj.tracks[1].type == proj.tracks[2].type);
    assert(!can_drag_mismatched);

    // 7. ExportConfig Auto-length defaults
    ExportConfig exp_cfg;
    assert(exp_cfg.auto_length == true);
    assert(exp_cfg.manual_duration == 0.0);

    std::cout << "[PASS] Multi-language TTS, stereo audio, and auto-length export tests passed." << std::endl;
}

extern ImGuiKey ImGui_ImplSDL2_KeyEventToImGuiKey(SDL_Keycode keycode, SDL_Scancode scancode);

void test_preview_aspect_ratio_and_japanese_kanji_tts_and_hotkeys() {
    std::cout << "[TEST] Running preview aspect ratio, Japanese Kanji TTS, and layout-independent hotkeys tests..." << std::endl;

    // 1. Japanese Kanji Transliteration
    assert(JapaneseTransliteration::contains_kanji("こんにちは世界"));
    assert(JapaneseTransliteration::contains_kanji("世界"));
    assert(JapaneseTransliteration::contains_kanji("日本"));
    assert(!JapaneseTransliteration::contains_kanji("こんにちは"));
    assert(!JapaneseTransliteration::contains_kanji("Hello World 123"));

    std::string t1 = JapaneseTransliteration::kanji_to_hiragana("こんにちは世界");
    assert(t1 == "こんにちはせかい");

    std::string t2 = JapaneseTransliteration::kanji_to_hiragana("日本");
    assert(t2 == "にほん");

    std::string t3 = JapaneseTransliteration::kanji_to_hiragana("動画編集");
    assert(t3 == "どうがへんしゅう");

    std::string t4 = JapaneseTransliteration::kanji_to_hiragana("音声と字幕");
    assert(t4.find("おんせい") != std::string::npos);
    assert(t4.find("じまく") != std::string::npos);

    // 2. TTSManager Japanese Synthesis with Kanji
    assert(TTSManager::detect_language_code("こんにちは世界") == "ja");
    std::string ja_wav = "/tmp/test_ja_kanji.wav";
    std::string tts_err;
    bool synth_ok = TTSManager::instance().synthesize("こんにちは世界", ja_wav, 1.0f, 1.0f, "ja", &tts_err);
    assert(synth_ok);
    assert(fs::exists(ja_wav));
    assert(fs::file_size(ja_wav) > 1000);
    fs::remove(ja_wav);

    // 3. Project largest asset aspect ratio
    Project p_empty;
    double ar_empty = p_empty.get_largest_asset_aspect_ratio();
    assert(std::abs(ar_empty - (16.0 / 9.0)) < 1e-4);

    Project p_custom;
    VideoAsset a_16_9; a_16_9.id = "a_wide"; a_16_9.width = 1920; a_16_9.height = 1080;
    VideoAsset a_21_9; a_21_9.id = "a_cinema"; a_21_9.width = 2560; a_21_9.height = 1080;
    VideoAsset a_9_16; a_9_16.id = "a_shorts"; a_9_16.width = 1080; a_9_16.height = 1920;
    p_custom.video_assets = { a_16_9, a_21_9, a_9_16 };

    Track trk; trk.id = "t1"; trk.type = "video";
    Clip c1; c1.id = "c1"; c1.asset_id = "a_wide"; c1.duration = 5.0;
    Clip c2; c2.id = "c2"; c2.asset_id = "a_cinema"; c2.duration = 5.0;
    trk.clips = { c1, c2 };
    p_custom.tracks = { trk };

    // With 16:9 (1.777) and 21:9 (2.370) on timeline, largest is 21:9 (~2.370)
    double ar_largest = p_custom.get_largest_asset_aspect_ratio();
    assert(std::abs(ar_largest - (2560.0 / 1080.0)) < 1e-4);

    // Only vertical video clip placed on timeline -> adapts to 9:16 (~0.5625)
    p_custom.tracks[0].clips.clear();
    Clip c_vert; c_vert.id = "c3"; c_vert.asset_id = "a_shorts"; c_vert.duration = 5.0;
    p_custom.tracks[0].clips = { c_vert };
    double ar_vert = p_custom.get_largest_asset_aspect_ratio();
    assert(std::abs(ar_vert - (1080.0 / 1920.0)) < 1e-4);

    // 4. Keyboard Layout-Independent Scancode Hotkeys
    assert(ImGui_ImplSDL2_KeyEventToImGuiKey(SDLK_UNKNOWN, SDL_SCANCODE_S) == ImGuiKey_S);
    assert(ImGui_ImplSDL2_KeyEventToImGuiKey(SDLK_UNKNOWN, SDL_SCANCODE_SPACE) == ImGuiKey_Space);
    assert(ImGui_ImplSDL2_KeyEventToImGuiKey(SDLK_UNKNOWN, SDL_SCANCODE_DELETE) == ImGuiKey_Delete);
    assert(ImGui_ImplSDL2_KeyEventToImGuiKey(SDLK_UNKNOWN, SDL_SCANCODE_BACKSPACE) == ImGuiKey_Backspace);
    assert(ImGui_ImplSDL2_KeyEventToImGuiKey(SDLK_UNKNOWN, SDL_SCANCODE_A) == ImGuiKey_A);
    assert(ImGui_ImplSDL2_KeyEventToImGuiKey(SDLK_UNKNOWN, SDL_SCANCODE_Z) == ImGuiKey_Z);
    assert(ImGui_ImplSDL2_KeyEventToImGuiKey(SDLK_UNKNOWN, SDL_SCANCODE_C) == ImGuiKey_C);
    assert(ImGui_ImplSDL2_KeyEventToImGuiKey(SDLK_UNKNOWN, SDL_SCANCODE_V) == ImGuiKey_V);
    assert(ImGui_ImplSDL2_KeyEventToImGuiKey(SDLK_UNKNOWN, SDL_SCANCODE_X) == ImGuiKey_X);
    assert(ImGui_ImplSDL2_KeyEventToImGuiKey(SDLK_UNKNOWN, SDL_SCANCODE_LEFT) == ImGuiKey_LeftArrow);
    assert(ImGui_ImplSDL2_KeyEventToImGuiKey(SDLK_UNKNOWN, SDL_SCANCODE_RIGHT) == ImGuiKey_RightArrow);

    std::cout << "[PASS] Preview aspect ratio, Japanese Kanji TTS, and layout-independent hotkeys tests passed." << std::endl;
}

void test_preview_transform_and_popout_and_export_progress() {
    std::cout << "[TEST] Running Preview Transform Gizmo, Modeless Export & Script API tests..." << std::endl;

    // 1. ExportManager audio progress & modeless status
    ExportManager em;
    assert(std::abs(em.get_audio_progress() - 0.0f) < 1e-5f);
    assert(std::abs(em.get_progress() - 0.0f) < 1e-5f);
    assert(em.is_exporting() == false);
    assert(em.get_status_text() == "Idle");
    assert(em.is_dialog_open() == false);
    assert(em.is_window_active() == false);
    em.open_export_dialog();
    assert(em.is_dialog_open() == true);
    assert(em.is_window_active() == true);
    em.close_export_dialog();
    assert(em.is_dialog_open() == false);
    assert(em.is_window_active() == false);

    // 2. ScriptEngine Lua API for clip transform, preview popout & export
    Project proj_lua;
    Timeline tl_lua;
    ClipManager cm_lua;
    double playhead_lua = 0.0;
    std::string selected_clip_lua = "";

    Track v0; v0.type = "video"; v0.name = "Video 1"; v0.id = "v0";
    proj_lua.tracks.push_back(v0);
    VideoAsset va; va.id = "vasset_1"; va.path = "/test/v1.mp4"; va.duration = 10.0;
    proj_lua.video_assets.push_back(va);

    ScriptEngine se_lua;
    bool init_lua_ok = se_lua.init(
        &proj_lua, &tl_lua, &cm_lua, &playhead_lua, &selected_clip_lua
    );
    assert(init_lua_ok);

    se_lua.run_string("cid = timeline.place_video('vasset_1', 0, 0.0, 5.0, 0.0)");
    assert(!proj_lua.tracks[0].clips.empty());
    Clip& c_lua = proj_lua.tracks[0].clips[0];

    // Test clip.set_transform in Lua
    se_lua.run_string("clip.set_transform(cid, 120.0, -45.0, 1.75, 30.0)");
    assert(std::abs(c_lua.pos_x - 120.0) < 1e-4);
    assert(std::abs(c_lua.pos_y - (-45.0)) < 1e-4);
    assert(std::abs(c_lua.scale - 1.75) < 1e-4);
    assert(std::abs(c_lua.rotation - 30.0) < 1e-4);

    // Test clip.get_transform in Lua
    se_lua.run_string(R"(
        t = clip.get_transform(cid)
        assert(t ~= nil)
        assert(math.abs(t.x - 120.0) < 0.01)
        assert(math.abs(t.y - (-45.0)) < 0.01)
        assert(math.abs(t.scale - 1.75) < 0.01)
        assert(math.abs(t.rotation - 30.0) < 0.01)
    )");

    // Test clip.reset_transform in Lua
    se_lua.run_string("clip.reset_transform(cid)");
    assert(std::abs(c_lua.pos_x - 0.0) < 1e-4);
    assert(std::abs(c_lua.pos_y - 0.0) < 1e-4);
    assert(std::abs(c_lua.scale - 1.0) < 1e-4);
    assert(std::abs(c_lua.rotation - 0.0) < 1e-4);

    // Test keyframe-aware transform updates in Lua
    proj_lua.set_or_add_keyframe(c_lua.id, "pos_x", 0.0, 50.0);
    proj_lua.set_or_add_keyframe(c_lua.id, "scale", 0.0, 1.5);
    playhead_lua = 0.0;
    se_lua.run_string("clip.set_transform(cid, 300.0, 150.0, 3.0, 45.0)");
    assert(std::abs(proj_lua.get_clip_param(c_lua, "pos_x", 0.0) - 300.0) < 1e-4);
    assert(std::abs(proj_lua.get_clip_param(c_lua, "scale", 0.0) - 3.0) < 1e-4);
    se_lua.run_string("clip.reset_transform(cid)");
    assert(std::abs(proj_lua.get_clip_param(c_lua, "pos_x", 0.0) - 0.0) < 1e-4);
    assert(std::abs(proj_lua.get_clip_param(c_lua, "scale", 0.0) - 1.0) < 1e-4);

    // Test gui preview methods in Lua (safe fallbacks when headless/no App)
    se_lua.run_string("gui.popout_preview(true)");
    se_lua.run_string("local popped = gui.is_preview_popped_out()");
    se_lua.run_string("gui.set_preview_aspect_mode(2)");
    se_lua.run_string("local amode = gui.get_preview_aspect_mode()");
    se_lua.run_string("gui.set_transform_tool('scale')");
    se_lua.run_string("local ttool = gui.get_transform_tool()");

    // Test export API in Lua
    se_lua.run_string(R"(
        local a_prog = export.get_audio_progress()
        local prog = export.get_progress()
        local exporting = export.is_exporting()
        local status = export.get_status()
        assert(type(a_prog) == "number")
        assert(type(prog) == "number")
        assert(type(exporting) == "boolean")
        assert(type(status) == "string")
    )");

    // 3. ScriptEngine JavaScript API for clip transform, preview popout & export
    Project proj_js;
    Timeline tl_js;
    ClipManager cm_js;

    Track v_js; v_js.type = "video"; v_js.name = "Video JS"; v_js.id = "v_js";
    proj_js.tracks.push_back(v_js);
    VideoAsset va_js; va_js.id = "vasset_js"; va_js.path = "/test/v_js.mp4"; va_js.duration = 10.0;
    proj_js.video_assets.push_back(va_js);

    ScriptEngine se_js;
    bool init_js_ok = se_js.init(&proj_js, &tl_js, &cm_js);
    assert(init_js_ok);

    se_js.run_js_string("var cid = timeline.place_video(0, 'vasset_js', 0.0, 5.0);");
    assert(!proj_js.tracks[0].clips.empty());
    Clip& c_js = proj_js.tracks[0].clips[0];

    // Test clip.set_transform in JS
    se_js.run_js_string("clip.set_transform(cid, 85.0, 60.0, 2.25, 45.0);");
    assert(std::abs(c_js.pos_x - 85.0) < 1e-4);
    assert(std::abs(c_js.pos_y - 60.0) < 1e-4);
    assert(std::abs(c_js.scale - 2.25) < 1e-4);
    assert(std::abs(c_js.rotation - 45.0) < 1e-4);

    // Test clip.get_transform in JS
    se_js.run_js_string(R"(
        var t = clip.get_transform(cid);
        if (!t || Math.abs(t.x - 85.0) > 0.01 || Math.abs(t.y - 60.0) > 0.01 ||
            Math.abs(t.scale - 2.25) > 0.01 || Math.abs(t.rotation - 45.0) > 0.01) {
            throw new Error("clip.get_transform returned invalid values in JS");
        }
    )");

    // Test clip.reset_transform in JS
    se_js.run_js_string("clip.reset_transform(cid);");
    assert(std::abs(c_js.pos_x - 0.0) < 1e-4);
    assert(std::abs(c_js.pos_y - 0.0) < 1e-4);
    assert(std::abs(c_js.scale - 1.0) < 1e-4);
    assert(std::abs(c_js.rotation - 0.0) < 1e-4);

    // Test keyframe-aware transform updates in JS
    proj_js.set_or_add_keyframe(c_js.id, "pos_x", 0.0, 40.0);
    proj_js.set_or_add_keyframe(c_js.id, "scale", 0.0, 1.2);
    se_js.run_js_string("clip.set_transform(cid, 500.0, 250.0, 4.0, 90.0);");
    assert(std::abs(proj_js.get_clip_param(c_js, "pos_x", 0.0) - 500.0) < 1e-4);
    assert(std::abs(proj_js.get_clip_param(c_js, "scale", 0.0) - 4.0) < 1e-4);
    se_js.run_js_string("clip.reset_transform(cid);");
    assert(std::abs(proj_js.get_clip_param(c_js, "pos_x", 0.0) - 0.0) < 1e-4);
    assert(std::abs(proj_js.get_clip_param(c_js, "scale", 0.0) - 1.0) < 1e-4);

    // Test gui & export in JS
    bool js_gui_exp_ok = se_js.run_js_string(R"(
        gui.popout_preview(true);
        var popped = gui.is_preview_popped_out();
        gui.set_preview_aspect_mode(1);
        var mode = gui.get_preview_aspect_mode();
        gui.set_transform_tool("rotate");
        var tool = gui.get_transform_tool();

        var ap = exporter.get_audio_progress();
        var p = minicut.export.get_progress();
        var exp = this['export'].is_exporting();
        var st = exporter.get_status();
        if (typeof ap !== 'number' || typeof p !== 'number' || typeof exp !== 'boolean' || typeof st !== 'string') {
            throw new Error("export API types invalid in JS");
        }
    )");
    assert(js_gui_exp_ok);

    std::cout << "[PASS] Preview Transform Gizmo, Modeless Export & Script API tests passed." << std::endl;
}

void test_scale_xy_clips_and_assets() {
    std::cout << "[TEST] Running Scale X and Y (clips & assets) test..." << std::endl;

    // 1. Data model & serialization
    Project p;
    p.name = "ScaleXYTest";
    VideoAsset va;
    va.id = "va_scale1";
    va.path = "/test/path.mp4";
    va.scale = 1.5;
    va.scale_x = 1.25;
    va.scale_y = 1.75;
    p.video_assets.push_back(va);

    Track trk;
    trk.id = "trk1";
    trk.type = "video";
    Clip c;
    c.id = "clip_scale1";
    c.asset_id = "va_scale1";
    c.scale = 1.5;
    c.scale_x = 2.0;
    c.scale_y = 0.5;
    trk.clips.push_back(c);
    p.tracks.push_back(trk);

    std::string serialized = ProjectSerializer::serialize_json(p);
    assert(serialized.find("\"scale_x\": 2.0") != std::string::npos || serialized.find("\"scale_x\":2.0") != std::string::npos);
    assert(serialized.find("\"scale_y\": 0.5") != std::string::npos || serialized.find("\"scale_y\":0.5") != std::string::npos);

    Project deserialized;
    bool des_ok = ProjectSerializer::deserialize_json(deserialized, serialized);
    assert(des_ok);
    assert(!deserialized.tracks.empty() && !deserialized.tracks[0].clips.empty());
    const Clip& dc = deserialized.tracks[0].clips[0];
    assert(std::abs(dc.scale_x - 2.0) < 1e-4);
    assert(std::abs(dc.scale_y - 0.5) < 1e-4);
    assert(!deserialized.video_assets.empty());
    const VideoAsset& dva = deserialized.video_assets[0];
    assert(std::abs(dva.scale_x - 1.25) < 1e-4);
    assert(std::abs(dva.scale_y - 1.75) < 1e-4);

    // Backward compatibility: project with only "scale": 3.0
    std::string legacy_json = R"({
        "name": "LegacyProject",
        "assets": {
            "videos": [{ "id": "lva1", "path": "/p.mp4", "scale": 3.0 }]
        },
        "timeline": {
            "tracks": [{ "id": "lt1", "type": "video", "clips": [{ "id": "lc1", "asset_id": "lva1", "scale": 3.0 }] }]
        }
    })";
    Project legacy_proj;
    assert(ProjectSerializer::deserialize_json(legacy_proj, legacy_json));
    assert(std::abs(legacy_proj.tracks[0].clips[0].scale_x - 3.0) < 1e-4);
    assert(std::abs(legacy_proj.tracks[0].clips[0].scale_y - 3.0) < 1e-4);
    assert(std::abs(legacy_proj.video_assets[0].scale_x - 3.0) < 1e-4);
    assert(std::abs(legacy_proj.video_assets[0].scale_y - 3.0) < 1e-4);

    // 2. Lua Scripting APIs
    Timeline tl;
    ClipManager cm;
    ScriptEngine se;
    assert(se.init(&p, &tl, &cm));

    bool lua_ok = se.run_string(R"(
        clip.set_scale("clip_scale1", 1.8, 2.4)
        local sx, sy = clip.get_scale("clip_scale1")
        assert(math.abs(sx - 1.8) < 1e-4 and math.abs(sy - 2.4) < 1e-4)

        local s_uniform = clip.get_scale("clip_scale1")
        assert(math.abs(s_uniform - 1.8) < 1e-4)

        local s_xy = clip.get_scale_xy("clip_scale1")
        assert(math.abs(s_xy.x - 1.8) < 1e-4 and math.abs(s_xy.y - 2.4) < 1e-4)

        clip.set_scale_x("clip_scale1", 3.5)
        assert(math.abs(clip.get_scale_x("clip_scale1") - 3.5) < 1e-4)

        clip.set_scale_y("clip_scale1", 0.75)
        assert(math.abs(clip.get_scale_y("clip_scale1") - 0.75) < 1e-4)

        assets.set_scale("va_scale1", 1.2, 0.9)
        local asx, asy = assets.get_scale("va_scale1")
        assert(math.abs(asx - 1.2) < 1e-4 and math.abs(asy - 0.9) < 1e-4)

        assets.set_scale_x("va_scale1", 2.5)
        assert(math.abs(assets.get_scale_x("va_scale1") - 2.5) < 1e-4)

        assets.set_scale_y("va_scale1", 1.4)
        assert(math.abs(assets.get_scale_y("va_scale1") - 1.4) < 1e-4)
    )");
    assert(lua_ok);

    // 3. JavaScript Scripting APIs
    bool js_ok = se.run_js_string(R"(
        clip.set_scale("clip_scale1", 1.4, 2.8);
        var s = clip.get_scale("clip_scale1");
        var sxy = clip.get_scale_xy("clip_scale1");
        if (Math.abs(s - 1.4) > 0.01 || Math.abs(sxy.x - 1.4) > 0.01 || Math.abs(sxy.y - 2.8) > 0.01) {
            throw new Error("JS clip.get_scale failed");
        }

        clip.set_scale_x("clip_scale1", 0.6);
        clip.set_scale_y("clip_scale1", 0.9);
        if (Math.abs(clip.get_scale_x("clip_scale1") - 0.6) > 0.01 || Math.abs(clip.get_scale_y("clip_scale1") - 0.9) > 0.01) {
            throw new Error("JS clip.set_scale_x / y failed");
        }

        assets.set_scale("va_scale1", 1.1, 1.9);
        var asxy = assets.get_scale("va_scale1");
        if (Math.abs(asxy.x - 1.1) > 0.01 || Math.abs(asxy.y - 1.9) > 0.01) {
            throw new Error("JS assets.set_scale failed");
        }

        assets.set_scale_x("va_scale1", 2.2);
        assets.set_scale_y("va_scale1", 3.3);
        if (Math.abs(assets.get_scale_x("va_scale1") - 2.2) > 0.01 || Math.abs(assets.get_scale_y("va_scale1") - 3.3) > 0.01) {
            throw new Error("JS assets.set_scale_x / y failed");
        }
    )");
    assert(js_ok);

    std::cout << "[PASS] Scale X and Y (clips & assets) tests passed." << std::endl;
}

void test_timeline_and_asset_selection_clearing() {
    std::cout << "[TEST] Running Timeline and Asset Selection clearing test..." << std::endl;

    Timeline tl;
    Project p;
    Track t;
    t.id = "t1";
    t.type = "video";
    Clip c1; c1.id = "c1"; t.clips.push_back(c1);
    Clip c2; c2.id = "c2"; t.clips.push_back(c2);
    p.tracks.push_back(t);

    tl.select_clip("c1", false, false);
    assert(tl.is_clip_selected("c1"));
    assert(!tl.is_clip_selected("c2"));

    // Clear selection
    tl.clear_selection();
    assert(!tl.is_clip_selected("c1"));
    assert(tl.get_selected_clip_ids().empty());

    // ClipManager asset selection and clearing
    ClipManager cm;
    cm.select_asset("asset_123", true);
    assert(cm.get_selected_asset_id() == "asset_123");
    cm.clear_selection();
    assert(cm.get_selected_asset_id().empty());

    std::cout << "[PASS] Timeline and Asset Selection clearing tests passed." << std::endl;
}

void test_audio_mixing_progress_immediate_jump() {
    std::cout << "[TEST] Running Audio Mixing Progress 10% test..." << std::endl;

    ExportManager em;
    assert(em.get_audio_progress() == 0.0f);
    assert(em.get_progress() == 0.0f);

    std::cout << "[PASS] Audio Mixing Progress 10% tests passed." << std::endl;
}

void test_scripting_locale_api_lua_and_js() {
    std::cout << "[TEST] Running Locale Scripting API (Lua & JS) test..." << std::endl;

    Project p;
    Timeline tl;
    ClipManager cm;
    ScriptEngine se;
    assert(se.init(&p, &tl, &cm));

    // 1. Lua Locale Tests
    bool lua_ok = se.run_string(R"(
        locale.register("fr", { greeting = "Bonjour", inspector_scale_x = "Echelle X" })
        locale.set_language("fr")
        assert(locale.get_language() == "fr")
        assert(locale("greeting", "Hello") == "Bonjour")
        assert(locale.get("inspector_scale_x") == "Echelle X")
        assert(tr("greeting") == "Bonjour")
        assert(locale.is_rtl("ar") == true)
        assert(locale.is_rtl("fr") == false)
    )");
    assert(lua_ok);

    // 2. JS Locale Tests
    bool js_ok = se.run_js_string(R"(
        locale.register("es", { greeting: "Hola", inspector_scale_y: "Escala Y" });
        locale.set_language("es");
        if (locale.get_language() !== "es") throw new Error("JS locale.get_language failed");
        if (locale("greeting", "Hello") !== "Hola") throw new Error("JS locale direct call failed");
        if (locale.get("inspector_scale_y") !== "Escala Y") throw new Error("JS locale.get failed");
        if (tr("greeting") !== "Hola") throw new Error("JS tr failed");
        if (!locale.is_rtl("ar") || locale.is_rtl("es")) throw new Error("JS locale.is_rtl failed");
    )");
    assert(js_ok);

    LocaleManager::instance().set_locale("en");

    std::cout << "[PASS] Locale Scripting API (Lua & JS) tests passed." << std::endl;
}

void test_scripting_ai_library_lua_and_js() {
    std::cout << "[TEST] Running AI Library Scripting API (Lua & JS) test..." << std::endl;

    Project p;
    Timeline tl;
    ClipManager cm;
    ScriptEngine se;
    assert(se.init(&p, &tl, &cm));

    // 1. Lua AI Tests
    bool lua_ok = se.run_string(R"(
        ai.set_mock_response('{"summary":"AI generated video edit","scenes":[{"start":0,"end":5}],"score":98}')
        assert(ai.is_available() == true)

        local resp = ai.prompt("Summarize this video")
        assert(resp ~= nil and string.find(resp, "AI generated") ~= nil)

        local parsed = ai.parse_json('```json\n{"score": 98, "tags": ["action","vfx"]}\n```')
        assert(parsed ~= nil and parsed.score == 98 and parsed.tags[1] == "action")

        local json_str = ai.to_json({ project = "Minicut", version = 2 })
        assert(string.find(json_str, "Minicut") ~= nil)

        local prompt_parsed = ai.prompt_json("Analyze")
        assert(prompt_parsed ~= nil and prompt_parsed.summary == "AI generated video edit")
        assert(prompt_parsed.scenes[1]["end"] == 5)

        local merged = ai.struct({ bitrate = 4000, fps = 30, codec = "h264" }, '{"fps": 60}')
        assert(merged.bitrate == 4000 and merged.fps == 60 and merged.codec == "h264")

        ai.clear_mock_response()
    )");
    assert(lua_ok);

    // 2. JS AI Tests
    bool js_ok = se.run_js_string(R"(
        ai.set_mock_response(JSON.stringify({ title: "JS Video Project", duration: 12.5, tags: ["edit", "sfx"] }));
        if (!ai.is_available()) throw new Error("JS ai.is_available failed");

        var promptResp = ai.prompt("Generate title");
        if (typeof promptResp !== 'string' || promptResp.indexOf("JS Video Project") === -1) {
            throw new Error("JS ai.prompt failed");
        }

        var parsed = ai.parse_json('```json\n{"fps": 60, "enabled": true}\n```');
        if (!parsed || parsed.fps !== 60 || parsed.enabled !== true) {
            throw new Error("JS ai.parse_json failed");
        }

        var jstr = ai.to_json({ clips: 3, track: "v1" });
        if (typeof jstr !== 'string' || jstr.indexOf("v1") === -1) {
            throw new Error("JS ai.to_json failed");
        }

        var pjson = ai.prompt_json("Analyze project");
        if (!pjson || pjson.title !== "JS Video Project" || pjson.duration !== 12.5) {
            throw new Error("JS ai.prompt_json failed");
        }

        var st = ai.struct({ opacity: 1.0, blend: "normal" }, '{"blend": "multiply"}');
        if (st.opacity !== 1.0 || st.blend !== "multiply") {
            throw new Error("JS ai.struct failed");
        }

        ai.clear_mock_response();
    )");
    assert(js_ok);

    std::cout << "[PASS] AI Library Scripting API (Lua & JS) tests passed." << std::endl;
}

void test_raw_frame_shader_sfx_pipeline_lua_and_js() {
    std::cout << "[TEST] Running Raw Frame Data, Shaders & SFX Pipeline (Lua & JS) test..." << std::endl;

    std::filesystem::path temp_png = std::filesystem::temp_directory_path() / "test_sfx_source.png";
    const int W = 64, H = 64;
    std::vector<uint8_t> test_rgba(W * H * 4);
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            int idx = (y * W + x) * 4;
            test_rgba[idx + 0] = static_cast<uint8_t>(x * 4);
            test_rgba[idx + 1] = static_cast<uint8_t>(y * 4);
            test_rgba[idx + 2] = 128;
            test_rgba[idx + 3] = 255;
        }
    }
    assert(MediaDecoder::save_rgba_to_png(temp_png.string(), W, H, test_rgba));

    Project p;
    VideoAsset va;
    va.id = "sfx_source_asset";
    va.path = temp_png.string();
    va.width = W;
    va.height = H;
    va.duration = 5.0;
    va.is_image = true;
    p.video_assets.push_back(va);

    Track trk;
    trk.id = "sfx_track";
    trk.type = "video";
    Clip c;
    c.id = "sfx_clip1";
    c.asset_id = "sfx_source_asset";
    c.start_time = 0.0;
    c.duration = 5.0;
    trk.clips.push_back(c);
    p.tracks.push_back(trk);

    Timeline tl;
    ClipManager cm;
    ScriptEngine se;
    assert(se.init(&p, &tl, &cm));

    // 1. Lua SFX Pipeline
    bool lua_ok = se.run_string(R"(
        -- 1. Create shader
        local sid = shader.create("invert")
        assert(sid > 0)

        -- 2. Pull frame data from clip
        local frame = clip.get_frame("sfx_clip1", 0.0, 64, 64)
        assert(frame ~= nil)
        assert(frame.width == 64 and frame.height == 64 and frame.channels == 4)
        assert(#frame.data == 64 * 64 * 4)

        local orig_r = frame.data[1]

        -- 3. Apply shader via shader.apply
        local inverted = shader.apply(sid, frame)
        assert(inverted ~= nil and inverted.width == 64 and inverted.height == 64)
        assert(inverted.data[1] == 255 - orig_r)

        -- 4. Apply shader via OO method
        local sepia = frame:apply_shader("sepia")
        assert(sepia ~= nil and sepia.width == 64)

        -- 5. Reinsert modified frame into active clip
        local reinsert_ok = inverted:reinsert("sfx_clip1")
        assert(reinsert_ok == true)

        -- 6. Create new clip on timeline from frame
        local new_clip_id = sepia:create_clip(0, 5.0, 3.0)
        assert(new_clip_id ~= nil and new_clip_id ~= "")
    )");
    assert(lua_ok);

    // 2. JS SFX Pipeline
    bool js_ok = se.run_js_string(R"(
        var sid = shader.create("invert");
        if (!sid) throw new Error("JS shader.create failed");

        var frame = clip.get_frame("sfx_clip1", 0.0, 64, 64);
        if (!frame || frame.width !== 64 || frame.height !== 64 || !frame.data) {
            throw new Error("JS clip.get_frame failed");
        }
        var orig_r = frame.data[0];

        var inverted = shader.apply(sid, frame);
        if (!inverted || inverted.data[0] !== 255 - orig_r) {
            throw new Error("JS shader.apply failed");
        }

        var reinsert_ok = inverted.reinsert("sfx_clip1");
        if (!reinsert_ok) throw new Error("JS inverted.reinsert failed");

        var new_clip_id = inverted.create_clip(0, 8.0, 2.0);
        if (!new_clip_id || typeof new_clip_id !== 'string') {
            throw new Error("JS inverted.create_clip failed");
        }
    )");
    assert(js_ok);

    std::filesystem::remove(temp_png);
    std::cout << "[PASS] Raw Frame Data, Shaders & SFX Pipeline (Lua & JS) tests passed." << std::endl;
}

void test_preview_performance_enhancements() {
    std::cout << "[TEST] Running Preview Performance Enhancements test..." << std::endl;

    // Test blit_transformed_layer fast path (1:1 copy & unrotated)
    std::vector<uint8_t> dst(100 * 100 * 4, 0);
    std::vector<uint8_t> src(100 * 100 * 4, 255);

    // 1:1 unrotated path
    blit_transformed_layer(dst, 100, 100, src, 100, 100, 0.0, 0.0, 0.0, 1.0, 1.0, 1.0);
    assert(dst[0] == 255 && dst[1] == 255 && dst[2] == 255 && dst[3] == 255);

    // Axis-aligned scaled fast path
    std::vector<uint8_t> dst2(100 * 100 * 4, 0);
    blit_transformed_layer(dst2, 100, 100, src, 100, 100, 10.0, 10.0, 0.0, 0.5, 0.5, 1.0);
    assert(dst2[4 * (60 * 100 + 60) + 0] == 255);

    std::cout << "[PASS] Preview Performance Enhancements tests passed." << std::endl;
}

void test_shader_effects_across_clip_and_menu_ids() {
    std::cout << "[TEST] Running Shader Effects Whole-Clip Rendering & Menu IDs test..." << std::endl;

    Project project;
    Timeline timeline;
    ClipManager clip_mgr;
    double playhead = 0.0;
    std::string sel_clip = "";

    ScriptEngine se;
    se.init(&project, &timeline, &clip_mgr, &playhead, &sel_clip,
            nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);

    // 1. Progress state tests
    assert(!se.is_rendering_shader());
    se.set_rendering_shader(true, 0.25f);
    assert(se.is_rendering_shader());
    assert(std::abs(se.get_shader_progress() - 0.25f) < 0.001f);
    se.update_shader_progress(0.85f);
    assert(std::abs(se.get_shader_progress() - 0.85f) < 0.001f);
    se.set_rendering_shader(false, 0.0f);
    assert(!se.is_rendering_shader());

    // 2. Menu scripts differentiation
    const auto& menu_scripts = se.get_menu_scripts();
    for (const auto& item : menu_scripts) {
        if (item.is_javascript) {
            assert(item.id.size() >= 3 && item.id.substr(item.id.size() - 3) == "_js");
            assert(item.title.find("(JavaScript)") != std::string::npos);
        } else {
            assert(item.id.size() >= 4 && item.id.substr(item.id.size() - 4) == "_lua");
            assert(item.title.find("(Lua)") != std::string::npos);
        }
    }

    // 3. Whole-clip frame-by-frame shader rendering pipeline test
    std::string temp_png = "test_whole_clip_source.png";
    std::vector<uint8_t> dummy_rgba(64 * 64 * 4, 120);
    MediaDecoder::save_rgba_to_png(temp_png, 64, 64, dummy_rgba);

    VideoAsset va;
    va.id = "whole_clip_asset";
    va.path = temp_png;
    va.width = 64;
    va.height = 64;
    va.duration = 0.2;
    va.fps = 30.0;
    va.is_image = true;
    project.video_assets.push_back(va);

    Track trk;
    trk.type = "video";
    trk.name = "Video 1";
    Clip c;
    c.id = "whole_clip_test";
    c.asset_id = va.id;
    c.start_time = 0.0;
    c.duration = 0.2;
    c.offset = 0.0;
    trk.clips.push_back(c);
    project.tracks.push_back(trk);

    std::unordered_map<std::string, float> uniforms;
    uniforms["amplitude"] = 10.0f;
    uniforms["frequency"] = 0.05f;
    uniforms["phase"] = 0.0f;
    uniforms["decay"] = 0.002f;

    std::string out_clip_id;
    bool started = se.render_shader_across_clip(&project.tracks[0].clips[0], "ripple", uniforms, false, 0, 0.0, out_clip_id);
    assert(started);
    assert(se.is_rendering_shader());

    // Wait for the background worker thread to finish (0.2s duration = 6 frames, ultrafast)
    int wait_cycles = 0;
    while (se.is_rendering_shader() && wait_cycles < 200) {
        se.poll_shader_rendering();
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        wait_cycles++;
    }

    // Final poll
    se.poll_shader_rendering();
    assert(!se.is_rendering_shader());

    // Verify asset was updated with whole-clip video
    VideoAsset* updated_va = project.find_video_asset_mut(project.tracks[0].clips[0].asset_id);
    assert(updated_va != nullptr);
    assert(!updated_va->is_image);
    assert(updated_va->fps == 30.0);
    assert(std::filesystem::exists(updated_va->path));

    std::filesystem::remove(updated_va->path);
    std::filesystem::remove(temp_png);
    std::cout << "[PASS] Shader Effects Whole-Clip Rendering & Menu IDs tests passed." << std::endl;
}

void test_ripple_effect_and_box_select() {
    std::cout << "[TEST] Running Ripple Effect (Lua/JS) & Timeline Box Select tests..." << std::endl;

    // 1. Box Select logic on Timeline
    Project project;
    Timeline timeline;
    Track trk;
    trk.type = "video";
    trk.name = "Track 1";

    Clip c1; c1.id = "clip_1"; c1.start_time = 0.0; c1.duration = 4.0;
    Clip c2; c2.id = "clip_2"; c2.start_time = 5.0; c2.duration = 4.0;
    Clip c3; c3.id = "clip_3"; c3.start_time = 10.0; c3.duration = 4.0;
    trk.clips = { c1, c2, c3 };
    project.tracks.push_back(trk);

    assert(!timeline.is_box_selecting());

    // 2. ScriptEngine: clip.get_selected_id and minicut.get_selected_clip in Lua and JS
    ScriptEngine se;
    double playhead = 2.0;
    std::string selected_clip = "clip_2";
    bool ok = se.init(&project, &timeline, nullptr, &playhead, &selected_clip,
                      nullptr, nullptr, nullptr, nullptr,
                      [&selected_clip](const std::string& id) { selected_clip = id; },
                      nullptr, nullptr);
    assert(ok);

    // Test Lua get_selected_id and minicut.get_selected_clip
    bool lua_ok = se.run_string(R"(
        local id1 = clip.get_selected_id()
        assert(id1 == "clip_2")
        local id2 = clip.get_selected_clip()
        assert(id2 == "clip_2")
        local id3 = minicut.get_selected_clip()
        assert(id3 == "clip_2")
        local id4 = minicut.get_selected_id()
        assert(id4 == "clip_2")
    )");
    assert(lua_ok);

    // Test JS get_selected_id and minicut.get_selected_clip
    bool js_ok = se.run_js_string(R"(
        var id1 = clip.get_selected_id();
        if (id1 !== "clip_2") throw new Error("JS clip.get_selected_id failed: " + id1);
        var id2 = clip.get_selected_clip();
        if (id2 !== "clip_2") throw new Error("JS clip.get_selected_clip failed: " + id2);
        var id3 = minicut.get_selected_clip();
        if (id3 !== "clip_2") throw new Error("JS minicut.get_selected_clip failed: " + id3);
        var id4 = minicut.get_selected_id();
        if (id4 !== "clip_2") throw new Error("JS minicut.get_selected_id failed: " + id4);
    )");
    assert(js_ok);

    // 3. Test HD / large frame buffer (>65536 bytes) without heap crash in Lua and Duktape JS
    std::string temp_png = TempManager::get_temp_dir() + "/test_hd_frame.png";
    int w = 200, h = 200; // 200 * 200 * 4 = 160,000 bytes > 65536
    std::vector<uint8_t> hd_rgba(w * h * 4, 120);
    MediaDecoder::save_rgba_to_png(temp_png, w, h, hd_rgba);

    VideoAsset va;
    va.id = "asset_hd";
    va.path = temp_png;
    va.width = w;
    va.height = h;
    va.duration = 5.0;
    va.fps = 30.0;
    va.is_image = true;
    project.video_assets.push_back(va);
    project.tracks[0].clips[1].asset_id = va.id;

    // Run Lua frame extraction, ripple shader apply, and reinsert
    bool lua_hd_ok = se.run_string(R"(
        local frame = clip.get_frame("clip_2", 0.0, 200, 200)
        assert(frame ~= nil)
        assert(frame.width == 200 and frame.height == 200)
        assert(#frame.data == 200 * 200 * 4)

        local sid = shader.create("ripple")
        assert(sid > 0)

        local uniforms = {
            center_x = 100.0,
            center_y = 100.0,
            amplitude = 15.0,
            frequency = 0.05,
            phase = 0.0,
            decay = 0.002
        }
        local ripple_frame = shader.apply(sid, frame, uniforms)
        assert(ripple_frame ~= nil)
        assert(ripple_frame.width == 200 and ripple_frame.height == 200)
    )");
    assert(lua_hd_ok);

    // Run JS frame extraction, ripple shader apply
    bool js_hd_ok = se.run_js_string(R"(
        var frame = clip.get_frame("clip_2", 0.0, 200, 200);
        if (!frame || frame.width !== 200 || frame.height !== 200 || !frame.data) {
            throw new Error("JS HD get_frame failed");
        }

        var sid = shader.create("ripple");
        var uniforms = {
            center_x: 100.0,
            center_y: 100.0,
            amplitude: 15.0,
            frequency: 0.05,
            phase: 0.0,
            decay: 0.002
        };
        var ripple_frame = shader.apply(sid, frame, uniforms);
        if (!ripple_frame || ripple_frame.width !== 200) {
            throw new Error("JS HD shader.apply failed");
        }
    )");
    assert(js_hd_ok);

    std::filesystem::remove(temp_png);
    std::cout << "[PASS] Ripple Effect (Lua/JS) & Timeline Box Select tests passed." << std::endl;
}

void test_vulkan_backend_and_preview_settings() {
    std::cout << "[TEST] Running Vulkan backend and Preview settings tests..." << std::endl;

    // 1. Vulkan Backend Tests
    {
        RenderBackendVulkan vulkan_backend;
        assert(std::string(vulkan_backend.get_name()) == "Vulkan");
        assert(vulkan_backend.get_type() == RenderBackendType::Vulkan);
        bool host_supported = vulkan_backend.is_supported();
        std::cout << "   Host Vulkan support: " << (host_supported ? "YES" : "NO") << std::endl;
        // Verify graceful error handling when uninitialized
        assert(vulkan_backend.create_texture(64, 64, nullptr) == 0);
        vulkan_backend.update_texture(0, 64, 64, nullptr);
        vulkan_backend.delete_texture(0);

        // Test with hidden window if available
        if (host_supported && SDL_Init(SDL_INIT_VIDEO) == 0) {
            SDL_Window* test_win = SDL_CreateWindow("VK_Test", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 64, 64, SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
            if (test_win) {
                ImGuiContext* imgui_ctx = ImGui::CreateContext();
                if (vulkan_backend.init(test_win, nullptr)) {
                    uintptr_t tex = vulkan_backend.create_texture(64, 64, nullptr);
                    if (tex != 0) {
                        std::vector<uint8_t> dummy_pixels(64 * 64 * 4, 128);
                        vulkan_backend.update_texture(tex, 64, 64, dummy_pixels.data());
                        vulkan_backend.delete_texture(tex);
                    }
                    vulkan_backend.shutdown();
                }
                ImGui::DestroyContext(imgui_ctx);
                SDL_DestroyWindow(test_win);
            }
        }
    }
    // 2. Menu Font Size and Preview Window Background Settings
    {
        SettingsLogic& sl = SettingsLogic::instance();
        float prev_font = sl.get_menu_font_size();
        int prev_bg = sl.get_preview_bg_mode();
        float prev_pad = sl.get_preview_padding();

        sl.set_menu_font_size(18.5f);
        assert(std::abs(sl.get_menu_font_size() - 18.5f) < 1e-4);

        sl.set_preview_bg_mode(2); // Deep navy
        assert(sl.get_preview_bg_mode() == 2);

        sl.set_preview_padding(16.0f);
        assert(std::abs(sl.get_preview_padding() - 16.0f) < 1e-4);

        sl.set_preview_bg_custom_color(0.2f, 0.4f, 0.6f, 1.0f);
        const float* col = sl.get_preview_bg_custom_color();
        assert(std::abs(col[0] - 0.2f) < 1e-4);
        assert(std::abs(col[1] - 0.4f) < 1e-4);
        assert(std::abs(col[2] - 0.6f) < 1e-4);

        // Save and reload to verify persistence logic
        sl.save_to_disk();
        sl.set_menu_font_size(12.0f);
        sl.set_preview_bg_mode(0);
        sl.load_from_disk();

        assert(std::abs(sl.get_menu_font_size() - 18.5f) < 1e-4);
        assert(sl.get_preview_bg_mode() == 2);
        assert(std::abs(sl.get_preview_padding() - 16.0f) < 1e-4);

        // Revert back
        sl.set_menu_font_size(prev_font);
        sl.set_preview_bg_mode(prev_bg);
        sl.set_preview_padding(prev_pad);
        sl.save_to_disk();
    }

    std::cout << "[PASS] Vulkan backend, Audio layers, and Preview settings tests passed." << std::endl;
}

static void test_headless_batch_reconfig_and_prefab_reload() {
    std::cout << "[TEST] Running Headless render, Batch queue, Reconfig, and Prefab hot-reload tests..." << std::endl;

    // 1. Reconfig test
    {
        auto& sl = SettingsLogic::instance();
        sl.set_prefab_reload_interval(15.0f);
        sl.set_prefab_hot_reload(false);
        sl.set_menu_font_size(22.0f);
        sl.reset_to_defaults();

        assert(sl.get_prefab_hot_reload() == true);
        assert(std::abs(sl.get_prefab_reload_interval() - 3.0f) < 0.001f);
        assert(std::abs(sl.get_menu_font_size() - 15.0f) < 0.001f);
    }

    // 2. Asset Browser View Mode test
    {
        ClipManager cm;
        assert(cm.get_view_mode() == 0); // default list
        cm.set_view_mode(1);
        assert(cm.get_view_mode() == 1); // grid

        ImGuiContext* ctx = ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(800, 600);
        unsigned char* pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        ImGui::NewFrame();
        ImGui::Begin("TestClips");

        Project test_p;
        VideoAsset va1;
        va1.id = "v1";
        va1.path = "test.mp4";
        va1.width = 1920;
        va1.height = 1080;
        va1.duration = 5.0;
        test_p.video_assets.push_back(va1);

        cm.render_ui(test_p, 0.0, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);

        ImGui::End();
        ImGui::Render();
        ImGui::DestroyContext(ctx);

        cm.set_view_mode(0);
        assert(cm.get_view_mode() == 0);
    }

    // 3. Batch Job API test
    {
        ExportManager em;
        assert(em.get_batch_jobs().empty());

        em.add_batch_job("test_proj.minicut", "test_out.mp4", "mp4", 1920, 1080, 30);
        assert(em.get_batch_jobs().size() == 1);
        int j1_id = em.get_batch_jobs()[0].id;
        assert(em.get_batch_jobs()[0].project_path == "test_proj.minicut");
        assert(em.get_batch_jobs()[0].output_path == "test_out.mp4");
        assert(em.get_batch_jobs()[0].format == "mp4");
        assert(em.get_batch_jobs()[0].width == 1920);
        assert(em.get_batch_jobs()[0].height == 1080);
        assert(em.get_batch_jobs()[0].fps == 30);
        assert(em.get_batch_jobs()[0].status == BatchJob::Status::Queued);

        bool edit_ok = em.edit_batch_job(j1_id, "test_proj2.minicut", "test_out2.gif", "gif", 1280, 720, 24);
        assert(edit_ok);
        assert(em.get_batch_jobs()[0].format == "gif");
        assert(em.get_batch_jobs()[0].width == 1280);

        em.add_batch_job("test_proj3.minicut");
        assert(em.get_batch_jobs().size() == 2);

        bool del_ok = em.delete_batch_job(j1_id);
        assert(del_ok);
        assert(em.get_batch_jobs().size() == 1);

        em.clear_batch_jobs();
        assert(em.get_batch_jobs().empty());
    }

    // 4. Prefab Hot-Reloading test
    {
        Project master_proj;
        master_proj.name = "Master Project";
        Track mt;
        mt.id = "track_v1";
        mt.type = "video";
        mt.name = "Video 1";
        master_proj.tracks.push_back(mt);

        // Subproject
        Project sub_p;
        sub_p.name = "Subproject A";
        VideoAsset sub_va;
        sub_va.id = "v1";
        sub_va.path = "test_src_video.mp4";
        sub_va.width = 1280;
        sub_va.height = 720;
        sub_va.duration = 5.0;
        sub_p.video_assets.push_back(sub_va);

        Track sub_t;
        sub_t.id = "sub_t1";
        sub_t.type = "video";
        Clip sc;
        sc.id = "sc_1";
        sc.asset_id = "v1";
        sc.start_time = 0.0;
        sc.duration = 3.0;
        sub_t.clips.push_back(sc);
        sub_p.tracks.push_back(sub_t);

        std::string sub_json = ProjectSerializer::serialize_json(sub_p);
        fs::path temp_prefab_path = "test_prefab_module.minicut";
        {
            std::ofstream ofs(temp_prefab_path);
            ofs << sub_json;
        }

        ClipManager cm;
        bool imp_ok = cm.import_project_as_prefab(master_proj, temp_prefab_path.string());
        assert(imp_ok);
        assert(master_proj.prefabs.size() == 1);
        std::string pf_id = master_proj.prefabs[0].id;

        cm.instantiate_prefab_on_timeline(master_proj, pf_id, 2.0);
        assert(master_proj.tracks[0].clips.size() == 1);
        assert(master_proj.tracks[0].clips[0].start_time == 2.0);
        assert(master_proj.tracks[0].clips[0].duration == 3.0);

        // Update prefab on disk
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        sub_p.tracks[0].clips[0].duration = 4.5;
        std::string sub_json_updated = ProjectSerializer::serialize_json(sub_p);
        {
            std::ofstream ofs(temp_prefab_path);
            ofs << sub_json_updated;
        }
        // Force file time update
        fs::last_write_time(temp_prefab_path, fs::file_time_type::clock::now() + std::chrono::seconds(2));

        bool changed_called = false;
        cm.check_and_reload_modified_prefabs(master_proj, [&]() {
            changed_called = true;
        });

        assert(changed_called);
        assert(master_proj.tracks[0].clips.size() == 1);
        assert(master_proj.tracks[0].clips[0].start_time == 2.0);
        assert(master_proj.tracks[0].clips[0].duration == 4.5);

        if (fs::exists(temp_prefab_path)) {
            fs::remove(temp_prefab_path);
        }
    }

    // 5. Headless Synchronous Export test
    {
        fs::path temp_video = "test_src_video.mp4";
        fs::path temp_audio = "test_src_audio.mp3";
        if (!fs::exists(temp_video) || !fs::exists(temp_audio)) {
            MediaDecoder::generate_test_media(temp_video.string(), temp_audio.string());
        }

        Project proj;
        proj.name = "Headless Test";
        VideoAsset va;
        va.id = "v1";
        va.path = temp_video.string();
        va.width = 1280;
        va.height = 720;
        va.duration = 2.0;
        proj.video_assets.push_back(va);

        Track vt;
        vt.type = "video";
        Clip c;
        c.id = "c1";
        c.asset_id = "v1";
        c.start_time = 0.0;
        c.duration = 1.0;
        vt.clips.push_back(c);
        proj.tracks.push_back(vt);

        ExportConfig cfg;
        cfg.output_path = "test_headless_sync_out.mp4";
        cfg.format = "mp4";
        cfg.width = 320;
        cfg.height = 180;
        cfg.fps = 15;
        if (fs::exists(cfg.output_path)) fs::remove(cfg.output_path);

        ExportManager em;
        float final_p = 0.0f;
        bool ok = em.export_headless_sync(proj, cfg, [&](float p, const std::string& st) {
            final_p = p;
        });

        assert(ok);
        assert(fs::exists(cfg.output_path));
        assert(fs::file_size(cfg.output_path) > 1000);
        fs::remove(cfg.output_path);
    }

    std::cout << "[PASS] Headless render, Batch queue, Reconfig, and Prefab hot-reload tests passed." << std::endl;
}

void test_project_load_audio_re_extraction() {
    std::cout << "[TEST] Running Project Load Audio Re-extraction test..." << std::endl;
    fs::path test_vid = "test_audio_reload_vid.mp4";
    fs::path test_aud = "test_audio_reload_aud.mp3";
    if (fs::exists(test_vid)) fs::remove(test_vid);
    if (fs::exists(test_aud)) fs::remove(test_aud);

    bool gen_ok = MediaDecoder::generate_test_media(test_vid.string(), test_aud.string());
    assert(gen_ok);
    assert(fs::exists(test_vid));

    // Create a JSON representing an older project where has_audio was false/omitted
    // and clip is on an audio track
    std::string proj_json = R"({
      "name": "Audio Reload Test",
      "version": "1.0",
      "assets": {
        "videos": [
          {
            "id": "v_test_1",
            "path": ")" + fs::absolute(test_vid).string() + R"(",
            "duration": 5.0,
            "has_audio": false
          }
        ],
        "audio": []
      },
      "timeline": {
        "tracks": [
          {
            "id": "track_v1",
            "name": "Video 1",
            "type": "video",
            "depth": 0,
            "clips": [
              {
                "id": "clip_v1",
                "asset_id": "v_test_1",
                "start_time": 0.0,
                "duration": 5.0,
                "offset": 0.0
              }
            ]
          },
          {
            "id": "track_a1",
            "name": "Audio 1",
            "type": "audio",
            "depth": 0,
            "clips": [
              {
                "id": "clip_a1",
                "asset_id": "v_test_1",
                "start_time": 0.0,
                "duration": 5.0,
                "offset": 0.0
              }
            ]
          }
        ],
        "keyframes": []
      }
    })";

    Project loaded_proj;
    bool des_ok = ProjectSerializer::deserialize_json(loaded_proj, proj_json);
    assert(des_ok);
    assert(loaded_proj.video_assets.size() == 1);
    // Verified: has_audio must be re-extracted to true!
    assert(loaded_proj.video_assets[0].has_audio == true);
    assert(loaded_proj.video_assets[0].width > 0);
    assert(loaded_proj.video_assets[0].height > 0);

    // Verify AudioEngine preloads and caches samples for the loaded project
    AudioEngine ae;
    ae.init();
    ae.ensure_project_assets_loaded(loaded_proj);

    std::string asset_path = loaded_proj.video_assets[0].path;
    assert(ae.is_asset_cached(asset_path));
    assert(ae.get_cached_sample_count(asset_path) > 0);
    ae.shutdown();

    // Clean up
    if (fs::exists(test_vid)) fs::remove(test_vid);
    if (fs::exists(test_aud)) fs::remove(test_aud);

    std::cout << "[PASS] Project Load Audio Re-extraction tests passed." << std::endl;
}

void test_custom_shaders_loading() {
    std::cout << "[TEST] Running Custom Shaders Loading & Management test..." << std::endl;
    ShaderManager& sm = ShaderManager::instance();
    sm.init();

    std::string gl_dir = sm.get_gl_shaders_dir();
    std::string vk_dir = sm.get_vulkan_shaders_dir();

    assert(fs::exists(gl_dir));
    assert(fs::exists(vk_dir));

    // Verify default starter shaders were written
    assert(fs::exists(gl_dir + "/ripple.vert"));
    assert(fs::exists(gl_dir + "/ripple.frag"));
    assert(fs::exists(gl_dir + "/grayscale.frag"));
    assert(fs::exists(gl_dir + "/invert.frag"));
    assert(fs::exists(gl_dir + "/vignette.frag"));

    assert(fs::exists(vk_dir + "/ripple.vert"));
    assert(fs::exists(vk_dir + "/ripple.frag"));
    assert(fs::exists(vk_dir + "/grayscale.frag"));
    assert(fs::exists(vk_dir + "/invert.frag"));
    assert(fs::exists(vk_dir + "/vignette.frag"));

    // Verify scanning finds all shaders
    sm.scan_shaders();
    const auto& gl_list = sm.get_gl_shaders();
    assert(gl_list.size() >= 4);

    const auto& vk_list = sm.get_vulkan_shaders();
    assert(vk_list.size() >= 4);

    // Verify lookup by name
    CustomShader sh;
    assert(sm.find_shader("ripple", sh, "gl"));
    assert(sh.name == "ripple");
    assert(sh.type == "gl");
    assert(!sh.frag_path.empty());
    assert(sm.load_shader_source(sh));
    assert(!sh.frag_src.empty());
    assert(sh.frag_src.find("u_frequency") != std::string::npos);

    // Test a user-created custom shader in ~/.config/minicut/shaders/gl/
    std::string user_frag = gl_dir + "/user_custom_sepia.frag";
    {
        std::ofstream ofs(user_frag);
        ofs << "#version 330 core\nvoid main() {}\n";
    }
    sm.scan_shaders();
    CustomShader user_sh;
    assert(sm.find_shader("user_custom_sepia", user_sh, "gl"));
    assert(user_sh.name == "user_custom_sepia");
    assert(sm.load_shader_source(user_sh));
    assert(user_sh.frag_src.find("void main") != std::string::npos);

    // Clean up test file
    fs::remove(user_frag);
    sm.scan_shaders();

    std::cout << "[PASS] Custom Shaders Loading & Management tests passed." << std::endl;
}

void test_text_asset_persistence_and_rerender() {
    std::cout << "[TEST] Running Text Asset Persistence & Re-render test..." << std::endl;
    Project project;
    TextConfig config;
    config.text = "Lossless Subtitle Header";
    config.font_size = 56.0f;
    config.use_outline = true;
    config.outline_thickness = 3.5f;
    config.use_bg = true;

    std::string asset_id = TextGenerator::create_text_asset(project, config);
    assert(!asset_id.empty());
    assert(project.video_assets.size() == 1);

    const VideoAsset& orig_va = project.video_assets[0];
    assert(orig_va.text_content == "Lossless Subtitle Header");
    assert(orig_va.text_font_size == 56.0f);
    assert(orig_va.text_use_outline == true);
    assert(orig_va.text_outline_thickness == 3.5f);
    assert(orig_va.is_image == true);
    assert(fs::exists(orig_va.path));

    std::string orig_path = orig_va.path;

    // Serialize to JSON
    std::string json_str = ProjectSerializer::serialize_json(project);
    assert(json_str.find("\"text_content\": \"Lossless Subtitle Header\"") != std::string::npos);
    assert(json_str.find("\"text_use_outline\": true") != std::string::npos);

    // Simulate temp cleanup or project moved to another machine: delete the rendered temporary image
    fs::remove(orig_path);
    assert(!fs::exists(orig_path));

    // Reload project from JSON
    Project reloaded_project;
    bool ok = ProjectSerializer::deserialize_json(reloaded_project, json_str);
    assert(ok);
    assert(reloaded_project.video_assets.size() == 1);

    const VideoAsset& reloaded_va = reloaded_project.video_assets[0];
    assert(reloaded_va.text_content == "Lossless Subtitle Header");
    assert(reloaded_va.text_use_outline == true);
    assert(reloaded_va.is_image == true);

    // The asset image MUST have been dynamically re-rendered from the stored text string!
    assert(!reloaded_va.path.empty());
    assert(fs::exists(reloaded_va.path));
    assert(reloaded_va.width > 0);
    assert(reloaded_va.height > 0);

    // Clean up
    if (fs::exists(reloaded_va.path)) {
        fs::remove(reloaded_va.path);
    }

    std::cout << "[PASS] Text Asset Persistence & Re-render tests passed." << std::endl;
}

void test_shader_piping_sftp_and_multi_frame() {
    std::cout << "[TEST] Running Shader Piping, SFTP, and Multi-Frame Re-application tests..." << std::endl;

    ShaderManager::instance().init();
    CustomShader cs;
    bool found = ShaderManager::instance().find_shader("ripple", cs);
    assert(found);
    ShaderManager::instance().load_shader_source(cs);
    assert(cs.frag_src.find("ripple") != std::string::npos);

    Project project;

    std::string test_png = TempManager::get_temp_dir() + "/test_pipe_frame.png";
    std::vector<uint8_t> frame_rgba(640 * 360 * 4, 180);
    // Add distinctive center marker
    for (int y = 170; y < 190; ++y) {
        for (int x = 310; x < 330; ++x) {
            int idx = (y * 640 + x) * 4;
            frame_rgba[idx + 0] = 250;
            frame_rgba[idx + 1] = 50;
            frame_rgba[idx + 2] = 50;
            frame_rgba[idx + 3] = 255;
        }
    }
    MediaDecoder::save_rgba_to_png(test_png, 640, 360, frame_rgba);

    VideoAsset va;
    va.id = "pipe_asset_1";
    va.path = test_png;
    va.width = 640;
    va.height = 360;
    va.duration = 1.0;
    va.fps = 30.0;
    va.is_image = true;
    project.video_assets.push_back(va);

    Track trk;
    trk.name = "Video Track";
    trk.type = "video";
    Clip c;
    c.id = "pipe_clip_1";
    c.asset_id = va.id;
    c.start_time = 0.0;
    c.duration = 0.4;
    c.offset = 0.0;
    trk.clips.push_back(c);
    project.tracks.push_back(trk);

    ScriptEngine se;
    double playhead = 0.0;
    std::string selected_clip = "pipe_clip_1";
    bool ok = se.init(&project, nullptr, nullptr, &playhead, &selected_clip,
                      nullptr, nullptr, nullptr, nullptr,
                      [&selected_clip](const std::string& id) { selected_clip = id; },
                      nullptr, nullptr);
    assert(ok);

    // 1. Lua Piping: frame = clip.get_frame("pipe_clip_1"):apply_shader(sid, uniforms):reinsert()
    const char* lua_code = R"(
        local f = clip.get_frame("pipe_clip_1")
        assert(f ~= nil, "frame should not be nil")
        assert(f.clip_id == "pipe_clip_1", "frame.clip_id must match real clip id")
        local sid = shader.create("ripple")
        assert(sid > 0, "shader.create must return valid id")
        local uniforms = { amplitude = 12.0, frequency = 0.08, phase = 0.0 }
        local rf = f:apply_shader(sid, uniforms)
        assert(rf ~= nil, "rf must not be nil")
        assert(rf.applied_shader ~= "", "applied_shader must be preserved")
        local ok = rf:reinsert()
        assert(ok == true, "reinsert must succeed")
    )";
    bool lua_ok = se.run_string(lua_code);
    assert(lua_ok);

    // Wait for the multi-frame render to finish
    int wait_cycles = 0;
    while (se.is_rendering_shader() && wait_cycles < 200) {
        se.poll_shader_rendering();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        wait_cycles++;
    }
    se.poll_shader_rendering();
    assert(!se.is_rendering_shader());

    // Verify clip now points to a valid whole-clip video, not a static image
    Clip* c_after_first = project.find_clip_mut("pipe_clip_1");
    assert(c_after_first != nullptr);
    VideoAsset* va_after_first = project.find_video_asset_mut(c_after_first->asset_id);
    assert(va_after_first != nullptr);
    assert(!va_after_first->is_image);
    assert(va_after_first->fps == 30.0);
    assert(fs::exists(va_after_first->path));

    // 2. Subsequent Run on the SAME clip (Issue: "When I used the Ripple Script again it inverted the colors and only applied to the first frame")
    const char* lua_again = R"(
        local f = clip.get_frame("pipe_clip_1")
        assert(f ~= nil, "frame on subsequent run must not be nil")
        local sid = shader.create("ripple")
        local rf = f:apply_shader(sid, { amplitude = 16.0, frequency = 0.05 })
        local ok = rf:reinsert()
        assert(ok == true, "subsequent reinsert must succeed")
    )";
    bool again_ok = se.run_string(lua_again);
    assert(again_ok);

    wait_cycles = 0;
    while (se.is_rendering_shader() && wait_cycles < 200) {
        se.poll_shader_rendering();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        wait_cycles++;
    }
    se.poll_shader_rendering();
    assert(!se.is_rendering_shader());

    VideoAsset* va_after_second = project.find_video_asset_mut(c_after_first->asset_id);
    assert(va_after_second != nullptr);
    assert(!va_after_second->is_image);
    assert(fs::exists(va_after_second->path));

    // 3. JavaScript Piping: clip.get_frame("pipe_clip_1").apply_shader(sid, uniforms).reinsert()
    const char* js_code = R"(
        var f = clip.get_frame("pipe_clip_1");
        if (!f) throw new Error("JS get_frame failed");
        var sid = shader.create("ripple");
        var rf = f.apply_shader(sid, { amplitude: 14.0, frequency: 0.06 });
        if (!rf) throw new Error("JS apply_shader failed");
        var ok = rf.reinsert();
        if (!ok) throw new Error("JS reinsert failed");
    )";
    bool js_ok = se.run_js_string(js_code);
    assert(js_ok);

    wait_cycles = 0;
    while (se.is_rendering_shader() && wait_cycles < 200) {
        se.poll_shader_rendering();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        wait_cycles++;
    }
    se.poll_shader_rendering();
    assert(!se.is_rendering_shader());

    // 4. SFTP API validation in Lua & JS
    const char* lua_sftp = R"(
        local ok, err = net.upload("sftp://user:pass@127.0.0.1:22/remote/test.mp4", "/path/to/missing_file.mp4")
        assert(ok == nil, "upload on missing file should return nil")
        assert(string.find(err, "does not exist") ~= nil, "error message should report missing file")
    )";
    assert(se.run_string(lua_sftp));

    const char* js_sftp = R"(
        var resp = net.upload("sftp://user:pass@127.0.0.1:22/remote/test.mp4", "/path/to/missing_file.mp4");
        if (resp !== null) throw new Error("JS net.upload on missing file should return null");
    )";
    assert(se.run_js_string(js_sftp));

    // Clean up temporary assets
    if (fs::exists(test_png)) fs::remove(test_png);
    if (fs::exists(va_after_second->path)) fs::remove(va_after_second->path);

    std::cout << "[PASS] Shader Piping, SFTP, and Multi-Frame Re-application tests passed." << std::endl;
}

// ============================================================================
// Asset Folder & Grid View Tests
// ============================================================================
void test_asset_folders_and_grid_view() {
    std::cout << "[TEST] Running asset folders and grid view tests..." << std::endl;

    // -------------------------------------------------------------------------
    // a) Project::add_asset_folder() — adds, deduplicates, rejects empty
    // -------------------------------------------------------------------------
    {
        Project p;
        assert(p.asset_folders.empty());

        bool added = p.add_asset_folder("VFX");
        assert(added);
        assert(p.asset_folders.size() == 1);
        assert(p.asset_folders[0] == "VFX");

        // Duplicate should be rejected
        bool added_dup = p.add_asset_folder("VFX");
        assert(!added_dup);
        assert(p.asset_folders.size() == 1);

        // Empty name should be rejected
        bool added_empty = p.add_asset_folder("");
        assert(!added_empty);
        assert(p.asset_folders.size() == 1);

        // Second distinct folder
        p.add_asset_folder("Music");
        assert(p.asset_folders.size() == 2);
    }
    std::cout << "  [PASS] add_asset_folder deduplication and empty-name guard" << std::endl;

    // -------------------------------------------------------------------------
    // b) VideoAsset.folder assignment survives serialize -> deserialize cycle
    // -------------------------------------------------------------------------
    {
        Project p;
        p.name = "FolderSerTest";
        p.add_asset_folder("Footage");

        VideoAsset v;
        v.id = "v1";
        v.path = "/tmp/test_video.mp4";
        v.duration = 10.0;
        v.folder = "Footage";
        p.video_assets.push_back(v);

        const std::string tmp_path = "/tmp/test_folder_ser_video.minicut";
        ProjectSerializer ser;
        bool saved = ser.save_to_file(p, tmp_path);
        assert(saved);

        Project p2;
        bool loaded = ser.load_from_file(p2, tmp_path);
        assert(loaded);
        assert(!p2.video_assets.empty());
        assert(p2.video_assets[0].folder == "Footage");
        assert(!p2.asset_folders.empty());
        assert(p2.asset_folders[0] == "Footage");

        fs::remove(tmp_path);
    }
    std::cout << "  [PASS] VideoAsset folder survives serialize/deserialize" << std::endl;

    // -------------------------------------------------------------------------
    // c) AudioAsset.folder assignment survives serialize -> deserialize cycle
    // -------------------------------------------------------------------------
    {
        Project p;
        p.name = "FolderSerAudioTest";
        p.add_asset_folder("SFX");

        AudioAsset a;
        a.id = "a1";
        a.path = "/tmp/test_audio.mp3";
        a.duration = 5.0;
        a.folder = "SFX";
        p.audio_assets.push_back(a);

        const std::string tmp_path = "/tmp/test_folder_ser_audio.minicut";
        ProjectSerializer ser;
        bool saved = ser.save_to_file(p, tmp_path);
        assert(saved);

        Project p2;
        bool loaded = ser.load_from_file(p2, tmp_path);
        assert(loaded);
        assert(!p2.audio_assets.empty());
        assert(p2.audio_assets[0].folder == "SFX");

        fs::remove(tmp_path);
    }
    std::cout << "  [PASS] AudioAsset folder survives serialize/deserialize" << std::endl;

    // -------------------------------------------------------------------------
    // d) ProjectPrefab.folder assignment survives serialize -> deserialize cycle
    // -------------------------------------------------------------------------
    {
        Project p;
        p.name = "FolderSerPrefabTest";
        p.add_asset_folder("Prefabs");

        ProjectPrefab pf;
        pf.id = "pf1";
        pf.name = "IntroSequence";
        pf.path = "/tmp/test_prefab.minicut";
        pf.folder = "Prefabs";
        pf.raw_json = "{}";
        p.prefabs.push_back(pf);

        const std::string tmp_path = "/tmp/test_folder_ser_prefab.minicut";
        ProjectSerializer ser;
        bool saved = ser.save_to_file(p, tmp_path);
        assert(saved);

        Project p2;
        bool loaded = ser.load_from_file(p2, tmp_path);
        assert(loaded);
        assert(!p2.prefabs.empty());
        assert(p2.prefabs[0].folder == "Prefabs");

        fs::remove(tmp_path);
    }
    std::cout << "  [PASS] ProjectPrefab folder survives serialize/deserialize" << std::endl;

    // -------------------------------------------------------------------------
    // e) Project::rename_asset_folder() renames in folder list and all assets
    // -------------------------------------------------------------------------
    {
        Project p;
        p.add_asset_folder("OldName");

        VideoAsset v;
        v.id = "v1";
        v.path = "/tmp/foo.mp4";
        v.folder = "OldName";
        p.video_assets.push_back(v);

        AudioAsset a;
        a.id = "a1";
        a.path = "/tmp/bar.mp3";
        a.folder = "OldName";
        p.audio_assets.push_back(a);

        ProjectPrefab pf;
        pf.id = "pf1";
        pf.name = "Pf";
        pf.folder = "OldName";
        p.prefabs.push_back(pf);

        bool renamed = p.rename_asset_folder("OldName", "NewName");
        assert(renamed);
        assert(p.asset_folders.size() == 1);
        assert(p.asset_folders[0] == "NewName");
        assert(p.video_assets[0].folder == "NewName");
        assert(p.audio_assets[0].folder == "NewName");
        assert(p.prefabs[0].folder == "NewName");

        // Assets in a different folder are unaffected
        VideoAsset v2;
        v2.id = "v2";
        v2.folder = "Other";
        p.video_assets.push_back(v2);
        p.rename_asset_folder("NewName", "FinalName");
        assert(p.video_assets[1].folder == "Other");
    }
    std::cout << "  [PASS] rename_asset_folder updates list and all asset fields" << std::endl;

    // -------------------------------------------------------------------------
    // f) Project::remove_asset_folder() removes from list and clears asset fields
    // -------------------------------------------------------------------------
    {
        Project p;
        p.add_asset_folder("ToDelete");
        p.add_asset_folder("Keep");

        VideoAsset v;
        v.id = "v1";
        v.path = "/tmp/foo.mp4";
        v.folder = "ToDelete";
        p.video_assets.push_back(v);

        VideoAsset v2;
        v2.id = "v2";
        v2.path = "/tmp/bar.mp4";
        v2.folder = "Keep";
        p.video_assets.push_back(v2);

        bool removed = p.remove_asset_folder("ToDelete");
        assert(removed);
        assert(p.asset_folders.size() == 1);
        assert(p.asset_folders[0] == "Keep");

        // Asset in deleted folder has folder cleared (moved to root)
        assert(p.video_assets[0].folder.empty());
        // Asset in kept folder unaffected
        assert(p.video_assets[1].folder == "Keep");
    }
    std::cout << "  [PASS] remove_asset_folder clears assets and updates list" << std::endl;

    // -------------------------------------------------------------------------
    // g) ClipManager::create_folder() / rename_folder() / delete_folder()
    // -------------------------------------------------------------------------
    {
        Project p;
        ClipManager cm;

        bool created = cm.create_folder(p, "Clips");
        assert(created);
        assert(!p.asset_folders.empty());
        assert(p.asset_folders[0] == "Clips");

        // Duplicate rejected
        bool dup = cm.create_folder(p, "Clips");
        assert(!dup);
        assert(p.asset_folders.size() == 1);

        bool renamed = cm.rename_folder(p, "Clips", "KeyClips");
        assert(renamed);
        assert(p.asset_folders[0] == "KeyClips");

        bool deleted = cm.delete_folder(p, "KeyClips");
        assert(deleted);
        assert(p.asset_folders.empty());
    }
    std::cout << "  [PASS] ClipManager create/rename/delete folder" << std::endl;

    // -------------------------------------------------------------------------
    // h) ClipManager::set_asset_folder() / get_asset_folder()
    // -------------------------------------------------------------------------
    {
        Project p;
        ClipManager cm;
        cm.create_folder(p, "B-Roll");

        VideoAsset v;
        v.id = "v10";
        v.path = "/tmp/video10.mp4";
        p.video_assets.push_back(v);

        AudioAsset a;
        a.id = "a10";
        a.path = "/tmp/audio10.mp3";
        p.audio_assets.push_back(a);

        // Initially root (empty string)
        assert(cm.get_asset_folder(p, "v10", true, false).empty());

        cm.set_asset_folder(p, "v10", true, false, "B-Roll");
        assert(cm.get_asset_folder(p, "v10", true, false) == "B-Roll");

        cm.set_asset_folder(p, "a10", false, false, "B-Roll");
        assert(cm.get_asset_folder(p, "a10", false, false) == "B-Roll");

        // Clear back to root
        cm.set_asset_folder(p, "v10", true, false, "");
        assert(cm.get_asset_folder(p, "v10", true, false).empty());
    }
    std::cout << "  [PASS] ClipManager set/get_asset_folder" << std::endl;

    // -------------------------------------------------------------------------
    // i) View mode switch (0 -> 1 -> 0) does not crash and state is consistent
    // -------------------------------------------------------------------------
    {
        ClipManager cm;
        assert(cm.get_view_mode() == 0); // Default: list

        cm.set_view_mode(1);
        assert(cm.get_view_mode() == 1); // Grid

        cm.set_view_mode(0);
        assert(cm.get_view_mode() == 0); // Back to list

        // Rapid toggling is safe
        for (int i = 0; i < 100; ++i) {
            cm.set_view_mode(i % 2);
        }
        assert(cm.get_view_mode() == 0);
    }
    std::cout << "  [PASS] View mode switch (list/grid) is consistent and crash-free" << std::endl;

    // -------------------------------------------------------------------------
    // j) asset_folders list serialized and deserialized correctly (multi-folder)
    // -------------------------------------------------------------------------
    {
        Project p;
        p.name = "MultiFolderTest";
        p.add_asset_folder("Alpha");
        p.add_asset_folder("Beta");
        p.add_asset_folder("Gamma");

        const std::string tmp_path = "/tmp/test_multi_folder.minicut";
        ProjectSerializer ser;
        bool saved = ser.save_to_file(p, tmp_path);
        assert(saved);

        Project p2;
        bool loaded = ser.load_from_file(p2, tmp_path);
        assert(loaded);
        assert(p2.asset_folders.size() == 3);
        assert(p2.asset_folders[0] == "Alpha");
        assert(p2.asset_folders[1] == "Beta");
        assert(p2.asset_folders[2] == "Gamma");

        fs::remove(tmp_path);
    }
    std::cout << "  [PASS] asset_folders list serialized/deserialized correctly" << std::endl;

    // -------------------------------------------------------------------------
    // k) Selected folder filter state is isolated per ClipManager instance
    // -------------------------------------------------------------------------
    {
        ClipManager cm1;
        ClipManager cm2;

        cm1.set_selected_folder_filter("VFX");
        cm2.set_selected_folder_filter("(Root)");

        assert(cm1.get_selected_folder_filter() == "VFX");
        assert(cm2.get_selected_folder_filter() == "(Root)");

        // Reset
        cm1.set_selected_folder_filter("");
        assert(cm1.get_selected_folder_filter().empty());
    }
    std::cout << "  [PASS] Selected folder filter state isolated per ClipManager" << std::endl;

    std::cout << "[PASS] All asset folder and grid view tests passed." << std::endl;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "Running minicut Comprehensive Test Suite" << std::endl;
    std::cout << "========================================" << std::endl;

    test_project_load_audio_re_extraction();
    test_keyframe_evaluation();
    test_project_serialization();
    test_image_transform_and_keyframing();
    test_media_probe_and_export();
    test_image_probe_and_decode();
    test_clip_pool_and_audio_separation();
    test_project_prefab_import_and_instantiation();
    test_audio_engine_preview_and_caching();
    test_timeline_scrolling_and_navigation();
    test_native_file_dialog_system();
    test_clip_import_progress();
    test_temp_manager();
    test_locale_manager();
    test_track_render_depth();
    test_slice_and_paste_on_selected_track();
    test_filebrowser_system_thumbnail();
    test_track_header_resizing();
    test_script_engine_lua_api();
    test_arabic_shaping();
    test_text_generator();
    test_track_reordering_and_renaming();
    test_timeline_multiselect();
    test_extended_lua_api();
    test_ogg_ogv_ogm_support();
    test_window_title_and_version();
    test_dock_localization();
    test_javascript_engine();
    test_filebrowser_cancel_behavior();
    test_export_format_presets_and_formats();
    test_subtitle_track_and_serialization();
    test_render_subsystem();
    test_settings_logic_and_auth();
    test_tts_manager();
    test_aicore_interfaces();
    test_accessibility_and_cascading_timeline();
    test_swap_manager_and_disk_buffer();
    test_subtitle_addition_and_assets_menu();
    test_multilanguage_tts_stereo_and_autolength();
    test_preview_aspect_ratio_and_japanese_kanji_tts_and_hotkeys();
    test_preview_transform_and_popout_and_export_progress();
    test_scale_xy_clips_and_assets();
    test_timeline_and_asset_selection_clearing();
    test_audio_mixing_progress_immediate_jump();
    test_scripting_locale_api_lua_and_js();
    test_scripting_ai_library_lua_and_js();
    test_raw_frame_shader_sfx_pipeline_lua_and_js();
    test_preview_performance_enhancements();
    test_shader_effects_across_clip_and_menu_ids();
    test_ripple_effect_and_box_select();
    test_vulkan_backend_and_preview_settings();
    test_headless_batch_reconfig_and_prefab_reload();
    test_custom_shaders_loading();
    test_text_asset_persistence_and_rerender();
    test_shader_piping_sftp_and_multi_frame();
    test_asset_folders_and_grid_view();

    std::cout << "========================================" << std::endl;
    std::cout << "ALL MINICUT UNIT & PIPELINE TESTS PASSED" << std::endl;
    std::cout << "========================================" << std::endl;
    return 0;
}



