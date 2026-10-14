#include "FileBrowser.h"
#include "MediaDecoder.h"
#include "MD5.h"
#include <imgui.h>
#include <iostream>
#include <algorithm>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <set>

namespace fs = std::filesystem;

static std::string format_file_size(uintmax_t bytes) {
    if (bytes < 1024) return std::to_string(bytes) + " B";
    double kb = bytes / 1024.0;
    if (kb < 1024.0) {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(1) << kb << " KB";
        return ss.str();
    }
    double mb = kb / 1024.0;
    if (mb < 1024.0) {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(1) << mb << " MB";
        return ss.str();
    }
    double gb = mb / 1024.0;
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2) << gb << " GB";
    return ss.str();
}

FileBrowser::FileBrowser() {
    try {
        m_current_dir = fs::current_path();
    } catch (...) {
        m_current_dir = "/";
    }
}

FileBrowser::~FileBrowser() {
    clear_thumbnail_cache();
}

void FileBrowser::clear_thumbnail_cache() {
    for (auto& pair : m_thumbnail_cache) {
        if (pair.second != 0) {
            glDeleteTextures(1, &pair.second);
        }
    }
    m_thumbnail_cache.clear();
}

std::string FileBrowser::find_system_thumbnail(const std::string& full_path) const {
    std::error_code ec;
    fs::path canonical_p = fs::canonical(full_path, ec);
    if (ec) {
        canonical_p = fs::absolute(full_path, ec);
    }
    std::string path_str = canonical_p.string();

    // FreeDesktop URI: file:// + percent-encoded path
    std::string uri = "file://";
    for (char c : path_str) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '/' || c == '.' ||
            c == '_' || c == '-' || c == '~') {
            uri.push_back(c);
        } else {
            std::ostringstream ss;
            ss << '%' << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
               << static_cast<int>(static_cast<unsigned char>(c));
            uri += ss.str();
        }
    }

    std::string hash_str = compute_md5_hex(uri);

    // FreeDesktop standard cache directories: large (256x256), normal (128x128), etc.
    std::vector<fs::path> base_dirs;
    const char* xdg_cache = std::getenv("XDG_CACHE_HOME");
    if (xdg_cache && xdg_cache[0] != '\0') {
        base_dirs.push_back(fs::path(xdg_cache) / "thumbnails");
    }
    const char* home = std::getenv("HOME");
    if (home && home[0] != '\0') {
        base_dirs.push_back(fs::path(home) / ".cache" / "thumbnails");
    }

    std::vector<std::string> sub_sizes = { "large", "normal", "x-large", "xx-large" };
    for (const auto& base : base_dirs) {
        for (const auto& sz : sub_sizes) {
            fs::path candidate = base / sz / (hash_str + ".png");
            if (fs::exists(candidate, ec) && fs::is_regular_file(candidate, ec)) {
                return candidate.string();
            }
        }
    }
    return "";
}

GLuint FileBrowser::get_or_load_thumbnail(const std::string& full_path, bool is_video, bool is_image) {
    if (!is_video && !is_image) return 0;
    auto it = m_thumbnail_cache.find(full_path);
    if (it != m_thumbnail_cache.end()) {
        return it->second;
    }

    // 1. Try pulling from FreeDesktop / system file manager thumbnail cache first
    std::string sys_thumb = find_system_thumbnail(full_path);
    if (!sys_thumb.empty()) {
        GLuint tex = MediaDecoder::create_thumbnail_texture(sys_thumb, 0.0);
        if (tex != 0) {
            m_thumbnail_cache[full_path] = tex;
            return tex;
        }
    }

    // 2. Fallback to decoding via FFmpeg
    GLuint tex = MediaDecoder::create_thumbnail_texture(full_path, is_video ? 0.5 : 0.0);
    m_thumbnail_cache[full_path] = tex;
    return tex;
}

void FileBrowser::refresh_mounted_devices() {
    m_mounted_devices.clear();
    std::set<std::string> seen_paths;

    auto add_mount = [&](const std::string& mount_path, const std::string& dev_name, const std::string& dev_type) {
        if (seen_paths.count(mount_path)) return;
        if (!fs::exists(mount_path)) return;

        MountedDevice dev;
        dev.path = mount_path;
        dev.name = dev_name;
        dev.type = dev_type;

        std::error_code ec;
        auto si = fs::space(mount_path, ec);
        if (!ec) {
            dev.total_space = si.capacity;
            dev.free_space = si.available;
        }
        seen_paths.insert(mount_path);
        m_mounted_devices.push_back(dev);
    };

    // 1. Scan standard Linux removable media folders (/run/media/$USER, /media/$USER, /mnt)
    const char* user_env = std::getenv("USER");
    std::vector<fs::path> media_dirs;
    if (user_env) {
        media_dirs.push_back(fs::path("/run/media") / user_env);
        media_dirs.push_back(fs::path("/media") / user_env);
    }
    media_dirs.push_back("/run/media");
    media_dirs.push_back("/media");
    media_dirs.push_back("/mnt");

    for (const auto& mdir : media_dirs) {
        if (fs::exists(mdir)) {
            try {
                for (const auto& entry : fs::directory_iterator(mdir, fs::directory_options::skip_permission_denied)) {
                    if (entry.is_directory()) {
                        std::string name = entry.path().filename().string();
                        if (name.empty() || name[0] == '.') continue;
                        std::string type = (name.find("SD") != std::string::npos || name.find("sd") != std::string::npos) ? "SD Card" : "Removable Drive";
                        add_mount(entry.path().string(), name, type);
                    }
                }
            } catch (...) {}
        }
    }

    // 2. Parse /proc/mounts to capture all mounted physical disks & MMC cards
    std::ifstream mounts_file("/proc/mounts");
    if (mounts_file.is_open()) {
        std::string line;
        while (std::getline(mounts_file, line)) {
            std::istringstream iss(line);
            std::string dev_node, mnt_point, fstype;
            if (iss >> dev_node >> mnt_point >> fstype) {
                if (mnt_point == "/" || mnt_point.rfind("/snap", 0) == 0 ||
                    mnt_point.rfind("/boot", 0) == 0 || mnt_point.rfind("/sys", 0) == 0 ||
                    mnt_point.rfind("/proc", 0) == 0 || mnt_point.rfind("/dev", 0) == 0 ||
                    mnt_point.rfind("/etc", 0) == 0) {
                    continue;
                }

                if (dev_node.rfind("/dev/mmcblk", 0) == 0) {
                    std::string name = fs::path(mnt_point).filename().string();
                    if (name.empty()) name = dev_node;
                    add_mount(mnt_point, name, "SD Card (" + fstype + ")");
                } else if (dev_node.rfind("/dev/sd", 0) == 0 || dev_node.rfind("/dev/nvme", 0) == 0) {
                    if (mnt_point.find("/media") != std::string::npos || mnt_point.find("/mnt") != std::string::npos) {
                        std::string name = fs::path(mnt_point).filename().string();
                        if (name.empty()) name = dev_node;
                        add_mount(mnt_point, name, "Drive (" + fstype + ")");
                    }
                }
            }
        }
    }
}

void FileBrowser::open(const std::string& title,
                       FileBrowserMode mode,
                       const std::vector<FileFilter>& filters,
                       const std::string& default_filename,
                       const std::string& default_extension,
                       const std::function<void(const std::string& path)>& on_confirm,
                       const std::string& confirm_btn_label,
                       const std::function<void()>& on_cancel) {
    m_title = title;
    m_mode = mode;
    m_filters = filters;
    m_current_filter_idx = 0;
    m_default_ext = default_extension;
    m_on_confirm = on_confirm;
    m_confirm_btn_label = confirm_btn_label;
    m_on_cancel = on_cancel;
    m_is_open = true;

    strncpy(m_filename_input, default_filename.c_str(), sizeof(m_filename_input) - 1);
    m_search_filter[0] = '\0';

    try {
        if (!fs::exists(m_current_dir)) {
            m_current_dir = fs::current_path();
        }
    } catch (...) {
        m_current_dir = "/";
    }

    strncpy(m_path_input, m_current_dir.string().c_str(), sizeof(m_path_input) - 1);
    refresh_mounted_devices();
    refresh_directory();
}

void FileBrowser::cancel() {
    if (m_on_cancel) {
        auto cb = m_on_cancel;
        m_on_cancel = nullptr;
        cb();
    }
    close();
}

void FileBrowser::close() {
    if (m_on_cancel) {
        auto cb = m_on_cancel;
        m_on_cancel = nullptr;
        cb();
    }
    clear_thumbnail_cache();
    m_is_open = false;
}

bool FileBrowser::matches_filter(const std::string& filename, const std::string& ext) const {
    if (m_filters.empty()) return true;
    if (m_current_filter_idx < 0 || m_current_filter_idx >= static_cast<int>(m_filters.size())) return true;

    const auto& filter = m_filters[m_current_filter_idx];
    if (filter.exts.empty()) return true; // All files

    std::string lower_ext = ext;
    std::transform(lower_ext.begin(), lower_ext.end(), lower_ext.begin(), ::tolower);

    for (const auto& e : filter.exts) {
        if (e == ".*" || e == "*") return true;
        std::string target = e;
        std::transform(target.begin(), target.end(), target.begin(), ::tolower);
        if (lower_ext == target) return true;
    }
    return false;
}

void FileBrowser::refresh_directory() {
    clear_thumbnail_cache();
    m_items.clear();
    try {
        for (const auto& entry : fs::directory_iterator(m_current_dir, fs::directory_options::skip_permission_denied)) {
            FileItem item;
            item.name = entry.path().filename().string();
            // Skip hidden dotfiles except parent
            if (!item.name.empty() && item.name[0] == '.' && item.name != "..") {
                continue;
            }

            std::error_code ec;
            item.is_directory = entry.is_directory(ec);
            if (!item.is_directory) {
                item.size = entry.file_size(ec);
                item.size_str = format_file_size(item.size);
                item.extension = entry.path().extension().string();

                item.is_video = is_video_file(item.extension);
                item.is_image = is_image_file(item.extension);
                item.is_audio = is_audio_file(item.extension);
                std::string ext_lower = item.extension;
                std::transform(ext_lower.begin(), ext_lower.end(), ext_lower.begin(), ::tolower);
                if (ext_lower == ".minicut") {
                    item.is_project = true;
                }
            } else {
                item.size_str = "<DIR>";
                item.extension = "";
            }

            if (item.is_directory || matches_filter(item.name, item.extension)) {
                m_items.push_back(item);
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Directory access error: " << e.what() << std::endl;
    }

    // Sort: directories first, then alphabetical
    std::sort(m_items.begin(), m_items.end(), [](const FileItem& a, const FileItem& b) {
        if (a.is_directory != b.is_directory) {
            return a.is_directory > b.is_directory;
        }
        return a.name < b.name;
    });
}

void FileBrowser::render() {
    if (!m_is_open) return;

    ImVec2 center = ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f);
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(880, 560), ImGuiCond_Appearing);
    ImGui::OpenPopup(m_title.c_str());

    bool popup_open = true;
    if (ImGui::BeginPopupModal(m_title.c_str(), &popup_open, ImGuiWindowFlags_NoCollapse)) {
        if (!popup_open) {
            cancel();
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }

        // Top Navigation Bar
        if (ImGui::Button("< Up")) {
            if (m_current_dir.has_parent_path()) {
                m_current_dir = m_current_dir.parent_path();
                strncpy(m_path_input, m_current_dir.string().c_str(), sizeof(m_path_input) - 1);
                refresh_directory();
            }
        }
        ImGui::SameLine();

        if (ImGui::Button("Home")) {
            const char* home = std::getenv("HOME");
            if (home && fs::exists(home)) {
                m_current_dir = home;
                strncpy(m_path_input, m_current_dir.string().c_str(), sizeof(m_path_input) - 1);
                refresh_directory();
            }
        }
        ImGui::SameLine();

        if (ImGui::Button("Refresh")) {
            refresh_mounted_devices();
            refresh_directory();
        }
        ImGui::SameLine();

        // Path input bar
        float avail_path_w = ImGui::GetContentRegionAvail().x - 170.0f;
        if (avail_path_w < 100.0f) avail_path_w = 100.0f;
        ImGui::SetNextItemWidth(avail_path_w);
        if (ImGui::InputText("##PathBar", m_path_input, sizeof(m_path_input), ImGuiInputTextFlags_EnterReturnsTrue)) {
            if (fs::exists(m_path_input) && fs::is_directory(m_path_input)) {
                m_current_dir = m_path_input;
                refresh_directory();
            }
        }

        ImGui::SameLine();
        ImGui::SetNextItemWidth(160.0f);
        if (ImGui::InputTextWithHint("##SearchFilter", "Search...", m_search_filter, sizeof(m_search_filter))) {
            // Live search
        }

        ImGui::Separator();

        // Left sidebar bookmarks & Right table layout
        float sidebar_w = 170.0f;
        float content_h = ImGui::GetContentRegionAvail().y - 80.0f;
        if (content_h < 150.0f) content_h = 150.0f;

        // Bookmarks Sidebar
        ImGui::BeginChild("BookmarksList", ImVec2(sidebar_w, content_h), true);
        ImGui::TextDisabled("PLACES");
        ImGui::Separator();

        auto add_bookmark = [&](const char* name, const fs::path& p) {
            if (fs::exists(p)) {
                if (ImGui::Selectable(name, m_current_dir == p)) {
                    m_current_dir = p;
                    strncpy(m_path_input, m_current_dir.string().c_str(), sizeof(m_path_input) - 1);
                    refresh_directory();
                }
            }
        };

        add_bookmark("Current Dir", fs::current_path());
        const char* home_env = std::getenv("HOME");
        if (home_env) {
            fs::path home_path = home_env;
            add_bookmark("Home (~)", home_path);
            add_bookmark("Videos", home_path / "Videos");
            add_bookmark("Music", home_path / "Music");
            add_bookmark("Desktop", home_path / "Desktop");
            add_bookmark("Documents", home_path / "Documents");
            add_bookmark("Downloads", home_path / "Downloads");
        }
        add_bookmark("Root (/)", "/");

        // Mounted Media Section
        if (!m_mounted_devices.empty()) {
            ImGui::Spacing();
            ImGui::Spacing();
            ImGui::TextDisabled("MOUNTED MEDIA");
            ImGui::Separator();
            for (const auto& dev : m_mounted_devices) {
                std::string icon = (dev.type.find("SD") != std::string::npos) ? "[SD] " : "[USB] ";
                std::string label = icon + dev.name;
                bool is_active = (m_current_dir.string() == dev.path || m_current_dir.string().find(dev.path + "/") == 0);
                if (ImGui::Selectable(label.c_str(), is_active)) {
                    m_current_dir = dev.path;
                    strncpy(m_path_input, m_current_dir.string().c_str(), sizeof(m_path_input) - 1);
                    refresh_directory();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s\nPath: %s\nCapacity: %s (%s free)",
                                      dev.type.c_str(), dev.path.c_str(),
                                      format_file_size(dev.total_space).c_str(),
                                      format_file_size(dev.free_space).c_str());
                }
            }
        }

        ImGui::EndChild();

        ImGui::SameLine();

        // File List Table
        std::string search_str = m_search_filter;
        std::transform(search_str.begin(), search_str.end(), search_str.begin(), ::tolower);

        static ImGuiTableFlags table_flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                             ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;

        bool confirm_requested = false;

        if (ImGui::BeginTable("FileTable", 4, table_flags, ImVec2(0, content_h))) {
            ImGui::TableSetupColumn("Thumb", ImGuiTableColumnFlags_WidthFixed, 48.0f);
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 75.0f);
            ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 85.0f);
            ImGui::TableHeadersRow();

            for (const auto& item : m_items) {
                std::string item_lower = item.name;
                std::transform(item_lower.begin(), item_lower.end(), item_lower.begin(), ::tolower);
                if (!search_str.empty() && item_lower.find(search_str) == std::string::npos) {
                    continue;
                }

                ImGui::TableNextRow();

                // Column 0: Thumbnail / Preview
                ImGui::TableSetColumnIndex(0);
                if (item.is_directory) {
                    ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.2f, 1.0f), "[DIR]");
                } else if (item.is_video || item.is_image) {
                    std::string item_full_path = (m_current_dir / item.name).string();
                    GLuint thumb_tex = get_or_load_thumbnail(item_full_path, item.is_video, item.is_image);
                    if (thumb_tex != 0) {
                        ImGui::Image((ImTextureID)(intptr_t)thumb_tex, ImVec2(40, 24));
                    } else if (item.is_video) {
                        ImGui::TextColored(ImVec4(0.3f, 0.7f, 1.0f, 1.0f), "[VID]");
                    } else {
                        ImGui::TextColored(ImVec4(0.9f, 0.65f, 0.2f, 1.0f), "[IMG]");
                    }
                } else if (item.is_audio) {
                    ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.5f, 1.0f), "[AUD]");
                } else if (item.is_project) {
                    ImGui::TextColored(ImVec4(0.85f, 0.45f, 1.0f, 1.0f), "[PRJ]");
                } else {
                    ImGui::TextDisabled("[FILE]");
                }

                // Column 1: Name
                ImGui::TableSetColumnIndex(1);
                bool is_selected = (std::string(m_filename_input) == item.name);

                if (ImGui::Selectable(item.name.c_str(), is_selected, ImGuiSelectableFlags_SpanAllColumns)) {
                    if (!item.is_directory) {
                        strncpy(m_filename_input, item.name.c_str(), sizeof(m_filename_input) - 1);
                    }
                }

                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                    if (item.is_directory) {
                        m_current_dir /= item.name;
                        strncpy(m_path_input, m_current_dir.string().c_str(), sizeof(m_path_input) - 1);
                        refresh_directory();
                        break;
                    } else {
                        strncpy(m_filename_input, item.name.c_str(), sizeof(m_filename_input) - 1);
                        confirm_requested = true;
                    }
                }

                // Column 2: Type
                ImGui::TableSetColumnIndex(2);
                if (item.is_directory) ImGui::Text("Folder");
                else if (item.is_video) ImGui::TextColored(ImVec4(0.3f, 0.7f, 1.0f, 1.0f), "Video");
                else if (item.is_image) ImGui::TextColored(ImVec4(0.9f, 0.65f, 0.2f, 1.0f), "Image");
                else if (item.is_audio) ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.5f, 1.0f), "Audio");
                else if (item.is_project) ImGui::TextColored(ImVec4(0.85f, 0.45f, 1.0f, 1.0f), "Project");
                else ImGui::Text("%s", item.extension.empty() ? "File" : item.extension.c_str());

                // Column 3: Size
                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%s", item.size_str.c_str());
            }

            ImGui::EndTable();
        }

        ImGui::Separator();

        // Selected Media Thumbnail / Specs Preview Card
        std::string sel_name = m_filename_input;
        if (!sel_name.empty()) {
            fs::path sel_path = m_current_dir / sel_name;
            if (fs::exists(sel_path) && !fs::is_directory(sel_path)) {
                std::string ext = sel_path.extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                bool is_v = (ext == ".mp4" || ext == ".mkv" || ext == ".mov" || ext == ".avi" || ext == ".webm" || ext == ".flv" || ext == ".wmv" || ext == ".m4v" || ext == ".ts" || ext == ".ogv" || ext == ".ogm");
                bool is_i = (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".webp");
                if (is_v || is_i) {
                    GLuint sel_tex = get_or_load_thumbnail(sel_path.string(), is_v, is_i);
                    if (sel_tex != 0) {
                        ImGui::Image((ImTextureID)(intptr_t)sel_tex, ImVec2(56, 32));
                        ImGui::SameLine();
                    }
                }
            }
        }

        // Bottom File input and Filter selection
        ImGui::Text("File name:");
        ImGui::SameLine();
        float fname_w = ImGui::GetContentRegionAvail().x - 220.0f;
        if (fname_w < 100.0f) fname_w = 100.0f;
        ImGui::SetNextItemWidth(fname_w);
        if (ImGui::InputText("##FileNameInput", m_filename_input, sizeof(m_filename_input), ImGuiInputTextFlags_EnterReturnsTrue)) {
            confirm_requested = true;
        }

        ImGui::SameLine();
        ImGui::SetNextItemWidth(210.0f);
        if (!m_filters.empty()) {
            std::vector<const char*> filter_labels;
            for (const auto& f : m_filters) filter_labels.push_back(f.name.c_str());
            if (ImGui::Combo("##FilterCombo", &m_current_filter_idx, filter_labels.data(), static_cast<int>(filter_labels.size()))) {
                refresh_directory();
            }
        }

        // Action Buttons
        std::string confirm_text = m_confirm_btn_label.empty() ? ((m_mode == FileBrowserMode::OpenFile) ? "Open" : "Save") : m_confirm_btn_label;
        if (ImGui::Button(confirm_text.c_str(), ImVec2(100, 26)) || confirm_requested) {
            std::string fname = m_filename_input;
            if (!fname.empty()) {
                fs::path full_path = m_current_dir / fname;

                // If in save mode and no extension was provided, append default extension
                if (m_mode == FileBrowserMode::SaveFile && !m_default_ext.empty() && full_path.extension().empty()) {
                    full_path += m_default_ext;
                }

                m_on_cancel = nullptr; // Confirmed, do not invoke cancel callback
                if (m_on_confirm) {
                    m_on_confirm(full_path.string());
                }
                close();
                ImGui::CloseCurrentPopup();
            }
        }

        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(80, 26)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            cancel();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

bool FileBrowser::is_video_file(const std::string& path) {
    std::string ext = std::filesystem::path(path).extension().string();
    if (ext.empty() && !path.empty() && path[0] == '.') ext = path;
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return (ext == ".mp4" || ext == ".mkv" || ext == ".mov" ||
            ext == ".avi" || ext == ".webm" || ext == ".flv" ||
            ext == ".wmv" || ext == ".m4v" || ext == ".ts" ||
            ext == ".ogv" || ext == ".ogm");
}

bool FileBrowser::is_audio_file(const std::string& path) {
    std::string ext = std::filesystem::path(path).extension().string();
    if (ext.empty() && !path.empty() && path[0] == '.') ext = path;
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return (ext == ".mp3" || ext == ".wav" || ext == ".aac" ||
            ext == ".flac" || ext == ".ogg" || ext == ".m4a" ||
            ext == ".opus");
}

bool FileBrowser::is_image_file(const std::string& path) {
    std::string ext = std::filesystem::path(path).extension().string();
    if (ext.empty() && !path.empty() && path[0] == '.') ext = path;
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return (ext == ".png" || ext == ".jpg" || ext == ".jpeg" ||
            ext == ".bmp" || ext == ".webp" || ext == ".tiff" ||
            ext == ".tif" || ext == ".gif");
}

