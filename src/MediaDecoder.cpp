#include "MediaDecoder.h"
#include <iostream>
#include <chrono>
#include <cstdlib>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
}

// ----------------------------------------------------------------------------
// DecodedVideoContext
// ----------------------------------------------------------------------------

DecodedVideoContext::~DecodedVideoContext() {
    close();
}

void DecodedVideoContext::close() {
    if (sws_ctx) {
        sws_freeContext(sws_ctx);
        sws_ctx = nullptr;
    }
    if (codec_ctx) {
        avcodec_free_context(&codec_ctx);
        codec_ctx = nullptr;
    }
    if (format_ctx) {
        avformat_close_input(&format_ctx);
        format_ctx = nullptr;
    }
    video_stream_idx = -1;
    last_decoded_pts = -1.0;
    is_image = false;
    cached_image_rgba.clear();
    cached_w = 0;
    cached_h = 0;
}

bool DecodedVideoContext::open(const std::string& filepath) {
    close();

    std::string ext = filepath.substr(filepath.find_last_of("."));
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".webp") {
        is_image = true;
    }

    if (avformat_open_input(&format_ctx, filepath.c_str(), nullptr, nullptr) != 0) {
        return false;
    }

    if (avformat_find_stream_info(format_ctx, nullptr) < 0) {
        close();
        return false;
    }

    video_stream_idx = -1;
    for (unsigned int i = 0; i < format_ctx->nb_streams; ++i) {
        if (format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_idx = static_cast<int>(i);
            break;
        }
    }

    if (video_stream_idx == -1) {
        close();
        return false;
    }

    AVCodecParameters* codecpar = format_ctx->streams[video_stream_idx]->codecpar;
    const AVCodec* decoder = avcodec_find_decoder(codecpar->codec_id);
    if (!decoder) {
        close();
        return false;
    }

    codec_ctx = avcodec_alloc_context3(decoder);
    if (!codec_ctx) {
        close();
        return false;
    }

    if (avcodec_parameters_to_context(codec_ctx, codecpar) < 0) {
        close();
        return false;
    }

    if (avcodec_open2(codec_ctx, decoder, nullptr) < 0) {
        close();
        return false;
    }

    width = codec_ctx->width;
    height = codec_ctx->height;
    return true;
}

bool DecodedVideoContext::decode_frame_at(double target_sec, int out_w, int out_h, std::vector<uint8_t>& out_rgba) {
    if (!format_ctx || !codec_ctx || video_stream_idx < 0) return false;

    if (is_image && !cached_image_rgba.empty() && cached_w == out_w && cached_h == out_h) {
        out_rgba = cached_image_rgba;
        return true;
    }

    AVStream* stream = format_ctx->streams[video_stream_idx];
    int64_t target_pts = static_cast<int64_t>(target_sec / av_q2d(stream->time_base));

    // Seek if not close to sequential playback
    bool need_seek = !is_image && (last_decoded_pts < 0.0 || target_sec < last_decoded_pts || (target_sec - last_decoded_pts) > 0.5);
    if (need_seek) {
        avcodec_flush_buffers(codec_ctx);
        av_seek_frame(format_ctx, video_stream_idx, target_pts, AVSEEK_FLAG_BACKWARD);
    }

    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    bool found = false;

    int max_packets = 120;
    while (max_packets-- > 0 && av_read_frame(format_ctx, packet) >= 0) {
        if (packet->stream_index == video_stream_idx) {
            if (avcodec_send_packet(codec_ctx, packet) == 0) {
                while (avcodec_receive_frame(codec_ctx, frame) == 0) {
                    double frame_pts_sec = frame->pts * av_q2d(stream->time_base);
                    last_decoded_pts = frame_pts_sec;

                    // If this frame is at or just before target_sec or target_sec <= 0.05
                    if (frame_pts_sec >= (target_sec - 0.04) || max_packets < 5) {
                        // Setup sws context if needed
                        sws_ctx = sws_getCachedContext(
                            sws_ctx,
                            codec_ctx->width, codec_ctx->height, codec_ctx->pix_fmt,
                            out_w, out_h, AV_PIX_FMT_RGBA,
                            SWS_BILINEAR, nullptr, nullptr, nullptr
                        );

                        out_rgba.resize(out_w * out_h * 4);
                        uint8_t* dst_data[4] = { out_rgba.data(), nullptr, nullptr, nullptr };
                        int dst_linesize[4] = { out_w * 4, 0, 0, 0 };

                        sws_scale(sws_ctx, frame->data, frame->linesize, 0, codec_ctx->height, dst_data, dst_linesize);
                        found = true;
                        break;
                    }
                }
            }
        }
        av_packet_unref(packet);
        if (found) break;
    }

    av_frame_free(&frame);
    av_packet_free(&packet);

    if (found && is_image) {
        cached_image_rgba = out_rgba;
        cached_w = out_w;
        cached_h = out_h;
    }

    return found;
}

// ----------------------------------------------------------------------------
// MediaDecoder
// ----------------------------------------------------------------------------

MediaDecoder::MediaDecoder() {
    av_log_set_level(AV_LOG_ERROR);
    m_ready_rgba.resize(m_ready_width * m_ready_height * 4, 20);
    m_worker_thread = std::thread(&MediaDecoder::worker_loop, this);
}

MediaDecoder::~MediaDecoder() {
    m_running.store(false);
    m_request_cv.notify_all();
    if (m_worker_thread.joinable()) {
        m_worker_thread.join();
    }
    m_video_contexts.clear();
}

void MediaDecoder::request_frame(double timeline_time, const Project& project_snapshot) {
    {
        std::lock_guard<std::mutex> lock(m_request_mutex);
        m_requested_time = timeline_time;
        m_project_snapshot = project_snapshot;
        m_has_new_request = true;
    }
    m_request_cv.notify_one();
}

bool MediaDecoder::update_preview_texture(GLuint texture_id, int& out_w, int& out_h, double& out_time) {
    std::lock_guard<std::mutex> lock(m_frame_mutex);
    if (!m_has_new_frame) {
        out_w = m_ready_width;
        out_h = m_ready_height;
        out_time = m_ready_time;
        return false;
    }

    glBindTexture(GL_TEXTURE_2D, texture_id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_ready_width, m_ready_height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, m_ready_rgba.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    out_w = m_ready_width;
    out_h = m_ready_height;
    out_time = m_ready_time;
    m_has_new_frame = false;
    return true;
}

void MediaDecoder::worker_loop() {
    std::vector<uint8_t> composite_buffer;
    std::vector<uint8_t> layer_buffer;
    const int preview_w = 1280;
    const int preview_h = 720;
    composite_buffer.resize(preview_w * preview_h * 4);

    while (m_running.load()) {
        double current_req_time = 0.0;
        Project current_project;

        {
            std::unique_lock<std::mutex> lock(m_request_mutex);
            m_request_cv.wait(lock, [this]() {
                return !m_running.load() || m_has_new_request;
            });

            if (!m_running.load()) break;

            current_req_time = m_requested_time;
            current_project = m_project_snapshot;
            m_has_new_request = false;
        }

        // Fill background with subtle dark checkerboard pattern
        for (int y = 0; y < preview_h; ++y) {
            for (int x = 0; x < preview_w; ++x) {
                int idx = (y * preview_w + x) * 4;
                bool check = ((x / 32) % 2) == ((y / 32) % 2);
                uint8_t bg = check ? 24 : 18;
                composite_buffer[idx + 0] = bg;
                composite_buffer[idx + 1] = bg;
                composite_buffer[idx + 2] = bg;
                composite_buffer[idx + 3] = 255;
            }
        }

        bool has_solo_video = false;
        for (const auto& track : current_project.tracks) {
            if (track.type == "video" && track.solo) {
                has_solo_video = true;
                break;
            }
        }

        // Collect video tracks and sort by depth (lower depth renders first/background, higher renders foreground)
        std::vector<const Track*> video_tracks;
        for (const auto& track : current_project.tracks) {
            if (track.type != "video") continue;
            if (track.muted) continue;
            if (has_solo_video && !track.solo) continue;
            video_tracks.push_back(&track);
        }
        std::stable_sort(video_tracks.begin(), video_tracks.end(), [](const Track* a, const Track* b) {
            return a->depth < b->depth;
        });

        for (const Track* track_ptr : video_tracks) {
            const Track& track = *track_ptr;
            for (const auto& clip : track.clips) {
                if (!clip.contains_time(current_req_time)) continue;

                const VideoAsset* asset = current_project.find_video_asset(clip.asset_id);
                if (!asset || asset->path.empty()) continue;

                // Ensure open context
                if (m_video_contexts.find(asset->path) == m_video_contexts.end()) {
                    auto ctx = std::make_unique<DecodedVideoContext>();
                    if (ctx->open(asset->path)) {
                        m_video_contexts[asset->path] = std::move(ctx);
                    }
                }

                auto ctx_it = m_video_contexts.find(asset->path);
                if (ctx_it != m_video_contexts.end()) {
                    double rel_time = clip.clip_relative_time(current_req_time);
                    double media_time = clip.media_time(current_req_time);

                    double pos_x = current_project.get_clip_param(clip, "pos_x", rel_time);
                    double pos_y = current_project.get_clip_param(clip, "pos_y", rel_time);
                    double rot   = current_project.get_clip_param(clip, "rotation", rel_time);
                    double scale = current_project.get_clip_param(clip, "scale", rel_time);
                    double opacity = current_project.get_clip_param(clip, "opacity", rel_time);
                    opacity = std::clamp(opacity, 0.0, 1.0);

                    bool has_spatial_transform = (std::abs(pos_x) > 1e-3 || std::abs(pos_y) > 1e-3 ||
                                                  std::abs(rot) > 1e-3 || std::abs(scale - 1.0) > 1e-3);

                    if (asset->is_image || has_spatial_transform) {
                        int src_w = ctx_it->second->width > 0 ? ctx_it->second->width : preview_w;
                        int src_h = ctx_it->second->height > 0 ? ctx_it->second->height : preview_h;
                        if (ctx_it->second->decode_frame_at(media_time, src_w, src_h, layer_buffer)) {
                            blit_transformed_layer(
                                composite_buffer, preview_w, preview_h,
                                layer_buffer, src_w, src_h,
                                pos_x, pos_y, rot, scale, opacity
                            );
                        }
                    } else {
                        if (ctx_it->second->decode_frame_at(media_time, preview_w, preview_h, layer_buffer)) {
                            float alpha = static_cast<float>(opacity);
                            for (size_t i = 0; i < composite_buffer.size(); i += 4) {
                                float src_a = (layer_buffer[i + 3] / 255.0f) * alpha;
                                float inv_a = 1.0f - src_a;

                                composite_buffer[i + 0] = static_cast<uint8_t>(layer_buffer[i + 0] * src_a + composite_buffer[i + 0] * inv_a);
                                composite_buffer[i + 1] = static_cast<uint8_t>(layer_buffer[i + 1] * src_a + composite_buffer[i + 1] * inv_a);
                                composite_buffer[i + 2] = static_cast<uint8_t>(layer_buffer[i + 2] * src_a + composite_buffer[i + 2] * inv_a);
                                composite_buffer[i + 3] = 255;
                            }
                        }
                    }
                }
            }
        }

        // Publish to main thread
        {
            std::lock_guard<std::mutex> lock(m_frame_mutex);
            m_ready_rgba = composite_buffer;
            m_ready_width = preview_w;
            m_ready_height = preview_h;
            m_ready_time = current_req_time;
            m_has_new_frame = true;
        }

        // Modest throttle to prevent 100% CPU in worker loop
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

// ----------------------------------------------------------------------------
// Probing & Thumbnail Generation
// ----------------------------------------------------------------------------

bool MediaDecoder::probe_video_file(const std::string& path, VideoAsset& out_asset) {
    AVFormatContext* fmt = nullptr;
    if (avformat_open_input(&fmt, path.c_str(), nullptr, nullptr) != 0) {
        return false;
    }
    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        avformat_close_input(&fmt);
        return false;
    }

    out_asset.path = path;
    out_asset.duration = (fmt->duration != AV_NOPTS_VALUE) ? (fmt->duration / static_cast<double>(AV_TIME_BASE)) : 0.0;
    out_asset.has_audio = false;

    for (unsigned int i = 0; i < fmt->nb_streams; ++i) {
        if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            out_asset.width = fmt->streams[i]->codecpar->width;
            out_asset.height = fmt->streams[i]->codecpar->height;
            AVRational r = fmt->streams[i]->r_frame_rate;
            if (r.den > 0) {
                out_asset.fps = av_q2d(r);
            }
        } else if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            out_asset.has_audio = true;
        }
    }

    size_t dot_pos = path.find_last_of(".");
    if (dot_pos != std::string::npos) {
        std::string ext = path.substr(dot_pos);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".webp") {
            out_asset.is_image = true;
            if (out_asset.duration <= 0.05) {
                out_asset.duration = 5.0; // Default 5 seconds for imported still images
            }
        }
    }

    avformat_close_input(&fmt);
    return true;
}

bool MediaDecoder::probe_audio_file(const std::string& path, AudioAsset& out_asset) {
    AVFormatContext* fmt = nullptr;
    if (avformat_open_input(&fmt, path.c_str(), nullptr, nullptr) != 0) {
        return false;
    }
    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        avformat_close_input(&fmt);
        return false;
    }

    out_asset.path = path;
    out_asset.duration = (fmt->duration != AV_NOPTS_VALUE) ? (fmt->duration / static_cast<double>(AV_TIME_BASE)) : 0.0;

    for (unsigned int i = 0; i < fmt->nb_streams; ++i) {
        if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            out_asset.sample_rate = fmt->streams[i]->codecpar->sample_rate;
            out_asset.channels = fmt->streams[i]->codecpar->ch_layout.nb_channels;
            if (out_asset.channels <= 0) out_asset.channels = 2;
            break;
        }
    }

    avformat_close_input(&fmt);
    return true;
}

bool MediaDecoder::decode_thumbnail_rgba(const std::string& path, double at_sec, int& out_w, int& out_h, std::vector<uint8_t>& out_rgba) {
    DecodedVideoContext ctx;
    if (!ctx.open(path)) return false;

    out_w = 160;
    out_h = 90;
    return ctx.decode_frame_at(at_sec, out_w, out_h, out_rgba);
}

GLuint MediaDecoder::upload_rgba_texture(int w, int h, const std::vector<uint8_t>& rgba) {
    if (rgba.empty() || w <= 0 || h <= 0) return 0;
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    return tex;
}

GLuint MediaDecoder::create_thumbnail_texture(const std::string& path, double at_sec) {
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;
    if (!decode_thumbnail_rgba(path, at_sec, w, h, rgba)) {
        return 0;
    }
    return upload_rgba_texture(w, h, rgba);
}

bool MediaDecoder::generate_test_media(const std::string& video_path, const std::string& audio_path) {
    std::string v_cmd = "ffmpeg -y -f lavfi -i testsrc=duration=5:size=1280x720:rate=30 -c:v libx264 -pix_fmt yuv420p \"" + video_path + "\" > /dev/null 2>&1";
    std::string a_cmd = "ffmpeg -y -f lavfi -i \"sine=frequency=440:duration=5\" \"" + audio_path + "\" > /dev/null 2>&1";
    int r1 = std::system(v_cmd.c_str());
    int r2 = std::system(a_cmd.c_str());
    return (r1 == 0 && r2 == 0);
}
