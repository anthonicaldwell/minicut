#include "AudioEngine.h"
#include <iostream>
#include <cstring>
#include <algorithm>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
}

AudioEngine::AudioEngine() {}

AudioEngine::~AudioEngine() {
    shutdown();
}

bool AudioEngine::init() {
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        std::cerr << "Failed to init SDL Audio: " << SDL_GetError() << std::endl;
        return false;
    }

    SDL_AudioSpec desired, obtained;
    std::memset(&desired, 0, sizeof(desired));
    desired.freq = 48000;
    desired.format = AUDIO_F32SYS;
    desired.channels = 2;
    desired.samples = 1024;
    desired.callback = audio_callback;
    desired.userdata = this;

    m_device_id = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained, 0);
    if (m_device_id == 0) {
        std::cerr << "Failed to open audio device: " << SDL_GetError() << std::endl;
        return false;
    }

    SDL_PauseAudioDevice(m_device_id, 0);
    return true;
}

void AudioEngine::set_playing(bool playing) {
    m_is_playing.store(playing);
    if (m_device_id != 0) {
        SDL_PauseAudioDevice(m_device_id, playing ? 0 : 1);
    }
}

void AudioEngine::shutdown() {
    if (m_device_id != 0) {
        SDL_CloseAudioDevice(m_device_id);
        m_device_id = 0;
    }
    std::lock_guard<std::mutex> lock(m_cache_mutex);
    m_audio_cache.clear();
}

void AudioEngine::update_project_snapshot(const Project& project) {
    std::lock_guard<std::mutex> lock(m_project_mutex);
    m_project_snapshot = project;
}

bool AudioEngine::is_asset_cached(const std::string& path) const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(m_cache_mutex));
    return m_audio_cache.find(path) != m_audio_cache.end();
}

size_t AudioEngine::get_cached_sample_count(const std::string& path) const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(m_cache_mutex));
    auto it = m_audio_cache.find(path);
    if (it != m_audio_cache.end() && it->second.pcm_samples) {
        return it->second.pcm_samples->size();
    }
    return 0;
}

void AudioEngine::ensure_project_assets_loaded(const Project& project) {
    for (const auto& a : project.audio_assets) {
        if (!a.path.empty()) {
            preload_asset_audio(a.path);
        }
    }
    for (const auto& v : project.video_assets) {
        if (v.has_audio && !v.is_image && !v.path.empty()) {
            preload_asset_audio(v.path);
        }
    }
}

void AudioEngine::preload_asset_audio(const std::string& path) {
    if (path.empty()) return;
    {
        std::lock_guard<std::mutex> lock(m_cache_mutex);
        if (m_audio_cache.find(path) != m_audio_cache.end()) {
            return;
        }
    }

    AVFormatContext* fmt = nullptr;
    if (avformat_open_input(&fmt, path.c_str(), nullptr, nullptr) != 0) {
        return;
    }
    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        avformat_close_input(&fmt);
        return;
    }

    int audio_stream_idx = -1;
    for (unsigned int i = 0; i < fmt->nb_streams; ++i) {
        if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audio_stream_idx = static_cast<int>(i);
            break;
        }
    }

    if (audio_stream_idx < 0) {
        avformat_close_input(&fmt);
        return;
    }

    AVCodecParameters* codecpar = fmt->streams[audio_stream_idx]->codecpar;
    const AVCodec* decoder = avcodec_find_decoder(codecpar->codec_id);
    if (!decoder) {
        avformat_close_input(&fmt);
        return;
    }

    AVCodecContext* codec_ctx = avcodec_alloc_context3(decoder);
    avcodec_parameters_to_context(codec_ctx, codecpar);
    if (avcodec_open2(codec_ctx, decoder, nullptr) < 0) {
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt);
        return;
    }

    AudioDecodeCache cache;
    cache.path = path;
    cache.sample_rate = 48000;
    cache.channels = 2;
    cache.pcm_samples = std::make_shared<SwapBuffer<float>>();

    SwrContext* swr = nullptr;
    AVChannelLayout out_ch_layout = AV_CHANNEL_LAYOUT_STEREO;

    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();

    auto setup_swr = [&](AVFrame* in_frame) -> bool {
        if (swr != nullptr) return true;

        AVChannelLayout in_ch_layout;
        if (in_frame->ch_layout.nb_channels > 0) {
            av_channel_layout_copy(&in_ch_layout, &in_frame->ch_layout);
        } else if (codec_ctx->ch_layout.nb_channels > 0) {
            av_channel_layout_copy(&in_ch_layout, &codec_ctx->ch_layout);
        } else {
            int ch = codecpar->ch_layout.nb_channels > 0 ? codecpar->ch_layout.nb_channels : 2;
            av_channel_layout_default(&in_ch_layout, ch);
        }

        int in_rate = in_frame->sample_rate > 0 ? in_frame->sample_rate : codec_ctx->sample_rate;
        if (in_rate <= 0) in_rate = 44100;
        AVSampleFormat in_fmt = (in_frame->format != AV_SAMPLE_FMT_NONE) ?
            static_cast<AVSampleFormat>(in_frame->format) : codec_ctx->sample_fmt;
        if (in_fmt == AV_SAMPLE_FMT_NONE) in_fmt = AV_SAMPLE_FMT_S16;

        int ret = swr_alloc_set_opts2(&swr,
            &out_ch_layout, AV_SAMPLE_FMT_FLT, 48000,
            &in_ch_layout, in_fmt, in_rate,
            0, nullptr);
        av_channel_layout_uninit(&in_ch_layout);

        if (ret < 0 || swr_init(swr) < 0) {
            if (swr) { swr_free(&swr); swr = nullptr; }
            return false;
        }
        return true;
    };

    while (av_read_frame(fmt, packet) >= 0) {
        if (packet->stream_index == audio_stream_idx) {
            if (avcodec_send_packet(codec_ctx, packet) == 0) {
                while (avcodec_receive_frame(codec_ctx, frame) == 0) {
                    if (setup_swr(frame)) {
                        int in_rate = frame->sample_rate > 0 ? frame->sample_rate : 44100;
                        int dst_nb_samples = av_rescale_rnd(
                            swr_get_delay(swr, in_rate) + frame->nb_samples,
                            48000, in_rate, AV_ROUND_UP);

                        std::vector<float> temp_buf(dst_nb_samples * 2);
                        uint8_t* out_data[1] = { reinterpret_cast<uint8_t*>(temp_buf.data()) };

                        int converted = swr_convert(swr, out_data, dst_nb_samples,
                                                    (const uint8_t**)frame->data, frame->nb_samples);
                        if (converted > 0) {
                            cache.pcm_samples->append(temp_buf.data(), converted * 2);
                        }
                    }
                }
            }
        }
        av_packet_unref(packet);
    }

    // Flush decoder
    if (avcodec_send_packet(codec_ctx, nullptr) == 0) {
        while (avcodec_receive_frame(codec_ctx, frame) == 0) {
            if (setup_swr(frame)) {
                int in_rate = frame->sample_rate > 0 ? frame->sample_rate : 44100;
                int dst_nb_samples = av_rescale_rnd(
                    swr_get_delay(swr, in_rate) + frame->nb_samples,
                    48000, in_rate, AV_ROUND_UP);

                std::vector<float> temp_buf(dst_nb_samples * 2);
                uint8_t* out_data[1] = { reinterpret_cast<uint8_t*>(temp_buf.data()) };

                int converted = swr_convert(swr, out_data, dst_nb_samples,
                                            (const uint8_t**)frame->data, frame->nb_samples);
                if (converted > 0) {
                    cache.pcm_samples->append(temp_buf.data(), converted * 2);
                }
            }
        }
    }

    // Flush swresample delay
    if (swr) {
        int delay = swr_get_delay(swr, 48000);
        if (delay > 0) {
            std::vector<float> temp_buf(delay * 2);
            uint8_t* out_data[1] = { reinterpret_cast<uint8_t*>(temp_buf.data()) };
            int converted = swr_convert(swr, out_data, delay, nullptr, 0);
            if (converted > 0) {
                cache.pcm_samples->append(temp_buf.data(), converted * 2);
            }
        }
    }

    av_frame_free(&frame);
    av_packet_free(&packet);
    if (swr) swr_free(&swr);
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&fmt);

    {
        std::lock_guard<std::mutex> lock(m_cache_mutex);
        m_audio_cache[path] = std::move(cache);
    }
}

void AudioEngine::audio_callback(void* userdata, Uint8* stream, int len) {
    auto* self = static_cast<AudioEngine*>(userdata);
    float* fstream = reinterpret_cast<float*>(stream);
    int num_samples = len / sizeof(float); // Total float samples (channels * frames)

    std::memset(stream, 0, len);
    if (!self->m_is_playing.load()) {
        return;
    }

    self->process_audio(fstream, num_samples);
}

void AudioEngine::process_audio(float* stream, int num_samples) {
    Project proj;
    {
        std::lock_guard<std::mutex> lock(m_project_mutex);
        proj = m_project_snapshot;
    }

    double current_time = m_playhead_time.load();
    int frames = num_samples / 2; // Stereo frames
    double dt_per_frame = 1.0 / 48000.0;

    bool has_solo_audio = false;
    for (const auto& track : proj.tracks) {
        if (track.solo) {
            has_solo_audio = true;
            break;
        }
    }

    // Determine which asset IDs are actively present on audio tracks
    // to prevent double playback of separated video tracks
    std::vector<std::string> audio_track_asset_ids;
    for (const auto& track : proj.tracks) {
        if (track.type == "audio") {
            for (const auto& clip : track.clips) {
                audio_track_asset_ids.push_back(clip.asset_id);
            }
        }
    }

    std::lock_guard<std::mutex> cache_lock(m_cache_mutex);

    for (int f = 0; f < frames; ++f) {
        double frame_time = current_time + (f * dt_per_frame);
        float mix_l = 0.0f;
        float mix_r = 0.0f;

        for (const auto& track : proj.tracks) {
            if (track.muted) continue;
            if (has_solo_audio && !track.solo) continue;

            for (const auto& clip : track.clips) {
                if (!clip.contains_time(frame_time)) continue;

                std::string asset_path;
                if (track.type == "audio") {
                    const AudioAsset* aa = proj.find_audio_asset(clip.asset_id);
                    if (aa) {
                        asset_path = aa->path;
                    } else {
                        const VideoAsset* va = proj.find_video_asset(clip.asset_id);
                        if (va && va->has_audio) {
                            asset_path = va->path;
                        }
                    }
                } else if (track.type == "video") {
                    // Only play video clip audio if this asset is not already present on an audio track
                    bool already_on_audio_track = false;
                    for (const auto& aid : audio_track_asset_ids) {
                        if (aid == clip.asset_id) {
                            already_on_audio_track = true;
                            break;
                        }
                    }
                    if (already_on_audio_track) continue;

                    const VideoAsset* va = proj.find_video_asset(clip.asset_id);
                    if (va && va->has_audio && !va->is_image) {
                        asset_path = va->path;
                    }
                }

                if (asset_path.empty()) continue;

                auto it = m_audio_cache.find(asset_path);
                if (it == m_audio_cache.end()) continue;

                const auto& cache = it->second;
                double media_sec = clip.media_time(frame_time);
                if (media_sec < 0.0) continue;

                int64_t sample_idx = static_cast<int64_t>(media_sec * 48000.0) * 2;
                if (cache.pcm_samples && sample_idx + 1 < static_cast<int64_t>(cache.pcm_samples->size())) {
                    // Evaluate stereo volume and channel keyframes
                    double rel_t = clip.clip_relative_time(frame_time);
                    double gain = proj.evaluate_keyframe(clip.id, "volume", rel_t, 1.0);
                    double gain_l = proj.evaluate_keyframe(clip.id, "volume_left", rel_t, 1.0);
                    double gain_r = proj.evaluate_keyframe(clip.id, "volume_right", rel_t, 1.0);
                    float eff_l = static_cast<float>(std::clamp(gain * gain_l, 0.0, 4.0));
                    float eff_r = static_cast<float>(std::clamp(gain * gain_r, 0.0, 4.0));

                    mix_l += (*cache.pcm_samples)[sample_idx] * eff_l;
                    mix_r += (*cache.pcm_samples)[sample_idx + 1] * eff_r;
                }
            }
        }

        // Apply preview volume adjustment (dB)
        float vol_db = m_preview_volume_db.load();
        float preview_scale = (vol_db <= -59.5f) ? 0.0f : std::pow(10.0f, vol_db / 20.0f);
        mix_l *= preview_scale;
        mix_r *= preview_scale;

        // Soft limit
        stream[f * 2 + 0] = std::clamp(mix_l, -1.0f, 1.0f);
        stream[f * 2 + 1] = std::clamp(mix_r, -1.0f, 1.0f);
    }

    // Advance clock smoothly
    double total_dur = proj.get_total_duration();
    double next_time = current_time + (frames * dt_per_frame);
    if (next_time >= total_dur) {
        next_time = 0.0; // Loop or stop
    }
    m_playhead_time.store(next_time);
}
