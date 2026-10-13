#include "TempManager.h"
#include <filesystem>
#include <cstdlib>
#include <chrono>
#include <iostream>

namespace fs = std::filesystem;

std::string TempManager::get_temp_dir() {
    const char* home = std::getenv("HOME");
    fs::path base;
    if (home && home[0] != '\0') {
        base = fs::path(home) / ".config" / "minicut" / "temp";
    } else {
        base = fs::temp_directory_path() / "minicut_temp";
    }

    std::error_code ec;
    fs::create_directories(base, ec);
    return base.string();
}

std::string TempManager::get_temp_file_path(const std::string& prefix, const std::string& ext) {
    fs::path dir = get_temp_dir();
    auto ts = std::chrono::system_clock::now().time_since_epoch().count();
    fs::path p = dir / (prefix + "_" + std::to_string(ts) + ext);
    return p.string();
}

size_t TempManager::cleanup_temp_files(uintmax_t* out_bytes_freed) {
    size_t count = 0;
    uintmax_t bytes = 0;
    fs::path dir = get_temp_dir();
    std::error_code ec;

    if (fs::exists(dir, ec)) {
        for (const auto& entry : fs::directory_iterator(dir, fs::directory_options::skip_permission_denied, ec)) {
            if (entry.is_regular_file(ec)) {
                uintmax_t sz = entry.file_size(ec);
                if (!ec) bytes += sz;
                if (fs::remove(entry.path(), ec)) {
                    count++;
                }
            }
        }
    }

    if (out_bytes_freed) {
        *out_bytes_freed = bytes;
    }
    return count;
}

size_t TempManager::get_temp_file_count(uintmax_t* out_bytes_total) {
    size_t count = 0;
    uintmax_t bytes = 0;
    fs::path dir = get_temp_dir();
    std::error_code ec;

    if (fs::exists(dir, ec)) {
        for (const auto& entry : fs::directory_iterator(dir, fs::directory_options::skip_permission_denied, ec)) {
            if (entry.is_regular_file(ec)) {
                uintmax_t sz = entry.file_size(ec);
                if (!ec) bytes += sz;
                count++;
            }
        }
    }

    if (out_bytes_total) {
        *out_bytes_total = bytes;
    }
    return count;
}
