#pragma once

#include "Common.h"
#include "SwapManager.h"
#include <SDL2/SDL.h>
#include <mutex>
#include <atomic>
#include <map>
#include <memory>
#include <vector>

struct AudioDecodeCache {
    std::string path;
    int sample_rate = 48000;
    int channels = 2;
    std::shared_ptr<SwapBuffer<float>> pcm_samples;
};

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    bool init();
    void shutdown();

    void set_playing(bool playing);
    bool is_playing() const { return m_is_playing.load(); }

    void set_playhead_time(double t) { m_playhead_time.store(t); }
    double get_playhead_time() const { return m_playhead_time.load(); }

    bool is_device_active() const { return m_device_id != 0; }
    void update_project_snapshot(const Project& project);

    // Decode audio asset to cache
    void preload_asset_audio(const std::string& path);
    void ensure_project_assets_loaded(const Project& project);
    bool is_asset_cached(const std::string& path) const;
    size_t get_cached_sample_count(const std::string& path) const;

    void set_preview_volume_db(float db) { m_preview_volume_db.store(db); }
    float get_preview_volume_db() const { return m_preview_volume_db.load(); }

private:
    static void audio_callback(void* userdata, Uint8* stream, int len);
    void process_audio(float* stream, int num_samples);

    SDL_AudioDeviceID m_device_id = 0;
    std::atomic<bool> m_is_playing{false};
    std::atomic<double> m_playhead_time{0.0};
    std::atomic<float> m_preview_volume_db{0.0f};

    std::mutex m_project_mutex;
    Project m_project_snapshot;

    std::mutex m_cache_mutex;
    std::map<std::string, AudioDecodeCache> m_audio_cache;
};
