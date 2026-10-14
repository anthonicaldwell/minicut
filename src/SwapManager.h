#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <algorithm>
#include <filesystem>
#include <iostream>

class SwapManager {
public:
    static SwapManager& instance();

    // Initialize swap directory (~/.config/minicut/swap/) and clean up stale swap files
    void init();

    // Shutdown and delete all active swap files
    void shutdown();

    // Reinitialize swap system
    void reinitialize();

    // Allocate a unique swap file path in ~/.config/minicut/swap/
    std::string allocate_swap_file(const std::string& prefix = "swap");

    // Explicit registration/unregistration of swap files
    void register_swap_file(const std::string& path);
    void unregister_swap_file(const std::string& path);

    // Get the configured swap directory path
    std::string get_swap_dir() const;

    // Get total bytes currently allocated across active swap files
    uintmax_t get_total_swap_bytes() const;

private:
    SwapManager();
    ~SwapManager();

    std::string m_swap_dir;
    mutable std::mutex m_mutex;
    std::vector<std::string> m_active_files;
    std::atomic<uint64_t> m_file_counter{0};
};

template<typename T>
class SwapBuffer {
public:
    SwapBuffer() = default;

    explicit SwapBuffer(size_t initial_size, const T& initial_val = T()) {
        resize(initial_size, initial_val);
    }

    ~SwapBuffer() {
        clear_and_close();
    }

    SwapBuffer(const SwapBuffer&) = delete;
    SwapBuffer& operator=(const SwapBuffer&) = delete;

    SwapBuffer(SwapBuffer&& other) noexcept {
        move_from(std::move(other));
    }

    SwapBuffer& operator=(SwapBuffer&& other) noexcept {
        if (this != &other) {
            clear_and_close();
            move_from(std::move(other));
        }
        return *this;
    }

    size_t size() const { return m_size; }
    size_t capacity() const { return m_capacity; }
    bool empty() const { return m_size == 0; }

    T* data() { return m_use_fallback ? m_fallback.data() : m_data; }
    const T* data() const { return m_use_fallback ? m_fallback.data() : m_data; }

    T& operator[](size_t idx) {
        return m_use_fallback ? m_fallback[idx] : m_data[idx];
    }

    const T& operator[](size_t idx) const {
        return m_use_fallback ? m_fallback[idx] : m_data[idx];
    }

    void reserve(size_t new_cap);
    void resize(size_t new_size, const T& default_val = T());
    void push_back(const T& val);
    void append(const T* src, size_t count);
    void clear();

    const std::string& get_swap_path() const { return m_path; }
    bool is_disk_backed() const { return !m_use_fallback && m_fd >= 0; }

private:
    std::string m_path;
    int m_fd = -1;
    T* m_data = nullptr;
    size_t m_size = 0;
    size_t m_capacity = 0;
    bool m_use_fallback = false;
    std::vector<T> m_fallback;

    void clear_and_close();
    void move_from(SwapBuffer&& other) noexcept;
    void ensure_capacity(size_t req_cap);
};

template<typename T>
void SwapBuffer<T>::clear_and_close() {
    if (m_data && m_capacity > 0) {
        munmap(m_data, m_capacity * sizeof(T));
        m_data = nullptr;
    }
    if (m_fd >= 0) {
        close(m_fd);
        m_fd = -1;
    }
    if (!m_path.empty()) {
        unlink(m_path.c_str());
        SwapManager::instance().unregister_swap_file(m_path);
        m_path.clear();
    }
    m_size = 0;
    m_capacity = 0;
    m_fallback.clear();
    m_use_fallback = false;
}

template<typename T>
void SwapBuffer<T>::move_from(SwapBuffer&& other) noexcept {
    m_path = std::move(other.m_path);
    m_fd = other.m_fd;
    m_data = other.m_data;
    m_size = other.m_size;
    m_capacity = other.m_capacity;
    m_use_fallback = other.m_use_fallback;
    m_fallback = std::move(other.m_fallback);

    other.m_fd = -1;
    other.m_data = nullptr;
    other.m_size = 0;
    other.m_capacity = 0;
    other.m_use_fallback = false;
}

template<typename T>
void SwapBuffer<T>::ensure_capacity(size_t req_cap) {
    if (m_use_fallback) {
        m_fallback.reserve(req_cap);
        m_capacity = m_fallback.capacity();
        return;
    }

    if (req_cap <= m_capacity) return;

    size_t new_cap = std::max(req_cap, m_capacity == 0 ? static_cast<size_t>(4096) : m_capacity * 2);
    size_t bytes = new_cap * sizeof(T);
    long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) page_size = 4096;
    bytes = ((bytes + page_size - 1) / page_size) * page_size;
    new_cap = bytes / sizeof(T);

    if (m_fd < 0) {
        m_path = SwapManager::instance().allocate_swap_file("swap_buf");
        m_fd = open(m_path.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0600);
        if (m_fd < 0) {
            m_use_fallback = true;
            m_fallback.reserve(req_cap);
            m_capacity = m_fallback.capacity();
            return;
        }
    }

    if (ftruncate(m_fd, bytes) != 0) {
        if (m_data && m_capacity > 0) munmap(m_data, m_capacity * sizeof(T));
        m_data = nullptr;
        close(m_fd);
        m_fd = -1;
        unlink(m_path.c_str());
        m_use_fallback = true;
        m_fallback.reserve(req_cap);
        m_capacity = m_fallback.capacity();
        return;
    }

    if (m_data && m_capacity > 0) {
        munmap(m_data, m_capacity * sizeof(T));
        m_data = nullptr;
    }

    void* mapped = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, m_fd, 0);
    if (mapped == MAP_FAILED) {
        close(m_fd);
        m_fd = -1;
        unlink(m_path.c_str());
        m_use_fallback = true;
        m_fallback.reserve(req_cap);
        m_capacity = m_fallback.capacity();
        return;
    }

    m_data = static_cast<T*>(mapped);
    m_capacity = new_cap;
}

template<typename T>
void SwapBuffer<T>::reserve(size_t new_cap) {
    if (new_cap > m_capacity) {
        ensure_capacity(new_cap);
    }
}

template<typename T>
void SwapBuffer<T>::resize(size_t new_size, const T& default_val) {
    if (m_use_fallback) {
        m_fallback.resize(new_size, default_val);
        m_size = m_fallback.size();
        m_capacity = m_fallback.capacity();
        return;
    }

    if (new_size > m_capacity) {
        ensure_capacity(new_size);
    }

    if (new_size > m_size && m_data) {
        for (size_t i = m_size; i < new_size; ++i) {
            m_data[i] = default_val;
        }
    }
    m_size = new_size;
}

template<typename T>
void SwapBuffer<T>::push_back(const T& val) {
    if (m_use_fallback) {
        m_fallback.push_back(val);
        m_size = m_fallback.size();
        m_capacity = m_fallback.capacity();
        return;
    }

    if (m_size >= m_capacity) {
        ensure_capacity(m_size + 1);
    }
    if (m_data) {
        m_data[m_size++] = val;
    }
}

template<typename T>
void SwapBuffer<T>::append(const T* src, size_t count) {
    if (count == 0 || !src) return;
    if (m_use_fallback) {
        m_fallback.insert(m_fallback.end(), src, src + count);
        m_size = m_fallback.size();
        m_capacity = m_fallback.capacity();
        return;
    }

    if (m_size + count > m_capacity) {
        ensure_capacity(m_size + count);
    }
    if (m_data) {
        std::memcpy(m_data + m_size, src, count * sizeof(T));
        m_size += count;
    }
}

template<typename T>
void SwapBuffer<T>::clear() {
    m_size = 0;
    if (m_use_fallback) {
        m_fallback.clear();
    }
}
