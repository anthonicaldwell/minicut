#pragma once

#include "Common.h"
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <atomic>

struct AVFormatContext;
struct AVCodecContext;
struct SwsContext;
struct AVFrame;
struct AVPacket;

struct DecodedVideoContext {
    AVFormatContext* format_ctx = nullptr;
    AVCodecContext* codec_ctx = nullptr;
    SwsContext* sws_ctx = nullptr;
    int video_stream_idx = -1;
    double last_decoded_pts = -1.0;
    int width = 0;
    int height = 0;

    bool is_image = false;
    std::vector<uint8_t> cached_image_rgba;
    int cached_w = 0;
    int cached_h = 0;

    ~DecodedVideoContext();
    bool open(const std::string& filepath);
    bool decode_frame_at(double target_sec, int out_w, int out_h, std::vector<uint8_t>& out_rgba);
    void close();
};

class MediaDecoder {
public:
    MediaDecoder();
    ~MediaDecoder();

    // Async frame request (Non-blocking)
    void request_frame(double timeline_time, const Project& project_snapshot);

    // Main thread update (uploads to GL texture if a new frame was decoded)
    bool update_preview_texture(GLuint texture_id, int& out_w, int& out_h, double& out_time);

    // Media inspection helpers
    static bool probe_video_file(const std::string& path, VideoAsset& out_asset);
    static bool probe_audio_file(const std::string& path, AudioAsset& out_asset);
    static GLuint create_thumbnail_texture(const std::string& path, double at_sec = 0.5);
    static bool decode_thumbnail_rgba(const std::string& path, double at_sec, int& out_w, int& out_h, std::vector<uint8_t>& out_rgba);
    static GLuint upload_rgba_texture(int w, int h, const std::vector<uint8_t>& rgba);

    // Helper to generate synthetic test media (e.g. SMPTE color bars video and 440Hz test audio)
    static bool generate_test_media(const std::string& video_path, const std::string& audio_path);

private:
    void worker_loop();

    std::atomic<bool> m_running{true};
    std::thread m_worker_thread;
    std::mutex m_request_mutex;
    std::condition_variable m_request_cv;

    // Latest request from main thread
    bool m_has_new_request = false;
    double m_requested_time = 0.0;
    Project m_project_snapshot;

    // Decoded frame buffer passed to main thread
    std::mutex m_frame_mutex;
    std::vector<uint8_t> m_ready_rgba;
    int m_ready_width = 1280;
    int m_ready_height = 720;
    double m_ready_time = 0.0;
    bool m_has_new_frame = false;

    // Cache of open video contexts for active files
    std::map<std::string, std::unique_ptr<DecodedVideoContext>> m_video_contexts;
};
