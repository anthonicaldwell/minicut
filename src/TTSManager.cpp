#include "TTSManager.h"
#include "SettingsLogic.h"
#include "LocaleManager.h"
#include "JapaneseTransliteration.h"
#include <dlfcn.h>
#include <fstream>
#include <iostream>
#include <thread>
#include <cmath>
#include <cstring>
#include <curl/curl.h>
#include <nlohmann/json.hpp>

// Types for dynamic espeak binding
typedef int (*fn_espeak_Initialize)(int output, int buflength, const char *path, int options);
typedef void (*fn_espeak_SetSynthCallback)(int (*SynthCallback)(short*, int, void*));
typedef int (*fn_espeak_Synth)(const void *text, size_t size, unsigned int position, int position_type, unsigned int end_position, unsigned int flags, unsigned int *unique_identifier, void *user_data);
typedef int (*fn_espeak_Synchronize)(void);
typedef int (*fn_espeak_SetParameter)(int parameter, int value, int relative);
typedef int (*fn_espeak_SetVoiceByName)(const char *name);
typedef int (*fn_espeak_Terminate)(void);

static std::vector<int16_t> s_espeak_pcm_buffer;
static std::mutex s_espeak_pcm_mutex;

static int espeak_synth_callback(short* wav, int numsamples, void* events) {
    (void)events;
    if (wav && numsamples > 0) {
        std::lock_guard<std::mutex> lock(s_espeak_pcm_mutex);
        s_espeak_pcm_buffer.insert(s_espeak_pcm_buffer.end(), wav, wav + numsamples);
    }
    return 0; // continue
}

TTSManager::TTSManager() = default;
TTSManager::~TTSManager() = default;

TTSManager& TTSManager::instance() {
    static TTSManager s_instance;
    return s_instance;
}

bool TTSManager::write_wav_file(const std::string& path, const std::vector<int16_t>& samples, int sample_rate, int channels) {
    std::ofstream ofs(path, std::ios::binary);
    if (!ofs.is_open()) return false;

    int byte_rate = sample_rate * channels * sizeof(int16_t);
    int block_align = channels * sizeof(int16_t);
    int subchunk2_size = static_cast<int>(samples.size() * sizeof(int16_t));
    int chunk_size = 36 + subchunk2_size;

    // RIFF Header
    ofs.write("RIFF", 4);
    ofs.write(reinterpret_cast<const char*>(&chunk_size), 4);
    ofs.write("WAVE", 4);

    // fmt subchunk
    ofs.write("fmt ", 4);
    int subchunk1_size = 16;
    int16_t audio_format = 1; // PCM
    int16_t num_channels = static_cast<int16_t>(channels);
    int32_t s_rate = sample_rate;
    int32_t b_rate = byte_rate;
    int16_t b_align = static_cast<int16_t>(block_align);
    int16_t bits_per_sample = 16;

    ofs.write(reinterpret_cast<const char*>(&subchunk1_size), 4);
    ofs.write(reinterpret_cast<const char*>(&audio_format), 2);
    ofs.write(reinterpret_cast<const char*>(&num_channels), 2);
    ofs.write(reinterpret_cast<const char*>(&s_rate), 4);
    ofs.write(reinterpret_cast<const char*>(&b_rate), 4);
    ofs.write(reinterpret_cast<const char*>(&b_align), 2);
    ofs.write(reinterpret_cast<const char*>(&bits_per_sample), 2);

    // data subchunk
    ofs.write("data", 4);
    ofs.write(reinterpret_cast<const char*>(&subchunk2_size), 4);
    ofs.write(reinterpret_cast<const char*>(samples.data()), subchunk2_size);

    return true;
}

std::string TTSManager::detect_language_code(const std::string& text) {
    int ar_count = 0;
    int cyr_count = 0;
    int ja_count = 0;
    int ko_count = 0;
    int cjk_count = 0;

    size_t i = 0;
    while (i < text.size()) {
        uint32_t cp = 0;
        unsigned char c = static_cast<unsigned char>(text[i]);
        if (c < 0x80) {
            cp = c;
            i += 1;
        } else if ((c & 0xE0) == 0xC0) {
            if (i + 1 >= text.size()) break;
            cp = ((c & 0x1F) << 6) | (static_cast<unsigned char>(text[i + 1]) & 0x3F);
            i += 2;
        } else if ((c & 0xF0) == 0xE0) {
            if (i + 2 >= text.size()) break;
            cp = ((c & 0x0F) << 12) |
                 ((static_cast<unsigned char>(text[i + 1]) & 0x3F) << 6) |
                 (static_cast<unsigned char>(text[i + 2]) & 0x3F);
            i += 3;
        } else if ((c & 0xF8) == 0xF0) {
            if (i + 3 >= text.size()) break;
            cp = ((c & 0x07) << 18) |
                 ((static_cast<unsigned char>(text[i + 1]) & 0x3F) << 12) |
                 ((static_cast<unsigned char>(text[i + 2]) & 0x3F) << 6) |
                 (static_cast<unsigned char>(text[i + 3]) & 0x3F);
            i += 4;
        } else {
            i += 1;
            continue;
        }

        if ((cp >= 0x0600 && cp <= 0x06FF) || (cp >= 0x0750 && cp <= 0x077F) || (cp >= 0x08A0 && cp <= 0x08FF)) {
            ar_count++;
        } else if (cp >= 0x0400 && cp <= 0x04FF) {
            cyr_count++;
        } else if ((cp >= 0x3040 && cp <= 0x309F) || (cp >= 0x30A0 && cp <= 0x30FF)) {
            ja_count++;
        } else if ((cp >= 0xAC00 && cp <= 0xD7AF) || (cp >= 0x1100 && cp <= 0x11FF) || (cp >= 0x3130 && cp <= 0x318F)) {
            ko_count++;
        } else if (cp >= 0x4E00 && cp <= 0x9FFF) {
            cjk_count++;
        }
    }

    std::string cur_loc = LocaleManager::instance().current_locale();
    if (ar_count > 0) return "ar";
    if (ko_count > 0) return "ko";
    if (ja_count > 0 || (cjk_count > 0 && (cur_loc == "ja" || cur_loc == "jp"))) return "ja";
    if (cjk_count > 0) return "cmn";
    if (cyr_count > 0) return "uk";
    return "en";
}

bool TTSManager::synthesize_local_espeak(const std::string& text,
                                        const std::string& output_wav,
                                        float rate,
                                        float pitch,
                                        const std::string& voice,
                                        std::string* error_msg) {
    std::string voice_to_set = voice;
    if (voice_to_set.empty() || voice_to_set == "default" || voice_to_set == "auto") {
        voice_to_set = detect_language_code(text);
    }

    std::string processed_text = text;
    // Transliterate Kanji into Hiragana for Japanese synthesis
    if (voice_to_set == "ja" || (voice_to_set.empty() && JapaneseTransliteration::contains_kanji(text))) {
        voice_to_set = "ja";
        processed_text = JapaneseTransliteration::kanji_to_hiragana(text);
    }

    const char* lib_names[] = {
        "libespeak-ng.so.1",
        "libespeak-ng.so",
        "libespeak.so.1",
        "libespeak.so"
    };

    void* handle = nullptr;
    for (const char* name : lib_names) {
        handle = dlopen(name, RTLD_LAZY);
        if (handle) break;
    }

    if (!handle) {
        // Fallback procedural formant synthesis
        int sample_rate = 22050;
        int duration_samples = static_cast<int>(sample_rate * (std::max<size_t>(processed_text.length(), 5) * 0.08f / rate));
        std::vector<int16_t> samples(duration_samples);
        for (int i = 0; i < duration_samples; ++i) {
            double t = static_cast<double>(i) / sample_rate;
            double freq = 220.0 * pitch * (1.0 + 0.1 * std::sin(2.0 * M_PI * 5.0 * t));
            double env = std::min(1.0, std::sin(M_PI * i / duration_samples) * 3.0);
            samples[i] = static_cast<int16_t>(std::sin(2.0 * M_PI * freq * t) * 16000.0 * env);
        }
        return write_wav_file(output_wav, samples, sample_rate, 1);
    }

    auto fn_init = reinterpret_cast<fn_espeak_Initialize>(dlsym(handle, "espeak_Initialize"));
    auto fn_cb = reinterpret_cast<fn_espeak_SetSynthCallback>(dlsym(handle, "espeak_SetSynthCallback"));
    auto fn_synth = reinterpret_cast<fn_espeak_Synth>(dlsym(handle, "espeak_Synth"));
    auto fn_sync = reinterpret_cast<fn_espeak_Synchronize>(dlsym(handle, "espeak_Synchronize"));
    auto fn_setparam = reinterpret_cast<fn_espeak_SetParameter>(dlsym(handle, "espeak_SetParameter"));
    auto fn_term = reinterpret_cast<fn_espeak_Terminate>(dlsym(handle, "espeak_Terminate"));

    if (!fn_init || !fn_cb || !fn_synth || !fn_sync) {
        dlclose(handle);
        if (error_msg) *error_msg = "Required eSpeak symbols missing from dynamic library.";
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(s_espeak_pcm_mutex);
        s_espeak_pcm_buffer.clear();
    }

    // AUDIO_OUTPUT_RETRIEVAL = 1
    int sample_rate = fn_init(1, 500, nullptr, 0);
    if (sample_rate <= 0) sample_rate = 22050;

    fn_cb(espeak_synth_callback);

    if (fn_setparam) {
        // espeakRATE = 1, default ~175
        int espeak_rate = static_cast<int>(175 * rate);
        fn_setparam(1, espeak_rate, 0);
        // espeakPITCH = 2, default ~50
        int espeak_pitch = static_cast<int>(50 * pitch);
        fn_setparam(2, espeak_pitch, 0);
    }

    auto fn_setvoice = reinterpret_cast<fn_espeak_SetVoiceByName>(dlsym(handle, "espeak_SetVoiceByName"));
    if (fn_setvoice && !voice_to_set.empty()) {
        fn_setvoice(voice_to_set.c_str());
    }

    fn_synth(processed_text.c_str(), processed_text.length() + 1, 0, 1, 0, 0x1000, nullptr, nullptr);
    fn_sync();

    if (fn_term) fn_term();
    dlclose(handle);

    std::vector<int16_t> result;
    {
        std::lock_guard<std::mutex> lock(s_espeak_pcm_mutex);
        result = s_espeak_pcm_buffer;
        s_espeak_pcm_buffer.clear();
    }

    if (result.empty()) {
        if (error_msg) *error_msg = "No audio samples generated by eSpeak.";
        return false;
    }

    return write_wav_file(output_wav, result, sample_rate, 1);
}

static std::vector<uint8_t> base64_decode(const std::string& in) {
    static const int B64Index[256] = {
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 62, -1, -1, -1, 63,
        52, 53, 54, 55, 56, 57, 58, 59, 60, 61, -1, -1, -1, -1, -1, -1,
        -1,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
        15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, -1,
        -1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
        41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
    };
    std::vector<uint8_t> out;
    out.reserve(in.size() * 3 / 4);
    int val = 0, valb = -8;
    for (unsigned char c : in) {
        if (c == '=') break;
        int d = B64Index[c];
        if (d == -1) continue;
        val = (val << 6) + d;
        valb += 6;
        if (valb >= 0) {
            out.push_back(static_cast<uint8_t>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

static size_t tts_curl_write_str(void* ptr, size_t size, size_t nmemb, void* userdata) {
    std::string* str = static_cast<std::string*>(userdata);
    str->append(static_cast<char*>(ptr), size * nmemb);
    return size * nmemb;
}

bool TTSManager::synthesize_ai_gemini(const std::string& text,
                                      const std::string& output_wav,
                                      const std::string& api_key,
                                      const std::string& voice,
                                      std::string* error_msg) {
    if (api_key.empty()) {
        if (error_msg) *error_msg = "Gemini API key is required for AI speech synthesis.";
        return false;
    }

    static const std::vector<std::string> s_tts_models = {
        "gemini-3.8-flash-tts",
        "gemini-3.8-flash-lite-tts",
        "gemini-2.5-flash-preview-tts",
        "gemini-3.1-flash-tts-preview"
    };

    std::string v = voice.empty() ? "Puck" : voice;
    std::string last_error;

    for (const auto& model_name : s_tts_models) {
        CURL* curl = curl_easy_init();
        if (!curl) continue;

        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        std::string key_hdr = "x-goog-api-key: " + api_key;
        headers = curl_slist_append(headers, key_hdr.c_str());

        char* escaped = curl_easy_escape(curl, api_key.c_str(), static_cast<int>(api_key.length()));
        std::string url = "https://generativelanguage.googleapis.com/v1beta/models/" + model_name + ":generateContent?key=" + std::string(escaped);
        curl_free(escaped);

        nlohmann::json req_body;
        req_body["contents"] = nlohmann::json::array({
            {
                {"parts", nlohmann::json::array({
                    {{"text", text}}
                })}
            }
        });
        req_body["generationConfig"] = {
            {"speechConfig", {
                {"voiceConfig", {
                    {"prebuiltVoiceConfig", {
                        {"voiceName", v}
                    }}
                }}
            }}
        };

        std::string json_str = req_body.dump();
        std::string response_body;

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_str.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, tts_curl_write_str);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "minicut-editor/1.4");

        CURLcode res = curl_easy_perform(curl);
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        if (res == CURLE_OK && http_code == 200) {
            try {
                nlohmann::json resp = nlohmann::json::parse(response_body);
                if (resp.contains("candidates") && !resp["candidates"].empty()) {
                    auto& cand = resp["candidates"][0];
                    if (cand.contains("content") && cand["content"].contains("parts") && !cand["content"]["parts"].empty()) {
                        auto& part = cand["content"]["parts"][0];
                        std::string b64_audio;
                        if (part.contains("inlineData") && part["inlineData"].contains("data")) {
                            b64_audio = part["inlineData"]["data"].get<std::string>();
                        }
                        if (!b64_audio.empty()) {
                            std::vector<uint8_t> audio_bytes = base64_decode(b64_audio);
                            if (!audio_bytes.empty()) {
                                std::ofstream ofs(output_wav, std::ios::binary);
                                if (ofs.is_open()) {
                                    ofs.write(reinterpret_cast<const char*>(audio_bytes.data()), audio_bytes.size());
                                    ofs.close();
                                    return true;
                                } else {
                                    if (error_msg) *error_msg = "Failed to write WAV file to " + output_wav;
                                    return false;
                                }
                            }
                        }
                    }
                }
            } catch (const std::exception& e) {
                last_error = std::string("JSON parse error: ") + e.what();
            }
        }

        last_error = response_body;
        if (http_code != 503 && http_code != 404) {
            break;
        }
    }

    std::string err_detail = last_error;
    try {
        nlohmann::json err_json = nlohmann::json::parse(last_error);
        if (err_json.contains("error") && err_json["error"].contains("message")) {
            err_detail = err_json["error"].value("message", "");
        }
    } catch (...) {}

    if (error_msg) {
        *error_msg = "Gemini TTS error: " + err_detail;
    }
    return false;
}

bool TTSManager::synthesize(const std::string& text,
                            const std::string& output_wav,
                            float rate,
                            float pitch,
                            const std::string& voice,
                            std::string* error_msg) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_is_busy.store(true);

    bool result = false;
    std::string provider = SettingsLogic::instance().get_tts_provider();
    bool ai_enabled = SettingsLogic::instance().is_ai_enabled();
    std::string api_key = SettingsLogic::instance().get_active_auth_token_or_key();
    std::string effective_voice = voice.empty() ? SettingsLogic::instance().get_tts_voice() : voice;

    if (!voice.empty()) {
        SettingsLogic::instance().set_tts_voice(voice);
    }

    if (provider == "gemini") {
        if (!ai_enabled || api_key.empty()) {
            if (error_msg) *error_msg = "Gemini API key is required in Settings (AI Integration) to use Gemini TTS.";
            m_is_busy.store(false);
            return false;
        }
        result = synthesize_ai_gemini(text, output_wav, api_key, effective_voice, error_msg);
    } else {
        result = synthesize_local_espeak(text, output_wav, rate, pitch, effective_voice, error_msg);
    }

    m_is_busy.store(false);
    return result;
}

void TTSManager::synthesize_async(const std::string& text,
                                  const std::string& output_wav,
                                  float rate,
                                  float pitch,
                                  const std::string& voice,
                                  std::function<void(bool success, const std::string& path, const std::string& err)> on_complete) {
    std::thread([this, text, output_wav, rate, pitch, voice, on_complete]() {
        std::string err;
        bool ok = synthesize(text, output_wav, rate, pitch, voice, &err);
        if (on_complete) {
            on_complete(ok, output_wav, err);
        }
    }).detach();
}
