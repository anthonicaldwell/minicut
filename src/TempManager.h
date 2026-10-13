#pragma once

#include <string>
#include <cstdint>

class TempManager {
public:
    // Returns ~/.config/minicut/temp/ (created if does not exist)
    static std::string get_temp_dir();

    // Generates a unique file path within ~/.config/minicut/temp/
    static std::string get_temp_file_path(const std::string& prefix, const std::string& ext);

    // Deletes all temporary files in ~/.config/minicut/temp/
    // Returns count of deleted files, and optionally total bytes freed.
    static size_t cleanup_temp_files(uintmax_t* out_bytes_freed = nullptr);

    // Returns count of temporary files and total size in bytes
    static size_t get_temp_file_count(uintmax_t* out_bytes_total = nullptr);
};
