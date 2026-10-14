#pragma once

#include "Common.h"
#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <atomic>

class AICore {
public:
    AICore();
    ~AICore();

    static AICore& instance();

    bool is_busy() const { return m_is_busy.load(); }
    std::string get_status() const;
    std::string get_last_output() const;

    // JavaScript script generation by prompt
    void generate_script_async(const std::string& prompt,
                              const std::string& api_key,
                              std::function<void(bool success, const std::string& script_path, const std::string& msg)> on_complete);

    // Low-level query helper
    static std::string query_gemini_api(const std::string& prompt,
                                        const std::string& system_instruction,
                                        const std::string& api_key,
                                        int timeout_sec = 25);

private:
    std::atomic<bool> m_is_busy{false};
    mutable std::mutex m_mutex;
    std::string m_status = "Idle";
    std::string m_last_output = "";
};
