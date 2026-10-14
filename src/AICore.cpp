#include "AICore.h"
#include <fstream>
#include <iostream>
#include <thread>
#include <chrono>
#include <regex>
#include <filesystem>
#include <curl/curl.h>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

AICore::AICore() = default;
AICore::~AICore() = default;

AICore& AICore::instance() {
    static AICore s_instance;
    return s_instance;
}

std::string AICore::get_status() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_status;
}

std::string AICore::get_last_output() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_last_output;
}

static size_t ai_curl_write_str(void* ptr, size_t size, size_t nmemb, void* userdata) {
    std::string* str = static_cast<std::string*>(userdata);
    str->append(static_cast<char*>(ptr), size * nmemb);
    return size * nmemb;
}

std::string AICore::query_gemini_api(const std::string& prompt,
                                     const std::string& system_instruction,
                                     const std::string& api_key,
                                     int timeout_sec) {
    if (api_key.empty()) {
        return "{\"error\": \"Gemini API key is not configured in Settings.\"}";
    }

    static const std::vector<std::string> s_models = {
        "gemini-flash-lite-latest",
        "gemini-flash-latest",
        "gemini-3.8-flash",
        "gemini-3.5-flash",
        "gemini-2.5-flash-lite"
    };

    std::string last_error_response;

    for (const auto& model_name : s_models) {
        CURL* curl = curl_easy_init();
        if (!curl) {
            return "{\"error\": \"Failed to initialize curl network client.\"}";
        }

        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        std::string key_hdr = "x-goog-api-key: " + api_key;
        headers = curl_slist_append(headers, key_hdr.c_str());

        char* escaped_key = curl_easy_escape(curl, api_key.c_str(), static_cast<int>(api_key.length()));
        std::string url = "https://generativelanguage.googleapis.com/v1beta/models/" + model_name + ":generateContent?key=" + std::string(escaped_key);
        curl_free(escaped_key);

        nlohmann::json root;
        if (!system_instruction.empty()) {
            root["system_instruction"] = {
                {"parts", {{{"text", system_instruction}}}}
            };
        }

        root["contents"] = nlohmann::json::array({
            {
                {"parts", nlohmann::json::array({
                    {{"text", prompt}}
                })}
            }
        });

        std::string request_body = root.dump();
        std::string response_body;

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_body.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, ai_curl_write_str);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(timeout_sec));
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
                        return cand["content"]["parts"][0].value("text", "");
                    }
                }
            } catch (...) {}
            return response_body;
        }

        last_error_response = response_body.empty() ? ("Curl error code: " + std::to_string(res)) : response_body;
        // If HTTP 503 or 404, fallback to next model
        if (http_code != 503 && http_code != 404) {
            break;
        }
    }

    std::string err_detail = last_error_response;
    try {
        nlohmann::json err_json = nlohmann::json::parse(last_error_response);
        if (err_json.contains("error") && err_json["error"].contains("message")) {
            err_detail = err_json["error"].value("message", "");
        }
    } catch (...) {}

    return "{\"error\": \"" + err_detail + "\"}";
}

void AICore::generate_script_async(const std::string& prompt,
                                   const std::string& api_key,
                                   std::function<void(bool success, const std::string& script_path, const std::string& msg)> on_complete) {
    m_is_busy.store(true);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_status = "Generating JavaScript plugin via Gemini AI...";
    }

    std::thread([this, prompt, api_key, on_complete]() {
        std::string sys =
            "You are an expert JavaScript developer for minicut video editor. "
            "The editor supports Duktape JavaScript plugins with global modules: "
            "gui (button, text, slider_float, checkbox, same_line, separator), "
            "timeline (place_clip, playhead, track_count, select_all), "
            "clip (set_pos, get_pos, set_scale, set_rotation, set_opacity, set_volume), "
            "project (save, load, get_duration), "
            "console (log, warn, error). "
            "A plugin implements plugin:init_panel() or top-level UI functions. "
            "Return ONLY executable JavaScript code, without markdown formatting or code fences.";

        std::string code = query_gemini_api(prompt, sys, api_key);

        // Strip markdown code fences
        if (code.find("```") != std::string::npos) {
            std::regex fence_re(R"(```(?:javascript|js)?\s*([\s\S]*?)```)");
            std::smatch match;
            if (std::regex_search(code, match, fence_re)) {
                code = match[1].str();
            }
        }

        bool has_error = (code.empty() || code.find("\"error\":") != std::string::npos);
        std::string script_path;
        bool ok = false;
        std::string msg;

        if (!has_error) {
            const char* home = std::getenv("HOME");
            std::string scripts_dir = (home ? (std::string(home) + "/.config/minicut/scripts") : "scripts");
            std::error_code ec;
            fs::create_directories(scripts_dir, ec);

            uint64_t ts = static_cast<uint64_t>(std::chrono::system_clock::now().time_since_epoch().count());
            script_path = scripts_dir + "/ai_tool_" + std::to_string(ts) + ".js";

            std::ofstream ofs(script_path);
            if (ofs.is_open()) {
                ofs << code;
                ok = true;
                msg = "Generated JavaScript plugin saved to " + script_path;
            } else {
                msg = "Failed to write script file to disk.";
            }
        } else {
            msg = "Gemini API error during script generation.";
        }

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_status = "Idle";
            m_last_output = code;
        }
        m_is_busy.store(false);

        if (on_complete) {
            on_complete(ok, script_path, msg);
        }
    }).detach();
}
