#include "SwapManager.h"
#include <cstdlib>
#include <chrono>

namespace fs = std::filesystem;

SwapManager& SwapManager::instance() {
    static SwapManager s_instance;
    return s_instance;
}

SwapManager::SwapManager() {
    init();
}

SwapManager::~SwapManager() {
    shutdown();
}

void SwapManager::init() {
    const char* home = std::getenv("HOME");
    if (home) {
        m_swap_dir = std::string(home) + "/.config/minicut/swap";
    } else {
        m_swap_dir = "./swap";
    }

    try {
        if (!fs::exists(m_swap_dir)) {
            fs::create_directories(m_swap_dir);
        } else {
            // Clean up any stale swap files left over from prior abnormal terminations
            for (const auto& entry : fs::directory_iterator(m_swap_dir)) {
                if (entry.is_regular_file()) {
                    std::string filename = entry.path().filename().string();
                    if (filename.rfind("swap_", 0) == 0 && entry.path().extension() == ".bin") {
                        fs::remove(entry.path());
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "[SwapManager] Error initializing swap directory: " << e.what() << std::endl;
    }
}

void SwapManager::shutdown() {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& path : m_active_files) {
        if (!path.empty() && fs::exists(path)) {
            try {
                fs::remove(path);
            } catch (...) {}
        }
    }
    m_active_files.clear();

    if (!m_swap_dir.empty() && fs::exists(m_swap_dir)) {
        try {
            for (const auto& entry : fs::directory_iterator(m_swap_dir)) {
                if (entry.is_regular_file()) {
                    std::string filename = entry.path().filename().string();
                    if (filename.rfind("swap_", 0) == 0 && entry.path().extension() == ".bin") {
                        fs::remove(entry.path());
                    }
                }
            }
        } catch (...) {}
    }
}

void SwapManager::reinitialize() {
    shutdown();
    init();
}

std::string SwapManager::allocate_swap_file(const std::string& prefix) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!fs::exists(m_swap_dir)) {
        try {
            fs::create_directories(m_swap_dir);
        } catch (...) {}
    }

    uint64_t count = ++m_file_counter;
    uint64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    pid_t pid = getpid();

    std::string filename = prefix + "_" + std::to_string(pid) + "_" +
                           std::to_string(now_ms) + "_" + std::to_string(count) + ".bin";
    std::string full_path = m_swap_dir + "/" + filename;

    m_active_files.push_back(full_path);
    return full_path;
}

void SwapManager::register_swap_file(const std::string& path) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (std::find(m_active_files.begin(), m_active_files.end(), path) == m_active_files.end()) {
        m_active_files.push_back(path);
    }
}

void SwapManager::unregister_swap_file(const std::string& path) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = std::find(m_active_files.begin(), m_active_files.end(), path);
    if (it != m_active_files.end()) {
        m_active_files.erase(it);
    }
}

std::string SwapManager::get_swap_dir() const {
    return m_swap_dir;
}

uintmax_t SwapManager::get_total_swap_bytes() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    uintmax_t total = 0;
    for (const auto& path : m_active_files) {
        if (fs::exists(path)) {
            try {
                total += fs::file_size(path);
            } catch (...) {}
        }
    }
    return total;
}
