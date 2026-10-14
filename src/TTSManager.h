#pragma once

#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <atomic>

class TTSManager {
public:
    TTSManager();
    ~TTSManager();

    static TTSManager& instance();

    bool synthesize(const std::string& text,
                    const std::string& output_wav,
                    float rate = 1.0f,
                    float pitch = 1.0f,
                    const std::string& voice = "",
                    std::string* error_msg = nullptr);

    void synthesize_async(const std::string& text,
                          const std::string& output_wav,
                          float rate,
                          float pitch,
                          const std::string& voice,
                          std::function<void(bool success, const std::string& path, const std::string& err)> on_complete);

    bool is_busy() const { return m_is_busy.load(); }

    static bool write_wav_file(const std::string& path, const std::vector<int16_t>& samples, int sample_rate, int channels = 1);
    static std::string detect_language_code(const std::string& text);

private:
    bool synthesize_local_espeak(const std::string& text,
                                const std::string& output_wav,
                                float rate,
                                float pitch,
                                const std::string& voice,
                                std::string* error_msg);

    bool synthesize_ai_gemini(const std::string& text,
                             const std::string& output_wav,
                             const std::string& api_key,
                             const std::string& voice,
                             std::string* error_msg);

    std::atomic<bool> m_is_busy{false};
    std::mutex m_mutex;
};
