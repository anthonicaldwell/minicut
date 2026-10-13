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

void test_image_probe_and_decode() {
    std::cout << "[TEST] Running image probe and decode test..." << std::endl;
    fs::path temp_img = "test_sample.png";
    assert(fs::exists(temp_img) && "test_sample.png should exist");

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
    std::string tone_path = "demo_tone.mp3";
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
    aa.path = "demo_tone.mp3";
    p.audio_assets.push_back(aa);
    engine.ensure_project_assets_loaded(p);
    assert(engine.is_asset_cached("demo_tone.mp3"));

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
    if (fs::exists("test_sample.png")) {
        bool ok = cm.import_image(p, "test_sample.png", 5.0);
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
    assert(lm.get("menu_file") == "ملف");
    assert(lm.get("timeline_play") == "تشغيل");

    // Reset to en
    lm.set_locale("en");
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
    std::string colorbars_path = "demo_colorbars.mp4";
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

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "Running minicut Comprehensive Test Suite" << std::endl;
    std::cout << "========================================" << std::endl;

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

    std::cout << "========================================" << std::endl;
    std::cout << "ALL MINICUT UNIT & PIPELINE TESTS PASSED" << std::endl;
    std::cout << "========================================" << std::endl;
    return 0;
}
