#include "ScriptEngine.h"
#include "Common.h"
#include "Timeline.h"
#include "ClipManager.h"
#include "MediaDecoder.h"
#include "ProjectSerializer.h"
#include "App.h"
#include "NativeFileDialog.h"
#include "TextGenerator.h"
#include "LocaleManager.h"
#include "SettingsLogic.h"
#include "ShaderManager.h"
#include "AICore.h"
#include "imgui.h"
#include <nlohmann/json.hpp>
#include <curl/curl.h>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace fs = std::filesystem;

static ScriptEngine* s_engine_instance = nullptr;

ScriptEngine::ScriptEngine() {
    s_engine_instance = this;
}

ScriptEngine::~ScriptEngine() {
    if (m_shader_thread) {
        if (m_shader_thread->joinable()) {
            m_shader_thread->join();
        }
        delete m_shader_thread;
        m_shader_thread = nullptr;
    }
    shutdown();
    if (s_engine_instance == this) {
        s_engine_instance = nullptr;
    }
}

bool ScriptEngine::init(Project* project,
                        Timeline* timeline,
                        ClipManager* clip_manager,
                        double* playhead_time,
                        std::string* selected_clip_id,
                        std::function<void()> on_push_undo,
                        std::function<void()> on_project_modified,
                        std::function<void(double)> on_set_playhead,
                        std::function<void(const std::string&, double)> on_set_status,
                        std::function<void(const std::string&)> on_select_clip,
                        std::function<bool(const std::string&)> on_save_project,
                        std::function<bool(const std::string&)> on_load_project) {
    m_project = project;
    m_timeline = timeline;
    m_clip_manager = clip_manager;
    m_playhead_time = playhead_time;
    m_selected_clip_id = selected_clip_id;
    m_on_push_undo = on_push_undo;
    m_on_project_modified = on_project_modified;
    m_on_set_playhead = on_set_playhead;
    m_on_set_status = on_set_status;
    m_on_select_clip = on_select_clip;
    m_on_save_project = on_save_project;
    m_on_load_project = on_load_project;

    s_engine_instance = this;

    curl_global_init(CURL_GLOBAL_ALL);

    // Determine paths
    const char* home = std::getenv("HOME");
    std::string base_cfg = home ? (std::string(home) + "/.config/minicut") : ".";
    m_scripts_dir = base_cfg + "/scripts";
    m_autorun_dir = base_cfg + "/autorun";
    m_icons_dir = base_cfg + "/icons";

    // Ensure all directories exist
    std::error_code ec;
    fs::create_directories(m_scripts_dir, ec);
    fs::create_directories(m_autorun_dir, ec);
    fs::create_directories(m_icons_dir, ec);

    // Initialize Lua state
    m_L = luaL_newstate();
    if (!m_L) {
        std::cerr << "[ScriptEngine] Failed to create Lua state!" << std::endl;
        return false;
    }

    luaL_openlibs(m_L);
    register_lua_api();

    // Initialize JavaScript (Duktape) state
    m_ctx = duk_create_heap_default();
    if (m_ctx) {
        register_js_api();
    } else {
        std::cerr << "[ScriptEngine] Failed to create Duktape JS context!" << std::endl;
    }

    // Scan directories & load autorun scripts
    reload_scripts();

    return true;
}

void ScriptEngine::shutdown() {
    clear_textures();
    if (m_L) {
        lua_close(m_L);
        m_L = nullptr;
    }
    if (m_ctx) {
        duk_destroy_heap(m_ctx);
        m_ctx = nullptr;
    }
    curl_global_cleanup();
}

void ScriptEngine::clear_textures() {
    for (auto& s : m_autorun_scripts) {
        if (s.icon_texture != 0) {
            glDeleteTextures(1, &s.icon_texture);
            s.icon_texture = 0;
        }
    }
    for (auto& s : m_menu_scripts) {
        if (s.icon_texture != 0) {
            glDeleteTextures(1, &s.icon_texture);
            s.icon_texture = 0;
        }
    }
}

static std::string format_title_from_stem(const std::string& stem) {
    std::string title = stem;
    for (char& c : title) {
        if (c == '_' || c == '-') c = ' ';
    }
    bool cap = true;
    for (char& c : title) {
        if (cap && c >= 'a' && c <= 'z') {
            c = c - ('a' - 'A');
            cap = false;
        } else if (c == ' ') {
            cap = true;
        }
    }
    return title;
}

void ScriptEngine::scan_directories() {
    clear_textures();
    m_autorun_scripts.clear();
    m_menu_scripts.clear();

    std::error_code ec;

    // 1. Autorun scripts in ~/.config/minicut/autorun/
    if (fs::exists(m_autorun_dir, ec)) {
        for (const auto& entry : fs::directory_iterator(m_autorun_dir, ec)) {
            std::string ext = entry.path().extension().string();
            if (entry.is_regular_file() && (ext == ".lua" || ext == ".js")) {
                ScriptItem item;
                item.is_javascript = (ext == ".js");
                item.id = entry.path().stem().string() + (item.is_javascript ? "_js" : "_lua");
                item.filename = entry.path().filename().string();
                item.path = entry.path().string();
                item.title = format_title_from_stem(entry.path().stem().string()) + (item.is_javascript ? " (JavaScript)" : " (Lua)");
                item.description = "Autorun script: " + item.filename;
                item.is_autorun = true;
                item.is_loaded = false;
                item.is_panel_open = false;
                item.icon_texture = 0;
                m_autorun_scripts.push_back(item);
            }
        }
    }

    std::sort(m_autorun_scripts.begin(), m_autorun_scripts.end(), [](const ScriptItem& a, const ScriptItem& b) {
        return a.title < b.title;
    });

    // 2. Menu scripts in ~/.config/minicut/scripts/
    if (fs::exists(m_scripts_dir, ec)) {
        for (const auto& entry : fs::directory_iterator(m_scripts_dir, ec)) {
            std::string ext = entry.path().extension().string();
            if (entry.is_regular_file() && (ext == ".lua" || ext == ".js")) {
                ScriptItem item;
                item.is_javascript = (ext == ".js");
                item.id = entry.path().stem().string() + (item.is_javascript ? "_js" : "_lua");
                item.filename = entry.path().filename().string();
                item.path = entry.path().string();
                item.title = format_title_from_stem(entry.path().stem().string()) + (item.is_javascript ? " (JavaScript)" : " (Lua)");
                item.description = "Script: " + item.filename;
                item.is_autorun = false;
                item.is_loaded = false;
                item.is_panel_open = false;
                item.icon_texture = 0;
                m_menu_scripts.push_back(item);
            }
        }
    }

    std::sort(m_menu_scripts.begin(), m_menu_scripts.end(), [](const ScriptItem& a, const ScriptItem& b) {
        return a.title < b.title;
    });
}

void ScriptEngine::load_autorun_icons() {
    std::error_code ec;
    const char* home = std::getenv("HOME");
    std::vector<std::string> search_dirs = {
        m_icons_dir,
        m_autorun_dir,
        m_scripts_dir
    };
    if (home) {
        search_dirs.push_back(std::string(home) + "/.local/share/minicut/icons");
    }
    search_dirs.push_back("/usr/local/share/minicut/icons");
    search_dirs.push_back("/usr/share/minicut/icons");
    search_dirs.push_back("icons");

    for (auto& item : m_autorun_scripts) {
        item.icon_checked = true;
        if (item.icon_texture != 0) continue;

        std::string raw_stem = fs::path(item.filename).stem().string();
        for (const auto& dir : search_dirs) {
            std::vector<std::string> candidates = {
                dir + "/" + item.id + ".png",
                dir + "/" + raw_stem + ".png",
                dir + "/" + item.id + ".jpg",
                dir + "/" + raw_stem + ".jpg",
                dir + "/" + item.id + ".jpeg",
                dir + "/" + raw_stem + ".jpeg",
                dir + "/" + item.filename + ".png"
            };
            bool loaded = false;
            for (const auto& icon_path : candidates) {
                if (fs::exists(icon_path, ec)) {
                    GLuint tex = MediaDecoder::create_thumbnail_texture(icon_path, 0.0);
                    if (tex != 0) {
                        item.icon_texture = tex;
                        loaded = true;
                        break;
                    }
                }
            }
            if (loaded) break;
        }
    }
}

void ScriptEngine::reload_scripts() {
    scan_directories();
    load_autorun_icons();

    // Auto-execute all autorun scripts at startup/reload
    for (auto& script : m_autorun_scripts) {
        execute_script(script);
    }
}

bool ScriptEngine::execute_script(ScriptItem& item) {
    if (item.is_javascript) {
        return execute_js_script(item);
    }
    if (!m_L) return false;

    // Setup Lua environment: create table plugins if not exists
    lua_getglobal(m_L, "plugins");
    if (!lua_istable(m_L, -1)) {
        lua_pop(m_L, 1);
        lua_newtable(m_L);
        lua_setglobal(m_L, "plugins");
        lua_getglobal(m_L, "plugins");
    }

    // Create plugin table:
    // plugin = { id = item.id, name = item.filename, title = item.title, path = item.path }
    lua_newtable(m_L);
    lua_pushstring(m_L, item.id.c_str());
    lua_setfield(m_L, -2, "id");
    lua_pushstring(m_L, item.filename.c_str());
    lua_setfield(m_L, -2, "name");
    lua_pushstring(m_L, item.title.c_str());
    lua_setfield(m_L, -2, "title");
    lua_pushstring(m_L, item.path.c_str());
    lua_setfield(m_L, -2, "path");

    // plugins[item.id] = plugin
    lua_pushvalue(m_L, -1);
    lua_setfield(m_L, -3, item.id.c_str());

    // set global plugin = plugin
    lua_setglobal(m_L, "plugin");
    lua_pop(m_L, 1); // pop plugins table

    // Execute script file
    if (luaL_dofile(m_L, item.path.c_str()) != 0) {
        const char* err = lua_tostring(m_L, -1);
        std::cerr << "[ScriptEngine] Error executing " << item.path << ": " << (err ? err : "unknown") << std::endl;
        set_status(std::string("Script error: ") + (err ? err : "unknown"));
        lua_pop(m_L, 1);
        return false;
    }

    item.is_loaded = true;
    return true;
}

bool ScriptEngine::run_file(const std::string& file_path) {
    if (!m_L) return false;
    if (luaL_dofile(m_L, file_path.c_str()) != 0) {
        const char* err = lua_tostring(m_L, -1);
        std::cerr << "[ScriptEngine] Error running file " << file_path << ": " << (err ? err : "unknown") << std::endl;
        set_status(std::string("Lua error: ") + (err ? err : "unknown"));
        lua_pop(m_L, 1);
        return false;
    }
    return true;
}

bool ScriptEngine::run_string(const std::string& lua_code) {
    if (!m_L) return false;

    // Ensure plugin table exists
    lua_getglobal(m_L, "plugin");
    if (!lua_istable(m_L, -1)) {
        lua_pop(m_L, 1);
        lua_newtable(m_L);
        lua_pushstring(m_L, "script"); lua_setfield(m_L, -2, "id");
        lua_pushstring(m_L, "Script Tool"); lua_setfield(m_L, -2, "title");
        lua_setglobal(m_L, "plugin");
    } else {
        lua_pop(m_L, 1);
    }

    if (luaL_dostring(m_L, lua_code.c_str()) != 0) {
        const char* err = lua_tostring(m_L, -1);
        std::cerr << "[ScriptEngine] Error running string: " << (err ? err : "unknown") << std::endl;
        set_status(std::string("Lua error: ") + (err ? err : "unknown"));
        lua_pop(m_L, 1);
        return false;
    }
    return true;
}

void ScriptEngine::update(double delta_time) {
    (void)delta_time;
    poll_shader_rendering();
}

void ScriptEngine::render_shelf_dock(float menu_h) {
    if (!m_shelf_visible) return;

    ImGuiIO& io = ImGui::GetIO();
    if (menu_h <= 0.0f) {
        menu_h = ImGui::GetFrameHeight();
    }
    float total_w = io.DisplaySize.x;

    ImGui::SetNextWindowPos(ImVec2(0, menu_h), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(total_w, m_shelf_height), ImGuiCond_Always);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
                             ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoScrollWithMouse |
                             ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 6.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 4.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    bool is_light = SettingsLogic::instance().get_light_theme();
    ImVec4 dock_bg = is_light ? ImVec4(0.92f, 0.93f, 0.94f, 1.0f) : ImVec4(0.13f, 0.14f, 0.16f, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, dock_bg);

    if (ImGui::Begin("##ToolShelfDock", nullptr, flags)) {
        // Dock Header Label
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(ImVec4(0.24f, 0.68f, 0.91f, 1.0f), "%s", _TR("dock_title"));
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();

        if (m_autorun_scripts.empty()) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("No autorun scripts in ~/.config/minicut/autorun/");
        } else {
            bool needs_load = false;
            for (const auto& s : m_autorun_scripts) {
                if (!s.icon_checked && s.icon_texture == 0) {
                    needs_load = true;
                    break;
                }
            }
            if (needs_load) {
                load_autorun_icons();
            }

            for (auto& script : m_autorun_scripts) {
                ImGui::PushID(script.id.c_str());

                bool clicked = false;
                if (script.icon_texture != 0) {
                    ImVec4 bg_col = script.is_panel_open ? ImVec4(0.24f, 0.68f, 0.91f, 0.45f) : ImVec4(0.18f, 0.20f, 0.23f, 0.6f);
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));
                    clicked = ImGui::ImageButton(("##btn_" + script.id).c_str(), (ImTextureID)(intptr_t)script.icon_texture, ImVec2(28, 28), ImVec2(0,0), ImVec2(1,1), bg_col);
                    ImGui::PopStyleVar();
                } else {
                    // Default 2-letter uppercase abbreviation
                    std::string abbr = script.title.substr(0, 2);
                    if (abbr.empty()) abbr = "FX";
                    std::transform(abbr.begin(), abbr.end(), abbr.begin(), ::toupper);
                    if (script.is_panel_open) {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.24f, 0.68f, 0.91f, 0.6f));
                    }
                    clicked = ImGui::Button(abbr.c_str(), ImVec2(32, 28));
                    if (script.is_panel_open) {
                        ImGui::PopStyleColor();
                    }
                }

                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s\nFile: %s\nClick to run / toggle tool panel", script.title.c_str(), script.filename.c_str());
                }

                if (clicked) {
                    if (!script.is_loaded) {
                        execute_script(script);
                    }
                    script.is_panel_open = !script.is_panel_open;
                    if (script.is_panel_open) {
                        set_status("Opened panel: " + script.title);
                    }
                }

                ImGui::SameLine();
                ImGui::PopID();
            }
        }

        // Shelf action controls on far right
        float right_w = 85.0f;
        if (ImGui::GetContentRegionAvail().x > right_w) {
            ImGui::SetCursorPosX(total_w - right_w);
            if (ImGui::Button("⟳ Reload", ImVec2(75, 28))) {
                reload_scripts();
                set_status("Scripts and tool shelf reloaded");
            }
        }

        // Bottom border line for tool shelf separation
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddLine(ImVec2(0, menu_h + m_shelf_height - 1.0f), ImVec2(total_w, menu_h + m_shelf_height - 1.0f), IM_COL32(45, 50, 58, 255), 1.0f);

        ImGui::End();
    }

    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
}

void ScriptEngine::render_scripts_menu() {
    if (m_menu_scripts.empty()) {
        ImGui::TextDisabled("%s", _TR("scripts_no_scripts"));
    } else {
        for (auto& script : m_menu_scripts) {
            std::string disp_title = _TR(script.title.c_str());
            std::string menu_id = disp_title + "##menu_" + script.id;
            if (ImGui::MenuItem(menu_id.c_str(), nullptr, script.is_panel_open)) {
                if (!script.is_loaded) {
                    execute_script(script);
                }
                script.is_panel_open = !script.is_panel_open;
                if (script.is_panel_open) {
                    set_status("Opened panel: " + script.title);
                }
            }
        }
    }

    ImGui::Separator();

    if (!m_autorun_scripts.empty()) {
        if (ImGui::BeginMenu(_TR("scripts_autorun_menu"))) {
            for (auto& script : m_autorun_scripts) {
                std::string disp_title = _TR(script.title.c_str());
                std::string menu_id = disp_title + "##autorun_menu_" + script.id;
                if (ImGui::MenuItem(menu_id.c_str(), nullptr, script.is_panel_open)) {
                    if (!script.is_loaded) {
                        execute_script(script);
                    }
                    script.is_panel_open = !script.is_panel_open;
                    if (script.is_panel_open) {
                        set_status("Opened panel: " + script.title);
                    }
                }
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
    }

    if (ImGui::MenuItem(_TR("scripts_reload"), "F5")) {
        reload_scripts();
        set_status("Reloaded all scripts");
    }

    if (ImGui::BeginMenu(_TR("scripts_directories"))) {
        std::string s_label = "Scripts: " + m_scripts_dir;
        std::string a_label = "Autorun: " + m_autorun_dir;
        std::string i_label = "Icons:   " + m_icons_dir;
        if (ImGui::MenuItem(s_label.c_str())) {
            std::error_code ec;
            std::filesystem::create_directories(m_scripts_dir, ec);
            std::string cmd = "xdg-open \"" + m_scripts_dir + "\" >/dev/null 2>&1 &";
            std::system(cmd.c_str());
        }
        if (ImGui::MenuItem(a_label.c_str())) {
            std::error_code ec;
            std::filesystem::create_directories(m_autorun_dir, ec);
            std::string cmd = "xdg-open \"" + m_autorun_dir + "\" >/dev/null 2>&1 &";
            std::system(cmd.c_str());
        }
        if (ImGui::MenuItem(i_label.c_str())) {
            std::error_code ec;
            std::filesystem::create_directories(m_icons_dir, ec);
            std::string cmd = "xdg-open \"" + m_icons_dir + "\" >/dev/null 2>&1 &";
            std::system(cmd.c_str());
        }
        ImGui::EndMenu();
    }
}


void ScriptEngine::render_script_panels(float total_w, float total_h, float top_offset) {
    auto render_panel_for_item = [&](ScriptItem& item) {
        if (!item.is_panel_open) return;

        // Default to the right side of preview/timeline workspace
        float default_w = 320.0f;
        float default_h = std::min(460.0f, total_h - 20.0f);
        float default_x = total_w - default_w - 10.0f;
        float default_y = top_offset + 10.0f;

        ImGui::SetNextWindowPos(ImVec2(default_x, default_y), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(default_w, default_h), ImGuiCond_FirstUseEver);

        std::string win_title = item.title + "###ScriptPanel_" + item.id;
        if (ImGui::Begin(win_title.c_str(), &item.is_panel_open)) {
            call_panel_render(item);
        }
        ImGui::End();
    };

    for (auto& item : m_autorun_scripts) {
        render_panel_for_item(item);
    }
    for (auto& item : m_menu_scripts) {
        render_panel_for_item(item);
    }
}

void ScriptEngine::call_panel_render(ScriptItem& item) {
    if (item.is_javascript) {
        call_js_panel_render(item);
        return;
    }
    if (!m_L) return;

    m_current_render_script_id = item.id;

    // 1. Look for plugins[item.id]
    lua_getglobal(m_L, "plugins");
    if (lua_istable(m_L, -1)) {
        lua_getfield(m_L, -1, item.id.c_str());
        if (lua_istable(m_L, -1)) {
            // Set global `plugin = plugins[item.id]` so the script can refer to plugin
            lua_pushvalue(m_L, -1);
            lua_setglobal(m_L, "plugin");

            // Look for `init_panel` method
            lua_getfield(m_L, -1, "init_panel");
            if (lua_isfunction(m_L, -1)) {
                // Call plugin:init_panel() passing plugin table as self
                lua_pushvalue(m_L, -2); // self = plugin table
                if (lua_pcall(m_L, 1, 0, 0) != 0) {
                    const char* err = lua_tostring(m_L, -1);
                    ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "Lua Error in plugin:init_panel:\n%s", err ? err : "unknown");
                    lua_pop(m_L, 1);
                }
                lua_pop(m_L, 2); // pop plugin table & plugins table
                return;
            }
            lua_pop(m_L, 1); // pop nil/non-function init_panel
        }
        lua_pop(m_L, 1); // pop plugin table or nil
    }
    lua_pop(m_L, 1); // pop plugins table

    // 2. Fallback: check global init_panel()
    lua_getglobal(m_L, "init_panel");
    if (lua_isfunction(m_L, -1)) {
        if (lua_pcall(m_L, 0, 0, 0) != 0) {
            const char* err = lua_tostring(m_L, -1);
            ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "Lua Error in init_panel:\n%s", err ? err : "unknown");
            lua_pop(m_L, 1);
        }
        return;
    }
    lua_pop(m_L, 1);

    // 3. Fallback: check global render_panel()
    lua_getglobal(m_L, "render_panel");
    if (lua_isfunction(m_L, -1)) {
        if (lua_pcall(m_L, 0, 0, 0) != 0) {
            const char* err = lua_tostring(m_L, -1);
            ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "Lua Error in render_panel:\n%s", err ? err : "unknown");
            lua_pop(m_L, 1);
        }
        return;
    }
    lua_pop(m_L, 1);

    ImGui::TextDisabled("No plugin:init_panel() defined in script.");
}

// ============================================================================
// Lua API Bindings Registration
// ============================================================================

void ScriptEngine::register_lua_api() {
    if (!m_L) return;

    // 1. `gui` Table
    lua_newtable(m_L);
    lua_pushcfunction(m_L, lua_gui_text);             lua_setfield(m_L, -2, "text");
    lua_pushcfunction(m_L, lua_gui_text_colored);     lua_setfield(m_L, -2, "text_colored");
    lua_pushcfunction(m_L, lua_gui_text_disabled);    lua_setfield(m_L, -2, "text_disabled");
    lua_pushcfunction(m_L, lua_gui_button);           lua_setfield(m_L, -2, "button");
    lua_pushcfunction(m_L, lua_gui_small_button);     lua_setfield(m_L, -2, "small_button");
    lua_pushcfunction(m_L, lua_gui_slider_float);     lua_setfield(m_L, -2, "slider_float");
    lua_pushcfunction(m_L, lua_gui_slider_int);       lua_setfield(m_L, -2, "slider_int");
    lua_pushcfunction(m_L, lua_gui_drag_float);       lua_setfield(m_L, -2, "drag_float");
    lua_pushcfunction(m_L, lua_gui_drag_int);         lua_setfield(m_L, -2, "drag_int");
    lua_pushcfunction(m_L, lua_gui_input_text);       lua_setfield(m_L, -2, "input_text");
    lua_pushcfunction(m_L, lua_gui_input_float);      lua_setfield(m_L, -2, "input_float");
    lua_pushcfunction(m_L, lua_gui_checkbox);         lua_setfield(m_L, -2, "checkbox");
    lua_pushcfunction(m_L, lua_gui_same_line);        lua_setfield(m_L, -2, "same_line");
    lua_pushcfunction(m_L, lua_gui_separator);        lua_setfield(m_L, -2, "separator");
    lua_pushcfunction(m_L, lua_gui_spacing);          lua_setfield(m_L, -2, "spacing");
    lua_pushcfunction(m_L, lua_gui_tooltip);          lua_setfield(m_L, -2, "tooltip");
    lua_pushcfunction(m_L, lua_gui_set_next_item_width); lua_setfield(m_L, -2, "set_next_item_width");
    lua_pushcfunction(m_L, lua_gui_file_dialog);         lua_setfield(m_L, -2, "file_dialog");
    lua_pushcfunction(m_L, lua_gui_native_file_dialog);  lua_setfield(m_L, -2, "native_file_dialog");
    lua_pushcfunction(m_L, lua_gui_builtin_file_dialog); lua_setfield(m_L, -2, "builtin_file_dialog");
    lua_pushcfunction(m_L, lua_gui_popout_preview);      lua_setfield(m_L, -2, "popout_preview");
    lua_pushcfunction(m_L, lua_gui_is_preview_popped_out); lua_setfield(m_L, -2, "is_preview_popped_out");
    lua_pushcfunction(m_L, lua_gui_set_preview_aspect_mode); lua_setfield(m_L, -2, "set_preview_aspect_mode");
    lua_pushcfunction(m_L, lua_gui_get_preview_aspect_mode); lua_setfield(m_L, -2, "get_preview_aspect_mode");
    lua_pushcfunction(m_L, lua_gui_set_transform_tool);  lua_setfield(m_L, -2, "set_transform_tool");
    lua_pushcfunction(m_L, lua_gui_get_transform_tool);  lua_setfield(m_L, -2, "get_transform_tool");
    lua_setglobal(m_L, "gui");

    // 2. `timeline` Table
    lua_newtable(m_L);
    lua_pushcfunction(m_L, lua_timeline_get_playhead);    lua_setfield(m_L, -2, "get_playhead");
    lua_pushcfunction(m_L, lua_timeline_set_playhead);    lua_setfield(m_L, -2, "set_playhead");
    lua_pushcfunction(m_L, lua_timeline_get_duration);    lua_setfield(m_L, -2, "get_duration");
    lua_pushcfunction(m_L, lua_timeline_get_track_count); lua_setfield(m_L, -2, "get_track_count");
    lua_pushcfunction(m_L, lua_timeline_add_track);       lua_setfield(m_L, -2, "add_track");
    lua_pushcfunction(m_L, lua_timeline_place_video);     lua_setfield(m_L, -2, "place_video");
    lua_pushcfunction(m_L, lua_timeline_place_audio);     lua_setfield(m_L, -2, "place_audio");
    lua_pushcfunction(m_L, lua_timeline_place_image);     lua_setfield(m_L, -2, "place_image");
    lua_pushcfunction(m_L, lua_timeline_place_text_clip); lua_setfield(m_L, -2, "place_text_clip");
    lua_pushcfunction(m_L, lua_timeline_get_selected_clip); lua_setfield(m_L, -2, "get_selected_clip");
    lua_pushcfunction(m_L, lua_timeline_get_selected_clips); lua_setfield(m_L, -2, "get_selected_clips");
    lua_pushcfunction(m_L, lua_timeline_select_clip);     lua_setfield(m_L, -2, "select_clip");
    lua_pushcfunction(m_L, lua_timeline_select_clips);    lua_setfield(m_L, -2, "select_clips");
    lua_pushcfunction(m_L, lua_timeline_clear_selection); lua_setfield(m_L, -2, "clear_selection");
    lua_pushcfunction(m_L, lua_timeline_select_all);      lua_setfield(m_L, -2, "select_all");
    lua_pushcfunction(m_L, lua_timeline_delete_clip);     lua_setfield(m_L, -2, "delete_clip");
    lua_pushcfunction(m_L, lua_timeline_split_clip);      lua_setfield(m_L, -2, "split_clip");
    // Track Reordering & Renaming
    lua_pushcfunction(m_L, lua_timeline_move_track_up);   lua_setfield(m_L, -2, "move_track_up");
    lua_pushcfunction(m_L, lua_timeline_move_track_down); lua_setfield(m_L, -2, "move_track_down");
    lua_pushcfunction(m_L, lua_timeline_move_track);      lua_setfield(m_L, -2, "move_track");
    lua_pushcfunction(m_L, lua_timeline_swap_tracks);     lua_setfield(m_L, -2, "swap_tracks");
    lua_pushcfunction(m_L, lua_timeline_set_track_name);  lua_setfield(m_L, -2, "set_track_name");
    lua_pushcfunction(m_L, lua_timeline_get_track_name);  lua_setfield(m_L, -2, "get_track_name");
    // Keyframing under timeline
    lua_pushcfunction(m_L, lua_timeline_add_keyframe);    lua_setfield(m_L, -2, "add_keyframe");
    lua_pushcfunction(m_L, lua_timeline_remove_keyframe); lua_setfield(m_L, -2, "remove_keyframe");
    lua_pushcfunction(m_L, lua_timeline_clear_keyframes); lua_setfield(m_L, -2, "clear_keyframes");
    lua_pushcfunction(m_L, lua_timeline_get_keyframes);   lua_setfield(m_L, -2, "get_keyframes");
    lua_pushcfunction(m_L, lua_timeline_evaluate_keyframe); lua_setfield(m_L, -2, "evaluate_keyframe");
    lua_setglobal(m_L, "timeline");

    // 3. `clip` Table
    lua_newtable(m_L);
    lua_pushcfunction(m_L, lua_clip_get_position); lua_setfield(m_L, -2, "get_position");
    lua_pushcfunction(m_L, lua_clip_set_position); lua_setfield(m_L, -2, "set_position");
    lua_pushcfunction(m_L, lua_clip_get_scale);    lua_setfield(m_L, -2, "get_scale");
    lua_pushcfunction(m_L, lua_clip_get_scale_xy); lua_setfield(m_L, -2, "get_scale_xy");
    lua_pushcfunction(m_L, lua_clip_get_scale_x);  lua_setfield(m_L, -2, "get_scale_x");
    lua_pushcfunction(m_L, lua_clip_get_scale_y);  lua_setfield(m_L, -2, "get_scale_y");
    lua_pushcfunction(m_L, lua_clip_set_scale);    lua_setfield(m_L, -2, "set_scale");
    lua_pushcfunction(m_L, lua_clip_set_scale_x);  lua_setfield(m_L, -2, "set_scale_x");
    lua_pushcfunction(m_L, lua_clip_set_scale_y);  lua_setfield(m_L, -2, "set_scale_y");
    lua_pushcfunction(m_L, lua_clip_get_rotation); lua_setfield(m_L, -2, "get_rotation");
    lua_pushcfunction(m_L, lua_clip_set_rotation); lua_setfield(m_L, -2, "set_rotation");
    lua_pushcfunction(m_L, lua_clip_get_opacity);  lua_setfield(m_L, -2, "get_opacity");
    lua_pushcfunction(m_L, lua_clip_set_opacity);  lua_setfield(m_L, -2, "set_opacity");
    lua_pushcfunction(m_L, lua_clip_get_volume);   lua_setfield(m_L, -2, "get_volume");
    lua_pushcfunction(m_L, lua_clip_set_volume);   lua_setfield(m_L, -2, "set_volume");
    lua_pushcfunction(m_L, lua_clip_get_time);     lua_setfield(m_L, -2, "get_time");
    lua_pushcfunction(m_L, lua_clip_set_time);     lua_setfield(m_L, -2, "set_time");
    lua_pushcfunction(m_L, lua_clip_get_info);     lua_setfield(m_L, -2, "get_info");
    lua_pushcfunction(m_L, lua_clip_get_transform); lua_setfield(m_L, -2, "get_transform");
    lua_pushcfunction(m_L, lua_clip_set_transform); lua_setfield(m_L, -2, "set_transform");
    lua_pushcfunction(m_L, lua_clip_reset_transform); lua_setfield(m_L, -2, "reset_transform");
    // SFX Frame manipulation on clip
    lua_pushcfunction(m_L, lua_clip_get_frame);    lua_setfield(m_L, -2, "get_frame");
    lua_pushcfunction(m_L, lua_clip_set_frame);    lua_setfield(m_L, -2, "set_frame");
    lua_pushcfunction(m_L, lua_clip_create_from_frame); lua_setfield(m_L, -2, "create_from_frame");
    lua_pushcfunction(m_L, lua_timeline_get_selected_clip); lua_setfield(m_L, -2, "get_selected_id");
    lua_pushcfunction(m_L, lua_timeline_get_selected_clip); lua_setfield(m_L, -2, "get_selected_clip");
    lua_setglobal(m_L, "clip");

    // 4. `project` Table
    lua_newtable(m_L);
    lua_pushcfunction(m_L, lua_project_save);         lua_setfield(m_L, -2, "save");
    lua_pushcfunction(m_L, lua_project_load);         lua_setfield(m_L, -2, "load");
    lua_pushcfunction(m_L, lua_project_import_video); lua_setfield(m_L, -2, "import_video");
    lua_pushcfunction(m_L, lua_project_import_audio); lua_setfield(m_L, -2, "import_audio");
    lua_pushcfunction(m_L, lua_project_import_image); lua_setfield(m_L, -2, "import_image");
    lua_pushcfunction(m_L, lua_assets_create_text);   lua_setfield(m_L, -2, "create_text_asset");
    lua_pushcfunction(m_L, lua_project_get_name);     lua_setfield(m_L, -2, "get_name");
    lua_pushcfunction(m_L, lua_project_set_name);     lua_setfield(m_L, -2, "set_name");
    lua_setglobal(m_L, "project");

    // Global project_save() and project_load() aliases
    lua_pushcfunction(m_L, lua_project_save); lua_setglobal(m_L, "project_save");
    lua_pushcfunction(m_L, lua_project_load); lua_setglobal(m_L, "project_load");

    // 5. `assets` Table
    lua_newtable(m_L);
    lua_pushcfunction(m_L, lua_assets_create_text); lua_setfield(m_L, -2, "create_text");
    lua_pushcfunction(m_L, lua_assets_get_scale);   lua_setfield(m_L, -2, "get_scale");
    lua_pushcfunction(m_L, lua_assets_get_scale_x); lua_setfield(m_L, -2, "get_scale_x");
    lua_pushcfunction(m_L, lua_assets_get_scale_y); lua_setfield(m_L, -2, "get_scale_y");
    lua_pushcfunction(m_L, lua_assets_set_scale);   lua_setfield(m_L, -2, "set_scale");
    lua_pushcfunction(m_L, lua_assets_set_scale_x); lua_setfield(m_L, -2, "set_scale_x");
    lua_pushcfunction(m_L, lua_assets_set_scale_y); lua_setfield(m_L, -2, "set_scale_y");
    lua_setglobal(m_L, "assets");

    // 6. `minicut` Table
    lua_newtable(m_L);
    lua_pushcfunction(m_L, lua_minicut_log);        lua_setfield(m_L, -2, "log");
    lua_pushcfunction(m_L, lua_minicut_set_status); lua_setfield(m_L, -2, "set_status");
    lua_pushcfunction(m_L, lua_minicut_push_undo);  lua_setfield(m_L, -2, "push_undo");
    lua_pushcfunction(m_L, lua_timeline_get_selected_clip); lua_setfield(m_L, -2, "get_selected_clip");
    lua_pushcfunction(m_L, lua_timeline_get_selected_clip); lua_setfield(m_L, -2, "get_selected_id");
    lua_setglobal(m_L, "minicut");

    // 7. `net` Table (HTTP, download, upload)
    lua_newtable(m_L);
    lua_pushcfunction(m_L, lua_net_get);      lua_setfield(m_L, -2, "get");
    lua_pushcfunction(m_L, lua_net_post);     lua_setfield(m_L, -2, "post");
    lua_pushcfunction(m_L, lua_net_download); lua_setfield(m_L, -2, "download");
    lua_pushcfunction(m_L, lua_net_upload);   lua_setfield(m_L, -2, "upload");
    lua_pushcfunction(m_L, lua_net_request);  lua_setfield(m_L, -2, "request");
    lua_setglobal(m_L, "net");

    // 8. `export` Table (Audio mixing progress, video rendering progress, status)
    lua_newtable(m_L);
    lua_pushcfunction(m_L, lua_export_get_audio_progress); lua_setfield(m_L, -2, "get_audio_progress");
    lua_pushcfunction(m_L, lua_export_get_progress);       lua_setfield(m_L, -2, "get_progress");
    lua_pushcfunction(m_L, lua_export_is_exporting);       lua_setfield(m_L, -2, "is_exporting");
    lua_pushcfunction(m_L, lua_export_get_status);         lua_setfield(m_L, -2, "get_status");
    lua_setglobal(m_L, "export");

    // 9. `locale` Table & callable metatable
    lua_newtable(m_L);
    lua_pushcfunction(m_L, lua_locale_get);          lua_setfield(m_L, -2, "get");
    lua_pushcfunction(m_L, lua_locale_get);          lua_setfield(m_L, -2, "tr");
    lua_pushcfunction(m_L, lua_locale_get_language); lua_setfield(m_L, -2, "get_language");
    lua_pushcfunction(m_L, lua_locale_set_language); lua_setfield(m_L, -2, "set_language");
    lua_pushcfunction(m_L, lua_locale_is_rtl);       lua_setfield(m_L, -2, "is_rtl");
    lua_pushcfunction(m_L, lua_locale_register);     lua_setfield(m_L, -2, "register");
    lua_pushcfunction(m_L, lua_locale_register);     lua_setfield(m_L, -2, "add");

    // Metatable to make `locale("key", "default")` callable directly
    lua_newtable(m_L);
    lua_pushcfunction(m_L, [](lua_State* L) -> int {
        // arg 1 is table self, arg 2 is key, arg 3 is default
        lua_remove(L, 1);
        return lua_locale_get(L);
    });
    lua_setfield(m_L, -2, "__call");
    lua_setmetatable(m_L, -2);

    lua_pushvalue(m_L, -1);
    lua_setglobal(m_L, "locale");

    // Global tr() alias
    lua_pushcfunction(m_L, lua_locale_get);
    lua_setglobal(m_L, "tr");

    // Alias in minicut.locale
    lua_getglobal(m_L, "minicut");
    if (lua_istable(m_L, -1)) {
        lua_pushvalue(m_L, -2);
        lua_setfield(m_L, -2, "locale");
    }
    lua_pop(m_L, 2); // pop minicut and locale

    // 10. `ai` Library Table
    lua_newtable(m_L);
    lua_pushcfunction(m_L, lua_ai_is_available);        lua_setfield(m_L, -2, "is_available");
    lua_pushcfunction(m_L, lua_ai_is_enabled);          lua_setfield(m_L, -2, "is_enabled");
    lua_pushcfunction(m_L, lua_ai_set_enabled);          lua_setfield(m_L, -2, "set_enabled");
    lua_pushcfunction(m_L, lua_ai_has_api_key);          lua_setfield(m_L, -2, "has_api_key");
    lua_pushcfunction(m_L, lua_ai_set_api_key);          lua_setfield(m_L, -2, "set_api_key");
    lua_pushcfunction(m_L, lua_ai_is_busy);             lua_setfield(m_L, -2, "is_busy");
    lua_pushcfunction(m_L, lua_ai_get_status);          lua_setfield(m_L, -2, "get_status");
    lua_pushcfunction(m_L, lua_ai_set_mock_response);   lua_setfield(m_L, -2, "set_mock_response");
    lua_pushcfunction(m_L, lua_ai_clear_mock_response); lua_setfield(m_L, -2, "clear_mock_response");
    lua_pushcfunction(m_L, lua_ai_prompt);              lua_setfield(m_L, -2, "prompt");
    lua_pushcfunction(m_L, lua_ai_prompt_json);         lua_setfield(m_L, -2, "prompt_json");
    lua_pushcfunction(m_L, lua_ai_parse_json);          lua_setfield(m_L, -2, "parse_json");
    lua_pushcfunction(m_L, lua_ai_to_json);             lua_setfield(m_L, -2, "to_json");
    lua_pushcfunction(m_L, lua_ai_struct);              lua_setfield(m_L, -2, "struct");
    lua_pushvalue(m_L, -1);
    lua_setglobal(m_L, "ai");

    lua_getglobal(m_L, "minicut");
    if (lua_istable(m_L, -1)) {
        lua_pushvalue(m_L, -2);
        lua_setfield(m_L, -2, "ai");
    }
    lua_pop(m_L, 2); // pop minicut and ai

    // 11. `shader` Table
    lua_newtable(m_L);
    lua_pushcfunction(m_L, lua_shader_create);      lua_setfield(m_L, -2, "create");
    lua_pushcfunction(m_L, lua_shader_set_uniform); lua_setfield(m_L, -2, "set_uniform");
    lua_pushcfunction(m_L, lua_shader_apply);       lua_setfield(m_L, -2, "apply");
    lua_setglobal(m_L, "shader");

    // 12. `sfx` Table
    lua_newtable(m_L);
    lua_pushcfunction(m_L, lua_clip_get_frame);          lua_setfield(m_L, -2, "get_frame");
    lua_pushcfunction(m_L, lua_shader_apply);            lua_setfield(m_L, -2, "apply_shader");
    lua_pushcfunction(m_L, lua_clip_set_frame);          lua_setfield(m_L, -2, "reinsert_frame");
    lua_pushcfunction(m_L, lua_clip_create_from_frame);  lua_setfield(m_L, -2, "create_clip");
    lua_pushcfunction(m_L, lua_shader_create);           lua_setfield(m_L, -2, "create_shader");
    lua_setglobal(m_L, "sfx");

    // 13. Global default `plugin` Table
    lua_newtable(m_L);
    lua_pushstring(m_L, "default"); lua_setfield(m_L, -2, "id");
    lua_pushstring(m_L, "Default Plugin"); lua_setfield(m_L, -2, "title");
    lua_setglobal(m_L, "plugin");
}


static void invoke_lua_callback(lua_State* L, int arg_idx) {
    if (lua_isfunction(L, arg_idx)) {
        lua_pushvalue(L, arg_idx);
        if (lua_pcall(L, 0, 0, 0) != 0) {
            const char* err = lua_tostring(L, -1);
            std::cerr << "[ScriptEngine] Callback error: " << (err ? err : "unknown") << std::endl;
            lua_pop(L, 1);
        }
    } else if (lua_isstring(L, arg_idx)) {
        const char* fn_name = lua_tostring(L, arg_idx);
        lua_getglobal(L, fn_name);
        if (lua_isfunction(L, -1)) {
            if (lua_pcall(L, 0, 0, 0) != 0) {
                const char* err = lua_tostring(L, -1);
                std::cerr << "[ScriptEngine] Callback error: " << (err ? err : "unknown") << std::endl;
                lua_pop(L, 1);
            }
        } else {
            lua_pop(L, 1);
        }
    }
}

// ----------------------------------------------------------------------------
// GUI C-API Implementation
// ----------------------------------------------------------------------------

int ScriptEngine::lua_gui_text(lua_State* L) {
    const char* str = luaL_optstring(L, 1, "");
    ImGui::TextUnformatted(str);
    return 0;
}

int ScriptEngine::lua_gui_text_colored(lua_State* L) {
    float r = (float)luaL_checknumber(L, 1);
    float g = (float)luaL_checknumber(L, 2);
    float b = (float)luaL_checknumber(L, 3);
    float a = (float)luaL_optnumber(L, 4, 1.0);
    const char* str = luaL_optstring(L, 5, "");
    ImGui::TextColored(ImVec4(r, g, b, a), "%s", str);
    return 0;
}

int ScriptEngine::lua_gui_text_disabled(lua_State* L) {
    const char* str = luaL_optstring(L, 1, "");
    ImGui::TextDisabled("%s", str);
    return 0;
}

int ScriptEngine::lua_gui_button(lua_State* L) {
    const char* label = luaL_checkstring(L, 1);
    bool clicked = ImGui::Button(label);
    if (clicked && lua_gettop(L) >= 2) {
        invoke_lua_callback(L, 2);
    }
    lua_pushboolean(L, clicked);
    return 1;
}

int ScriptEngine::lua_gui_small_button(lua_State* L) {
    const char* label = luaL_checkstring(L, 1);
    bool clicked = ImGui::SmallButton(label);
    if (clicked && lua_gettop(L) >= 2) {
        invoke_lua_callback(L, 2);
    }
    lua_pushboolean(L, clicked);
    return 1;
}

int ScriptEngine::lua_gui_slider_float(lua_State* L) {
    const char* label = luaL_checkstring(L, 1);
    float val = (float)luaL_checknumber(L, 2);
    float v_min = (float)luaL_checknumber(L, 3);
    float v_max = (float)luaL_checknumber(L, 4);

    bool changed = ImGui::SliderFloat(label, &val, v_min, v_max);
    if (changed && lua_gettop(L) >= 5 && lua_isfunction(L, 5)) {
        lua_pushvalue(L, 5);
        lua_pushnumber(L, val);
        lua_pcall(L, 1, 0, 0);
    }

    lua_pushnumber(L, val);
    lua_pushboolean(L, changed);
    return 2;
}

int ScriptEngine::lua_gui_slider_int(lua_State* L) {
    const char* label = luaL_checkstring(L, 1);
    int val = (int)luaL_checkinteger(L, 2);
    int v_min = (int)luaL_checkinteger(L, 3);
    int v_max = (int)luaL_checkinteger(L, 4);

    bool changed = ImGui::SliderInt(label, &val, v_min, v_max);
    if (changed && lua_gettop(L) >= 5 && lua_isfunction(L, 5)) {
        lua_pushvalue(L, 5);
        lua_pushinteger(L, val);
        lua_pcall(L, 1, 0, 0);
    }

    lua_pushinteger(L, val);
    lua_pushboolean(L, changed);
    return 2;
}

int ScriptEngine::lua_gui_drag_float(lua_State* L) {
    const char* label = luaL_checkstring(L, 1);
    float val = (float)luaL_checknumber(L, 2);
    float speed = (float)luaL_optnumber(L, 3, 1.0);
    float v_min = (float)luaL_optnumber(L, 4, -10000.0);
    float v_max = (float)luaL_optnumber(L, 5, 10000.0);

    bool changed = ImGui::DragFloat(label, &val, speed, v_min, v_max);
    if (changed && lua_gettop(L) >= 6 && lua_isfunction(L, 6)) {
        lua_pushvalue(L, 6);
        lua_pushnumber(L, val);
        lua_pcall(L, 1, 0, 0);
    }

    lua_pushnumber(L, val);
    lua_pushboolean(L, changed);
    return 2;
}

int ScriptEngine::lua_gui_drag_int(lua_State* L) {
    const char* label = luaL_checkstring(L, 1);
    int val = (int)luaL_checkinteger(L, 2);
    float speed = (float)luaL_optnumber(L, 3, 1.0);
    int v_min = (int)luaL_optinteger(L, 4, -10000);
    int v_max = (int)luaL_optinteger(L, 5, 10000);

    bool changed = ImGui::DragInt(label, &val, speed, v_min, v_max);
    if (changed && lua_gettop(L) >= 6 && lua_isfunction(L, 6)) {
        lua_pushvalue(L, 6);
        lua_pushinteger(L, val);
        lua_pcall(L, 1, 0, 0);
    }

    lua_pushinteger(L, val);
    lua_pushboolean(L, changed);
    return 2;
}

int ScriptEngine::lua_gui_input_text(lua_State* L) {
    const char* label = luaL_checkstring(L, 1);
    const char* init_text = luaL_optstring(L, 2, "");
    static char buf[1024];
    strncpy(buf, init_text, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    bool changed = ImGui::InputText(label, buf, sizeof(buf));
    if (changed && lua_gettop(L) >= 3 && lua_isfunction(L, 3)) {
        lua_pushvalue(L, 3);
        lua_pushstring(L, buf);
        lua_pcall(L, 1, 0, 0);
    }

    lua_pushstring(L, buf);
    lua_pushboolean(L, changed);
    return 2;
}

int ScriptEngine::lua_gui_input_float(lua_State* L) {
    const char* label = luaL_checkstring(L, 1);
    float val = (float)luaL_checknumber(L, 2);

    bool changed = ImGui::InputFloat(label, &val);
    if (changed && lua_gettop(L) >= 3 && lua_isfunction(L, 3)) {
        lua_pushvalue(L, 3);
        lua_pushnumber(L, val);
        lua_pcall(L, 1, 0, 0);
    }

    lua_pushnumber(L, val);
    lua_pushboolean(L, changed);
    return 2;
}

int ScriptEngine::lua_gui_checkbox(lua_State* L) {
    const char* label = luaL_checkstring(L, 1);
    bool val = lua_toboolean(L, 2);

    bool changed = ImGui::Checkbox(label, &val);
    if (changed && lua_gettop(L) >= 3 && lua_isfunction(L, 3)) {
        lua_pushvalue(L, 3);
        lua_pushboolean(L, val);
        lua_pcall(L, 1, 0, 0);
    }

    lua_pushboolean(L, val);
    lua_pushboolean(L, changed);
    return 2;
}

int ScriptEngine::lua_gui_same_line(lua_State* L) {
    (void)L;
    ImGui::SameLine();
    return 0;
}

int ScriptEngine::lua_gui_separator(lua_State* L) {
    (void)L;
    ImGui::Separator();
    return 0;
}

int ScriptEngine::lua_gui_spacing(lua_State* L) {
    (void)L;
    ImGui::Spacing();
    return 0;
}

int ScriptEngine::lua_gui_tooltip(lua_State* L) {
    const char* text = luaL_checkstring(L, 1);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", text);
    }
    return 0;
}

int ScriptEngine::lua_gui_set_next_item_width(lua_State* L) {
    float w = (float)luaL_checknumber(L, 1);
    ImGui::SetNextItemWidth(w);
    return 0;
}

// ----------------------------------------------------------------------------
// Timeline C-API Implementation
// ----------------------------------------------------------------------------

int ScriptEngine::lua_timeline_get_playhead(lua_State* L) {
    if (!s_engine_instance) {
        lua_pushnumber(L, 0.0);
        return 1;
    }
    lua_pushnumber(L, s_engine_instance->get_playhead_time());
    return 1;
}

int ScriptEngine::lua_timeline_set_playhead(lua_State* L) {
    if (!s_engine_instance) return 0;
    double t = luaL_checknumber(L, 1);
    s_engine_instance->set_playhead_time(t);
    return 0;
}

int ScriptEngine::lua_timeline_get_duration(lua_State* L) {
    if (!s_engine_instance || !s_engine_instance->m_project) {
        lua_pushnumber(L, 0.0);
        return 1;
    }
    lua_pushnumber(L, s_engine_instance->m_project->get_total_duration());
    return 1;
}

int ScriptEngine::lua_timeline_get_track_count(lua_State* L) {
    if (!s_engine_instance || !s_engine_instance->m_project) {
        lua_pushinteger(L, 0);
        return 1;
    }
    lua_pushinteger(L, static_cast<int>(s_engine_instance->m_project->tracks.size()));
    return 1;
}

int ScriptEngine::lua_timeline_add_track(lua_State* L) {
    if (!s_engine_instance || !s_engine_instance->m_project) {
        lua_pushinteger(L, -1);
        return 1;
    }
    const char* type = luaL_optstring(L, 1, "video");
    Project& proj = *s_engine_instance->m_project;
    s_engine_instance->push_undo_state();

    Track trk;
    trk.type = (std::string(type) == "audio") ? "audio" : "video";
    int count = 1;
    for (const auto& t : proj.tracks) {
        if (t.type == trk.type) count++;
    }
    trk.name = (trk.type == "video" ? "Video " : "Audio ") + std::to_string(count);
    trk.id = "track_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count() % 1000000);
    proj.tracks.push_back(trk);

    s_engine_instance->notify_project_modified();
    lua_pushinteger(L, static_cast<int>(proj.tracks.size() - 1));
    return 1;
}

static std::string ensure_asset_imported(ScriptEngine* engine, const std::string& path_or_id, const std::string& type, double duration = 5.0) {
    if (!engine || !engine->get_project()) return path_or_id;
    Project& proj = *engine->get_project();

    // Check if ID already exists
    if (type == "video") {
        if (proj.find_video_asset(path_or_id)) return path_or_id;
    } else if (type == "audio") {
        if (proj.find_audio_asset(path_or_id)) return path_or_id;
    } else if (type == "image") {
        if (proj.find_video_asset(path_or_id)) return path_or_id;
    }

    // Check if path exists on disk
    std::error_code ec;
    if (fs::exists(path_or_id, ec) && engine->get_clip_manager()) {
        if (type == "video") {
            if (engine->get_clip_manager()->import_video(proj, path_or_id)) {
                return proj.video_assets.back().id;
            }
        } else if (type == "audio") {
            if (engine->get_clip_manager()->import_audio(proj, path_or_id)) {
                return proj.audio_assets.back().id;
            }
        } else if (type == "image") {
            if (engine->get_clip_manager()->import_image(proj, path_or_id, duration)) {
                return proj.video_assets.back().id;
            }
        }
    }

    return path_or_id;
}

int ScriptEngine::lua_timeline_place_video(lua_State* L) {
    if (!s_engine_instance || !s_engine_instance->m_project) {
        lua_pushstring(L, "");
        return 1;
    }

    const char* path_or_id = luaL_checkstring(L, 1);
    int track_idx = (int)luaL_optinteger(L, 2, 0);
    double start_time = luaL_optnumber(L, 3, s_engine_instance->get_playhead_time());
    double duration = luaL_optnumber(L, 4, -1.0);
    double offset = luaL_optnumber(L, 5, 0.0);

    Project& proj = *s_engine_instance->m_project;
    std::string asset_id = ensure_asset_imported(s_engine_instance, path_or_id, "video");
    const VideoAsset* va = proj.find_video_asset(asset_id);
    if (!va) {
        lua_pushstring(L, "");
        return 1;
    }

    if (duration <= 0.0) {
        duration = (va->duration > 0.0) ? va->duration : 5.0;
    }

    // Ensure valid video track
    if (track_idx < 0 || track_idx >= static_cast<int>(proj.tracks.size()) || proj.tracks[track_idx].type != "video") {
        track_idx = -1;
        for (size_t t = 0; t < proj.tracks.size(); ++t) {
            if (proj.tracks[t].type == "video") {
                track_idx = static_cast<int>(t);
                break;
            }
        }
        if (track_idx < 0) {
            Track vt;
            vt.type = "video";
            vt.name = "Video 1";
            vt.id = "track_v_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count() % 1000000);
            proj.tracks.push_back(vt);
            track_idx = static_cast<int>(proj.tracks.size() - 1);
        }
    }

    s_engine_instance->push_undo_state();

    Clip c;
    std::string ts = std::to_string(std::chrono::system_clock::now().time_since_epoch().count() % 1000000);
    c.id = "clip_v_" + ts;
    c.asset_id = asset_id;
    c.start_time = std::max(0.0, start_time);
    c.duration = std::max(0.1, duration);
    c.offset = std::max(0.0, offset);

    proj.tracks[track_idx].clips.push_back(c);
    s_engine_instance->set_selected_clip_id(c.id);
    s_engine_instance->notify_project_modified();

    lua_pushstring(L, c.id.c_str());
    return 1;
}

int ScriptEngine::lua_timeline_place_audio(lua_State* L) {
    if (!s_engine_instance || !s_engine_instance->m_project) {
        lua_pushstring(L, "");
        return 1;
    }

    const char* path_or_id = luaL_checkstring(L, 1);
    int track_idx = (int)luaL_optinteger(L, 2, -1);
    double start_time = luaL_optnumber(L, 3, s_engine_instance->get_playhead_time());
    double duration = luaL_optnumber(L, 4, -1.0);
    double offset = luaL_optnumber(L, 5, 0.0);

    Project& proj = *s_engine_instance->m_project;
    std::string asset_id = ensure_asset_imported(s_engine_instance, path_or_id, "audio");
    const AudioAsset* aa = proj.find_audio_asset(asset_id);
    if (!aa) {
        lua_pushstring(L, "");
        return 1;
    }

    if (duration <= 0.0) {
        duration = (aa->duration > 0.0) ? aa->duration : 5.0;
    }

    // Ensure valid audio track
    if (track_idx < 0 || track_idx >= static_cast<int>(proj.tracks.size()) || proj.tracks[track_idx].type != "audio") {
        track_idx = -1;
        for (size_t t = 0; t < proj.tracks.size(); ++t) {
            if (proj.tracks[t].type == "audio") {
                track_idx = static_cast<int>(t);
                break;
            }
        }
        if (track_idx < 0) {
            Track at;
            at.type = "audio";
            at.name = "Audio 1";
            at.id = "track_a_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count() % 1000000);
            proj.tracks.push_back(at);
            track_idx = static_cast<int>(proj.tracks.size() - 1);
        }
    }

    s_engine_instance->push_undo_state();

    Clip c;
    std::string ts = std::to_string(std::chrono::system_clock::now().time_since_epoch().count() % 1000000);
    c.id = "clip_a_" + ts;
    c.asset_id = asset_id;
    c.start_time = std::max(0.0, start_time);
    c.duration = std::max(0.1, duration);
    c.offset = std::max(0.0, offset);

    proj.tracks[track_idx].clips.push_back(c);
    s_engine_instance->set_selected_clip_id(c.id);
    s_engine_instance->notify_project_modified();

    lua_pushstring(L, c.id.c_str());
    return 1;
}

int ScriptEngine::lua_timeline_place_image(lua_State* L) {
    if (!s_engine_instance || !s_engine_instance->m_project) {
        lua_pushstring(L, "");
        return 1;
    }

    const char* path_or_id = luaL_checkstring(L, 1);
    int track_idx = (int)luaL_optinteger(L, 2, -1);
    double start_time = luaL_optnumber(L, 3, s_engine_instance->get_playhead_time());
    double duration = luaL_optnumber(L, 4, 5.0);

    Project& proj = *s_engine_instance->m_project;
    std::string asset_id = ensure_asset_imported(s_engine_instance, path_or_id, "image", duration);
    const VideoAsset* va = proj.find_video_asset(asset_id);
    if (!va) {
        lua_pushstring(L, "");
        return 1;
    }

    // Ensure valid video track
    if (track_idx < 0 || track_idx >= static_cast<int>(proj.tracks.size()) || proj.tracks[track_idx].type != "video") {
        track_idx = -1;
        for (size_t t = 0; t < proj.tracks.size(); ++t) {
            if (proj.tracks[t].type == "video") {
                track_idx = static_cast<int>(t);
                break;
            }
        }
        if (track_idx < 0) {
            Track vt;
            vt.type = "video";
            vt.name = "Video 1";
            vt.id = "track_v_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count() % 1000000);
            proj.tracks.push_back(vt);
            track_idx = static_cast<int>(proj.tracks.size() - 1);
        }
    }

    s_engine_instance->push_undo_state();

    Clip c;
    std::string ts = std::to_string(std::chrono::system_clock::now().time_since_epoch().count() % 1000000);
    c.id = "clip_img_" + ts;
    c.asset_id = asset_id;
    c.start_time = std::max(0.0, start_time);
    c.duration = std::max(0.1, duration);
    c.offset = 0.0;

    proj.tracks[track_idx].clips.push_back(c);
    s_engine_instance->set_selected_clip_id(c.id);
    s_engine_instance->notify_project_modified();

    lua_pushstring(L, c.id.c_str());
    return 1;
}

int ScriptEngine::lua_timeline_get_selected_clip(lua_State* L) {
    if (!s_engine_instance) {
        lua_pushstring(L, "");
        return 1;
    }
    lua_pushstring(L, s_engine_instance->get_selected_clip_id().c_str());
    return 1;
}

int ScriptEngine::lua_timeline_select_clip(lua_State* L) {
    if (!s_engine_instance) return 0;
    const char* clip_id = luaL_optstring(L, 1, "");
    s_engine_instance->set_selected_clip_id(clip_id);
    return 0;
}

int ScriptEngine::lua_timeline_delete_clip(lua_State* L) {
    if (!s_engine_instance || !s_engine_instance->m_project) {
        lua_pushboolean(L, false);
        return 1;
    }
    const char* clip_id = luaL_checkstring(L, 1);
    Project& proj = *s_engine_instance->m_project;

    for (auto& trk : proj.tracks) {
        for (auto it = trk.clips.begin(); it != trk.clips.end(); ++it) {
            if (it->id == clip_id) {
                s_engine_instance->push_undo_state();
                proj.remove_clip_keyframes(clip_id);
                trk.clips.erase(it);
                s_engine_instance->notify_project_modified();
                lua_pushboolean(L, true);
                return 1;
            }
        }
    }

    lua_pushboolean(L, false);
    return 1;
}

int ScriptEngine::lua_timeline_split_clip(lua_State* L) {
    if (!s_engine_instance || !s_engine_instance->m_project || !s_engine_instance->m_timeline) {
        lua_pushboolean(L, false);
        return 1;
    }
    const char* clip_id = luaL_checkstring(L, 1);
    double split_time = luaL_optnumber(L, 2, s_engine_instance->get_playhead_time());

    s_engine_instance->push_undo_state();
    bool res = s_engine_instance->m_timeline->split_clip_at(*s_engine_instance->m_project, clip_id, split_time);
    if (res) {
        s_engine_instance->notify_project_modified();
    }
    lua_pushboolean(L, res);
    return 1;
}

// ----------------------------------------------------------------------------
// Keyframing C-API Implementation
// ----------------------------------------------------------------------------

int ScriptEngine::lua_timeline_add_keyframe(lua_State* L) {
    if (!s_engine_instance || !s_engine_instance->m_project) return 0;
    const char* clip_id = luaL_checkstring(L, 1);
    const char* param = luaL_checkstring(L, 2);
    double time = luaL_checknumber(L, 3);
    double val = luaL_checknumber(L, 4);

    s_engine_instance->push_undo_state();
    s_engine_instance->m_project->set_or_add_keyframe(clip_id, param, time, val);
    s_engine_instance->notify_project_modified();
    return 0;
}

int ScriptEngine::lua_timeline_remove_keyframe(lua_State* L) {
    if (!s_engine_instance || !s_engine_instance->m_project) return 0;
    const char* clip_id = luaL_checkstring(L, 1);
    const char* param = luaL_checkstring(L, 2);
    double time = luaL_checknumber(L, 3);

    Project& proj = *s_engine_instance->m_project;
    for (auto it = proj.keyframes.begin(); it != proj.keyframes.end(); ++it) {
        if (it->clip_id == clip_id && it->parameter == param && std::abs(it->time - time) < 0.05) {
            s_engine_instance->push_undo_state();
            proj.keyframes.erase(it);
            s_engine_instance->notify_project_modified();
            break;
        }
    }
    return 0;
}

int ScriptEngine::lua_timeline_clear_keyframes(lua_State* L) {
    if (!s_engine_instance || !s_engine_instance->m_project) return 0;
    const char* clip_id = luaL_checkstring(L, 1);
    const char* param = luaL_optstring(L, 2, nullptr);

    Project& proj = *s_engine_instance->m_project;
    s_engine_instance->push_undo_state();

    if (!param) {
        proj.remove_clip_keyframes(clip_id);
    } else {
        auto it = std::remove_if(proj.keyframes.begin(), proj.keyframes.end(), [&](const Keyframe& kf) {
            return kf.clip_id == clip_id && kf.parameter == param;
        });
        proj.keyframes.erase(it, proj.keyframes.end());
    }

    s_engine_instance->notify_project_modified();
    return 0;
}

int ScriptEngine::lua_timeline_get_keyframes(lua_State* L) {
    if (!s_engine_instance || !s_engine_instance->m_project) {
        lua_newtable(L);
        return 1;
    }
    const char* clip_id = luaL_checkstring(L, 1);
    const char* param = luaL_optstring(L, 2, nullptr);

    lua_newtable(L);
    int idx = 1;
    const Project& proj = *s_engine_instance->m_project;

    for (const auto& kf : proj.keyframes) {
        if (kf.clip_id == clip_id) {
            if (!param || kf.parameter == param) {
                lua_newtable(L);
                lua_pushstring(L, kf.parameter.c_str()); lua_setfield(L, -2, "parameter");
                lua_pushnumber(L, kf.time);              lua_setfield(L, -2, "time");
                lua_pushnumber(L, kf.value);             lua_setfield(L, -2, "value");
                lua_rawseti(L, -2, idx++);
            }
        }
    }
    return 1;
}

int ScriptEngine::lua_timeline_evaluate_keyframe(lua_State* L) {
    if (!s_engine_instance || !s_engine_instance->m_project) {
        lua_pushnumber(L, 0.0);
        return 1;
    }
    const char* clip_id = luaL_checkstring(L, 1);
    const char* param = luaL_checkstring(L, 2);
    double rel_time = luaL_checknumber(L, 3);
    double def_val = luaL_optnumber(L, 4, 1.0);

    double val = s_engine_instance->m_project->evaluate_keyframe(clip_id, param, rel_time, def_val);
    lua_pushnumber(L, val);
    return 1;
}

// ----------------------------------------------------------------------------
// Clip Transforms & Positioning C-API
// ----------------------------------------------------------------------------

int ScriptEngine::lua_clip_get_position(lua_State* L) {
    if (!s_engine_instance || !s_engine_instance->m_project) {
        lua_pushnumber(L, 0.0);
        lua_pushnumber(L, 0.0);
        return 2;
    }
    const char* clip_id = luaL_checkstring(L, 1);
    const Clip* c = s_engine_instance->m_project->find_clip(clip_id);
    if (!c) {
        lua_pushnumber(L, 0.0);
        lua_pushnumber(L, 0.0);
        return 2;
    }
    lua_pushnumber(L, c->pos_x);
    lua_pushnumber(L, c->pos_y);
    return 2;
}

int ScriptEngine::lua_clip_set_position(lua_State* L) {
    if (!s_engine_instance || !s_engine_instance->m_project) return 0;
    const char* clip_id = luaL_checkstring(L, 1);
    double x = luaL_checknumber(L, 2);
    double y = luaL_checknumber(L, 3);

    Clip* c = s_engine_instance->m_project->find_clip_mut(clip_id);
    if (c) {
        c->pos_x = x;
        c->pos_y = y;
        s_engine_instance->notify_project_modified();
    }
    return 0;
}

int ScriptEngine::lua_clip_get_scale(lua_State* L) {
    if (!s_engine_instance || !s_engine_instance->m_project) {
        lua_pushnumber(L, 1.0);
        lua_pushnumber(L, 1.0);
        return 2;
    }
    const char* clip_id = luaL_checkstring(L, 1);
    const Clip* c = s_engine_instance->m_project->find_clip(clip_id);
    double sx = c ? c->scale_x : 1.0;
    double sy = c ? c->scale_y : 1.0;
    lua_pushnumber(L, sx);
    lua_pushnumber(L, sy);
    return 2;
}

int ScriptEngine::lua_clip_get_scale_xy(lua_State* L) {
    if (!s_engine_instance || !s_engine_instance->m_project) {
        lua_newtable(L);
        lua_pushnumber(L, 1.0); lua_setfield(L, -2, "x");
        lua_pushnumber(L, 1.0); lua_setfield(L, -2, "y");
        return 1;
    }
    const char* clip_id = luaL_checkstring(L, 1);
    const Clip* c = s_engine_instance->m_project->find_clip(clip_id);
    double sx = c ? c->scale_x : 1.0;
    double sy = c ? c->scale_y : 1.0;
    lua_newtable(L);
    lua_pushnumber(L, sx); lua_setfield(L, -2, "x");
    lua_pushnumber(L, sy); lua_setfield(L, -2, "y");
    return 1;
}

int ScriptEngine::lua_clip_get_scale_x(lua_State* L) {
    if (!s_engine_instance || !s_engine_instance->m_project) {
        lua_pushnumber(L, 1.0);
        return 1;
    }
    const char* clip_id = luaL_checkstring(L, 1);
    const Clip* c = s_engine_instance->m_project->find_clip(clip_id);
    lua_pushnumber(L, c ? c->scale_x : 1.0);
    return 1;
}

int ScriptEngine::lua_clip_get_scale_y(lua_State* L) {
    if (!s_engine_instance || !s_engine_instance->m_project) {
        lua_pushnumber(L, 1.0);
        return 1;
    }
    const char* clip_id = luaL_checkstring(L, 1);
    const Clip* c = s_engine_instance->m_project->find_clip(clip_id);
    lua_pushnumber(L, c ? c->scale_y : 1.0);
    return 1;
}

int ScriptEngine::lua_clip_set_scale(lua_State* L) {
    if (!s_engine_instance || !s_engine_instance->m_project) return 0;
    const char* clip_id = luaL_checkstring(L, 1);
    double sx = luaL_checknumber(L, 2);
    double sy = lua_isnumber(L, 3) ? lua_tonumber(L, 3) : sx;

    Clip* c = s_engine_instance->m_project->find_clip_mut(clip_id);
    if (c) {
        s_engine_instance->push_undo_state();
        c->scale_x = std::max(0.01, sx);
        c->scale_y = std::max(0.01, sy);
        c->scale = c->scale_x;
        double rel_t = c->clip_relative_time(s_engine_instance->get_playhead_time());
        if (!s_engine_instance->m_project->get_clip_keyframes(c->id, "scale_x").empty()) {
            s_engine_instance->m_project->set_or_add_keyframe(c->id, "scale_x", rel_t, c->scale_x);
        }
        if (!s_engine_instance->m_project->get_clip_keyframes(c->id, "scale_y").empty()) {
            s_engine_instance->m_project->set_or_add_keyframe(c->id, "scale_y", rel_t, c->scale_y);
        }
        if (!s_engine_instance->m_project->get_clip_keyframes(c->id, "scale").empty()) {
            s_engine_instance->m_project->set_or_add_keyframe(c->id, "scale", rel_t, c->scale_x);
        }
        s_engine_instance->notify_project_modified();
    }
    return 0;
}

int ScriptEngine::lua_clip_set_scale_x(lua_State* L) {
    if (!s_engine_instance || !s_engine_instance->m_project) return 0;
    const char* clip_id = luaL_checkstring(L, 1);
    double sx = luaL_checknumber(L, 2);

    Clip* c = s_engine_instance->m_project->find_clip_mut(clip_id);
    if (c) {
        s_engine_instance->push_undo_state();
        c->scale_x = std::max(0.01, sx);
        c->scale = c->scale_x;
        double rel_t = c->clip_relative_time(s_engine_instance->get_playhead_time());
        if (!s_engine_instance->m_project->get_clip_keyframes(c->id, "scale_x").empty()) {
            s_engine_instance->m_project->set_or_add_keyframe(c->id, "scale_x", rel_t, c->scale_x);
        }
        if (!s_engine_instance->m_project->get_clip_keyframes(c->id, "scale").empty()) {
            s_engine_instance->m_project->set_or_add_keyframe(c->id, "scale", rel_t, c->scale_x);
        }
        s_engine_instance->notify_project_modified();
    }
    return 0;
}

int ScriptEngine::lua_clip_set_scale_y(lua_State* L) {
    if (!s_engine_instance || !s_engine_instance->m_project) return 0;
    const char* clip_id = luaL_checkstring(L, 1);
    double sy = luaL_checknumber(L, 2);

    Clip* c = s_engine_instance->m_project->find_clip_mut(clip_id);
    if (c) {
        s_engine_instance->push_undo_state();
        c->scale_y = std::max(0.01, sy);
        double rel_t = c->clip_relative_time(s_engine_instance->get_playhead_time());
        if (!s_engine_instance->m_project->get_clip_keyframes(c->id, "scale_y").empty()) {
            s_engine_instance->m_project->set_or_add_keyframe(c->id, "scale_y", rel_t, c->scale_y);
        }
        s_engine_instance->notify_project_modified();
    }
    return 0;
}

int ScriptEngine::lua_clip_get_rotation(lua_State* L) {
    if (!s_engine_instance || !s_engine_instance->m_project) {
        lua_pushnumber(L, 0.0);
        return 1;
    }
    const char* clip_id = luaL_checkstring(L, 1);
    const Clip* c = s_engine_instance->m_project->find_clip(clip_id);
    lua_pushnumber(L, c ? c->rotation : 0.0);
    return 1;
}

int ScriptEngine::lua_clip_set_rotation(lua_State* L) {
    if (!s_engine_instance || !s_engine_instance->m_project) return 0;
    const char* clip_id = luaL_checkstring(L, 1);
    double r = luaL_checknumber(L, 2);

    Clip* c = s_engine_instance->m_project->find_clip_mut(clip_id);
    if (c) {
        c->rotation = r;
        s_engine_instance->notify_project_modified();
    }
    return 0;
}

int ScriptEngine::lua_clip_get_opacity(lua_State* L) {
    if (!s_engine_instance || !s_engine_instance->m_project) {
        lua_pushnumber(L, 1.0);
        return 1;
    }
    const char* clip_id = luaL_checkstring(L, 1);
    const Clip* c = s_engine_instance->m_project->find_clip(clip_id);
    lua_pushnumber(L, c ? c->opacity : 1.0);
    return 1;
}

int ScriptEngine::lua_clip_set_opacity(lua_State* L) {
    if (!s_engine_instance || !s_engine_instance->m_project) return 0;
    const char* clip_id = luaL_checkstring(L, 1);
    double op = luaL_checknumber(L, 2);

    Clip* c = s_engine_instance->m_project->find_clip_mut(clip_id);
    if (c) {
        c->opacity = std::clamp(op, 0.0, 1.0);
        s_engine_instance->notify_project_modified();
    }
    return 0;
}

int ScriptEngine::lua_clip_get_volume(lua_State* L) {
    if (!s_engine_instance || !s_engine_instance->m_project) {
        lua_pushnumber(L, 1.0);
        return 1;
    }
    const char* clip_id = luaL_checkstring(L, 1);
    double vol = s_engine_instance->m_project->evaluate_keyframe(clip_id, "volume", 0.0, 1.0);
    lua_pushnumber(L, vol);
    return 1;
}

int ScriptEngine::lua_clip_set_volume(lua_State* L) {
    if (!s_engine_instance || !s_engine_instance->m_project) return 0;
    const char* clip_id = luaL_checkstring(L, 1);
    double vol = luaL_checknumber(L, 2);

    s_engine_instance->m_project->set_or_add_keyframe(clip_id, "volume", 0.0, std::clamp(vol, 0.0, 2.0));
    s_engine_instance->notify_project_modified();
    return 0;
}

int ScriptEngine::lua_clip_get_time(lua_State* L) {
    if (!s_engine_instance || !s_engine_instance->m_project) {
        lua_pushnumber(L, 0.0); lua_pushnumber(L, 0.0); lua_pushnumber(L, 0.0);
        return 3;
    }
    const char* clip_id = luaL_checkstring(L, 1);
    const Clip* c = s_engine_instance->m_project->find_clip(clip_id);
    if (!c) {
        lua_pushnumber(L, 0.0); lua_pushnumber(L, 0.0); lua_pushnumber(L, 0.0);
        return 3;
    }
    lua_pushnumber(L, c->start_time);
    lua_pushnumber(L, c->duration);
    lua_pushnumber(L, c->offset);
    return 3;
}

int ScriptEngine::lua_clip_set_time(lua_State* L) {
    if (!s_engine_instance || !s_engine_instance->m_project) return 0;
    const char* clip_id = luaL_checkstring(L, 1);
    double start_time = luaL_checknumber(L, 2);
    double duration = luaL_checknumber(L, 3);
    double offset = luaL_optnumber(L, 4, -1.0);

    Clip* c = s_engine_instance->m_project->find_clip_mut(clip_id);
    if (c) {
        c->start_time = std::max(0.0, start_time);
        c->duration = std::max(0.1, duration);
        if (offset >= 0.0) c->offset = offset;
        s_engine_instance->notify_project_modified();
    }
    return 0;
}

int ScriptEngine::lua_clip_get_info(lua_State* L) {
    if (!s_engine_instance || !s_engine_instance->m_project) {
        lua_newtable(L);
        return 1;
    }
    const char* clip_id = luaL_checkstring(L, 1);
    int t_idx = -1, c_idx = -1;
    const Clip* c = s_engine_instance->m_project->find_clip(clip_id, &t_idx, &c_idx);
    if (!c) {
        lua_newtable(L);
        return 1;
    }

    lua_newtable(L);
    lua_pushstring(L, c->id.c_str());        lua_setfield(L, -2, "id");
    lua_pushstring(L, c->asset_id.c_str());  lua_setfield(L, -2, "asset_id");
    lua_pushinteger(L, t_idx);               lua_setfield(L, -2, "track_idx");
    lua_pushnumber(L, c->start_time);        lua_setfield(L, -2, "start_time");
    lua_pushnumber(L, c->duration);          lua_setfield(L, -2, "duration");
    lua_pushnumber(L, c->offset);            lua_setfield(L, -2, "offset");
    lua_pushnumber(L, c->pos_x);             lua_setfield(L, -2, "pos_x");
    lua_pushnumber(L, c->pos_y);             lua_setfield(L, -2, "pos_y");
    lua_pushnumber(L, c->scale);             lua_setfield(L, -2, "scale");
    lua_pushnumber(L, c->scale_x);           lua_setfield(L, -2, "scale_x");
    lua_pushnumber(L, c->scale_y);           lua_setfield(L, -2, "scale_y");
    lua_pushnumber(L, c->rotation);          lua_setfield(L, -2, "rotation");
    lua_pushnumber(L, c->opacity);           lua_setfield(L, -2, "opacity");
    return 1;
}

int ScriptEngine::lua_clip_get_transform(lua_State* L) {
    if (!s_engine_instance || !s_engine_instance->m_project) {
        lua_newtable(L);
        return 1;
    }
    const char* clip_id = luaL_checkstring(L, 1);
    const Clip* c = s_engine_instance->m_project->find_clip(clip_id);
    lua_newtable(L);
    if (c) {
        lua_pushnumber(L, c->pos_x);    lua_setfield(L, -2, "x");
        lua_pushnumber(L, c->pos_y);    lua_setfield(L, -2, "y");
        lua_pushnumber(L, c->scale);    lua_setfield(L, -2, "scale");
        lua_pushnumber(L, c->scale_x);  lua_setfield(L, -2, "scale_x");
        lua_pushnumber(L, c->scale_y);  lua_setfield(L, -2, "scale_y");
        lua_pushnumber(L, c->rotation); lua_setfield(L, -2, "rotation");
    }
    return 1;
}

int ScriptEngine::lua_clip_set_transform(lua_State* L) {
    if (!s_engine_instance || !s_engine_instance->m_project) return 0;
    const char* clip_id = luaL_checkstring(L, 1);
    Clip* c = s_engine_instance->m_project->find_clip_mut(clip_id);
    if (c) {
        s_engine_instance->push_undo_state();
        double rel_t = c->clip_relative_time(s_engine_instance->get_playhead_time());
        if (lua_isnumber(L, 2)) {
            c->pos_x = lua_tonumber(L, 2);
            if (!s_engine_instance->m_project->get_clip_keyframes(c->id, "pos_x").empty()) {
                s_engine_instance->m_project->set_or_add_keyframe(c->id, "pos_x", rel_t, c->pos_x);
            }
        }
        if (lua_isnumber(L, 3)) {
            c->pos_y = lua_tonumber(L, 3);
            if (!s_engine_instance->m_project->get_clip_keyframes(c->id, "pos_y").empty()) {
                s_engine_instance->m_project->set_or_add_keyframe(c->id, "pos_y", rel_t, c->pos_y);
            }
        }
        int top = lua_gettop(L);
        if (top >= 6 && lua_isnumber(L, 4) && lua_isnumber(L, 5)) {
            double sx = std::max(0.01, lua_tonumber(L, 4));
            double sy = std::max(0.01, lua_tonumber(L, 5));
            c->scale_x = sx;
            c->scale_y = sy;
            c->scale = sx;
            if (!s_engine_instance->m_project->get_clip_keyframes(c->id, "scale_x").empty()) {
                s_engine_instance->m_project->set_or_add_keyframe(c->id, "scale_x", rel_t, sx);
            }
            if (!s_engine_instance->m_project->get_clip_keyframes(c->id, "scale_y").empty()) {
                s_engine_instance->m_project->set_or_add_keyframe(c->id, "scale_y", rel_t, sy);
            }
            if (!s_engine_instance->m_project->get_clip_keyframes(c->id, "scale").empty()) {
                s_engine_instance->m_project->set_or_add_keyframe(c->id, "scale", rel_t, sx);
            }
            if (lua_isnumber(L, 6)) {
                c->rotation = lua_tonumber(L, 6);
                if (!s_engine_instance->m_project->get_clip_keyframes(c->id, "rotation").empty()) {
                    s_engine_instance->m_project->set_or_add_keyframe(c->id, "rotation", rel_t, c->rotation);
                }
            }
        } else {
            if (lua_isnumber(L, 4)) {
                double s = std::max(0.01, lua_tonumber(L, 4));
                c->scale = s;
                c->scale_x = s;
                c->scale_y = s;
                if (!s_engine_instance->m_project->get_clip_keyframes(c->id, "scale_x").empty()) {
                    s_engine_instance->m_project->set_or_add_keyframe(c->id, "scale_x", rel_t, s);
                }
                if (!s_engine_instance->m_project->get_clip_keyframes(c->id, "scale_y").empty()) {
                    s_engine_instance->m_project->set_or_add_keyframe(c->id, "scale_y", rel_t, s);
                }
                if (!s_engine_instance->m_project->get_clip_keyframes(c->id, "scale").empty()) {
                    s_engine_instance->m_project->set_or_add_keyframe(c->id, "scale", rel_t, s);
                }
            }
            if (lua_isnumber(L, 5)) {
                c->rotation = lua_tonumber(L, 5);
                if (!s_engine_instance->m_project->get_clip_keyframes(c->id, "rotation").empty()) {
                    s_engine_instance->m_project->set_or_add_keyframe(c->id, "rotation", rel_t, c->rotation);
                }
            }
        }
        s_engine_instance->notify_project_modified();
    }
    return 0;
}

int ScriptEngine::lua_clip_reset_transform(lua_State* L) {
    if (!s_engine_instance || !s_engine_instance->m_project) return 0;
    const char* clip_id = luaL_checkstring(L, 1);
    Clip* c = s_engine_instance->m_project->find_clip_mut(clip_id);
    if (c) {
        s_engine_instance->push_undo_state();
        double rel_t = c->clip_relative_time(s_engine_instance->get_playhead_time());
        c->pos_x = 0.0;
        c->pos_y = 0.0;
        c->scale = 1.0;
        c->scale_x = 1.0;
        c->scale_y = 1.0;
        c->rotation = 0.0;
        if (!s_engine_instance->m_project->get_clip_keyframes(c->id, "pos_x").empty()) {
            s_engine_instance->m_project->set_or_add_keyframe(c->id, "pos_x", rel_t, 0.0);
        }
        if (!s_engine_instance->m_project->get_clip_keyframes(c->id, "pos_y").empty()) {
            s_engine_instance->m_project->set_or_add_keyframe(c->id, "pos_y", rel_t, 0.0);
        }
        if (!s_engine_instance->m_project->get_clip_keyframes(c->id, "scale_x").empty()) {
            s_engine_instance->m_project->set_or_add_keyframe(c->id, "scale_x", rel_t, 1.0);
        }
        if (!s_engine_instance->m_project->get_clip_keyframes(c->id, "scale_y").empty()) {
            s_engine_instance->m_project->set_or_add_keyframe(c->id, "scale_y", rel_t, 1.0);
        }
        if (!s_engine_instance->m_project->get_clip_keyframes(c->id, "scale").empty()) {
            s_engine_instance->m_project->set_or_add_keyframe(c->id, "scale", rel_t, 1.0);
        }
        if (!s_engine_instance->m_project->get_clip_keyframes(c->id, "rotation").empty()) {
            s_engine_instance->m_project->set_or_add_keyframe(c->id, "rotation", rel_t, 0.0);
        }
        s_engine_instance->notify_project_modified();
    }
    return 0;
}

// ----------------------------------------------------------------------------
// SFX Pipeline: Frame & Shader Helpers for Lua
// ----------------------------------------------------------------------------

static void run_shader_effect(const std::string& shader_src,
                              const std::vector<uint8_t>& src,
                              int width, int height,
                              const std::unordered_map<std::string, float>& uniforms,
                              std::vector<uint8_t>& dst) {
    dst.resize(src.size());
    if (src.empty() || width <= 0 || height <= 0) return;

    std::string effect = shader_src;
    std::transform(effect.begin(), effect.end(), effect.begin(), ::tolower);

    auto get_u = [&](const std::string& name, float def_val) -> float {
        auto it = uniforms.find(name);
        if (it != uniforms.end()) return it->second;
        it = uniforms.find("u_" + name);
        if (it != uniforms.end()) return it->second;
        if (name.rfind("u_", 0) == 0) {
            it = uniforms.find(name.substr(2));
            if (it != uniforms.end()) return it->second;
        }
        return def_val;
    };

    if (effect.find("grayscale") != std::string::npos || effect.find("greyscale") != std::string::npos) {
        for (size_t i = 0; i < src.size(); i += 4) {
            uint8_t gray = static_cast<uint8_t>(0.299f * src[i] + 0.587f * src[i + 1] + 0.114f * src[i + 2]);
            dst[i + 0] = gray;
            dst[i + 1] = gray;
            dst[i + 2] = gray;
            dst[i + 3] = src[i + 3];
        }
    } else if (effect.find("sepia") != std::string::npos) {
        for (size_t i = 0; i < src.size(); i += 4) {
            float r = src[i + 0], g = src[i + 1], b = src[i + 2];
            dst[i + 0] = static_cast<uint8_t>(std::clamp(0.393f * r + 0.769f * g + 0.189f * b, 0.0f, 255.0f));
            dst[i + 1] = static_cast<uint8_t>(std::clamp(0.349f * r + 0.686f * g + 0.168f * b, 0.0f, 255.0f));
            dst[i + 2] = static_cast<uint8_t>(std::clamp(0.272f * r + 0.534f * g + 0.131f * b, 0.0f, 255.0f));
            dst[i + 3] = src[i + 3];
        }
    } else if (effect.find("brightness") != std::string::npos) {
        float factor = get_u("factor", get_u("brightness", 1.2f));
        for (size_t i = 0; i < src.size(); i += 4) {
            dst[i + 0] = static_cast<uint8_t>(std::clamp(src[i + 0] * factor, 0.0f, 255.0f));
            dst[i + 1] = static_cast<uint8_t>(std::clamp(src[i + 1] * factor, 0.0f, 255.0f));
            dst[i + 2] = static_cast<uint8_t>(std::clamp(src[i + 2] * factor, 0.0f, 255.0f));
            dst[i + 3] = src[i + 3];
        }
    } else if (effect.find("contrast") != std::string::npos) {
        float factor = get_u("factor", get_u("contrast", 1.2f));
        for (size_t i = 0; i < src.size(); i += 4) {
            dst[i + 0] = static_cast<uint8_t>(std::clamp(((src[i + 0] / 255.0f - 0.5f) * factor + 0.5f) * 255.0f, 0.0f, 255.0f));
            dst[i + 1] = static_cast<uint8_t>(std::clamp(((src[i + 1] / 255.0f - 0.5f) * factor + 0.5f) * 255.0f, 0.0f, 255.0f));
            dst[i + 2] = static_cast<uint8_t>(std::clamp(((src[i + 2] / 255.0f - 0.5f) * factor + 0.5f) * 255.0f, 0.0f, 255.0f));
            dst[i + 3] = src[i + 3];
        }
    } else if (effect.find("tint") != std::string::npos) {
        float tr = get_u("r", 1.0f);
        float tg = get_u("g", 0.8f);
        float tb = get_u("b", 0.8f);
        for (size_t i = 0; i < src.size(); i += 4) {
            dst[i + 0] = static_cast<uint8_t>(std::clamp(src[i + 0] * tr, 0.0f, 255.0f));
            dst[i + 1] = static_cast<uint8_t>(std::clamp(src[i + 1] * tg, 0.0f, 255.0f));
            dst[i + 2] = static_cast<uint8_t>(std::clamp(src[i + 2] * tb, 0.0f, 255.0f));
            dst[i + 3] = src[i + 3];
        }
    } else if (effect.find("threshold") != std::string::npos) {
        float thresh = get_u("threshold", 128.0f);
        for (size_t i = 0; i < src.size(); i += 4) {
            float lum = 0.299f * src[i] + 0.587f * src[i + 1] + 0.114f * src[i + 2];
            uint8_t val = (lum >= thresh) ? 255 : 0;
            dst[i + 0] = val;
            dst[i + 1] = val;
            dst[i + 2] = val;
            dst[i + 3] = src[i + 3];
        }
    } else if (effect.find("vignette") != std::string::npos) {
        float radius = get_u("radius", 0.75f);
        float softness = get_u("softness", 0.45f);
        float cx = width * 0.5f;
        float cy = height * 0.5f;
        float max_d = std::sqrt(cx * cx + cy * cy);
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                int idx = (y * width + x) * 4;
                float dx = (x - cx);
                float dy = (y - cy);
                float dist = std::sqrt(dx * dx + dy * dy) / max_d;
                float vig = std::clamp((dist - radius) / softness, 0.0f, 1.0f);
                float factor = 1.0f - vig;
                dst[idx + 0] = static_cast<uint8_t>(src[idx + 0] * factor);
                dst[idx + 1] = static_cast<uint8_t>(src[idx + 1] * factor);
                dst[idx + 2] = static_cast<uint8_t>(src[idx + 2] * factor);
                dst[idx + 3] = src[idx + 3];
            }
        }
    } else if (effect.find("pixelate") != std::string::npos) {
        int block = std::max(2, static_cast<int>(get_u("block_size", 8.0f)));
        for (int y = 0; y < height; y += block) {
            for (int x = 0; x < width; x += block) {
                int sample_x = std::min(x + block / 2, width - 1);
                int sample_y = std::min(y + block / 2, height - 1);
                int sample_idx = (sample_y * width + sample_x) * 4;
                uint8_t r = src[sample_idx + 0];
                uint8_t g = src[sample_idx + 1];
                uint8_t b = src[sample_idx + 2];
                uint8_t a = src[sample_idx + 3];
                for (int by = 0; by < block && y + by < height; ++by) {
                    for (int bx = 0; bx < block && x + bx < width; ++bx) {
                        int out_idx = ((y + by) * width + (x + bx)) * 4;
                        dst[out_idx + 0] = r;
                        dst[out_idx + 1] = g;
                        dst[out_idx + 2] = b;
                        dst[out_idx + 3] = a;
                    }
                }
            }
        }
    } else if (effect.find("flip_x") != std::string::npos) {
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                int src_idx = (y * width + x) * 4;
                int dst_idx = (y * width + (width - 1 - x)) * 4;
                dst[dst_idx + 0] = src[src_idx + 0];
                dst[dst_idx + 1] = src[src_idx + 1];
                dst[dst_idx + 2] = src[src_idx + 2];
                dst[dst_idx + 3] = src[src_idx + 3];
            }
        }
    } else if (effect.find("flip_y") != std::string::npos) {
        for (int y = 0; y < height; ++y) {
            int src_row = y * width * 4;
            int dst_row = (height - 1 - y) * width * 4;
            std::memcpy(&dst[dst_row], &src[src_row], width * 4);
        }
    } else if (effect.find("ripple") != std::string::npos || effect.find("wave") != std::string::npos ||
               effect.find("sin(r *") != std::string::npos || effect.find("u_frequency") != std::string::npos ||
               effect.find("length(p)") != std::string::npos) {
        float cx = get_u("center_x", width * 0.5f);
        float cy = get_u("center_y", height * 0.5f);
        float amplitude = get_u("amplitude", 15.0f);
        if (amplitude > 0.0f && amplitude < 1.0f) {
            amplitude *= std::min(width, height);
        }
        float frequency = get_u("frequency", 0.05f);
        if (frequency > 1.0f) {
            frequency = frequency / static_cast<float>(std::min(width, height));
            if (frequency < 0.005f) frequency = 0.05f;
        }
        float phase = get_u("phase", 0.0f);
        float u_time = get_u("u_time", get_u("time", 0.0f));
        if (phase == 0.0f && u_time != 0.0f) {
            phase = u_time * 4.0f;
        }
        float decay = get_u("decay", 0.002f);

        for (int y = 0; y < height; ++y) {
            float dy = y - cy;
            for (int x = 0; x < width; ++x) {
                float dx = x - cx;
                float dist = std::sqrt(dx * dx + dy * dy);
                float wave = std::sin(dist * frequency - phase);
                float atten = 1.0f / (1.0f + decay * dist);
                float offset = amplitude * wave * atten;

                float sx = x;
                float sy = y;
                if (dist > 0.001f) {
                    sx += (dx / dist) * offset;
                    sy += (dy / dist) * offset;
                }
                sx = std::clamp(sx, 0.0f, static_cast<float>(width - 1));
                sy = std::clamp(sy, 0.0f, static_cast<float>(height - 1));

                int x0 = static_cast<int>(sx);
                int y0 = static_cast<int>(sy);
                int x1 = std::min(x0 + 1, width - 1);
                int y1 = std::min(y0 + 1, height - 1);
                float fx = sx - x0;
                float fy = sy - y0;

                int idx00 = (y0 * width + x0) * 4;
                int idx10 = (y0 * width + x1) * 4;
                int idx01 = (y1 * width + x0) * 4;
                int idx11 = (y1 * width + x1) * 4;
                int out_idx = (y * width + x) * 4;

                for (int c = 0; c < 4; ++c) {
                    float top = (1.0f - fx) * src[idx00 + c] + fx * src[idx10 + c];
                    float btm = (1.0f - fx) * src[idx01 + c] + fx * src[idx11 + c];
                    dst[out_idx + c] = static_cast<uint8_t>(std::clamp((1.0f - fy) * top + fy * btm, 0.0f, 255.0f));
                }
            }
        }
    } else if (effect.find("invert") != std::string::npos || effect.find("negative") != std::string::npos) {
        for (size_t i = 0; i < src.size(); i += 4) {
            dst[i + 0] = 255 - src[i + 0];
            dst[i + 1] = 255 - src[i + 1];
            dst[i + 2] = 255 - src[i + 2];
            dst[i + 3] = src[i + 3];
        }
    } else {
        // Pass-through unchanged: never invert colors unexpectedly
        std::memcpy(dst.data(), src.data(), src.size());
    }
}

void ScriptEngine::poll_shader_rendering() {
    if (m_shader_thread && m_shader_task_done.load()) {
        if (m_shader_thread->joinable()) {
            m_shader_thread->join();
        }
        delete m_shader_thread;
        m_shader_thread = nullptr;

        if (m_project) {
            VideoAsset* va = m_project->find_video_asset_mut(m_pending_asset_id);
            if (va && fs::exists(m_pending_video_path)) {
                va->path = m_pending_video_path;
                va->is_image = false;
                va->fps = m_pending_fps;
                va->duration = m_pending_duration;
                if (m_app) {
                    m_app->get_media_decoder().invalidate_context(m_pending_video_path);
                    m_app->get_media_decoder().request_frame(get_playhead_time(), *m_project);
                }
                notify_project_modified();
                set_status("Shader rendered across " + std::to_string(m_pending_total_frames) + " frames");
            }
        }
        m_is_rendering_shader.store(false);
        m_shader_progress.store(0.0f);
        m_shader_task_done.store(false);
    }
}

bool ScriptEngine::render_shader_across_clip(Clip* clip,
                                            const std::string& shader_src,
                                            const std::unordered_map<std::string, float>& uniforms,
                                            bool create_new,
                                            int target_track,
                                            double new_start_time,
                                            std::string& out_clip_id) {
    if (!clip || !m_project) return false;

    if (m_shader_thread) {
        if (m_shader_thread->joinable()) {
            m_shader_thread->join();
        }
        delete m_shader_thread;
        m_shader_thread = nullptr;
        m_shader_task_done.store(true);
        poll_shader_rendering();
    }

    const VideoAsset* orig_va = m_project->find_video_asset(clip->asset_id);
    std::string orig_path = (orig_va && !orig_va->path.empty()) ? orig_va->path : "";
    bool is_image = orig_va ? orig_va->is_image : false;
    bool orig_has_audio = (orig_va && orig_va->has_audio && !orig_va->is_image);
    double fps = (orig_va && orig_va->fps > 0.0) ? orig_va->fps : 30.0;
    int width = (orig_va && orig_va->width > 0) ? orig_va->width : 1280;
    int height = (orig_va && orig_va->height > 0) ? orig_va->height : 720;
    if (width % 2 != 0) width++;
    if (height % 2 != 0) height++;

    double duration = clip->duration;
    if (duration <= 0.0) duration = 5.0;
    double clip_offset = clip->offset;
    double clip_pos_x = clip->pos_x;
    double clip_pos_y = clip->pos_y;
    double clip_scale_x = clip->scale_x;
    double clip_scale_y = clip->scale_y;
    double clip_scale = clip->scale;
    double clip_rotation = clip->rotation;
    double clip_opacity = clip->opacity;
    std::string clip_id = clip->id;

    int total_frames = std::max(1, static_cast<int>(std::round(duration * fps)));

    auto now_epoch = std::chrono::system_clock::now().time_since_epoch().count();
    std::string temp_png_path = TempManager::get_temp_dir() + "/sfx_preview_" + std::to_string(now_epoch) + ".png";
    std::string temp_video_path = TempManager::get_temp_dir() + "/sfx_video_" + std::to_string(now_epoch) + ".mp4";

    double cur_rel_time = std::clamp(clip->clip_relative_time(get_playhead_time()), 0.0, duration);
    double cur_decode_time = clip_offset + cur_rel_time;

    std::vector<uint8_t> initial_src_rgba;
    bool has_initial_src = false;
    if (!orig_path.empty()) {
        DecodedVideoContext dec;
        if (dec.open(orig_path)) {
            has_initial_src = dec.decode_frame_at(is_image ? 0.0 : cur_decode_time, width, height, initial_src_rgba);
        }
    }
    if (!has_initial_src || initial_src_rgba.empty()) {
        initial_src_rgba.assign(width * height * 4, 30);
    }

    std::vector<uint8_t> initial_dst_rgba;
    auto initial_uniforms = uniforms;
    initial_uniforms["time"] = static_cast<float>(cur_rel_time);
    initial_uniforms["u_time"] = static_cast<float>(cur_rel_time);
    float base_phase = 0.0f;
    auto p_it = uniforms.find("phase");
    if (p_it != uniforms.end()) base_phase = p_it->second;
    float speed = 3.0f;
    auto sp_it = uniforms.find("speed");
    if (sp_it != uniforms.end()) speed = sp_it->second;
    initial_uniforms["phase"] = base_phase + static_cast<float>(cur_rel_time * speed * 2.0 * 3.14159265);

    run_shader_effect(shader_src, initial_src_rgba, width, height, initial_uniforms, initial_dst_rgba);
    MediaDecoder::save_rgba_to_png(temp_png_path, width, height, initial_dst_rgba);

    push_undo_state();

    VideoAsset new_va;
    new_va.id = "asset_sfx_" + std::to_string(now_epoch % 10000000);
    new_va.path = temp_png_path;
    new_va.width = width;
    new_va.height = height;
    new_va.duration = duration;
    new_va.fps = fps;
    new_va.is_image = true;
    new_va.has_audio = orig_has_audio;
    new_va.scale_x = 1.0;
    new_va.scale_y = 1.0;
    new_va.scale = 1.0;
    m_project->video_assets.push_back(new_va);

    if (create_new) {
        Clip new_clip;
        new_clip.id = "clip_sfx_" + std::to_string(now_epoch % 10000000);
        new_clip.asset_id = new_va.id;
        new_clip.start_time = new_start_time;
        new_clip.duration = duration;
        new_clip.offset = 0.0;
        new_clip.pos_x = clip_pos_x;
        new_clip.pos_y = clip_pos_y;
        new_clip.scale_x = clip_scale_x;
        new_clip.scale_y = clip_scale_y;
        new_clip.scale = clip_scale;
        new_clip.rotation = clip_rotation;
        new_clip.opacity = clip_opacity;

        if (target_track < 0) target_track = 0;
        while (target_track >= static_cast<int>(m_project->tracks.size())) {
            m_project->tracks.push_back(Track{ "Track " + std::to_string(m_project->tracks.size() + 1), "video" });
        }
        m_project->tracks[target_track].clips.push_back(new_clip);
        out_clip_id = new_clip.id;
        set_selected_clip_id(new_clip.id);
    } else {
        Clip* target = m_project->find_clip_mut(clip_id);
        if (target) {
            target->asset_id = new_va.id;
        } else {
            clip->asset_id = new_va.id;
        }
        out_clip_id = clip_id;
    }

    notify_project_modified();
    if (m_app) {
        m_app->get_media_decoder().invalidate_context(temp_png_path);
        m_app->get_media_decoder().request_frame(get_playhead_time(), *m_project);
    }

    m_pending_asset_id = new_va.id;
    m_pending_video_path = temp_video_path;
    m_pending_fps = fps;
    m_pending_duration = duration;
    m_pending_total_frames = total_frames;
    m_shader_task_done.store(false);
    m_shader_progress.store(0.01f);
    m_is_rendering_shader.store(true);

    m_shader_thread = new std::thread([this, orig_path, is_image, clip_offset, duration, fps, width, height,
                                       total_frames, shader_src, uniforms, temp_video_path, orig_has_audio,
                                       base_phase, speed, initial_src_rgba]() {
        std::string ffmpeg_cmd = "ffmpeg -y -f rawvideo -vcodec rawvideo -s " + std::to_string(width) + "x" + std::to_string(height) +
                                 " -pix_fmt rgba -r " + std::to_string(fps) + " -i - ";
        if (orig_has_audio && !orig_path.empty() && fs::exists(orig_path) && !is_image) {
            ffmpeg_cmd += "-ss " + std::to_string(clip_offset) + " -t " + std::to_string(duration) +
                          " -i \"" + orig_path + "\" -c:v libx264 -preset ultrafast -pix_fmt yuv420p -c:a aac -map 0:v:0 -map 1:a:0? \"" + temp_video_path + "\" > /dev/null 2>&1";
        } else {
            ffmpeg_cmd += "-c:v libx264 -preset ultrafast -pix_fmt yuv420p \"" + temp_video_path + "\" > /dev/null 2>&1";
        }

        FILE* pipe = popen(ffmpeg_cmd.c_str(), "w");
        if (!pipe) {
            m_shader_task_done.store(true);
            return;
        }

        DecodedVideoContext decoder;
        bool dec_open = false;
        std::vector<uint8_t> base_image_rgba;
        if (!orig_path.empty() && fs::exists(orig_path)) {
            dec_open = decoder.open(orig_path);
            if (dec_open && is_image) {
                decoder.decode_frame_at(0.0, width, height, base_image_rgba);
            }
        }
        if (is_image && base_image_rgba.empty() && !initial_src_rgba.empty()) {
            base_image_rgba = initial_src_rgba;
        }

        std::vector<uint8_t> src_rgba(width * height * 4, 0);
        std::vector<uint8_t> dst_rgba(width * height * 4, 0);

        for (int i = 0; i < total_frames; ++i) {
            double rel_t = (fps > 0.0) ? (static_cast<double>(i) / fps) : 0.0;
            double t = clip_offset + rel_t;

            if (is_image && !base_image_rgba.empty()) {
                src_rgba = base_image_rgba;
            } else if (dec_open) {
                if (!decoder.decode_frame_at(t, width, height, src_rgba)) {
                    // keep previous src_rgba
                }
            } else if (!initial_src_rgba.empty()) {
                src_rgba = initial_src_rgba;
            }

            auto frame_uniforms = uniforms;
            frame_uniforms["time"] = static_cast<float>(rel_t);
            frame_uniforms["u_time"] = static_cast<float>(rel_t);
            frame_uniforms["phase"] = base_phase + static_cast<float>(rel_t * speed * 2.0 * 3.14159265);
            frame_uniforms["progress"] = static_cast<float>(static_cast<double>(i) / std::max(1, total_frames - 1));

            run_shader_effect(shader_src, src_rgba, width, height, frame_uniforms, dst_rgba);

            size_t written = fwrite(dst_rgba.data(), 1, dst_rgba.size(), pipe);
            if (written < dst_rgba.size()) {
                std::cerr << "[ScriptEngine] FFmpeg pipe closed prematurely at frame " << i << std::endl;
                break;
            }
            m_shader_progress.store(static_cast<float>(i + 1) / static_cast<float>(total_frames));
        }

        pclose(pipe);
        m_shader_task_done.store(true);
    });

    return true;
}

static void push_lua_frame_table(lua_State* L, const ScriptFrameBuffer& fb) {
    lua_newtable(L);
    lua_pushinteger(L, fb.id);       lua_setfield(L, -2, "id");
    lua_pushinteger(L, fb.width);    lua_setfield(L, -2, "width");
    lua_pushinteger(L, fb.height);   lua_setfield(L, -2, "height");
    lua_pushinteger(L, fb.channels); lua_setfield(L, -2, "channels");
    lua_pushnumber(L, fb.time);      lua_setfield(L, -2, "time");
    lua_pushstring(L, fb.clip_id.c_str()); lua_setfield(L, -2, "clip_id");
    lua_pushstring(L, fb.applied_shader.c_str()); lua_setfield(L, -2, "applied_shader");

    if (fb.rgba.size() <= 65536) {
        lua_newtable(L);
        for (size_t i = 0; i < fb.rgba.size(); ++i) {
            lua_pushinteger(L, fb.rgba[i]);
            lua_rawseti(L, -2, static_cast<int>(i + 1));
        }
    } else {
        lua_pushlstring(L, reinterpret_cast<const char*>(fb.rgba.data()), fb.rgba.size());
    }
    lua_setfield(L, -2, "data");

    // Add object-oriented methods to the frame table
    lua_pushcfunction(L, ScriptEngine::lua_shader_apply);
    lua_setfield(L, -2, "apply_shader");
    lua_pushcfunction(L, ScriptEngine::lua_clip_set_frame);
    lua_setfield(L, -2, "reinsert");
    lua_pushcfunction(L, ScriptEngine::lua_clip_create_from_frame);
    lua_setfield(L, -2, "create_clip");
}

static bool sync_lua_frame_data(lua_State* L, int frame_idx, ScriptFrameBuffer& fb) {
    if (!lua_istable(L, frame_idx)) return false;
    lua_getfield(L, frame_idx, "id");
    if (lua_isnumber(L, -1)) fb.id = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);

    lua_getfield(L, frame_idx, "width");
    if (lua_isnumber(L, -1)) fb.width = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);

    lua_getfield(L, frame_idx, "height");
    if (lua_isnumber(L, -1)) fb.height = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);

    lua_getfield(L, frame_idx, "clip_id");
    if (lua_isstring(L, -1)) fb.clip_id = lua_tostring(L, -1);
    lua_pop(L, 1);

    lua_getfield(L, frame_idx, "applied_shader");
    if (lua_isstring(L, -1)) {
        const char* s = lua_tostring(L, -1);
        if (s && *s) fb.applied_shader = s;
    }
    lua_pop(L, 1);

    if (fb.id > 0 && s_engine_instance) {
        auto it = s_engine_instance->m_frame_buffers.find(fb.id);
        if (it != s_engine_instance->m_frame_buffers.end() && !it->second.rgba.empty()) {
            fb.rgba = it->second.rgba;
            if (fb.applied_shader.empty()) fb.applied_shader = it->second.applied_shader;
            if (fb.applied_uniforms.empty()) fb.applied_uniforms = it->second.applied_uniforms;
            return true;
        }
    }

    lua_getfield(L, frame_idx, "data");
    if (lua_isstring(L, -1)) {
        size_t len = 0;
        const char* str = lua_tolstring(L, -1, &len);
        if (str && len > 0) {
            fb.rgba.assign(reinterpret_cast<const uint8_t*>(str), reinterpret_cast<const uint8_t*>(str) + len);
        }
    } else if (lua_istable(L, -1)) {
        size_t len = lua_objlen(L, -1);
        if (len > 0) {
            fb.rgba.resize(len);
            for (size_t i = 0; i < len; ++i) {
                lua_rawgeti(L, -1, static_cast<int>(i + 1));
                fb.rgba[i] = static_cast<uint8_t>(lua_tonumber(L, -1));
                lua_pop(L, 1);
            }
        }
    }
    lua_pop(L, 1);
    return !fb.rgba.empty();
}

int ScriptEngine::lua_clip_get_frame(lua_State* L) {
    if (!s_engine_instance || !s_engine_instance->m_project) {
        lua_pushnil(L);
        return 1;
    }

    std::string clip_id = "";
    int arg_idx = 1;
    if (lua_isstring(L, 1)) {
        clip_id = lua_tostring(L, 1);
        arg_idx = 2;
    }
    if (clip_id.empty() || clip_id == "active" || clip_id == "selected") {
        clip_id = s_engine_instance->get_selected_clip_id();
    }

    Clip* clip = nullptr;
    if (!clip_id.empty()) {
        clip = s_engine_instance->m_project->find_clip_mut(clip_id);
    }
    if (!clip) {
        for (auto& trk : s_engine_instance->m_project->tracks) {
            if (trk.type == "video" && !trk.clips.empty()) {
                clip = &trk.clips.front();
                clip_id = clip->id;
                break;
            }
        }
    }
    if (!clip) {
        lua_pushnil(L);
        return 1;
    }

    const VideoAsset* va = s_engine_instance->m_project->find_video_asset(clip->asset_id);
    if (!va) {
        lua_pushnil(L);
        return 1;
    }

    double rel_time = 0.0;
    if (lua_isnumber(L, arg_idx)) {
        rel_time = lua_tonumber(L, arg_idx++);
    } else {
        rel_time = clip->clip_relative_time(s_engine_instance->get_playhead_time());
    }

    int target_w = (va->width > 0) ? va->width : 1280;
    int target_h = (va->height > 0) ? va->height : 720;
    if (lua_isnumber(L, arg_idx) && lua_isnumber(L, arg_idx + 1)) {
        target_w = (int)lua_tointeger(L, arg_idx++);
        target_h = (int)lua_tointeger(L, arg_idx++);
    }

    DecodedVideoContext decoder;
    if (!decoder.open(va->path)) {
        lua_pushnil(L);
        return 1;
    }

    double decode_time = clip->offset + std::clamp(rel_time, 0.0, clip->duration);
    ScriptFrameBuffer fb;
    fb.id = s_engine_instance->m_next_frame_id++;
    fb.width = target_w;
    fb.height = target_h;
    fb.channels = 4;
    fb.time = rel_time;
    fb.clip_id = clip->id;

    if (!decoder.decode_frame_at(decode_time, target_w, target_h, fb.rgba) || fb.rgba.empty()) {
        lua_pushnil(L);
        return 1;
    }

    s_engine_instance->m_frame_buffers[fb.id] = fb;
    push_lua_frame_table(L, fb);
    return 1;
}

int ScriptEngine::lua_shader_create(lua_State* L) {
    if (!s_engine_instance) {
        lua_pushinteger(L, 0);
        return 1;
    }
    const char* src_or_preset = luaL_checkstring(L, 1);
    std::string key = src_or_preset ? src_or_preset : "invert";

    // Query ShaderManager for user shader from ~/.config/minicut/shaders/gl or ~/.config/minicut/shaders/vulkan
    CustomShader cs;
    if (ShaderManager::instance().find_shader(key, cs)) {
        ShaderManager::instance().load_shader_source(cs);
        if (!cs.frag_src.empty()) {
            key = cs.frag_src;
            if (key.find("// Shader: ") == std::string::npos && key.find(cs.name) == std::string::npos) {
                key = "// Shader: " + cs.name + "\n" + key;
            }
        }
    }

    int sid = s_engine_instance->m_next_shader_id++;
    s_engine_instance->m_shaders[sid] = key;
    lua_pushinteger(L, sid);
    return 1;
}

int ScriptEngine::lua_shader_set_uniform(lua_State* L) {
    (void)L;
    // Uniforms are stored or passed directly to shader.apply
    lua_pushboolean(L, 1);
    return 1;
}

int ScriptEngine::lua_shader_apply(lua_State* L) {
    if (!s_engine_instance) {
        lua_pushnil(L);
        return 1;
    }
    // Formats:
    // shader.apply(shader_id_or_source, frame, [uniforms])
    // frame:apply_shader(shader_id_or_source, [uniforms])
    std::string shader_src = "invert";
    int frame_arg = 1;
    int uniforms_arg = 2;

    if (lua_istable(L, 1) && (lua_isstring(L, 2) || lua_isnumber(L, 2) || lua_isfunction(L, 2))) {
        // Method style: frame:apply_shader(shader, [uniforms])
        frame_arg = 1;
        if (lua_isnumber(L, 2)) {
            int sid = (int)lua_tointeger(L, 2);
            auto it = s_engine_instance->m_shaders.find(sid);
            if (it != s_engine_instance->m_shaders.end()) shader_src = it->second;
        } else if (lua_isstring(L, 2)) {
            shader_src = lua_tostring(L, 2);
        }
        uniforms_arg = 3;
    } else {
        if (lua_isnumber(L, 1)) {
            int sid = (int)lua_tointeger(L, 1);
            auto it = s_engine_instance->m_shaders.find(sid);
            if (it != s_engine_instance->m_shaders.end()) shader_src = it->second;
        } else if (lua_isstring(L, 1)) {
            shader_src = lua_tostring(L, 1);
        }
        frame_arg = 2;
        uniforms_arg = 3;
    }

    ScriptFrameBuffer src_fb;
    if (lua_istable(L, frame_arg)) {
        lua_getfield(L, frame_arg, "id");
        if (lua_isnumber(L, -1)) {
            int fid = (int)lua_tointeger(L, -1);
            auto it = s_engine_instance->m_frame_buffers.find(fid);
            if (it != s_engine_instance->m_frame_buffers.end()) src_fb = it->second;
        }
        lua_pop(L, 1);
        sync_lua_frame_data(L, frame_arg, src_fb);
    }
    if (src_fb.rgba.empty()) {
        lua_pushnil(L);
        return 1;
    }

    std::unordered_map<std::string, float> uniforms;
    if (lua_istable(L, uniforms_arg)) {
        lua_pushnil(L);
        while (lua_next(L, uniforms_arg) != 0) {
            if (lua_isstring(L, -2) && lua_isnumber(L, -1)) {
                uniforms[lua_tostring(L, -2)] = (float)lua_tonumber(L, -1);
            }
            lua_pop(L, 1);
        }
    }

    ScriptFrameBuffer out_fb;
    out_fb.id = s_engine_instance->m_next_frame_id++;
    out_fb.width = src_fb.width;
    out_fb.height = src_fb.height;
    out_fb.channels = 4;
    out_fb.time = src_fb.time;
    out_fb.clip_id = src_fb.clip_id;

    // Check if custom Lua per-pixel shader function passed
    if (lua_isfunction(L, 1) || (frame_arg == 1 && lua_isfunction(L, 2))) {
        int fn_idx = lua_isfunction(L, 1) ? 1 : 2;
        out_fb.rgba.resize(src_fb.rgba.size());
        for (int y = 0; y < src_fb.height; ++y) {
            for (int x = 0; x < src_fb.width; ++x) {
                int idx = (y * src_fb.width + x) * 4;
                lua_pushvalue(L, fn_idx);
                lua_pushinteger(L, src_fb.rgba[idx + 0]);
                lua_pushinteger(L, src_fb.rgba[idx + 1]);
                lua_pushinteger(L, src_fb.rgba[idx + 2]);
                lua_pushinteger(L, src_fb.rgba[idx + 3]);
                lua_pushinteger(L, x);
                lua_pushinteger(L, y);
                if (lua_pcall(L, 6, 4, 0) == 0) {
                    out_fb.rgba[idx + 0] = static_cast<uint8_t>(lua_tonumber(L, -4));
                    out_fb.rgba[idx + 1] = static_cast<uint8_t>(lua_tonumber(L, -3));
                    out_fb.rgba[idx + 2] = static_cast<uint8_t>(lua_tonumber(L, -2));
                    out_fb.rgba[idx + 3] = static_cast<uint8_t>(lua_tonumber(L, -1));
                    lua_pop(L, 4);
                } else {
                    lua_pop(L, 1); // pop error
                    out_fb.rgba[idx + 0] = src_fb.rgba[idx + 0];
                    out_fb.rgba[idx + 1] = src_fb.rgba[idx + 1];
                    out_fb.rgba[idx + 2] = src_fb.rgba[idx + 2];
                    out_fb.rgba[idx + 3] = src_fb.rgba[idx + 3];
                }
            }
        }
    } else {
        run_shader_effect(shader_src, src_fb.rgba, src_fb.width, src_fb.height, uniforms, out_fb.rgba);
    }

    out_fb.applied_shader = shader_src;
    out_fb.applied_uniforms = uniforms;

    s_engine_instance->m_frame_buffers[out_fb.id] = out_fb;
    push_lua_frame_table(L, out_fb);
    return 1;
}

int ScriptEngine::lua_clip_set_frame(lua_State* L) {
    if (!s_engine_instance || !s_engine_instance->m_project) {
        lua_pushboolean(L, 0);
        return 1;
    }
    // Formats:
    // clip.set_frame(frame)
    // clip.set_frame(clip_id, frame)
    // frame:reinsert([clip_id])
    std::string target_clip_id = "";
    int frame_arg = 1;
    if (lua_isstring(L, 1)) {
        target_clip_id = lua_tostring(L, 1);
        frame_arg = 2;
    } else if (lua_istable(L, 1) && lua_isstring(L, 2)) {
        frame_arg = 1;
        target_clip_id = lua_tostring(L, 2);
    }

    ScriptFrameBuffer fb;
    if (lua_istable(L, frame_arg)) {
        lua_getfield(L, frame_arg, "id");
        if (lua_isnumber(L, -1)) {
            int fid = (int)lua_tointeger(L, -1);
            auto it = s_engine_instance->m_frame_buffers.find(fid);
            if (it != s_engine_instance->m_frame_buffers.end()) fb = it->second;
        }
        lua_pop(L, 1);
        sync_lua_frame_data(L, frame_arg, fb);
    }
    if (fb.rgba.empty()) {
        lua_pushboolean(L, 0);
        return 1;
    }

    if (target_clip_id.empty() && !fb.clip_id.empty()) {
        target_clip_id = fb.clip_id;
    }
    if (target_clip_id.empty() || target_clip_id == "active" || target_clip_id == "selected") {
        target_clip_id = s_engine_instance->get_selected_clip_id();
    }

    Clip* target_clip = nullptr;
    if (!target_clip_id.empty() && target_clip_id != "active" && target_clip_id != "selected") {
        target_clip = s_engine_instance->m_project->find_clip_mut(target_clip_id);
    }
    if (!target_clip) {
        std::string sel = s_engine_instance->get_selected_clip_id();
        if (!sel.empty()) {
            target_clip = s_engine_instance->m_project->find_clip_mut(sel);
        }
    }
    if (!target_clip) {
        for (auto& trk : s_engine_instance->m_project->tracks) {
            if (trk.type == "video" && !trk.clips.empty()) {
                target_clip = &trk.clips.front();
                break;
            }
        }
    }
    if (!target_clip) {
        lua_pushboolean(L, 0);
        return 1;
    }

    if (!fb.applied_shader.empty()) {
        std::string out_clip_id;
        if (s_engine_instance->render_shader_across_clip(target_clip, fb.applied_shader, fb.applied_uniforms, false, 0, 0.0, out_clip_id)) {
            lua_pushboolean(L, 1);
            lua_pushstring(L, target_clip->asset_id.c_str());
            return 2;
        }
    }

    // Save RGBA buffer to PNG in Temp directory
    std::string png_path = TempManager::get_temp_dir() + "/sfx_frame_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) + ".png";
    if (!MediaDecoder::save_rgba_to_png(png_path, fb.width, fb.height, fb.rgba)) {
        lua_pushboolean(L, 0);
        return 1;
    }

    s_engine_instance->push_undo_state();
    VideoAsset new_va;
    new_va.id = "asset_sfx_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count() % 1000000);
    new_va.path = png_path;
    new_va.width = fb.width;
    new_va.height = fb.height;
    new_va.duration = target_clip->duration;
    new_va.is_image = true;
    new_va.has_audio = false;
    new_va.scale_x = 1.0;
    new_va.scale_y = 1.0;
    new_va.scale = 1.0;
    s_engine_instance->m_project->video_assets.push_back(new_va);

    target_clip->asset_id = new_va.id;
    if (s_engine_instance && s_engine_instance->m_app) {
        s_engine_instance->m_app->get_media_decoder().invalidate_context(png_path);
    }
    s_engine_instance->notify_project_modified();

    lua_pushboolean(L, 1);
    lua_pushstring(L, new_va.id.c_str());
    return 2;
}

int ScriptEngine::lua_clip_create_from_frame(lua_State* L) {
    if (!s_engine_instance || !s_engine_instance->m_project) {
        lua_pushstring(L, "");
        return 1;
    }
    // Formats:
    // clip.create_from_frame(frame, [track_idx], [start_time], [duration])
    // frame:create_clip([track_idx], [start_time], [duration])
    int frame_arg = 1;
    int track_idx = 0;
    double start_time = s_engine_instance->get_playhead_time();
    double duration = 5.0;

    if (lua_istable(L, 1)) {
        frame_arg = 1;
        if (lua_isnumber(L, 2)) track_idx = (int)lua_tointeger(L, 2);
        if (lua_isnumber(L, 3)) start_time = lua_tonumber(L, 3);
        if (lua_isnumber(L, 4)) duration = lua_tonumber(L, 4);
    }

    ScriptFrameBuffer fb;
    if (lua_istable(L, frame_arg)) {
        lua_getfield(L, frame_arg, "id");
        if (lua_isnumber(L, -1)) {
            int fid = (int)lua_tointeger(L, -1);
            auto it = s_engine_instance->m_frame_buffers.find(fid);
            if (it != s_engine_instance->m_frame_buffers.end()) fb = it->second;
        }
        lua_pop(L, 1);
        sync_lua_frame_data(L, frame_arg, fb);
    }
    if (fb.rgba.empty()) {
        lua_pushstring(L, "");
        return 1;
    }

    if (!fb.applied_shader.empty()) {
        std::string new_clip_id;
        Clip* base_clip = nullptr;
        if (!fb.clip_id.empty() && fb.clip_id != "active" && fb.clip_id != "selected") {
            base_clip = s_engine_instance->m_project->find_clip_mut(fb.clip_id);
        }
        if (!base_clip && !s_engine_instance->get_selected_clip_id().empty()) {
            base_clip = s_engine_instance->m_project->find_clip_mut(s_engine_instance->get_selected_clip_id());
        }
        if (!base_clip) {
            for (auto& trk : s_engine_instance->m_project->tracks) {
                if (trk.type == "video" && !trk.clips.empty()) {
                    base_clip = &trk.clips.front();
                    break;
                }
            }
        }
        if (base_clip) {
            Clip temp_clip = *base_clip;
            temp_clip.duration = duration;
            if (s_engine_instance->render_shader_across_clip(&temp_clip, fb.applied_shader, fb.applied_uniforms, true, track_idx, start_time, new_clip_id)) {
                lua_pushstring(L, new_clip_id.c_str());
                return 1;
            }
        }
    }

    std::string png_path = TempManager::get_temp_dir() + "/sfx_clip_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) + ".png";
    if (!MediaDecoder::save_rgba_to_png(png_path, fb.width, fb.height, fb.rgba)) {
        lua_pushstring(L, "");
        return 1;
    }

    Project& proj = *s_engine_instance->m_project;
    s_engine_instance->push_undo_state();

    VideoAsset new_va;
    std::string ts = std::to_string(std::chrono::system_clock::now().time_since_epoch().count() % 1000000);
    new_va.id = "asset_sfx_" + ts;
    new_va.path = png_path;
    new_va.width = fb.width;
    new_va.height = fb.height;
    new_va.duration = duration;
    new_va.is_image = true;
    new_va.has_audio = false;
    new_va.scale_x = 1.0;
    new_va.scale_y = 1.0;
    new_va.scale = 1.0;
    proj.video_assets.push_back(new_va);

    // Ensure valid video track
    if (track_idx < 0 || track_idx >= static_cast<int>(proj.tracks.size()) || proj.tracks[track_idx].type != "video") {
        track_idx = -1;
        for (size_t t = 0; t < proj.tracks.size(); ++t) {
            if (proj.tracks[t].type == "video") {
                track_idx = static_cast<int>(t);
                break;
            }
        }
        if (track_idx < 0) {
            Track vt;
            vt.type = "video";
            vt.name = "Video 1";
            vt.id = "track_v_" + ts;
            proj.tracks.push_back(vt);
            track_idx = static_cast<int>(proj.tracks.size() - 1);
        }
    }

    Clip c;
    c.id = "clip_sfx_" + ts;
    c.asset_id = new_va.id;
    c.start_time = std::max(0.0, start_time);
    c.duration = std::max(0.1, duration);
    c.offset = 0.0;
    c.scale_x = 1.0;
    c.scale_y = 1.0;
    c.scale = 1.0;

    proj.tracks[track_idx].clips.push_back(c);
    s_engine_instance->set_selected_clip_id(c.id);
    s_engine_instance->notify_project_modified();

    lua_pushstring(L, c.id.c_str());
    return 1;
}

// ----------------------------------------------------------------------------
// Localization API for Lua
// ----------------------------------------------------------------------------

int ScriptEngine::lua_locale_get(lua_State* L) {
    if (lua_istable(L, 1)) {
        // Handle metatable self call or registration: locale({ en = { ... } })
        return lua_locale_register(L);
    }
    const char* key = luaL_checkstring(L, 1);
    const char* fallback = luaL_optstring(L, 2, key);
    std::string str = LocaleManager::instance().get_string(key ? key : "", fallback ? fallback : "");
    lua_pushstring(L, str.c_str());
    return 1;
}

int ScriptEngine::lua_locale_get_language(lua_State* L) {
    lua_pushstring(L, LocaleManager::instance().current_locale().c_str());
    return 1;
}

int ScriptEngine::lua_locale_set_language(lua_State* L) {
    const char* code = luaL_checkstring(L, 1);
    if (code) {
        LocaleManager::instance().set_locale(code);
        SettingsLogic::instance().set_language(code);
    }
    return 0;
}

int ScriptEngine::lua_locale_is_rtl(lua_State* L) {
    const char* lang = lua_isstring(L, 1) ? lua_tostring(L, 1) : "";
    lua_pushboolean(L, LocaleManager::instance().is_rtl(lang ? lang : ""));
    return 1;
}

int ScriptEngine::lua_locale_register(lua_State* L) {
    // 1. locale.register("ja", "key", "val")
    // 2. locale.register("ja", { k1 = "v1", k2 = "v2" })
    // 3. locale.register({ ja = { k1 = "v1" }, en = { k1 = "v2" } })
    if (lua_isstring(L, 1) && lua_isstring(L, 2) && lua_isstring(L, 3)) {
        std::string lang = lua_tostring(L, 1);
        std::string k = lua_tostring(L, 2);
        std::string v = lua_tostring(L, 3);
        LocaleManager::instance().register_string(lang, k, v);
        lua_pushboolean(L, true);
        return 1;
    }
    if (lua_isstring(L, 1) && lua_istable(L, 2)) {
        std::string lang = lua_tostring(L, 1);
        lua_pushnil(L);
        while (lua_next(L, 2) != 0) {
            if (lua_isstring(L, -2) && lua_isstring(L, -1)) {
                LocaleManager::instance().register_string(lang, lua_tostring(L, -2), lua_tostring(L, -1));
            }
            lua_pop(L, 1);
        }
        lua_pushboolean(L, true);
        return 1;
    }
    if (lua_istable(L, 1)) {
        lua_pushnil(L);
        while (lua_next(L, 1) != 0) {
            if (lua_isstring(L, -2) && lua_istable(L, -1)) {
                std::string lang = lua_tostring(L, -2);
                lua_pushnil(L);
                while (lua_next(L, -2) != 0) {
                    if (lua_isstring(L, -2) && lua_isstring(L, -1)) {
                        LocaleManager::instance().register_string(lang, lua_tostring(L, -2), lua_tostring(L, -1));
                    }
                    lua_pop(L, 1);
                }
            }
            lua_pop(L, 1);
        }
        lua_pushboolean(L, true);
        return 1;
    }
    lua_pushboolean(L, false);
    return 1;
}

// ----------------------------------------------------------------------------
// AI Library for Lua
// ----------------------------------------------------------------------------

static void json_to_lua(lua_State* L, const nlohmann::json& j) {
    if (j.is_null()) {
        lua_pushnil(L);
    } else if (j.is_boolean()) {
        lua_pushboolean(L, j.get<bool>() ? 1 : 0);
    } else if (j.is_number_integer()) {
        lua_pushinteger(L, j.get<int64_t>());
    } else if (j.is_number_float()) {
        lua_pushnumber(L, j.get<double>());
    } else if (j.is_string()) {
        lua_pushstring(L, j.get<std::string>().c_str());
    } else if (j.is_array()) {
        lua_newtable(L);
        int idx = 1;
        for (const auto& elem : j) {
            json_to_lua(L, elem);
            lua_rawseti(L, -2, idx++);
        }
    } else if (j.is_object()) {
        lua_newtable(L);
        for (auto it = j.begin(); it != j.end(); ++it) {
            lua_pushstring(L, it.key().c_str());
            json_to_lua(L, it.value());
            lua_settable(L, -3);
        }
    } else {
        lua_pushnil(L);
    }
}

static nlohmann::json lua_to_json(lua_State* L, int idx) {
    int t = lua_type(L, idx);
    if (t == LUA_TNIL || t == LUA_TNONE) {
        return nullptr;
    } else if (t == LUA_TBOOLEAN) {
        return (bool)lua_toboolean(L, idx);
    } else if (t == LUA_TNUMBER) {
        double d = lua_tonumber(L, idx);
        if (std::floor(d) == d && !std::isinf(d) && !std::isnan(d)) {
            return (int64_t)d;
        }
        return d;
    } else if (t == LUA_TSTRING) {
        return std::string(lua_tostring(L, idx));
    } else if (t == LUA_TTABLE) {
        bool is_array = true;
        size_t len = lua_objlen(L, idx);
        lua_pushnil(L);
        size_t count = 0;
        int abs_idx = (idx > 0) ? idx : (lua_gettop(L) + idx + 1);
        while (lua_next(L, abs_idx) != 0) {
            count++;
            if (!lua_isnumber(L, -2)) {
                is_array = false;
                lua_pop(L, 2);
                break;
            }
            double k = lua_tonumber(L, -2);
            if (k < 1.0 || k > (double)(len + 1000000) || std::floor(k) != k) {
                is_array = false;
                lua_pop(L, 2);
                break;
            }
            lua_pop(L, 1);
        }
        if (count != len || len == 0) {
            if (count == 0) return nlohmann::json::object();
            is_array = (count == len && len > 0);
        }

        if (is_array && len > 0) {
            nlohmann::json arr = nlohmann::json::array();
            for (size_t i = 1; i <= len; ++i) {
                lua_rawgeti(L, abs_idx, (int)i);
                arr.push_back(lua_to_json(L, -1));
                lua_pop(L, 1);
            }
            return arr;
        } else {
            nlohmann::json obj = nlohmann::json::object();
            lua_pushnil(L);
            while (lua_next(L, abs_idx) != 0) {
                std::string key;
                if (lua_isstring(L, -2)) {
                    key = lua_tostring(L, -2);
                } else if (lua_isnumber(L, -2)) {
                    key = std::to_string((int64_t)lua_tonumber(L, -2));
                }
                if (!key.empty()) {
                    obj[key] = lua_to_json(L, -1);
                }
                lua_pop(L, 1);
            }
            return obj;
        }
    }
    return nullptr;
}

static std::string clean_json_markdown(const std::string& input) {
    std::string s = input;
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n')) s.erase(s.begin());
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) s.pop_back();

    if (s.rfind("```", 0) == 0) {
        size_t first_newline = s.find('\n');
        if (first_newline != std::string::npos) {
            s = s.substr(first_newline + 1);
        }
        size_t last_fence = s.rfind("```");
        if (last_fence != std::string::npos) {
            s = s.substr(0, last_fence);
        }
    }
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n')) s.erase(s.begin());
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) s.pop_back();
    return s;
}

int ScriptEngine::lua_ai_is_available(lua_State* L) {
    bool avail = false;
    if (s_engine_instance && s_engine_instance->has_mock_ai_response()) {
        avail = true;
    } else {
        avail = SettingsLogic::instance().is_ai_enabled() && SettingsLogic::instance().is_authenticated();
    }
    lua_pushboolean(L, avail);
    return 1;
}

int ScriptEngine::lua_ai_is_enabled(lua_State* L) {
    lua_pushboolean(L, SettingsLogic::instance().is_ai_enabled());
    return 1;
}

int ScriptEngine::lua_ai_set_enabled(lua_State* L) {
    bool en = lua_toboolean(L, 1);
    SettingsLogic::instance().set_ai_enabled(en);
    SettingsLogic::instance().save_to_disk();
    return 0;
}

int ScriptEngine::lua_ai_has_api_key(lua_State* L) {
    lua_pushboolean(L, SettingsLogic::instance().is_authenticated());
    return 1;
}

int ScriptEngine::lua_ai_set_api_key(lua_State* L) {
    const char* key = luaL_checkstring(L, 1);
    if (key) {
        SettingsLogic::instance().set_gemini_api_key(key);
        SettingsLogic::instance().save_to_disk();
    }
    return 0;
}

int ScriptEngine::lua_ai_is_busy(lua_State* L) {
    lua_pushboolean(L, AICore::instance().is_busy());
    return 1;
}

int ScriptEngine::lua_ai_get_status(lua_State* L) {
    lua_pushstring(L, AICore::instance().get_status().c_str());
    return 1;
}

int ScriptEngine::lua_ai_set_mock_response(lua_State* L) {
    if (lua_isstring(L, 1)) {
        if (s_engine_instance) s_engine_instance->set_mock_ai_response(lua_tostring(L, 1));
    } else if (lua_istable(L, 1)) {
        nlohmann::json j = lua_to_json(L, 1);
        if (s_engine_instance) s_engine_instance->set_mock_ai_response(j.dump());
    }
    return 0;
}

int ScriptEngine::lua_ai_clear_mock_response(lua_State* L) {
    (void)L;
    if (s_engine_instance) s_engine_instance->clear_mock_ai_response();
    return 0;
}

int ScriptEngine::lua_ai_prompt(lua_State* L) {
    const char* prompt = luaL_checkstring(L, 1);
    const char* sys_inst = luaL_optstring(L, 2, "You are an AI assistant in minicut video editor.");

    if (s_engine_instance && s_engine_instance->has_mock_ai_response()) {
        lua_pushstring(L, s_engine_instance->get_mock_ai_response().c_str());
        lua_pushboolean(L, true);
        return 2;
    }

    if (!SettingsLogic::instance().is_ai_enabled() || !SettingsLogic::instance().is_authenticated()) {
        lua_pushnil(L);
        lua_pushstring(L, "AI is not enabled or API key is missing");
        return 2;
    }

    std::string key = SettingsLogic::instance().get_gemini_api_key();
    std::string resp = AICore::query_gemini_api(prompt ? prompt : "", sys_inst ? sys_inst : "", key);
    if (resp.empty()) {
        lua_pushnil(L);
        lua_pushstring(L, "No response from AI API");
        return 2;
    }

    lua_pushstring(L, resp.c_str());
    lua_pushboolean(L, true);
    return 2;
}

int ScriptEngine::lua_ai_parse_json(lua_State* L) {
    const char* raw = luaL_checkstring(L, 1);
    if (!raw) {
        lua_pushnil(L);
        return 1;
    }
    std::string cleaned = clean_json_markdown(raw);
    try {
        nlohmann::json j = nlohmann::json::parse(cleaned);
        json_to_lua(L, j);
        return 1;
    } catch (const std::exception& e) {
        lua_pushnil(L);
        lua_pushstring(L, e.what());
        return 2;
    }
}

int ScriptEngine::lua_ai_to_json(lua_State* L) {
    int indent = (int)luaL_optinteger(L, 2, -1);
    nlohmann::json j = lua_to_json(L, 1);
    std::string str = (indent >= 0) ? j.dump(indent) : j.dump();
    lua_pushstring(L, str.c_str());
    return 1;
}

int ScriptEngine::lua_ai_prompt_json(lua_State* L) {
    const char* prompt = luaL_checkstring(L, 1);
    std::string schema_inst = "Respond strictly with valid JSON without any markdown formatting or explanations.";
    if (lua_isstring(L, 2)) {
        schema_inst = lua_tostring(L, 2);
    } else if (lua_istable(L, 2)) {
        nlohmann::json schema = lua_to_json(L, 2);
        schema_inst = "Respond strictly with valid JSON adhering to this schema/default structure: " + schema.dump();
    }

    std::string resp;
    if (s_engine_instance && s_engine_instance->has_mock_ai_response()) {
        resp = s_engine_instance->get_mock_ai_response();
    } else {
        if (!SettingsLogic::instance().is_ai_enabled() || !SettingsLogic::instance().is_authenticated()) {
            lua_pushnil(L);
            lua_pushstring(L, "AI is not enabled or API key is missing");
            return 2;
        }
        std::string key = SettingsLogic::instance().get_gemini_api_key();
        resp = AICore::query_gemini_api(prompt ? prompt : "", schema_inst, key);
    }

    if (resp.empty()) {
        lua_pushnil(L);
        lua_pushstring(L, "Empty response from AI API");
        return 2;
    }

    std::string cleaned = clean_json_markdown(resp);
    try {
        nlohmann::json j = nlohmann::json::parse(cleaned);
        json_to_lua(L, j);
        return 1;
    } catch (const std::exception& e) {
        lua_pushnil(L);
        lua_pushstring(L, e.what());
        return 2;
    }
}

int ScriptEngine::lua_ai_struct(lua_State* L) {
    // ai.struct(schema_defaults, [data_or_json_string])
    nlohmann::json base = nlohmann::json::object();
    if (lua_istable(L, 1)) {
        base = lua_to_json(L, 1);
    }
    if (lua_isstring(L, 2)) {
        try {
            nlohmann::json overlay = nlohmann::json::parse(clean_json_markdown(lua_tostring(L, 2)));
            base.merge_patch(overlay);
        } catch (...) {}
    } else if (lua_istable(L, 2)) {
        nlohmann::json overlay = lua_to_json(L, 2);
        base.merge_patch(overlay);
    }
    json_to_lua(L, base);
    return 1;
}

// ----------------------------------------------------------------------------
// Export Progress & Status Lua APIs
// ----------------------------------------------------------------------------

int ScriptEngine::lua_export_get_audio_progress(lua_State* L) {
    if (s_engine_instance && s_engine_instance->m_app) {
        lua_pushnumber(L, s_engine_instance->m_app->get_export_manager().get_audio_progress());
    } else {
        lua_pushnumber(L, 0.0);
    }
    return 1;
}

int ScriptEngine::lua_export_get_progress(lua_State* L) {
    if (s_engine_instance && s_engine_instance->m_app) {
        lua_pushnumber(L, s_engine_instance->m_app->get_export_manager().get_progress());
    } else {
        lua_pushnumber(L, 0.0);
    }
    return 1;
}

int ScriptEngine::lua_export_is_exporting(lua_State* L) {
    if (s_engine_instance && s_engine_instance->m_app) {
        lua_pushboolean(L, s_engine_instance->m_app->get_export_manager().is_exporting());
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

int ScriptEngine::lua_export_get_status(lua_State* L) {
    if (s_engine_instance && s_engine_instance->m_app) {
        lua_pushstring(L, s_engine_instance->m_app->get_export_manager().get_status_text().c_str());
    } else {
        lua_pushstring(L, "Idle");
    }
    return 1;
}

// ----------------------------------------------------------------------------
// Project & Minicut Utilities C-API
// ----------------------------------------------------------------------------

int ScriptEngine::lua_project_import_video(lua_State* L) {
    if (!s_engine_instance || !s_engine_instance->m_project) {
        lua_pushstring(L, "");
        return 1;
    }
    const char* path = luaL_checkstring(L, 1);
    std::string id = ensure_asset_imported(s_engine_instance, path, "video");
    lua_pushstring(L, id.c_str());
    return 1;
}

int ScriptEngine::lua_project_import_audio(lua_State* L) {
    if (!s_engine_instance || !s_engine_instance->m_project) {
        lua_pushstring(L, "");
        return 1;
    }
    const char* path = luaL_checkstring(L, 1);
    std::string id = ensure_asset_imported(s_engine_instance, path, "audio");
    lua_pushstring(L, id.c_str());
    return 1;
}

int ScriptEngine::lua_project_import_image(lua_State* L) {
    if (!s_engine_instance || !s_engine_instance->m_project) {
        lua_pushstring(L, "");
        return 1;
    }
    const char* path = luaL_checkstring(L, 1);
    double dur = luaL_optnumber(L, 2, 5.0);
    std::string id = ensure_asset_imported(s_engine_instance, path, "image", dur);
    lua_pushstring(L, id.c_str());
    return 1;
}

int ScriptEngine::lua_project_get_name(lua_State* L) {
    if (!s_engine_instance || !s_engine_instance->m_project) {
        lua_pushstring(L, "");
        return 1;
    }
    lua_pushstring(L, s_engine_instance->m_project->name.c_str());
    return 1;
}

int ScriptEngine::lua_project_set_name(lua_State* L) {
    if (!s_engine_instance || !s_engine_instance->m_project) return 0;
    const char* name = luaL_checkstring(L, 1);
    s_engine_instance->m_project->name = name;
    return 0;
}

int ScriptEngine::lua_minicut_log(lua_State* L) {
    const char* msg = luaL_optstring(L, 1, "");
    std::cout << "[Lua Script] " << msg << std::endl;
    return 0;
}

int ScriptEngine::lua_minicut_set_status(lua_State* L) {
    if (!s_engine_instance) return 0;
    const char* msg = luaL_checkstring(L, 1);
    double dur = luaL_optnumber(L, 2, 4.0);
    s_engine_instance->set_status(msg, dur);
    return 0;
}

int ScriptEngine::lua_minicut_push_undo(lua_State* L) {
    (void)L;
    if (s_engine_instance) {
        s_engine_instance->push_undo_state();
    }
    return 0;
}

// ----------------------------------------------------------------------------
// Project Save & Load Implementation
// ----------------------------------------------------------------------------

int ScriptEngine::lua_project_save(lua_State* L) {
    if (!s_engine_instance || !s_engine_instance->m_project) {
        lua_pushboolean(L, 0);
        lua_pushstring(L, "Project engine not initialized");
        return 2;
    }
    const char* path = luaL_optstring(L, 1, nullptr);
    std::string target_path;
    if (path && std::strlen(path) > 0) {
        target_path = path;
    } else if (!s_engine_instance->m_project->file_path.empty()) {
        target_path = s_engine_instance->m_project->file_path;
    } else {
        target_path = "untitled.minicut";
    }

    bool success = false;
    if (s_engine_instance->m_on_save_project) {
        success = s_engine_instance->m_on_save_project(target_path);
    } else {
        success = ProjectSerializer::save_to_file(*s_engine_instance->m_project, target_path);
        if (success) {
            s_engine_instance->m_project->file_path = target_path;
            s_engine_instance->notify_project_modified();
        }
    }

    if (success) {
        lua_pushboolean(L, 1);
        lua_pushstring(L, target_path.c_str());
    } else {
        lua_pushboolean(L, 0);
        lua_pushstring(L, ("Failed to save project to: " + target_path).c_str());
    }
    return 2;
}

int ScriptEngine::lua_project_load(lua_State* L) {
    if (!s_engine_instance || !s_engine_instance->m_project) {
        lua_pushboolean(L, 0);
        lua_pushstring(L, "Project engine not initialized");
        return 2;
    }
    const char* path = luaL_checkstring(L, 1);
    if (!path || std::strlen(path) == 0) {
        lua_pushboolean(L, 0);
        lua_pushstring(L, "No project filepath specified");
        return 2;
    }

    bool success = false;
    if (s_engine_instance->m_on_load_project) {
        success = s_engine_instance->m_on_load_project(path);
    } else {
        success = ProjectSerializer::load_from_file(*s_engine_instance->m_project, path);
        if (success) {
            s_engine_instance->m_project->file_path = path;
            s_engine_instance->notify_project_modified();
        }
    }

    if (success) {
        lua_pushboolean(L, 1);
        lua_pushstring(L, path);
    } else {
        lua_pushboolean(L, 0);
        lua_pushstring(L, ("Failed to load project from: " + std::string(path)).c_str());
    }
    return 2;
}

// ----------------------------------------------------------------------------
// Net API Implementation (libcurl)
// ----------------------------------------------------------------------------

struct HttpResponse {
    long status_code = 0;
    std::string body;
    std::vector<std::string> headers;
    std::string error;
    bool success = false;
};

static size_t curl_string_write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    size_t total = size * nmemb;
    auto* s = static_cast<std::string*>(userdata);
    s->append(ptr, total);
    return total;
}

static size_t curl_file_write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    size_t total = size * nmemb;
    FILE* fp = static_cast<FILE*>(userdata);
    return std::fwrite(ptr, 1, total, fp);
}

static size_t curl_header_write_cb(char* buffer, size_t size, size_t nitems, void* userdata) {
    size_t total = size * nitems;
    auto* headers = static_cast<std::vector<std::string>*>(userdata);
    std::string h(buffer, total);
    while (!h.empty() && (h.back() == '\r' || h.back() == '\n')) h.pop_back();
    if (!h.empty()) {
        headers->push_back(h);
    }
    return total;
}

static void push_http_response_table(lua_State* L, const HttpResponse& resp) {
    lua_newtable(L);
    lua_pushinteger(L, resp.status_code);
    lua_setfield(L, -2, "status");

    lua_pushboolean(L, (resp.status_code >= 200 && resp.status_code < 300) ? 1 : 0);
    lua_setfield(L, -2, "ok");

    lua_pushlstring(L, resp.body.data(), resp.body.size());
    lua_setfield(L, -2, "body");

    lua_newtable(L);
    for (size_t i = 0; i < resp.headers.size(); ++i) {
        lua_pushstring(L, resp.headers[i].c_str());
        lua_rawseti(L, -2, static_cast<int>(i + 1));
        auto colon = resp.headers[i].find(':');
        if (colon != std::string::npos) {
            std::string k = resp.headers[i].substr(0, colon);
            std::string v = resp.headers[i].substr(colon + 1);
            while (!k.empty() && k.back() == ' ') k.pop_back();
            while (!v.empty() && v.front() == ' ') v.erase(v.begin());
            lua_pushstring(L, v.c_str());
            lua_setfield(L, -2, k.c_str());
        }
    }
    lua_setfield(L, -2, "headers");
}

int ScriptEngine::lua_net_get(lua_State* L) {
    const char* url = luaL_checkstring(L, 1);
    long timeout = 30;
    curl_slist* header_list = nullptr;

    if (lua_istable(L, 2)) {
        lua_getfield(L, 2, "timeout");
        if (lua_isnumber(L, -1)) timeout = static_cast<long>(lua_tonumber(L, -1));
        lua_pop(L, 1);

        lua_getfield(L, 2, "headers");
        if (lua_istable(L, -1)) {
            lua_pushnil(L);
            while (lua_next(L, -2) != 0) {
                if (lua_isstring(L, -2) && lua_isstring(L, -1)) {
                    std::string h = std::string(lua_tostring(L, -2)) + ": " + lua_tostring(L, -1);
                    header_list = curl_slist_append(header_list, h.c_str());
                } else if (lua_isstring(L, -1)) {
                    header_list = curl_slist_append(header_list, lua_tostring(L, -1));
                }
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        if (header_list) curl_slist_free_all(header_list);
        lua_pushnil(L);
        lua_pushstring(L, "Failed to initialize CURL");
        return 2;
    }

    HttpResponse resp;
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "minicut-editor/1.0");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_string_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp.body);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, curl_header_write_cb);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &resp.headers);
    if (header_list) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
    }

    CURLcode res = curl_easy_perform(curl);
    if (res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp.status_code);
        resp.success = true;
    } else {
        resp.error = curl_easy_strerror(res);
    }

    curl_easy_cleanup(curl);
    if (header_list) curl_slist_free_all(header_list);

    if (!resp.success) {
        lua_pushnil(L);
        lua_pushstring(L, resp.error.c_str());
        return 2;
    }

    push_http_response_table(L, resp);
    return 1;
}

int ScriptEngine::lua_net_post(lua_State* L) {
    const char* url = luaL_checkstring(L, 1);
    std::string post_data;
    if (lua_isstring(L, 2)) {
        size_t len = 0;
        const char* p = lua_tolstring(L, 2, &len);
        if (p) post_data.assign(p, len);
    } else if (lua_istable(L, 2)) {
        // Form urlencode table
        std::ostringstream ss;
        bool first = true;
        lua_pushnil(L);
        while (lua_next(L, 2) != 0) {
            if (!first) ss << "&";
            first = false;
            if (lua_isstring(L, -2)) ss << lua_tostring(L, -2) << "=";
            if (lua_isstring(L, -1)) ss << lua_tostring(L, -1);
            lua_pop(L, 1);
        }
        post_data = ss.str();
    }

    long timeout = 30;
    curl_slist* header_list = nullptr;
    if (lua_istable(L, 3)) {
        lua_getfield(L, 3, "timeout");
        if (lua_isnumber(L, -1)) timeout = static_cast<long>(lua_tonumber(L, -1));
        lua_pop(L, 1);

        lua_getfield(L, 3, "headers");
        if (lua_istable(L, -1)) {
            lua_pushnil(L);
            while (lua_next(L, -2) != 0) {
                if (lua_isstring(L, -2) && lua_isstring(L, -1)) {
                    std::string h = std::string(lua_tostring(L, -2)) + ": " + lua_tostring(L, -1);
                    header_list = curl_slist_append(header_list, h.c_str());
                } else if (lua_isstring(L, -1)) {
                    header_list = curl_slist_append(header_list, lua_tostring(L, -1));
                }
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        if (header_list) curl_slist_free_all(header_list);
        lua_pushnil(L);
        lua_pushstring(L, "Failed to initialize CURL");
        return 2;
    }

    HttpResponse resp;
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data.data());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(post_data.size()));
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "minicut-editor/1.0");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_string_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp.body);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, curl_header_write_cb);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &resp.headers);
    if (header_list) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
    }

    CURLcode res = curl_easy_perform(curl);
    if (res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp.status_code);
        resp.success = true;
    } else {
        resp.error = curl_easy_strerror(res);
    }

    curl_easy_cleanup(curl);
    if (header_list) curl_slist_free_all(header_list);

    if (!resp.success) {
        lua_pushnil(L);
        lua_pushstring(L, resp.error.c_str());
        return 2;
    }

    push_http_response_table(L, resp);
    return 1;
}

int ScriptEngine::lua_net_download(lua_State* L) {
    const char* url = luaL_checkstring(L, 1);
    const char* dest_path = luaL_checkstring(L, 2);
    long timeout = 60;
    curl_slist* header_list = nullptr;

    if (lua_istable(L, 3)) {
        lua_getfield(L, 3, "timeout");
        if (lua_isnumber(L, -1)) timeout = static_cast<long>(lua_tonumber(L, -1));
        lua_pop(L, 1);

        lua_getfield(L, 3, "headers");
        if (lua_istable(L, -1)) {
            lua_pushnil(L);
            while (lua_next(L, -2) != 0) {
                if (lua_isstring(L, -2) && lua_isstring(L, -1)) {
                    std::string h = std::string(lua_tostring(L, -2)) + ": " + lua_tostring(L, -1);
                    header_list = curl_slist_append(header_list, h.c_str());
                } else if (lua_isstring(L, -1)) {
                    header_list = curl_slist_append(header_list, lua_tostring(L, -1));
                }
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);
    }

    FILE* fp = std::fopen(dest_path, "wb");
    if (!fp) {
        if (header_list) curl_slist_free_all(header_list);
        lua_pushboolean(L, 0);
        lua_pushstring(L, ("Failed to open local destination file: " + std::string(dest_path)).c_str());
        return 2;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        std::fclose(fp);
        if (header_list) curl_slist_free_all(header_list);
        lua_pushboolean(L, 0);
        lua_pushstring(L, "Failed to initialize CURL");
        return 2;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "minicut-editor/1.0");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_file_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    if (header_list) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
    }

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_easy_cleanup(curl);
    std::fclose(fp);
    if (header_list) curl_slist_free_all(header_list);

    if (res != CURLE_OK) {
        std::remove(dest_path);
        lua_pushboolean(L, 0);
        lua_pushstring(L, curl_easy_strerror(res));
        return 2;
    }

    bool is_sftp = (std::string(url).rfind("sftp://", 0) == 0 || std::string(url).rfind("ftp://", 0) == 0);
    if (!is_sftp && http_code >= 400) {
        std::remove(dest_path);
        lua_pushboolean(L, 0);
        lua_pushstring(L, ("HTTP error " + std::to_string(http_code)).c_str());
        return 2;
    }

    lua_pushboolean(L, 1);
    lua_pushstring(L, dest_path);
    return 2;
}

int ScriptEngine::lua_net_upload(lua_State* L) {
    const char* url = luaL_checkstring(L, 1);
    const char* filepath = luaL_checkstring(L, 2);
    const char* field_name = "file";
    long timeout = 120;
    curl_slist* header_list = nullptr;

    std::vector<std::pair<std::string, std::string>> extra_fields;

    if (lua_istable(L, 3)) {
        lua_getfield(L, 3, "field_name");
        if (lua_isstring(L, -1)) field_name = lua_tostring(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, 3, "timeout");
        if (lua_isnumber(L, -1)) timeout = static_cast<long>(lua_tonumber(L, -1));
        lua_pop(L, 1);

        lua_getfield(L, 3, "extra_fields");
        if (lua_istable(L, -1)) {
            lua_pushnil(L);
            while (lua_next(L, -2) != 0) {
                if (lua_isstring(L, -2) && lua_isstring(L, -1)) {
                    extra_fields.push_back({ lua_tostring(L, -2), lua_tostring(L, -1) });
                }
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);

        lua_getfield(L, 3, "headers");
        if (lua_istable(L, -1)) {
            lua_pushnil(L);
            while (lua_next(L, -2) != 0) {
                if (lua_isstring(L, -2) && lua_isstring(L, -1)) {
                    std::string h = std::string(lua_tostring(L, -2)) + ": " + lua_tostring(L, -1);
                    header_list = curl_slist_append(header_list, h.c_str());
                } else if (lua_isstring(L, -1)) {
                    header_list = curl_slist_append(header_list, lua_tostring(L, -1));
                }
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);
    }

    if (!fs::exists(filepath)) {
        if (header_list) curl_slist_free_all(header_list);
        lua_pushnil(L);
        lua_pushstring(L, ("Upload file does not exist: " + std::string(filepath)).c_str());
        return 2;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        if (header_list) curl_slist_free_all(header_list);
        lua_pushnil(L);
        lua_pushstring(L, "Failed to initialize CURL");
        return 2;
    }

    bool is_sftp = (std::string(url).rfind("sftp://", 0) == 0 || std::string(url).rfind("ftp://", 0) == 0);
    curl_mime* mime = nullptr;
    FILE* up_fp = nullptr;
    if (is_sftp) {
        up_fp = std::fopen(filepath, "rb");
        if (!up_fp) {
            if (header_list) curl_slist_free_all(header_list);
            curl_easy_cleanup(curl);
            lua_pushnil(L);
            lua_pushstring(L, ("Failed to open file for SFTP upload: " + std::string(filepath)).c_str());
            return 2;
        }
        curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
        curl_easy_setopt(curl, CURLOPT_READDATA, up_fp);
        curl_off_t fsize = static_cast<curl_off_t>(fs::file_size(filepath));
        curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, fsize);
    } else {
        mime = curl_mime_init(curl);
        curl_mimepart* part = curl_mime_addpart(mime);
        curl_mime_name(part, field_name);
        curl_mime_filedata(part, filepath);

        for (const auto& kv : extra_fields) {
            curl_mimepart* p = curl_mime_addpart(mime);
            curl_mime_name(p, kv.first.c_str());
            curl_mime_data(p, kv.second.c_str(), CURL_ZERO_TERMINATED);
        }
        curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);
    }

    HttpResponse resp;
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "minicut-editor/1.0");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_string_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp.body);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, curl_header_write_cb);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &resp.headers);
    if (header_list) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
    }

    CURLcode res = curl_easy_perform(curl);
    if (res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp.status_code);
        resp.success = true;
    } else {
        resp.error = curl_easy_strerror(res);
    }

    if (mime) curl_mime_free(mime);
    if (up_fp) std::fclose(up_fp);
    curl_easy_cleanup(curl);
    if (header_list) curl_slist_free_all(header_list);

    if (!resp.success) {
        lua_pushnil(L);
        lua_pushstring(L, resp.error.c_str());
        return 2;
    }

    push_http_response_table(L, resp);
    return 1;
}

int ScriptEngine::lua_net_request(lua_State* L) {
    if (!lua_istable(L, 1)) {
        lua_pushnil(L);
        lua_pushstring(L, "net.request expects options table");
        return 2;
    }

    lua_getfield(L, 1, "url");
    const char* url = lua_tostring(L, -1);
    if (!url) {
        lua_pop(L, 1);
        lua_pushnil(L);
        lua_pushstring(L, "net.request missing url field");
        return 2;
    }
    lua_pop(L, 1);

    std::string method = "GET";
    lua_getfield(L, 1, "method");
    if (lua_isstring(L, -1)) method = lua_tostring(L, -1);
    lua_pop(L, 1);
    std::transform(method.begin(), method.end(), method.begin(), ::toupper);

    std::string body;
    lua_getfield(L, 1, "body");
    if (lua_isstring(L, -1)) {
        size_t len = 0;
        const char* b = lua_tolstring(L, -1, &len);
        if (b) body.assign(b, len);
    }
    lua_pop(L, 1);

    long timeout = 30;
    lua_getfield(L, 1, "timeout");
    if (lua_isnumber(L, -1)) timeout = static_cast<long>(lua_tonumber(L, -1));
    lua_pop(L, 1);

    curl_slist* header_list = nullptr;
    lua_getfield(L, 1, "headers");
    if (lua_istable(L, -1)) {
        lua_pushnil(L);
        while (lua_next(L, -2) != 0) {
            if (lua_isstring(L, -2) && lua_isstring(L, -1)) {
                std::string h = std::string(lua_tostring(L, -2)) + ": " + lua_tostring(L, -1);
                header_list = curl_slist_append(header_list, h.c_str());
            } else if (lua_isstring(L, -1)) {
                header_list = curl_slist_append(header_list, lua_tostring(L, -1));
            }
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);

    CURL* curl = curl_easy_init();
    if (!curl) {
        if (header_list) curl_slist_free_all(header_list);
        lua_pushnil(L);
        lua_pushstring(L, "Failed to initialize CURL");
        return 2;
    }

    HttpResponse resp;
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());
    if (!body.empty()) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.data());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    }
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "minicut-editor/1.0");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_string_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp.body);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, curl_header_write_cb);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &resp.headers);
    if (header_list) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
    }

    CURLcode res = curl_easy_perform(curl);
    if (res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp.status_code);
        resp.success = true;
    } else {
        resp.error = curl_easy_strerror(res);
    }

    curl_easy_cleanup(curl);
    if (header_list) curl_slist_free_all(header_list);

    if (!resp.success) {
        lua_pushnil(L);
        lua_pushstring(L, resp.error.c_str());
        return 2;
    }

    push_http_response_table(L, resp);
    return 1;
}

// ----------------------------------------------------------------------------
// File Dialog & Browser Lua APIs
// ----------------------------------------------------------------------------

struct DialogArgs {
    std::string title = "Select File";
    std::string filter_patterns = "*";
    std::string filter_desc = "All Files (*)";
    std::string default_path = "";
    bool is_save = false;
    int callback_ref = LUA_NOREF;
};

static DialogArgs parse_dialog_args(lua_State* L) {
    DialogArgs args;
    if (lua_istable(L, 1)) {
        lua_getfield(L, 1, "title");
        if (lua_isstring(L, -1)) args.title = lua_tostring(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, 1, "filter");
        if (lua_isstring(L, -1)) {
            args.filter_patterns = lua_tostring(L, -1);
            args.filter_desc = args.filter_patterns;
        }
        lua_pop(L, 1);

        lua_getfield(L, 1, "path");
        if (lua_isstring(L, -1)) args.default_path = lua_tostring(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, 1, "save");
        if (lua_isboolean(L, -1)) args.is_save = lua_toboolean(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, 1, "callback");
        if (lua_isfunction(L, -1)) {
            args.callback_ref = luaL_ref(L, LUA_REGISTRYINDEX);
        } else {
            lua_pop(L, 1);
        }
    } else {
        if (lua_gettop(L) >= 1 && lua_isstring(L, 1)) args.title = lua_tostring(L, 1);
        if (lua_gettop(L) >= 2 && lua_isstring(L, 2)) {
            args.filter_patterns = lua_tostring(L, 2);
            args.filter_desc = args.filter_patterns;
        }
        if (lua_gettop(L) >= 3 && lua_isstring(L, 3)) args.default_path = lua_tostring(L, 3);
        if (lua_gettop(L) >= 4 && lua_isboolean(L, 4)) args.is_save = lua_toboolean(L, 4);
        if (lua_gettop(L) >= 5 && lua_isfunction(L, 5)) {
            lua_pushvalue(L, 5);
            args.callback_ref = luaL_ref(L, LUA_REGISTRYINDEX);
        } else if (lua_gettop(L) >= 2 && lua_isfunction(L, 2)) {
            lua_pushvalue(L, 2);
            args.callback_ref = luaL_ref(L, LUA_REGISTRYINDEX);
        } else if (lua_gettop(L) >= 3 && lua_isfunction(L, 3)) {
            lua_pushvalue(L, 3);
            args.callback_ref = luaL_ref(L, LUA_REGISTRYINDEX);
        }
    }
    return args;
}

static void fire_lua_dialog_callback(lua_State* L, int cb_ref, const std::string& path) {
    if (cb_ref != LUA_NOREF && cb_ref != LUA_REFNIL) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, cb_ref);
        lua_pushstring(L, path.c_str());
        if (lua_pcall(L, 1, 0, 0) != 0) {
            std::cerr << "[ScriptEngine] File dialog callback error: " << lua_tostring(L, -1) << std::endl;
            lua_pop(L, 1);
        }
        luaL_unref(L, LUA_REGISTRYINDEX, cb_ref);
    }
}

int ScriptEngine::lua_gui_native_file_dialog(lua_State* L) {
    DialogArgs args = parse_dialog_args(L);
    if (args.callback_ref != LUA_NOREF) {
        int ref = args.callback_ref;
        if (args.is_save) {
            NativeFileDialog::save_file(args.title, args.default_path, args.filter_desc, args.filter_patterns,
                [L, ref](const std::string& path) {
                    fire_lua_dialog_callback(L, ref, path);
                });
        } else {
            NativeFileDialog::open_file(args.title, args.default_path, args.filter_desc, args.filter_patterns,
                [L, ref](const std::string& path) {
                    fire_lua_dialog_callback(L, ref, path);
                });
        }
        lua_pushboolean(L, true);
        return 1;
    } else {
        std::string res;
        if (args.is_save) {
            res = NativeFileDialog::save_file_sync(args.title, args.default_path, args.filter_desc, args.filter_patterns);
        } else {
            res = NativeFileDialog::open_file_sync(args.title, args.default_path, args.filter_desc, args.filter_patterns);
        }
        lua_pushstring(L, res.c_str());
        return 1;
    }
}

int ScriptEngine::lua_gui_builtin_file_dialog(lua_State* L) {
    DialogArgs args = parse_dialog_args(L);
    if (!s_engine_instance || !s_engine_instance->m_app) {
        if (args.callback_ref != LUA_NOREF) luaL_unref(L, LUA_REGISTRYINDEX, args.callback_ref);
        lua_pushstring(L, "");
        return 1;
    }

    std::vector<FileFilter> filters;
    if (!args.filter_patterns.empty() && args.filter_patterns != "*") {
        FileFilter ff;
        ff.name = args.filter_desc;
        std::stringstream ss(args.filter_patterns);
        std::string item;
        while (std::getline(ss, item, ';')) {
            if (!item.empty() && item[0] == '*') item = item.substr(1);
            if (!item.empty()) ff.exts.push_back(item);
        }
        filters.push_back(ff);
    }

    int ref = args.callback_ref;
    s_engine_instance->m_app->get_file_browser().open(
        args.title,
        args.is_save ? FileBrowserMode::SaveFile : FileBrowserMode::OpenFile,
        filters,
        args.default_path,
        "",
        [L, ref](const std::string& chosen) {
            if (ref != LUA_NOREF) {
                fire_lua_dialog_callback(L, ref, chosen);
            }
        }
    );

    lua_pushboolean(L, true);
    return 1;
}

int ScriptEngine::lua_gui_file_dialog(lua_State* L) {
    bool use_sys = true;
    if (s_engine_instance && s_engine_instance->m_app) {
        use_sys = s_engine_instance->m_app->get_use_system_file_dialog();
    }
    if (use_sys && NativeFileDialog::is_available()) {
        return lua_gui_native_file_dialog(L);
    } else {
        return lua_gui_builtin_file_dialog(L);
    }
}

int ScriptEngine::lua_gui_popout_preview(lua_State* L) {
    if (s_engine_instance && s_engine_instance->m_app) {
        bool pop = lua_toboolean(L, 1);
        s_engine_instance->m_app->set_preview_popped_out(pop);
    }
    return 0;
}

int ScriptEngine::lua_gui_is_preview_popped_out(lua_State* L) {
    if (s_engine_instance && s_engine_instance->m_app) {
        lua_pushboolean(L, s_engine_instance->m_app->is_preview_popped_out());
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

int ScriptEngine::lua_gui_set_preview_aspect_mode(lua_State* L) {
    if (s_engine_instance && s_engine_instance->m_app) {
        int idx = static_cast<int>(luaL_checkinteger(L, 1));
        s_engine_instance->m_app->set_preview_aspect_idx(idx);
    }
    return 0;
}

int ScriptEngine::lua_gui_get_preview_aspect_mode(lua_State* L) {
    if (s_engine_instance && s_engine_instance->m_app) {
        lua_pushinteger(L, s_engine_instance->m_app->get_preview_aspect_idx());
    } else {
        lua_pushinteger(L, 0);
    }
    return 1;
}

int ScriptEngine::lua_gui_set_transform_tool(lua_State* L) {
    if (s_engine_instance && s_engine_instance->m_app) {
        const char* tool_str = luaL_checkstring(L, 1);
        if (tool_str) {
            std::string s = tool_str;
            std::transform(s.begin(), s.end(), s.begin(), ::tolower);
            if (s == "none" || s == "off" || s == "false" || s == "0") {
                s_engine_instance->m_app->set_preview_transform_enabled(false);
            } else {
                s_engine_instance->m_app->set_preview_transform_enabled(true);
            }
        }
    }
    return 0;
}

int ScriptEngine::lua_gui_get_transform_tool(lua_State* L) {
    if (s_engine_instance && s_engine_instance->m_app) {
        lua_pushstring(L, s_engine_instance->m_app->is_preview_transform_enabled() ? "transform" : "none");
    } else {
        lua_pushstring(L, "transform");
    }
    return 1;
}

// ----------------------------------------------------------------------------
// Timeline Multi-Selection & Track Management Lua APIs
// ----------------------------------------------------------------------------

int ScriptEngine::lua_timeline_get_selected_clips(lua_State* L) {
    if (!s_engine_instance || !s_engine_instance->m_timeline) {
        lua_newtable(L); return 1;
    }
    const auto& ids = s_engine_instance->m_timeline->get_selected_clip_ids();
    lua_newtable(L);
    for (size_t i = 0; i < ids.size(); ++i) {
        lua_pushstring(L, ids[i].c_str());
        lua_rawseti(L, -2, static_cast<int>(i + 1));
    }
    return 1;
}

int ScriptEngine::lua_timeline_select_clips(lua_State* L) {
    if (!s_engine_instance || !s_engine_instance->m_timeline) return 0;
    if (lua_istable(L, 1)) {
        s_engine_instance->m_timeline->clear_selection();
        int len = (int)lua_objlen(L, 1);
        for (int i = 1; i <= len; ++i) {
            lua_rawgeti(L, 1, i);
            if (lua_isstring(L, -1)) {
                s_engine_instance->m_timeline->select_clip(lua_tostring(L, -1), false, true);
            }
            lua_pop(L, 1);
        }
    } else if (lua_isstring(L, 1)) {
        std::string id = lua_tostring(L, 1);
        bool toggle = lua_toboolean(L, 2);
        bool add = lua_toboolean(L, 3);
        s_engine_instance->m_timeline->select_clip(id, toggle, add);
    }
    return 0;
}

int ScriptEngine::lua_timeline_clear_selection(lua_State* L) {
    if (s_engine_instance && s_engine_instance->m_timeline) {
        s_engine_instance->m_timeline->clear_selection();
    }
    return 0;
}

int ScriptEngine::lua_timeline_select_all(lua_State* L) {
    if (s_engine_instance && s_engine_instance->m_timeline && s_engine_instance->m_project) {
        s_engine_instance->m_timeline->select_all(*s_engine_instance->m_project);
    }
    return 0;
}

int ScriptEngine::lua_timeline_move_track_up(lua_State* L) {
    if (!s_engine_instance || !s_engine_instance->m_timeline || !s_engine_instance->m_project) {
        lua_pushboolean(L, false); return 1;
    }
    int idx = (int)luaL_checkinteger(L, 1);
    s_engine_instance->push_undo_state();
    s_engine_instance->m_timeline->move_track_up(*s_engine_instance->m_project, idx);
    s_engine_instance->notify_project_modified();
    lua_pushboolean(L, true);
    return 1;
}

int ScriptEngine::lua_timeline_move_track_down(lua_State* L) {
    if (!s_engine_instance || !s_engine_instance->m_timeline || !s_engine_instance->m_project) {
        lua_pushboolean(L, false); return 1;
    }
    int idx = (int)luaL_checkinteger(L, 1);
    s_engine_instance->push_undo_state();
    s_engine_instance->m_timeline->move_track_down(*s_engine_instance->m_project, idx);
    s_engine_instance->notify_project_modified();
    lua_pushboolean(L, true);
    return 1;
}

int ScriptEngine::lua_timeline_move_track(lua_State* L) {
    if (!s_engine_instance || !s_engine_instance->m_timeline || !s_engine_instance->m_project) {
        lua_pushboolean(L, false); return 1;
    }
    int from_idx = (int)luaL_checkinteger(L, 1);
    int to_idx = (int)luaL_checkinteger(L, 2);
    s_engine_instance->push_undo_state();
    s_engine_instance->m_timeline->reorder_track(*s_engine_instance->m_project, from_idx, to_idx);
    s_engine_instance->notify_project_modified();
    lua_pushboolean(L, true);
    return 1;
}

int ScriptEngine::lua_timeline_swap_tracks(lua_State* L) {
    if (!s_engine_instance || !s_engine_instance->m_timeline || !s_engine_instance->m_project) {
        lua_pushboolean(L, false); return 1;
    }
    int idx1 = (int)luaL_checkinteger(L, 1);
    int idx2 = (int)luaL_checkinteger(L, 2);
    s_engine_instance->push_undo_state();
    s_engine_instance->m_timeline->swap_tracks(*s_engine_instance->m_project, idx1, idx2);
    s_engine_instance->notify_project_modified();
    lua_pushboolean(L, true);
    return 1;
}

int ScriptEngine::lua_timeline_set_track_name(lua_State* L) {
    if (!s_engine_instance || !s_engine_instance->m_timeline || !s_engine_instance->m_project) {
        lua_pushboolean(L, false); return 1;
    }
    int idx = (int)luaL_checkinteger(L, 1);
    const char* name = luaL_checkstring(L, 2);
    s_engine_instance->push_undo_state();
    s_engine_instance->m_timeline->set_track_name(*s_engine_instance->m_project, idx, name ? name : "");
    s_engine_instance->notify_project_modified();
    lua_pushboolean(L, true);
    return 1;
}

int ScriptEngine::lua_timeline_get_track_name(lua_State* L) {
    if (!s_engine_instance || !s_engine_instance->m_timeline || !s_engine_instance->m_project) {
        lua_pushstring(L, ""); return 1;
    }
    int idx = (int)luaL_checkinteger(L, 1);
    std::string name = s_engine_instance->m_timeline->get_track_name(*s_engine_instance->m_project, idx);
    lua_pushstring(L, name.c_str());
    return 1;
}

// ----------------------------------------------------------------------------
// Text Asset Creation & Timeline Placement Lua APIs
// ----------------------------------------------------------------------------

int ScriptEngine::lua_assets_create_text(lua_State* L) {
    if (!s_engine_instance || !s_engine_instance->m_project) {
        lua_pushstring(L, ""); return 1;
    }
    const char* text_str = luaL_checkstring(L, 1);
    TextConfig cfg;
    cfg.text = text_str ? text_str : "";

    std::string asset_name = "";
    if (lua_gettop(L) >= 2 && lua_istable(L, 2)) {
        lua_getfield(L, 2, "font");
        if (lua_isstring(L, -1)) cfg.font_path = lua_tostring(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, 2, "size");
        if (lua_isnumber(L, -1)) cfg.font_size = (float)lua_tonumber(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, 2, "color");
        if (lua_istable(L, -1)) {
            for (int i = 0; i < 4; ++i) {
                lua_rawgeti(L, -1, i + 1);
                if (lua_isnumber(L, -1)) cfg.font_color[i] = (float)lua_tonumber(L, -1);
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);

        lua_getfield(L, 2, "bg");
        if (lua_isboolean(L, -1)) cfg.use_bg = lua_toboolean(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, 2, "bg_color");
        if (lua_istable(L, -1)) {
            for (int i = 0; i < 4; ++i) {
                lua_rawgeti(L, -1, i + 1);
                if (lua_isnumber(L, -1)) cfg.bg_color[i] = (float)lua_tonumber(L, -1);
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);

        lua_getfield(L, 2, "outline");
        if (lua_isboolean(L, -1)) cfg.use_outline = lua_toboolean(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, 2, "outline_color");
        if (lua_istable(L, -1)) {
            for (int i = 0; i < 4; ++i) {
                lua_rawgeti(L, -1, i + 1);
                if (lua_isnumber(L, -1)) cfg.outline_color[i] = (float)lua_tonumber(L, -1);
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);

        lua_getfield(L, 2, "outline_width");
        if (lua_isnumber(L, -1)) cfg.outline_thickness = (float)lua_tonumber(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, 2, "letter_spacing");
        if (lua_isnumber(L, -1)) cfg.letter_spacing = (float)lua_tonumber(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, 2, "line_spacing");
        if (lua_isnumber(L, -1)) cfg.line_spacing = (float)lua_tonumber(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, 2, "justification");
        if (lua_isnumber(L, -1)) cfg.justification = (int)lua_tointeger(L, -1);
        else if (lua_isstring(L, -1)) {
            std::string j = lua_tostring(L, -1);
            if (j == "center") cfg.justification = 1;
            else if (j == "right") cfg.justification = 2;
            else cfg.justification = 0;
        }
        lua_pop(L, 1);

        lua_getfield(L, 2, "name");
        if (lua_isstring(L, -1)) asset_name = lua_tostring(L, -1);
        lua_pop(L, 1);
    }

    std::string asset_id = TextGenerator::create_text_asset(*s_engine_instance->m_project, cfg, asset_name);
    if (!asset_id.empty()) {
        s_engine_instance->notify_project_modified();
    }
    lua_pushstring(L, asset_id.c_str());
    return 1;
}

int ScriptEngine::lua_assets_get_scale(lua_State* L) {
    if (!s_engine_instance || !s_engine_instance->m_project) {
        lua_pushnumber(L, 1.0);
        lua_pushnumber(L, 1.0);
        return 2;
    }
    const char* asset_id = luaL_checkstring(L, 1);
    const VideoAsset* va = s_engine_instance->m_project->find_video_asset(asset_id);
    if (!va) {
        lua_pushnumber(L, 1.0);
        lua_pushnumber(L, 1.0);
        return 2;
    }
    lua_pushnumber(L, va->scale_x);
    lua_pushnumber(L, va->scale_y);
    return 2;
}

int ScriptEngine::lua_assets_get_scale_x(lua_State* L) {
    if (!s_engine_instance || !s_engine_instance->m_project) {
        lua_pushnumber(L, 1.0);
        return 1;
    }
    const char* asset_id = luaL_checkstring(L, 1);
    const VideoAsset* va = s_engine_instance->m_project->find_video_asset(asset_id);
    lua_pushnumber(L, va ? va->scale_x : 1.0);
    return 1;
}

int ScriptEngine::lua_assets_get_scale_y(lua_State* L) {
    if (!s_engine_instance || !s_engine_instance->m_project) {
        lua_pushnumber(L, 1.0);
        return 1;
    }
    const char* asset_id = luaL_checkstring(L, 1);
    const VideoAsset* va = s_engine_instance->m_project->find_video_asset(asset_id);
    lua_pushnumber(L, va ? va->scale_y : 1.0);
    return 1;
}

int ScriptEngine::lua_assets_set_scale(lua_State* L) {
    if (!s_engine_instance || !s_engine_instance->m_project) return 0;
    const char* asset_id = luaL_checkstring(L, 1);
    double sx = luaL_checknumber(L, 2);
    double sy = lua_isnumber(L, 3) ? lua_tonumber(L, 3) : sx;

    VideoAsset* va = s_engine_instance->m_project->find_video_asset_mut(asset_id);
    if (va) {
        s_engine_instance->push_undo_state();
        va->scale_x = std::max(0.01, sx);
        va->scale_y = std::max(0.01, sy);
        va->scale = va->scale_x;
        s_engine_instance->notify_project_modified();
    }
    return 0;
}

int ScriptEngine::lua_assets_set_scale_x(lua_State* L) {
    if (!s_engine_instance || !s_engine_instance->m_project) return 0;
    const char* asset_id = luaL_checkstring(L, 1);
    double sx = luaL_checknumber(L, 2);

    VideoAsset* va = s_engine_instance->m_project->find_video_asset_mut(asset_id);
    if (va) {
        s_engine_instance->push_undo_state();
        va->scale_x = std::max(0.01, sx);
        va->scale = va->scale_x;
        s_engine_instance->notify_project_modified();
    }
    return 0;
}

int ScriptEngine::lua_assets_set_scale_y(lua_State* L) {
    if (!s_engine_instance || !s_engine_instance->m_project) return 0;
    const char* asset_id = luaL_checkstring(L, 1);
    double sy = luaL_checknumber(L, 2);

    VideoAsset* va = s_engine_instance->m_project->find_video_asset_mut(asset_id);
    if (va) {
        s_engine_instance->push_undo_state();
        va->scale_y = std::max(0.01, sy);
        s_engine_instance->notify_project_modified();
    }
    return 0;
}

int ScriptEngine::lua_timeline_place_text_clip(lua_State* L) {
    if (!s_engine_instance || !s_engine_instance->m_project) {
        lua_pushstring(L, ""); return 1;
    }
    const char* text_or_asset_id = luaL_checkstring(L, 1);
    int track_idx = (int)luaL_optinteger(L, 2, 0);
    double start_time = luaL_optnumber(L, 3, s_engine_instance->get_playhead_time());
    double duration = luaL_optnumber(L, 4, 5.0);

    Project& proj = *s_engine_instance->m_project;
    std::string asset_id;

    if (proj.find_video_asset(text_or_asset_id) != nullptr) {
        asset_id = text_or_asset_id;
    } else {
        TextConfig cfg;
        cfg.text = text_or_asset_id;
        if (lua_gettop(L) >= 5 && lua_istable(L, 5)) {
            lua_getfield(L, 5, "font");
            if (lua_isstring(L, -1)) cfg.font_path = lua_tostring(L, -1);
            lua_pop(L, 1);

            lua_getfield(L, 5, "size");
            if (lua_isnumber(L, -1)) cfg.font_size = (float)lua_tonumber(L, -1);
            lua_pop(L, 1);

            lua_getfield(L, 5, "justification");
            if (lua_isnumber(L, -1)) cfg.justification = (int)lua_tointeger(L, -1);
            lua_pop(L, 1);
        }
        asset_id = TextGenerator::create_text_asset(proj, cfg);
    }

    if (asset_id.empty()) {
        lua_pushstring(L, ""); return 1;
    }

    if (track_idx < 0 || track_idx >= static_cast<int>(proj.tracks.size()) || proj.tracks[track_idx].type != "video") {
        track_idx = -1;
        for (size_t t = 0; t < proj.tracks.size(); ++t) {
            if (proj.tracks[t].type == "video") {
                track_idx = static_cast<int>(t);
                break;
            }
        }
        if (track_idx == -1) {
            Track trk;
            trk.id = "track_v" + std::to_string(proj.tracks.size() + 1);
            trk.name = "Video " + std::to_string(proj.tracks.size() + 1);
            trk.type = "video";
            proj.tracks.push_back(trk);
            track_idx = static_cast<int>(proj.tracks.size() - 1);
        }
    }

    s_engine_instance->push_undo_state();
    std::string ts = std::to_string(std::chrono::system_clock::now().time_since_epoch().count() % 1000000);
    Clip clp;
    clp.id = "clip_txt_" + ts;
    clp.asset_id = asset_id;
    clp.start_time = start_time;
    clp.duration = duration;
    clp.offset = 0.0;
    proj.tracks[track_idx].clips.push_back(clp);

    s_engine_instance->set_selected_clip_id(clp.id);
    s_engine_instance->notify_project_modified();

    lua_pushstring(L, clp.id.c_str());
    return 1;
}

// ============================================================================
// JavaScript (Duktape) Scripting Engine Implementation
// ============================================================================

static bool read_file_to_string(const std::string& path, std::string& out_content) {
    std::ifstream file(path);
    if (!file.is_open()) return false;
    std::stringstream buffer;
    buffer << file.rdbuf();
    out_content = buffer.str();
    return true;
}

bool ScriptEngine::run_js_file(const std::string& file_path) {
    if (!m_ctx) return false;
    std::string code;
    if (!read_file_to_string(file_path, code)) {
        std::cerr << "[ScriptEngine] Failed to open JS file: " << file_path << std::endl;
        set_status("Failed to open JS file: " + file_path);
        return false;
    }
    if (duk_peval_string(m_ctx, code.c_str()) != 0) {
        const char* err = duk_safe_to_string(m_ctx, -1);
        std::cerr << "[ScriptEngine] Error running JS file " << file_path << ": " << (err ? err : "unknown") << std::endl;
        set_status(std::string("JS error: ") + (err ? err : "unknown"));
        duk_pop(m_ctx);
        return false;
    }
    duk_pop(m_ctx);
    return true;
}

bool ScriptEngine::run_js_string(const std::string& js_code) {
    if (!m_ctx) return false;
    if (duk_peval_string(m_ctx, js_code.c_str()) != 0) {
        const char* err = duk_safe_to_string(m_ctx, -1);
        std::cerr << "[ScriptEngine] Error running JS string: " << (err ? err : "unknown") << std::endl;
        set_status(std::string("JS error: ") + (err ? err : "unknown"));
        duk_pop(m_ctx);
        return false;
    }
    duk_pop(m_ctx);
    return true;
}

bool ScriptEngine::execute_js_script(ScriptItem& item) {
    if (!m_ctx) return false;

    // Create plugin object:
    // plugin = { id: item.id, name: item.filename, title: item.title, path: item.path }
    duk_push_object(m_ctx);
    duk_push_string(m_ctx, item.id.c_str()); duk_put_prop_string(m_ctx, -2, "id");
    duk_push_string(m_ctx, item.filename.c_str()); duk_put_prop_string(m_ctx, -2, "name");
    duk_push_string(m_ctx, item.title.c_str()); duk_put_prop_string(m_ctx, -2, "title");
    duk_push_string(m_ctx, item.path.c_str()); duk_put_prop_string(m_ctx, -2, "path");

    // plugins[item.id] = plugin
    duk_get_global_string(m_ctx, "plugins");
    if (!duk_is_object(m_ctx, -1)) {
        duk_pop(m_ctx);
        duk_push_object(m_ctx);
        duk_dup(m_ctx, -1);
        duk_put_global_string(m_ctx, "plugins");
    }
    duk_dup(m_ctx, -2);
    duk_put_prop_string(m_ctx, -2, item.id.c_str());
    duk_pop(m_ctx); // pop plugins

    duk_put_global_string(m_ctx, "plugin"); // sets global plugin

    std::string code;
    if (!read_file_to_string(item.path, code)) {
        std::cerr << "[ScriptEngine] Failed to open JS script: " << item.path << std::endl;
        set_status("Failed to open JS script: " + item.path);
        return false;
    }
    if (duk_peval_string(m_ctx, code.c_str()) != 0) {
        const char* err = duk_safe_to_string(m_ctx, -1);
        std::cerr << "[ScriptEngine] Error executing JS " << item.path << ": " << (err ? err : "unknown") << std::endl;
        set_status(std::string("JS error: ") + (err ? err : "unknown"));
        duk_pop(m_ctx);
        return false;
    }
    duk_pop(m_ctx); // pop eval result

    item.is_loaded = true;
    return true;
}

void ScriptEngine::call_js_panel_render(ScriptItem& item) {
    if (!m_ctx) return;
    m_current_render_script_id = item.id;

    // 1. Look for plugins[item.id]
    duk_get_global_string(m_ctx, "plugins");
    if (duk_is_object(m_ctx, -1)) {
        duk_get_prop_string(m_ctx, -1, item.id.c_str());
        if (duk_is_object(m_ctx, -1)) {
            // Set global plugin
            duk_dup(m_ctx, -1);
            duk_put_global_string(m_ctx, "plugin");

            duk_get_prop_string(m_ctx, -1, "init_panel");
            if (duk_is_function(m_ctx, -1)) {
                duk_dup(m_ctx, -2); // this = plugin
                if (duk_pcall_method(m_ctx, 0) != 0) {
                    const char* err = duk_safe_to_string(m_ctx, -1);
                    ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "JS Error in plugin.init_panel:\n%s", err ? err : "unknown");
                }
                duk_pop_n(m_ctx, 3); // pop return value, plugin, plugins
                return;
            }
            duk_pop(m_ctx); // pop init_panel non-function
        }
        duk_pop(m_ctx); // pop plugin object or nil
    }
    duk_pop(m_ctx); // pop plugins

    // 2. Global init_panel
    duk_get_global_string(m_ctx, "init_panel");
    if (duk_is_function(m_ctx, -1)) {
        if (duk_pcall(m_ctx, 0) != 0) {
            const char* err = duk_safe_to_string(m_ctx, -1);
            ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "JS Error in init_panel:\n%s", err ? err : "unknown");
        }
        duk_pop(m_ctx);
        return;
    }
    duk_pop(m_ctx);

    // 3. Global render_panel
    duk_get_global_string(m_ctx, "render_panel");
    if (duk_is_function(m_ctx, -1)) {
        if (duk_pcall(m_ctx, 0) != 0) {
            const char* err = duk_safe_to_string(m_ctx, -1);
            ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "JS Error in render_panel:\n%s", err ? err : "unknown");
        }
        duk_pop(m_ctx);
        return;
    }
    duk_pop(m_ctx);

    ImGui::TextDisabled("No plugin.init_panel() defined in script.");
}

static void push_js_http_response(duk_context* ctx, const HttpResponse& resp) {
    duk_push_object(ctx);
    duk_push_int(ctx, static_cast<int>(resp.status_code)); duk_put_prop_string(ctx, -2, "status");
    duk_push_int(ctx, static_cast<int>(resp.status_code)); duk_put_prop_string(ctx, -2, "status_code");
    duk_push_string(ctx, resp.body.c_str());               duk_put_prop_string(ctx, -2, "body");
    duk_push_boolean(ctx, resp.success);                  duk_put_prop_string(ctx, -2, "ok");
    duk_push_boolean(ctx, resp.success);                  duk_put_prop_string(ctx, -2, "success");
    if (!resp.error.empty()) {
        duk_push_string(ctx, resp.error.c_str());         duk_put_prop_string(ctx, -2, "error");
    }
}

// ----------------------------------------------------------------------------
// JS GUI Dispatchers
// ----------------------------------------------------------------------------

duk_ret_t ScriptEngine::js_gui_text(duk_context* ctx) {
    const char* str = duk_get_string(ctx, 0);
    ImGui::Text("%s", str ? str : "");
    return 0;
}

duk_ret_t ScriptEngine::js_gui_text_colored(duk_context* ctx) {
    float r = (float)duk_get_number(ctx, 0);
    float g = (float)duk_get_number(ctx, 1);
    float b = (float)duk_get_number(ctx, 2);
    float a = duk_is_number(ctx, 3) ? (float)duk_get_number(ctx, 3) : 1.0f;
    const char* str = duk_get_string(ctx, 4);
    ImGui::TextColored(ImVec4(r, g, b, a), "%s", str ? str : "");
    return 0;
}

duk_ret_t ScriptEngine::js_gui_text_disabled(duk_context* ctx) {
    const char* str = duk_get_string(ctx, 0);
    ImGui::TextDisabled("%s", str ? str : "");
    return 0;
}

duk_ret_t ScriptEngine::js_gui_button(duk_context* ctx) {
    const char* label = duk_get_string(ctx, 0);
    bool clicked = ImGui::Button(label ? label : "Button");
    if (clicked && duk_get_top(ctx) >= 2 && duk_is_function(ctx, 1)) {
        duk_dup(ctx, 1);
        if (duk_pcall(ctx, 0) != 0) {
            std::cerr << "[JS Callback Error] " << duk_safe_to_string(ctx, -1) << std::endl;
        }
        duk_pop(ctx);
    }
    duk_push_boolean(ctx, clicked);
    return 1;
}

duk_ret_t ScriptEngine::js_gui_small_button(duk_context* ctx) {
    const char* label = duk_get_string(ctx, 0);
    bool clicked = ImGui::SmallButton(label ? label : "Button");
    if (clicked && duk_get_top(ctx) >= 2 && duk_is_function(ctx, 1)) {
        duk_dup(ctx, 1);
        if (duk_pcall(ctx, 0) != 0) {
            std::cerr << "[JS Callback Error] " << duk_safe_to_string(ctx, -1) << std::endl;
        }
        duk_pop(ctx);
    }
    duk_push_boolean(ctx, clicked);
    return 1;
}

duk_ret_t ScriptEngine::js_gui_slider_float(duk_context* ctx) {
    const char* label = duk_get_string(ctx, 0);
    float val = (float)duk_get_number(ctx, 1);
    float v_min = (float)duk_get_number(ctx, 2);
    float v_max = (float)duk_get_number(ctx, 3);
    bool changed = ImGui::SliderFloat(label ? label : "##slider", &val, v_min, v_max);
    if (changed && duk_get_top(ctx) >= 5 && duk_is_function(ctx, 4)) {
        duk_dup(ctx, 4);
        duk_push_number(ctx, val);
        if (duk_pcall(ctx, 1) != 0) duk_pop(ctx);
        else duk_pop(ctx);
    }
    duk_push_number(ctx, val);
    return 1;
}

duk_ret_t ScriptEngine::js_gui_slider_int(duk_context* ctx) {
    const char* label = duk_get_string(ctx, 0);
    int val = (int)duk_get_number(ctx, 1);
    int v_min = (int)duk_get_number(ctx, 2);
    int v_max = (int)duk_get_number(ctx, 3);
    bool changed = ImGui::SliderInt(label ? label : "##slider", &val, v_min, v_max);
    if (changed && duk_get_top(ctx) >= 5 && duk_is_function(ctx, 4)) {
        duk_dup(ctx, 4);
        duk_push_int(ctx, val);
        if (duk_pcall(ctx, 1) != 0) duk_pop(ctx);
        else duk_pop(ctx);
    }
    duk_push_int(ctx, val);
    return 1;
}

duk_ret_t ScriptEngine::js_gui_drag_float(duk_context* ctx) {
    const char* label = duk_get_string(ctx, 0);
    float val = (float)duk_get_number(ctx, 1);
    float speed = duk_is_number(ctx, 2) ? (float)duk_get_number(ctx, 2) : 1.0f;
    float v_min = duk_is_number(ctx, 3) ? (float)duk_get_number(ctx, 3) : 0.0f;
    float v_max = duk_is_number(ctx, 4) ? (float)duk_get_number(ctx, 4) : 0.0f;
    bool changed = ImGui::DragFloat(label ? label : "##drag", &val, speed, v_min, v_max);
    if (changed && duk_get_top(ctx) >= 6 && duk_is_function(ctx, 5)) {
        duk_dup(ctx, 5);
        duk_push_number(ctx, val);
        if (duk_pcall(ctx, 1) != 0) duk_pop(ctx);
        else duk_pop(ctx);
    }
    duk_push_number(ctx, val);
    return 1;
}

duk_ret_t ScriptEngine::js_gui_drag_int(duk_context* ctx) {
    const char* label = duk_get_string(ctx, 0);
    int val = (int)duk_get_number(ctx, 1);
    float speed = duk_is_number(ctx, 2) ? (float)duk_get_number(ctx, 2) : 1.0f;
    int v_min = duk_is_number(ctx, 3) ? (int)duk_get_number(ctx, 3) : 0;
    int v_max = duk_is_number(ctx, 4) ? (int)duk_get_number(ctx, 4) : 0;
    bool changed = ImGui::DragInt(label ? label : "##drag", &val, speed, v_min, v_max);
    if (changed && duk_get_top(ctx) >= 6 && duk_is_function(ctx, 5)) {
        duk_dup(ctx, 5);
        duk_push_int(ctx, val);
        if (duk_pcall(ctx, 1) != 0) duk_pop(ctx);
        else duk_pop(ctx);
    }
    duk_push_int(ctx, val);
    return 1;
}

duk_ret_t ScriptEngine::js_gui_input_text(duk_context* ctx) {
    const char* label = duk_get_string(ctx, 0);
    const char* val = duk_get_string(ctx, 1);
    char buf[512] = {0};
    if (val) strncpy(buf, val, sizeof(buf) - 1);
    bool changed = ImGui::InputText(label ? label : "##input", buf, sizeof(buf));
    if (changed && duk_get_top(ctx) >= 3 && duk_is_function(ctx, 2)) {
        duk_dup(ctx, 2);
        duk_push_string(ctx, buf);
        if (duk_pcall(ctx, 1) != 0) duk_pop(ctx);
        else duk_pop(ctx);
    }
    duk_push_string(ctx, buf);
    return 1;
}

duk_ret_t ScriptEngine::js_gui_checkbox(duk_context* ctx) {
    const char* label = duk_get_string(ctx, 0);
    bool val = duk_get_boolean(ctx, 1);
    bool changed = ImGui::Checkbox(label ? label : "##chk", &val);
    if (changed && duk_get_top(ctx) >= 3 && duk_is_function(ctx, 2)) {
        duk_dup(ctx, 2);
        duk_push_boolean(ctx, val);
        if (duk_pcall(ctx, 1) != 0) duk_pop(ctx);
        else duk_pop(ctx);
    }
    duk_push_boolean(ctx, val);
    return 1;
}

duk_ret_t ScriptEngine::js_gui_same_line(duk_context* ctx) {
    (void)ctx;
    ImGui::SameLine();
    return 0;
}

duk_ret_t ScriptEngine::js_gui_separator(duk_context* ctx) {
    (void)ctx;
    ImGui::Separator();
    return 0;
}

duk_ret_t ScriptEngine::js_gui_spacing(duk_context* ctx) {
    (void)ctx;
    ImGui::Spacing();
    return 0;
}

duk_ret_t ScriptEngine::js_gui_tooltip(duk_context* ctx) {
    const char* str = duk_get_string(ctx, 0);
    if (str && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", str);
    return 0;
}

duk_ret_t ScriptEngine::js_gui_set_next_item_width(duk_context* ctx) {
    float w = (float)duk_get_number(ctx, 0);
    ImGui::SetNextItemWidth(w);
    return 0;
}

duk_ret_t ScriptEngine::js_gui_file_dialog(duk_context* ctx) {
    std::string mode = "open";
    std::string title = "Select File";
    std::string default_path = "";
    std::string filter_name = "All Files (*.*)";
    std::string filter_pat = "*.*";
    if (duk_is_object(ctx, 0)) {
        duk_get_prop_string(ctx, 0, "mode"); if (duk_is_string(ctx, -1)) mode = duk_get_string(ctx, -1); duk_pop(ctx);
        duk_get_prop_string(ctx, 0, "title"); if (duk_is_string(ctx, -1)) title = duk_get_string(ctx, -1); duk_pop(ctx);
        duk_get_prop_string(ctx, 0, "default_path"); if (duk_is_string(ctx, -1)) default_path = duk_get_string(ctx, -1); duk_pop(ctx);
        duk_get_prop_string(ctx, 0, "filter_name"); if (duk_is_string(ctx, -1)) filter_name = duk_get_string(ctx, -1); duk_pop(ctx);
        duk_get_prop_string(ctx, 0, "filter_pattern"); if (duk_is_string(ctx, -1)) filter_pat = duk_get_string(ctx, -1); duk_pop(ctx);
    } else if (duk_is_string(ctx, 0)) {
        mode = duk_get_string(ctx, 0);
        if (duk_is_string(ctx, 1)) title = duk_get_string(ctx, 1);
        if (duk_is_string(ctx, 2)) default_path = duk_get_string(ctx, 2);
    }

    if (mode == "save") {
        NativeFileDialog::save_file(title, default_path, filter_name, filter_pat, [](const std::string& path) {
            if (s_engine_instance) s_engine_instance->set_status("Selected file: " + path);
        });
    } else {
        NativeFileDialog::open_file(title, default_path, filter_name, filter_pat, [](const std::string& path) {
            if (s_engine_instance) s_engine_instance->set_status("Selected file: " + path);
        });
    }
    return 0;
}

duk_ret_t ScriptEngine::js_gui_native_file_dialog(duk_context* ctx) {
    return js_gui_file_dialog(ctx);
}

duk_ret_t ScriptEngine::js_gui_builtin_file_dialog(duk_context* ctx) {
    return js_gui_file_dialog(ctx);
}

duk_ret_t ScriptEngine::js_gui_popout_preview(duk_context* ctx) {
    if (s_engine_instance && s_engine_instance->m_app) {
        bool pop = duk_get_boolean(ctx, 0);
        s_engine_instance->m_app->set_preview_popped_out(pop);
    }
    return 0;
}

duk_ret_t ScriptEngine::js_gui_is_preview_popped_out(duk_context* ctx) {
    if (s_engine_instance && s_engine_instance->m_app) {
        duk_push_boolean(ctx, s_engine_instance->m_app->is_preview_popped_out());
    } else {
        duk_push_boolean(ctx, false);
    }
    return 1;
}

duk_ret_t ScriptEngine::js_gui_set_preview_aspect_mode(duk_context* ctx) {
    if (s_engine_instance && s_engine_instance->m_app) {
        int idx = duk_get_int(ctx, 0);
        s_engine_instance->m_app->set_preview_aspect_idx(idx);
    }
    return 0;
}

duk_ret_t ScriptEngine::js_gui_get_preview_aspect_mode(duk_context* ctx) {
    if (s_engine_instance && s_engine_instance->m_app) {
        duk_push_int(ctx, s_engine_instance->m_app->get_preview_aspect_idx());
    } else {
        duk_push_int(ctx, 0);
    }
    return 1;
}

duk_ret_t ScriptEngine::js_gui_set_transform_tool(duk_context* ctx) {
    if (s_engine_instance && s_engine_instance->m_app) {
        const char* tool_str = duk_get_string(ctx, 0);
        if (tool_str) {
            std::string s = tool_str;
            std::transform(s.begin(), s.end(), s.begin(), ::tolower);
            if (s == "none" || s == "off" || s == "false" || s == "0") {
                s_engine_instance->m_app->set_preview_transform_enabled(false);
            } else {
                s_engine_instance->m_app->set_preview_transform_enabled(true);
            }
        }
    }
    return 0;
}

duk_ret_t ScriptEngine::js_gui_get_transform_tool(duk_context* ctx) {
    if (s_engine_instance && s_engine_instance->m_app) {
        duk_push_string(ctx, s_engine_instance->m_app->is_preview_transform_enabled() ? "transform" : "none");
    } else {
        duk_push_string(ctx, "transform");
    }
    return 1;
}

// ----------------------------------------------------------------------------
// JS Timeline Dispatchers
// ----------------------------------------------------------------------------

duk_ret_t ScriptEngine::js_timeline_get_playhead(duk_context* ctx) {
    duk_push_number(ctx, s_engine_instance ? s_engine_instance->get_playhead_time() : 0.0);
    return 1;
}

duk_ret_t ScriptEngine::js_timeline_set_playhead(duk_context* ctx) {
    double t = duk_get_number(ctx, 0);
    if (s_engine_instance) s_engine_instance->set_playhead_time(t);
    return 0;
}

duk_ret_t ScriptEngine::js_timeline_get_duration(duk_context* ctx) {
    duk_push_number(ctx, (s_engine_instance && s_engine_instance->m_project) ? s_engine_instance->m_project->get_total_duration() : 0.0);
    return 1;
}

duk_ret_t ScriptEngine::js_timeline_get_track_count(duk_context* ctx) {
    duk_push_int(ctx, (s_engine_instance && s_engine_instance->m_project) ? (int)s_engine_instance->m_project->tracks.size() : 0);
    return 1;
}

duk_ret_t ScriptEngine::js_timeline_add_track(duk_context* ctx) {
    if (!s_engine_instance || !s_engine_instance->m_project) {
        duk_push_int(ctx, -1);
        return 1;
    }
    const char* type = duk_is_string(ctx, 0) ? duk_get_string(ctx, 0) : "video";
    const char* name = duk_is_string(ctx, 1) ? duk_get_string(ctx, 1) : nullptr;
    Project& proj = *s_engine_instance->m_project;
    s_engine_instance->push_undo_state();

    Track trk;
    trk.type = (std::string(type) == "audio") ? "audio" : "video";
    int count = 1;
    for (const auto& t : proj.tracks) {
        if (t.type == trk.type) count++;
    }
    trk.id = "track_" + std::string(trk.type == "audio" ? "a" : "v") + std::to_string(count);
    if (name && name[0] != '\0') {
        trk.name = name;
    } else {
        trk.name = (trk.type == "audio" ? "Audio " : "Video ") + std::to_string(count);
    }
    proj.tracks.push_back(trk);
    s_engine_instance->notify_project_modified();
    duk_push_int(ctx, static_cast<int>(proj.tracks.size() - 1));
    return 1;
}

duk_ret_t ScriptEngine::js_timeline_place_video(duk_context* ctx) {
    if (!s_engine_instance || !s_engine_instance->m_project) {
        duk_push_string(ctx, "");
        return 1;
    }
    int track_idx = duk_is_number(ctx, 0) ? duk_get_int(ctx, 0) : 0;
    const char* asset_id = duk_is_string(ctx, 1) ? duk_get_string(ctx, 1) : "";
    double start_time = duk_is_number(ctx, 2) ? duk_get_number(ctx, 2) : 0.0;
    double duration = duk_is_number(ctx, 3) ? duk_get_number(ctx, 3) : 5.0;

    Project& proj = *s_engine_instance->m_project;
    if (track_idx < 0 || track_idx >= static_cast<int>(proj.tracks.size())) {
        track_idx = 0;
    }
    s_engine_instance->push_undo_state();
    std::string ts = std::to_string(std::chrono::system_clock::now().time_since_epoch().count() % 1000000);
    Clip clp;
    clp.id = "clip_v_" + ts;
    clp.asset_id = asset_id;
    clp.start_time = start_time;
    clp.duration = duration;
    clp.offset = 0.0;
    proj.tracks[track_idx].clips.push_back(clp);
    s_engine_instance->set_selected_clip_id(clp.id);
    s_engine_instance->notify_project_modified();

    duk_push_string(ctx, clp.id.c_str());
    return 1;
}

duk_ret_t ScriptEngine::js_timeline_place_audio(duk_context* ctx) {
    if (!s_engine_instance || !s_engine_instance->m_project) {
        duk_push_string(ctx, "");
        return 1;
    }
    int track_idx = duk_is_number(ctx, 0) ? duk_get_int(ctx, 0) : 0;
    const char* asset_id = duk_is_string(ctx, 1) ? duk_get_string(ctx, 1) : "";
    double start_time = duk_is_number(ctx, 2) ? duk_get_number(ctx, 2) : 0.0;
    double duration = duk_is_number(ctx, 3) ? duk_get_number(ctx, 3) : 5.0;

    Project& proj = *s_engine_instance->m_project;
    if (track_idx < 0 || track_idx >= static_cast<int>(proj.tracks.size())) {
        track_idx = 0;
    }
    s_engine_instance->push_undo_state();
    std::string ts = std::to_string(std::chrono::system_clock::now().time_since_epoch().count() % 1000000);
    Clip clp;
    clp.id = "clip_a_" + ts;
    clp.asset_id = asset_id;
    clp.start_time = start_time;
    clp.duration = duration;
    clp.offset = 0.0;
    proj.tracks[track_idx].clips.push_back(clp);
    s_engine_instance->set_selected_clip_id(clp.id);
    s_engine_instance->notify_project_modified();

    duk_push_string(ctx, clp.id.c_str());
    return 1;
}

duk_ret_t ScriptEngine::js_timeline_place_image(duk_context* ctx) {
    if (!s_engine_instance || !s_engine_instance->m_project) {
        duk_push_string(ctx, "");
        return 1;
    }
    int track_idx = duk_is_number(ctx, 0) ? duk_get_int(ctx, 0) : 0;
    const char* asset_id = duk_is_string(ctx, 1) ? duk_get_string(ctx, 1) : "";
    double start_time = duk_is_number(ctx, 2) ? duk_get_number(ctx, 2) : 0.0;
    double duration = duk_is_number(ctx, 3) ? duk_get_number(ctx, 3) : 5.0;

    Project& proj = *s_engine_instance->m_project;
    if (track_idx < 0 || track_idx >= static_cast<int>(proj.tracks.size())) {
        track_idx = 0;
    }
    s_engine_instance->push_undo_state();
    std::string ts = std::to_string(std::chrono::system_clock::now().time_since_epoch().count() % 1000000);
    Clip clp;
    clp.id = "clip_img_" + ts;
    clp.asset_id = asset_id;
    clp.start_time = start_time;
    clp.duration = duration;
    clp.offset = 0.0;
    proj.tracks[track_idx].clips.push_back(clp);
    s_engine_instance->set_selected_clip_id(clp.id);
    s_engine_instance->notify_project_modified();

    duk_push_string(ctx, clp.id.c_str());
    return 1;
}

duk_ret_t ScriptEngine::js_timeline_place_text_clip(duk_context* ctx) {
    if (!s_engine_instance || !s_engine_instance->m_project) {
        duk_push_string(ctx, "");
        return 1;
    }
    int track_idx = duk_is_number(ctx, 0) ? duk_get_int(ctx, 0) : 0;
    const char* asset_or_text = duk_is_string(ctx, 1) ? duk_get_string(ctx, 1) : "Text";
    double start_time = duk_is_number(ctx, 2) ? duk_get_number(ctx, 2) : 0.0;
    double duration = duk_is_number(ctx, 3) ? duk_get_number(ctx, 3) : 5.0;

    Project& proj = *s_engine_instance->m_project;
    std::string asset_id;
    if (proj.find_video_asset(asset_or_text)) {
        asset_id = asset_or_text;
    } else {
        TextConfig cfg;
        cfg.text = asset_or_text;
        asset_id = TextGenerator::create_text_asset(proj, cfg);
    }
    if (asset_id.empty()) {
        duk_push_string(ctx, "");
        return 1;
    }
    if (track_idx < 0 || track_idx >= static_cast<int>(proj.tracks.size())) {
        track_idx = 0;
    }
    s_engine_instance->push_undo_state();
    std::string ts = std::to_string(std::chrono::system_clock::now().time_since_epoch().count() % 1000000);
    Clip clp;
    clp.id = "clip_txt_" + ts;
    clp.asset_id = asset_id;
    clp.start_time = start_time;
    clp.duration = duration;
    clp.offset = 0.0;
    proj.tracks[track_idx].clips.push_back(clp);
    s_engine_instance->set_selected_clip_id(clp.id);
    s_engine_instance->notify_project_modified();
    duk_push_string(ctx, clp.id.c_str());
    return 1;
}

duk_ret_t ScriptEngine::js_timeline_get_selected_clip(duk_context* ctx) {
    duk_push_string(ctx, s_engine_instance ? s_engine_instance->get_selected_clip_id().c_str() : "");
    return 1;
}

duk_ret_t ScriptEngine::js_timeline_get_selected_clips(duk_context* ctx) {
    duk_push_array(ctx);
    if (s_engine_instance && s_engine_instance->m_timeline) {
        const auto& ids = s_engine_instance->m_timeline->get_selected_clip_ids();
        for (duk_uarridx_t i = 0; i < ids.size(); ++i) {
            duk_push_string(ctx, ids[i].c_str());
            duk_put_prop_index(ctx, -2, i);
        }
    }
    return 1;
}

duk_ret_t ScriptEngine::js_timeline_select_clip(duk_context* ctx) {
    if (!s_engine_instance || !s_engine_instance->m_timeline) return 0;
    const char* clip_id = duk_get_string(ctx, 0);
    if (!clip_id) return 0;
    bool toggle = duk_is_boolean(ctx, 1) ? duk_get_boolean(ctx, 1) : false;
    bool add_to_sel = duk_is_boolean(ctx, 2) ? duk_get_boolean(ctx, 2) : false;
    s_engine_instance->m_timeline->select_clip(clip_id, toggle, add_to_sel);
    s_engine_instance->set_selected_clip_id(clip_id);
    return 0;
}

duk_ret_t ScriptEngine::js_timeline_select_clips(duk_context* ctx) {
    if (!s_engine_instance || !s_engine_instance->m_timeline) return 0;
    if (duk_is_array(ctx, 0)) {
        s_engine_instance->m_timeline->clear_selection();
        duk_uarridx_t len = (duk_uarridx_t)duk_get_length(ctx, 0);
        for (duk_uarridx_t i = 0; i < len; ++i) {
            duk_get_prop_index(ctx, 0, i);
            if (duk_is_string(ctx, -1)) {
                s_engine_instance->m_timeline->select_clip(duk_get_string(ctx, -1), false, true);
            }
            duk_pop(ctx);
        }
    }
    return 0;
}

duk_ret_t ScriptEngine::js_timeline_clear_selection(duk_context* ctx) {
    (void)ctx;
    if (s_engine_instance && s_engine_instance->m_timeline) {
        s_engine_instance->m_timeline->clear_selection();
        s_engine_instance->set_selected_clip_id("");
    }
    return 0;
}

duk_ret_t ScriptEngine::js_timeline_select_all(duk_context* ctx) {
    (void)ctx;
    if (s_engine_instance && s_engine_instance->m_timeline && s_engine_instance->m_project) {
        s_engine_instance->m_timeline->select_all(*s_engine_instance->m_project);
    }
    return 0;
}

duk_ret_t ScriptEngine::js_timeline_delete_clip(duk_context* ctx) {
    if (!s_engine_instance || !s_engine_instance->m_project) {
        duk_push_boolean(ctx, false);
        return 1;
    }
    const char* clip_id = duk_get_string(ctx, 0);
    if (!clip_id) { duk_push_boolean(ctx, false); return 1; }
    Project& proj = *s_engine_instance->m_project;

    for (auto& trk : proj.tracks) {
        for (auto it = trk.clips.begin(); it != trk.clips.end(); ++it) {
            if (it->id == clip_id) {
                s_engine_instance->push_undo_state();
                proj.remove_clip_keyframes(clip_id);
                trk.clips.erase(it);
                if (s_engine_instance->get_selected_clip_id() == clip_id) {
                    s_engine_instance->set_selected_clip_id("");
                }
                s_engine_instance->notify_project_modified();
                duk_push_boolean(ctx, true);
                return 1;
            }
        }
    }
    duk_push_boolean(ctx, false);
    return 1;
}

duk_ret_t ScriptEngine::js_timeline_split_clip(duk_context* ctx) {
    if (!s_engine_instance || !s_engine_instance->m_timeline || !s_engine_instance->m_project) {
        duk_push_boolean(ctx, false);
        return 1;
    }
    const char* clip_id = duk_get_string(ctx, 0);
    double split_t = duk_is_number(ctx, 1) ? duk_get_number(ctx, 1) : s_engine_instance->get_playhead_time();
    s_engine_instance->push_undo_state();
    bool res = s_engine_instance->m_timeline->split_clip_at(*s_engine_instance->m_project, clip_id ? clip_id : "", split_t);
    if (res) {
        s_engine_instance->notify_project_modified();
    }
    duk_push_boolean(ctx, res);
    return 1;
}

duk_ret_t ScriptEngine::js_timeline_move_track_up(duk_context* ctx) {
    int idx = duk_get_int(ctx, 0);
    if (s_engine_instance && s_engine_instance->m_timeline && s_engine_instance->m_project) {
        s_engine_instance->push_undo_state();
        s_engine_instance->m_timeline->move_track_up(*s_engine_instance->m_project, idx);
        s_engine_instance->notify_project_modified();
    }
    return 0;
}

duk_ret_t ScriptEngine::js_timeline_move_track_down(duk_context* ctx) {
    int idx = duk_get_int(ctx, 0);
    if (s_engine_instance && s_engine_instance->m_timeline && s_engine_instance->m_project) {
        s_engine_instance->push_undo_state();
        s_engine_instance->m_timeline->move_track_down(*s_engine_instance->m_project, idx);
        s_engine_instance->notify_project_modified();
    }
    return 0;
}

duk_ret_t ScriptEngine::js_timeline_move_track(duk_context* ctx) {
    int from_idx = duk_get_int(ctx, 0);
    int to_idx = duk_get_int(ctx, 1);
    if (s_engine_instance && s_engine_instance->m_timeline && s_engine_instance->m_project) {
        s_engine_instance->push_undo_state();
        s_engine_instance->m_timeline->reorder_track(*s_engine_instance->m_project, from_idx, to_idx);
        s_engine_instance->notify_project_modified();
    }
    return 0;
}

duk_ret_t ScriptEngine::js_timeline_swap_tracks(duk_context* ctx) {
    int idx1 = duk_get_int(ctx, 0);
    int idx2 = duk_get_int(ctx, 1);
    if (s_engine_instance && s_engine_instance->m_timeline && s_engine_instance->m_project) {
        s_engine_instance->push_undo_state();
        s_engine_instance->m_timeline->swap_tracks(*s_engine_instance->m_project, idx1, idx2);
        s_engine_instance->notify_project_modified();
    }
    return 0;
}

duk_ret_t ScriptEngine::js_timeline_set_track_name(duk_context* ctx) {
    int idx = duk_get_int(ctx, 0);
    const char* name = duk_get_string(ctx, 1);
    if (s_engine_instance && s_engine_instance->m_timeline && s_engine_instance->m_project && name) {
        s_engine_instance->push_undo_state();
        s_engine_instance->m_timeline->set_track_name(*s_engine_instance->m_project, idx, name);
        s_engine_instance->notify_project_modified();
    }
    return 0;
}

duk_ret_t ScriptEngine::js_timeline_get_track_name(duk_context* ctx) {
    int idx = duk_get_int(ctx, 0);
    std::string name = "";
    if (s_engine_instance && s_engine_instance->m_timeline && s_engine_instance->m_project) {
        name = s_engine_instance->m_timeline->get_track_name(*s_engine_instance->m_project, idx);
    }
    duk_push_string(ctx, name.c_str());
    return 1;
}

duk_ret_t ScriptEngine::js_timeline_add_keyframe(duk_context* ctx) {
    if (!s_engine_instance || !s_engine_instance->m_project) return 0;
    const char* clip_id = duk_get_string(ctx, 0);
    const char* prop = duk_get_string(ctx, 1);
    double t = duk_get_number(ctx, 2);
    double val = duk_get_number(ctx, 3);
    if (!clip_id || !prop) return 0;
    s_engine_instance->push_undo_state();
    s_engine_instance->m_project->set_or_add_keyframe(clip_id, prop, t, val);
    s_engine_instance->notify_project_modified();
    return 0;
}

duk_ret_t ScriptEngine::js_timeline_remove_keyframe(duk_context* ctx) {
    if (!s_engine_instance || !s_engine_instance->m_project) return 0;
    const char* clip_id = duk_get_string(ctx, 0);
    const char* prop = duk_get_string(ctx, 1);
    double t = duk_get_number(ctx, 2);
    if (!clip_id || !prop) return 0;
    Project& proj = *s_engine_instance->m_project;
    for (auto it = proj.keyframes.begin(); it != proj.keyframes.end(); ++it) {
        if (it->clip_id == clip_id && it->parameter == prop && std::abs(it->time - t) < 0.05) {
            s_engine_instance->push_undo_state();
            proj.keyframes.erase(it);
            s_engine_instance->notify_project_modified();
            break;
        }
    }
    return 0;
}

duk_ret_t ScriptEngine::js_timeline_clear_keyframes(duk_context* ctx) {
    if (!s_engine_instance || !s_engine_instance->m_project) return 0;
    const char* clip_id = duk_get_string(ctx, 0);
    const char* prop = duk_is_string(ctx, 1) ? duk_get_string(ctx, 1) : nullptr;
    if (!clip_id) return 0;
    Project& proj = *s_engine_instance->m_project;
    s_engine_instance->push_undo_state();
    if (!prop || std::strlen(prop) == 0) {
        proj.remove_clip_keyframes(clip_id);
    } else {
        auto it = std::remove_if(proj.keyframes.begin(), proj.keyframes.end(), [&](const Keyframe& kf) {
            return kf.clip_id == clip_id && kf.parameter == prop;
        });
        proj.keyframes.erase(it, proj.keyframes.end());
    }
    s_engine_instance->notify_project_modified();
    return 0;
}

duk_ret_t ScriptEngine::js_timeline_get_keyframes(duk_context* ctx) {
    duk_push_array(ctx);
    if (!s_engine_instance || !s_engine_instance->m_project) return 1;
    const char* clip_id = duk_get_string(ctx, 0);
    const char* prop = duk_is_string(ctx, 1) ? duk_get_string(ctx, 1) : nullptr;
    if (!clip_id) return 1;
    const Project& proj = *s_engine_instance->m_project;
    duk_uarridx_t idx = 0;
    for (const auto& kf : proj.keyframes) {
        if (kf.clip_id == clip_id) {
            if (!prop || std::strlen(prop) == 0 || kf.parameter == prop) {
                duk_push_object(ctx);
                duk_push_string(ctx, kf.parameter.c_str()); duk_put_prop_string(ctx, -2, "parameter");
                duk_push_number(ctx, kf.time);              duk_put_prop_string(ctx, -2, "time");
                duk_push_number(ctx, kf.value);             duk_put_prop_string(ctx, -2, "value");
                duk_put_prop_index(ctx, -2, idx++);
            }
        }
    }
    return 1;
}

duk_ret_t ScriptEngine::js_timeline_evaluate_keyframe(duk_context* ctx) {
    if (!s_engine_instance || !s_engine_instance->m_project) {
        duk_push_number(ctx, 0.0);
        return 1;
    }
    const char* clip_id = duk_get_string(ctx, 0);
    const char* prop = duk_get_string(ctx, 1);
    double t = duk_get_number(ctx, 2);
    double def_val = duk_is_number(ctx, 3) ? duk_get_number(ctx, 3) : 1.0;
    double result = def_val;
    if (clip_id && prop) {
        result = s_engine_instance->m_project->evaluate_keyframe(clip_id, prop, t, def_val);
    }
    duk_push_number(ctx, result);
    return 1;
}

// ----------------------------------------------------------------------------
// JS Frame & JSON Helpers
// ----------------------------------------------------------------------------

static void push_js_frame_object(duk_context* ctx, const ScriptFrameBuffer& fb) {
    duk_idx_t obj_idx = duk_push_object(ctx);
    duk_push_int(ctx, fb.id); duk_put_prop_string(ctx, obj_idx, "id");
    duk_push_int(ctx, fb.width); duk_put_prop_string(ctx, obj_idx, "width");
    duk_push_int(ctx, fb.height); duk_put_prop_string(ctx, obj_idx, "height");
    duk_push_int(ctx, fb.channels); duk_put_prop_string(ctx, obj_idx, "channels");
    duk_push_number(ctx, fb.time); duk_put_prop_string(ctx, obj_idx, "time");
    duk_push_string(ctx, fb.clip_id.c_str()); duk_put_prop_string(ctx, obj_idx, "clip_id");
    duk_push_string(ctx, fb.applied_shader.c_str()); duk_put_prop_string(ctx, obj_idx, "applied_shader");

    if (fb.rgba.size() <= 65536) {
        duk_idx_t arr_idx = duk_push_array(ctx);
        for (size_t i = 0; i < fb.rgba.size(); ++i) {
            duk_push_uint(ctx, fb.rgba[i]);
            duk_put_prop_index(ctx, arr_idx, (duk_uarridx_t)i);
        }
        duk_put_prop_string(ctx, obj_idx, "data");
    } else {
        void* ptr = duk_push_fixed_buffer(ctx, fb.rgba.size());
        if (ptr && !fb.rgba.empty()) {
            std::memcpy(ptr, fb.rgba.data(), fb.rgba.size());
        }
        duk_put_prop_string(ctx, obj_idx, "data");
    }

    // Methods
    duk_push_c_function(ctx, ScriptEngine::js_shader_apply, DUK_VARARGS);
    duk_put_prop_string(ctx, obj_idx, "apply_shader");
    duk_push_c_function(ctx, ScriptEngine::js_clip_set_frame, DUK_VARARGS);
    duk_put_prop_string(ctx, obj_idx, "reinsert");
    duk_push_c_function(ctx, ScriptEngine::js_clip_create_from_frame, DUK_VARARGS);
    duk_put_prop_string(ctx, obj_idx, "create_clip");
}

static bool sync_js_frame_data(duk_context* ctx, duk_idx_t frame_idx, ScriptFrameBuffer& fb) {
    frame_idx = duk_normalize_index(ctx, frame_idx);
    if (!duk_is_object(ctx, frame_idx)) return false;

    duk_get_prop_string(ctx, frame_idx, "id");
    if (duk_is_number(ctx, -1)) fb.id = duk_get_int(ctx, -1);
    duk_pop(ctx);

    duk_get_prop_string(ctx, frame_idx, "width");
    if (duk_is_number(ctx, -1)) fb.width = duk_get_int(ctx, -1);
    duk_pop(ctx);

    duk_get_prop_string(ctx, frame_idx, "height");
    if (duk_is_number(ctx, -1)) fb.height = duk_get_int(ctx, -1);
    duk_pop(ctx);

    duk_get_prop_string(ctx, frame_idx, "clip_id");
    if (duk_is_string(ctx, -1)) fb.clip_id = duk_get_string(ctx, -1);
    duk_pop(ctx);

    duk_get_prop_string(ctx, frame_idx, "applied_shader");
    if (duk_is_string(ctx, -1)) {
        const char* s = duk_get_string(ctx, -1);
        if (s && *s) fb.applied_shader = s;
    }
    duk_pop(ctx);

    if (fb.id > 0 && s_engine_instance) {
        auto it = s_engine_instance->m_frame_buffers.find(fb.id);
        if (it != s_engine_instance->m_frame_buffers.end() && !it->second.rgba.empty()) {
            fb.rgba = it->second.rgba;
            if (fb.applied_shader.empty()) fb.applied_shader = it->second.applied_shader;
            if (fb.applied_uniforms.empty()) fb.applied_uniforms = it->second.applied_uniforms;
            return true;
        }
    }

    duk_get_prop_string(ctx, frame_idx, "data");
    if (duk_is_buffer_data(ctx, -1)) {
        duk_size_t sz = 0;
        void* ptr = duk_get_buffer_data(ctx, -1, &sz);
        if (ptr && sz > 0) {
            const uint8_t* uptr = reinterpret_cast<const uint8_t*>(ptr);
            fb.rgba.assign(uptr, uptr + sz);
        }
    } else if (duk_is_array(ctx, -1)) {
        duk_size_t len = duk_get_length(ctx, -1);
        if (len > 0) {
            fb.rgba.resize(len);
            for (duk_uarridx_t i = 0; i < len; ++i) {
                duk_get_prop_index(ctx, -1, i);
                fb.rgba[i] = static_cast<uint8_t>(duk_get_uint(ctx, -1));
                duk_pop(ctx);
            }
        }
    }
    duk_pop(ctx);
    return !fb.rgba.empty();
}

static void json_to_js(duk_context* ctx, const nlohmann::json& j) {
    if (j.is_null()) {
        duk_push_null(ctx);
    } else if (j.is_boolean()) {
        duk_push_boolean(ctx, j.get<bool>());
    } else if (j.is_number_integer()) {
        duk_push_int(ctx, j.get<int>());
    } else if (j.is_number_unsigned()) {
        duk_push_uint(ctx, j.get<unsigned int>());
    } else if (j.is_number_float()) {
        duk_push_number(ctx, j.get<double>());
    } else if (j.is_string()) {
        duk_push_string(ctx, j.get<std::string>().c_str());
    } else if (j.is_array()) {
        duk_idx_t arr_idx = duk_push_array(ctx);
        duk_uarridx_t i = 0;
        for (const auto& el : j) {
            json_to_js(ctx, el);
            duk_put_prop_index(ctx, arr_idx, i++);
        }
    } else if (j.is_object()) {
        duk_idx_t obj_idx = duk_push_object(ctx);
        for (auto it = j.begin(); it != j.end(); ++it) {
            json_to_js(ctx, it.value());
            duk_put_prop_string(ctx, obj_idx, it.key().c_str());
        }
    } else {
        duk_push_undefined(ctx);
    }
}

static nlohmann::json js_to_json(duk_context* ctx, duk_idx_t idx) {
    idx = duk_normalize_index(ctx, idx);
    int type = duk_get_type(ctx, idx);
    if (type == DUK_TYPE_NULL || type == DUK_TYPE_UNDEFINED) {
        return nullptr;
    } else if (type == DUK_TYPE_BOOLEAN) {
        return (bool)duk_get_boolean(ctx, idx);
    } else if (type == DUK_TYPE_NUMBER) {
        double d = duk_get_number(ctx, idx);
        if (std::floor(d) == d && !std::isinf(d) && !std::isnan(d)) {
            return (int64_t)d;
        }
        return d;
    } else if (type == DUK_TYPE_STRING) {
        return std::string(duk_get_string(ctx, idx));
    } else if (type == DUK_TYPE_OBJECT) {
        if (duk_is_array(ctx, idx)) {
            nlohmann::json arr = nlohmann::json::array();
            duk_size_t len = duk_get_length(ctx, idx);
            for (duk_uarridx_t i = 0; i < len; ++i) {
                duk_get_prop_index(ctx, idx, i);
                arr.push_back(js_to_json(ctx, -1));
                duk_pop(ctx);
            }
            return arr;
        } else {
            nlohmann::json obj = nlohmann::json::object();
            duk_enum(ctx, idx, DUK_ENUM_OWN_PROPERTIES_ONLY);
            while (duk_next(ctx, -1, 1)) {
                std::string key = duk_get_string(ctx, -2);
                obj[key] = js_to_json(ctx, -1);
                duk_pop_2(ctx);
            }
            duk_pop(ctx);
            return obj;
        }
    }
    return nullptr;
}

// ----------------------------------------------------------------------------
// JS Clip Dispatchers
// ----------------------------------------------------------------------------

duk_ret_t ScriptEngine::js_clip_get_position(duk_context* ctx) {
    const char* id = duk_get_string(ctx, 0);
    duk_push_object(ctx);
    if (s_engine_instance && s_engine_instance->m_project && id) {
        const Clip* c = s_engine_instance->m_project->find_clip(id);
        if (c) {
            duk_push_number(ctx, c->pos_x); duk_put_prop_string(ctx, -2, "x");
            duk_push_number(ctx, c->pos_y); duk_put_prop_string(ctx, -2, "y");
            return 1;
        }
    }
    duk_push_number(ctx, 0.0); duk_put_prop_string(ctx, -2, "x");
    duk_push_number(ctx, 0.0); duk_put_prop_string(ctx, -2, "y");
    return 1;
}

duk_ret_t ScriptEngine::js_clip_set_position(duk_context* ctx) {
    const char* id = duk_get_string(ctx, 0);
    double x = duk_get_number(ctx, 1);
    double y = duk_get_number(ctx, 2);
    if (s_engine_instance && s_engine_instance->m_project && id) {
        Clip* c = s_engine_instance->m_project->find_clip_mut(id);
        if (c) {
            s_engine_instance->push_undo_state();
            c->pos_x = x;
            c->pos_y = y;
            s_engine_instance->notify_project_modified();
        }
    }
    return 0;
}

duk_ret_t ScriptEngine::js_clip_get_scale(duk_context* ctx) {
    const char* id = duk_get_string(ctx, 0);
    double s = 1.0;
    if (s_engine_instance && s_engine_instance->m_project && id) {
        const Clip* c = s_engine_instance->m_project->find_clip(id);
        if (c) s = c->scale_x;
    }
    duk_push_number(ctx, s);
    return 1;
}

duk_ret_t ScriptEngine::js_clip_get_scale_xy(duk_context* ctx) {
    const char* id = duk_get_string(ctx, 0);
    duk_push_object(ctx);
    double sx = 1.0, sy = 1.0;
    if (s_engine_instance && s_engine_instance->m_project && id) {
        const Clip* c = s_engine_instance->m_project->find_clip(id);
        if (c) { sx = c->scale_x; sy = c->scale_y; }
    }
    duk_push_number(ctx, sx); duk_put_prop_string(ctx, -2, "x");
    duk_push_number(ctx, sy); duk_put_prop_string(ctx, -2, "y");
    return 1;
}

duk_ret_t ScriptEngine::js_clip_get_scale_x(duk_context* ctx) {
    const char* id = duk_get_string(ctx, 0);
    double sx = 1.0;
    if (s_engine_instance && s_engine_instance->m_project && id) {
        const Clip* c = s_engine_instance->m_project->find_clip(id);
        if (c) sx = c->scale_x;
    }
    duk_push_number(ctx, sx);
    return 1;
}

duk_ret_t ScriptEngine::js_clip_get_scale_y(duk_context* ctx) {
    const char* id = duk_get_string(ctx, 0);
    double sy = 1.0;
    if (s_engine_instance && s_engine_instance->m_project && id) {
        const Clip* c = s_engine_instance->m_project->find_clip(id);
        if (c) sy = c->scale_y;
    }
    duk_push_number(ctx, sy);
    return 1;
}

duk_ret_t ScriptEngine::js_clip_set_scale(duk_context* ctx) {
    const char* id = duk_get_string(ctx, 0);
    double sx = duk_get_number(ctx, 1);
    double sy = duk_is_number(ctx, 2) ? duk_get_number(ctx, 2) : sx;
    if (s_engine_instance && s_engine_instance->m_project && id) {
        Clip* c = s_engine_instance->m_project->find_clip_mut(id);
        if (c) {
            s_engine_instance->push_undo_state();
            c->scale_x = std::max(0.01, sx);
            c->scale_y = std::max(0.01, sy);
            c->scale = c->scale_x;
            double rel_t = c->clip_relative_time(s_engine_instance->get_playhead_time());
            if (!s_engine_instance->m_project->get_clip_keyframes(c->id, "scale_x").empty()) {
                s_engine_instance->m_project->set_or_add_keyframe(c->id, "scale_x", rel_t, c->scale_x);
            }
            if (!s_engine_instance->m_project->get_clip_keyframes(c->id, "scale_y").empty()) {
                s_engine_instance->m_project->set_or_add_keyframe(c->id, "scale_y", rel_t, c->scale_y);
            }
            if (!s_engine_instance->m_project->get_clip_keyframes(c->id, "scale").empty()) {
                s_engine_instance->m_project->set_or_add_keyframe(c->id, "scale", rel_t, c->scale_x);
            }
            s_engine_instance->notify_project_modified();
        }
    }
    return 0;
}

duk_ret_t ScriptEngine::js_clip_set_scale_x(duk_context* ctx) {
    const char* id = duk_get_string(ctx, 0);
    double sx = duk_get_number(ctx, 1);
    if (s_engine_instance && s_engine_instance->m_project && id) {
        Clip* c = s_engine_instance->m_project->find_clip_mut(id);
        if (c) {
            s_engine_instance->push_undo_state();
            c->scale_x = std::max(0.01, sx);
            c->scale = c->scale_x;
            double rel_t = c->clip_relative_time(s_engine_instance->get_playhead_time());
            if (!s_engine_instance->m_project->get_clip_keyframes(c->id, "scale_x").empty()) {
                s_engine_instance->m_project->set_or_add_keyframe(c->id, "scale_x", rel_t, c->scale_x);
            }
            if (!s_engine_instance->m_project->get_clip_keyframes(c->id, "scale").empty()) {
                s_engine_instance->m_project->set_or_add_keyframe(c->id, "scale", rel_t, c->scale_x);
            }
            s_engine_instance->notify_project_modified();
        }
    }
    return 0;
}

duk_ret_t ScriptEngine::js_clip_set_scale_y(duk_context* ctx) {
    const char* id = duk_get_string(ctx, 0);
    double sy = duk_get_number(ctx, 1);
    if (s_engine_instance && s_engine_instance->m_project && id) {
        Clip* c = s_engine_instance->m_project->find_clip_mut(id);
        if (c) {
            s_engine_instance->push_undo_state();
            c->scale_y = std::max(0.01, sy);
            double rel_t = c->clip_relative_time(s_engine_instance->get_playhead_time());
            if (!s_engine_instance->m_project->get_clip_keyframes(c->id, "scale_y").empty()) {
                s_engine_instance->m_project->set_or_add_keyframe(c->id, "scale_y", rel_t, c->scale_y);
            }
            s_engine_instance->notify_project_modified();
        }
    }
    return 0;
}

duk_ret_t ScriptEngine::js_clip_get_rotation(duk_context* ctx) {
    const char* id = duk_get_string(ctx, 0);
    double rot = 0.0;
    if (s_engine_instance && s_engine_instance->m_project && id) {
        const Clip* c = s_engine_instance->m_project->find_clip(id);
        if (c) rot = c->rotation;
    }
    duk_push_number(ctx, rot);
    return 1;
}

duk_ret_t ScriptEngine::js_clip_set_rotation(duk_context* ctx) {
    const char* id = duk_get_string(ctx, 0);
    double rot = duk_get_number(ctx, 1);
    if (s_engine_instance && s_engine_instance->m_project && id) {
        Clip* c = s_engine_instance->m_project->find_clip_mut(id);
        if (c) {
            s_engine_instance->push_undo_state();
            c->rotation = rot;
            s_engine_instance->notify_project_modified();
        }
    }
    return 0;
}

duk_ret_t ScriptEngine::js_clip_get_opacity(duk_context* ctx) {
    const char* id = duk_get_string(ctx, 0);
    double op = 1.0;
    if (s_engine_instance && s_engine_instance->m_project && id) {
        const Clip* c = s_engine_instance->m_project->find_clip(id);
        if (c) op = c->opacity;
    }
    duk_push_number(ctx, op);
    return 1;
}

duk_ret_t ScriptEngine::js_clip_set_opacity(duk_context* ctx) {
    const char* id = duk_get_string(ctx, 0);
    double op = duk_get_number(ctx, 1);
    if (s_engine_instance && s_engine_instance->m_project && id) {
        Clip* c = s_engine_instance->m_project->find_clip_mut(id);
        if (c) {
            s_engine_instance->push_undo_state();
            c->opacity = std::clamp(op, 0.0, 1.0);
            s_engine_instance->notify_project_modified();
        }
    }
    return 0;
}

duk_ret_t ScriptEngine::js_clip_get_volume(duk_context* ctx) {
    const char* id = duk_get_string(ctx, 0);
    double vol = 1.0;
    if (s_engine_instance && s_engine_instance->m_project && id) {
        vol = s_engine_instance->m_project->evaluate_keyframe(id, "volume", 0.0, 1.0);
    }
    duk_push_number(ctx, vol);
    return 1;
}

duk_ret_t ScriptEngine::js_clip_set_volume(duk_context* ctx) {
    const char* id = duk_get_string(ctx, 0);
    double vol = duk_get_number(ctx, 1);
    if (s_engine_instance && s_engine_instance->m_project && id) {
        s_engine_instance->push_undo_state();
        s_engine_instance->m_project->set_or_add_keyframe(id, "volume", 0.0, std::clamp(vol, 0.0, 2.0));
        s_engine_instance->notify_project_modified();
    }
    return 0;
}

duk_ret_t ScriptEngine::js_clip_get_time(duk_context* ctx) {
    const char* id = duk_get_string(ctx, 0);
    duk_push_object(ctx);
    if (s_engine_instance && s_engine_instance->m_project && id) {
        const Clip* c = s_engine_instance->m_project->find_clip(id);
        if (c) {
            duk_push_number(ctx, c->start_time); duk_put_prop_string(ctx, -2, "start_time");
            duk_push_number(ctx, c->duration);   duk_put_prop_string(ctx, -2, "duration");
            duk_push_number(ctx, c->offset);     duk_put_prop_string(ctx, -2, "offset");
            return 1;
        }
    }
    duk_push_number(ctx, 0.0); duk_put_prop_string(ctx, -2, "start_time");
    duk_push_number(ctx, 0.0); duk_put_prop_string(ctx, -2, "duration");
    duk_push_number(ctx, 0.0); duk_put_prop_string(ctx, -2, "offset");
    return 1;
}

duk_ret_t ScriptEngine::js_clip_set_time(duk_context* ctx) {
    const char* id = duk_get_string(ctx, 0);
    double start_t = duk_get_number(ctx, 1);
    double dur = duk_get_number(ctx, 2);
    double off = duk_is_number(ctx, 3) ? duk_get_number(ctx, 3) : -1.0;
    if (s_engine_instance && s_engine_instance->m_project && id) {
        Clip* c = s_engine_instance->m_project->find_clip_mut(id);
        if (c) {
            s_engine_instance->push_undo_state();
            c->start_time = std::max(0.0, start_t);
            c->duration = std::max(0.1, dur);
            if (off >= 0.0) c->offset = off;
            s_engine_instance->notify_project_modified();
        }
    }
    return 0;
}

duk_ret_t ScriptEngine::js_clip_get_info(duk_context* ctx) {
    const char* id = duk_get_string(ctx, 0);
    duk_push_object(ctx);
    if (s_engine_instance && s_engine_instance->m_project && id) {
        int t_idx = -1, c_idx = -1;
        const Clip* c = s_engine_instance->m_project->find_clip(id, &t_idx, &c_idx);
        if (c) {
            duk_push_string(ctx, c->id.c_str()); duk_put_prop_string(ctx, -2, "id");
            duk_push_string(ctx, c->asset_id.c_str()); duk_put_prop_string(ctx, -2, "asset_id");
            duk_push_int(ctx, t_idx); duk_put_prop_string(ctx, -2, "track_idx");
            duk_push_number(ctx, c->start_time); duk_put_prop_string(ctx, -2, "start_time");
            duk_push_number(ctx, c->duration); duk_put_prop_string(ctx, -2, "duration");
            duk_push_number(ctx, c->offset); duk_put_prop_string(ctx, -2, "offset");
            duk_push_number(ctx, c->pos_x); duk_put_prop_string(ctx, -2, "pos_x");
            duk_push_number(ctx, c->pos_y); duk_put_prop_string(ctx, -2, "pos_y");
            duk_push_number(ctx, c->scale); duk_put_prop_string(ctx, -2, "scale");
            duk_push_number(ctx, c->scale_x); duk_put_prop_string(ctx, -2, "scale_x");
            duk_push_number(ctx, c->scale_y); duk_put_prop_string(ctx, -2, "scale_y");
            duk_push_number(ctx, c->rotation); duk_put_prop_string(ctx, -2, "rotation");
            duk_push_number(ctx, c->opacity); duk_put_prop_string(ctx, -2, "opacity");
            double vol = s_engine_instance->m_project->evaluate_keyframe(c->id, "volume", 0.0, 1.0);
            duk_push_number(ctx, vol); duk_put_prop_string(ctx, -2, "volume");
        }
    }
    return 1;
}

duk_ret_t ScriptEngine::js_clip_get_transform(duk_context* ctx) {
    if (!s_engine_instance || !s_engine_instance->m_project) {
        duk_push_object(ctx);
        return 1;
    }
    const char* clip_id = duk_get_string(ctx, 0);
    const Clip* c = clip_id ? s_engine_instance->m_project->find_clip(clip_id) : nullptr;
    duk_push_object(ctx);
    if (c) {
        duk_push_number(ctx, c->pos_x);    duk_put_prop_string(ctx, -2, "x");
        duk_push_number(ctx, c->pos_y);    duk_put_prop_string(ctx, -2, "y");
        duk_push_number(ctx, c->scale);    duk_put_prop_string(ctx, -2, "scale");
        duk_push_number(ctx, c->scale_x);  duk_put_prop_string(ctx, -2, "scale_x");
        duk_push_number(ctx, c->scale_y);  duk_put_prop_string(ctx, -2, "scale_y");
        duk_push_number(ctx, c->rotation); duk_put_prop_string(ctx, -2, "rotation");
    }
    return 1;
}

duk_ret_t ScriptEngine::js_clip_set_transform(duk_context* ctx) {
    if (!s_engine_instance || !s_engine_instance->m_project) return 0;
    const char* clip_id = duk_get_string(ctx, 0);
    Clip* c = clip_id ? s_engine_instance->m_project->find_clip_mut(clip_id) : nullptr;
    if (c) {
        s_engine_instance->push_undo_state();
        double rel_t = c->clip_relative_time(s_engine_instance->get_playhead_time());
        if (duk_is_number(ctx, 1)) {
            c->pos_x = duk_get_number(ctx, 1);
            if (!s_engine_instance->m_project->get_clip_keyframes(c->id, "pos_x").empty()) {
                s_engine_instance->m_project->set_or_add_keyframe(c->id, "pos_x", rel_t, c->pos_x);
            }
        }
        if (duk_is_number(ctx, 2)) {
            c->pos_y = duk_get_number(ctx, 2);
            if (!s_engine_instance->m_project->get_clip_keyframes(c->id, "pos_y").empty()) {
                s_engine_instance->m_project->set_or_add_keyframe(c->id, "pos_y", rel_t, c->pos_y);
            }
        }
        if (duk_is_number(ctx, 5)) {
            if (duk_is_number(ctx, 3)) {
                c->scale_x = std::max(0.01, duk_get_number(ctx, 3));
                c->scale = c->scale_x;
                if (!s_engine_instance->m_project->get_clip_keyframes(c->id, "scale_x").empty()) {
                    s_engine_instance->m_project->set_or_add_keyframe(c->id, "scale_x", rel_t, c->scale_x);
                }
                if (!s_engine_instance->m_project->get_clip_keyframes(c->id, "scale").empty()) {
                    s_engine_instance->m_project->set_or_add_keyframe(c->id, "scale", rel_t, c->scale_x);
                }
            }
            if (duk_is_number(ctx, 4)) {
                c->scale_y = std::max(0.01, duk_get_number(ctx, 4));
                if (!s_engine_instance->m_project->get_clip_keyframes(c->id, "scale_y").empty()) {
                    s_engine_instance->m_project->set_or_add_keyframe(c->id, "scale_y", rel_t, c->scale_y);
                }
            }
            if (duk_is_number(ctx, 5)) {
                c->rotation = duk_get_number(ctx, 5);
                if (!s_engine_instance->m_project->get_clip_keyframes(c->id, "rotation").empty()) {
                    s_engine_instance->m_project->set_or_add_keyframe(c->id, "rotation", rel_t, c->rotation);
                }
            }
        } else {
            if (duk_is_number(ctx, 3)) {
                double s = std::max(0.01, duk_get_number(ctx, 3));
                c->scale = s;
                c->scale_x = s;
                c->scale_y = s;
                if (!s_engine_instance->m_project->get_clip_keyframes(c->id, "scale_x").empty()) {
                    s_engine_instance->m_project->set_or_add_keyframe(c->id, "scale_x", rel_t, s);
                }
                if (!s_engine_instance->m_project->get_clip_keyframes(c->id, "scale_y").empty()) {
                    s_engine_instance->m_project->set_or_add_keyframe(c->id, "scale_y", rel_t, s);
                }
                if (!s_engine_instance->m_project->get_clip_keyframes(c->id, "scale").empty()) {
                    s_engine_instance->m_project->set_or_add_keyframe(c->id, "scale", rel_t, s);
                }
            }
            if (duk_is_number(ctx, 4)) {
                c->rotation = duk_get_number(ctx, 4);
                if (!s_engine_instance->m_project->get_clip_keyframes(c->id, "rotation").empty()) {
                    s_engine_instance->m_project->set_or_add_keyframe(c->id, "rotation", rel_t, c->rotation);
                }
            }
        }
        s_engine_instance->notify_project_modified();
    }
    return 0;
}

duk_ret_t ScriptEngine::js_clip_reset_transform(duk_context* ctx) {
    if (!s_engine_instance || !s_engine_instance->m_project) return 0;
    const char* clip_id = duk_get_string(ctx, 0);
    Clip* c = clip_id ? s_engine_instance->m_project->find_clip_mut(clip_id) : nullptr;
    if (c) {
        s_engine_instance->push_undo_state();
        double rel_t = c->clip_relative_time(s_engine_instance->get_playhead_time());
        c->pos_x = 0.0;
        c->pos_y = 0.0;
        c->scale = 1.0;
        c->scale_x = 1.0;
        c->scale_y = 1.0;
        c->rotation = 0.0;
        if (!s_engine_instance->m_project->get_clip_keyframes(c->id, "pos_x").empty()) {
            s_engine_instance->m_project->set_or_add_keyframe(c->id, "pos_x", rel_t, 0.0);
        }
        if (!s_engine_instance->m_project->get_clip_keyframes(c->id, "pos_y").empty()) {
            s_engine_instance->m_project->set_or_add_keyframe(c->id, "pos_y", rel_t, 0.0);
        }
        if (!s_engine_instance->m_project->get_clip_keyframes(c->id, "scale_x").empty()) {
            s_engine_instance->m_project->set_or_add_keyframe(c->id, "scale_x", rel_t, 1.0);
        }
        if (!s_engine_instance->m_project->get_clip_keyframes(c->id, "scale_y").empty()) {
            s_engine_instance->m_project->set_or_add_keyframe(c->id, "scale_y", rel_t, 1.0);
        }
        if (!s_engine_instance->m_project->get_clip_keyframes(c->id, "scale").empty()) {
            s_engine_instance->m_project->set_or_add_keyframe(c->id, "scale", rel_t, 1.0);
        }
        if (!s_engine_instance->m_project->get_clip_keyframes(c->id, "rotation").empty()) {
            s_engine_instance->m_project->set_or_add_keyframe(c->id, "rotation", rel_t, 0.0);
        }
        s_engine_instance->notify_project_modified();
    }
    return 0;
}

// ----------------------------------------------------------------------------
// JS SFX Pipeline: Frame & Shader Dispatchers
// ----------------------------------------------------------------------------

duk_ret_t ScriptEngine::js_clip_get_frame(duk_context* ctx) {
    if (!s_engine_instance || !s_engine_instance->m_project) {
        duk_push_null(ctx);
        return 1;
    }
    std::string clip_id = "";
    duk_idx_t arg_idx = 0;
    if (duk_is_string(ctx, 0)) {
        clip_id = duk_get_string(ctx, 0);
        arg_idx = 1;
    }
    if (clip_id.empty() || clip_id == "active" || clip_id == "selected") {
        clip_id = s_engine_instance->get_selected_clip_id();
    }

    Clip* clip = nullptr;
    if (!clip_id.empty()) {
        clip = s_engine_instance->m_project->find_clip_mut(clip_id);
    }
    if (!clip) {
        for (auto& trk : s_engine_instance->m_project->tracks) {
            if (trk.type == "video" && !trk.clips.empty()) {
                clip = &trk.clips.front();
                clip_id = clip->id;
                break;
            }
        }
    }
    if (!clip) {
        duk_push_null(ctx);
        return 1;
    }

    const VideoAsset* va = s_engine_instance->m_project->find_video_asset(clip->asset_id);
    if (!va) {
        duk_push_null(ctx);
        return 1;
    }

    double rel_time = 0.0;
    if (duk_is_number(ctx, arg_idx)) {
        rel_time = duk_get_number(ctx, arg_idx++);
    } else {
        rel_time = clip->clip_relative_time(s_engine_instance->get_playhead_time());
    }

    int target_w = (va->width > 0) ? va->width : 1280;
    int target_h = (va->height > 0) ? va->height : 720;
    if (duk_is_number(ctx, arg_idx) && duk_is_number(ctx, arg_idx + 1)) {
        target_w = duk_get_int(ctx, arg_idx++);
        target_h = duk_get_int(ctx, arg_idx++);
    }

    DecodedVideoContext decoder;
    if (!decoder.open(va->path)) {
        duk_push_null(ctx);
        return 1;
    }

    double decode_time = clip->offset + std::clamp(rel_time, 0.0, clip->duration);
    ScriptFrameBuffer fb;
    fb.id = s_engine_instance->m_next_frame_id++;
    fb.width = target_w;
    fb.height = target_h;
    fb.channels = 4;
    fb.time = rel_time;
    fb.clip_id = clip->id;

    if (!decoder.decode_frame_at(decode_time, target_w, target_h, fb.rgba) || fb.rgba.empty()) {
        duk_push_null(ctx);
        return 1;
    }

    s_engine_instance->m_frame_buffers[fb.id] = fb;
    push_js_frame_object(ctx, fb);
    return 1;
}

duk_ret_t ScriptEngine::js_shader_create(duk_context* ctx) {
    if (!s_engine_instance) {
        duk_push_int(ctx, 0);
        return 1;
    }
    const char* src_or_preset = duk_is_string(ctx, 0) ? duk_get_string(ctx, 0) : "invert";
    std::string key = src_or_preset ? src_or_preset : "invert";

    // Query ShaderManager for user shader from ~/.config/minicut/shaders/gl or ~/.config/minicut/shaders/vulkan
    CustomShader cs;
    if (ShaderManager::instance().find_shader(key, cs)) {
        ShaderManager::instance().load_shader_source(cs);
        if (!cs.frag_src.empty()) {
            key = cs.frag_src;
            if (key.find("// Shader: ") == std::string::npos && key.find(cs.name) == std::string::npos) {
                key = "// Shader: " + cs.name + "\n" + key;
            }
        }
    }

    int sid = s_engine_instance->m_next_shader_id++;
    s_engine_instance->m_shaders[sid] = key;
    duk_push_int(ctx, sid);
    return 1;
}

duk_ret_t ScriptEngine::js_shader_set_uniform(duk_context* ctx) {
    (void)ctx;
    duk_push_boolean(ctx, true);
    return 1;
}

duk_ret_t ScriptEngine::js_shader_apply(duk_context* ctx) {
    if (!s_engine_instance) {
        duk_push_null(ctx);
        return 1;
    }
    std::string shader_src = "invert";
    duk_idx_t frame_arg = 0;
    duk_idx_t uniforms_arg = 1;
    bool has_custom_fn = false;
    duk_idx_t fn_idx = -1;

    duk_push_this(ctx);
    bool this_is_frame = false;
    if (duk_is_object(ctx, -1) && duk_has_prop_string(ctx, -1, "data") && duk_has_prop_string(ctx, -1, "width")) {
        this_is_frame = true;
    }
    duk_idx_t this_idx = duk_normalize_index(ctx, -1);

    if (this_is_frame) {
        frame_arg = this_idx;
        if (duk_is_number(ctx, 0)) {
            int sid = duk_get_int(ctx, 0);
            auto it = s_engine_instance->m_shaders.find(sid);
            if (it != s_engine_instance->m_shaders.end()) shader_src = it->second;
        } else if (duk_is_string(ctx, 0)) {
            shader_src = duk_get_string(ctx, 0);
        } else if (duk_is_function(ctx, 0)) {
            has_custom_fn = true;
            fn_idx = 0;
        }
        uniforms_arg = 1;
    } else {
        duk_pop(ctx);
        if (duk_is_object(ctx, 0) && (duk_is_string(ctx, 1) || duk_is_number(ctx, 1) || duk_is_function(ctx, 1))) {
            frame_arg = 0;
            if (duk_is_number(ctx, 1)) {
                int sid = duk_get_int(ctx, 1);
                auto it = s_engine_instance->m_shaders.find(sid);
                if (it != s_engine_instance->m_shaders.end()) shader_src = it->second;
            } else if (duk_is_string(ctx, 1)) {
                shader_src = duk_get_string(ctx, 1);
            } else if (duk_is_function(ctx, 1)) {
                has_custom_fn = true;
                fn_idx = 1;
            }
            uniforms_arg = 2;
        } else {
            if (duk_is_number(ctx, 0)) {
                int sid = duk_get_int(ctx, 0);
                auto it = s_engine_instance->m_shaders.find(sid);
                if (it != s_engine_instance->m_shaders.end()) shader_src = it->second;
            } else if (duk_is_string(ctx, 0)) {
                shader_src = duk_get_string(ctx, 0);
            } else if (duk_is_function(ctx, 0)) {
                has_custom_fn = true;
                fn_idx = 0;
            }
            frame_arg = 1;
            uniforms_arg = 2;
        }
    }

    ScriptFrameBuffer src_fb;
    if (frame_arg >= 0 && duk_is_object(ctx, frame_arg)) {
        duk_get_prop_string(ctx, frame_arg, "id");
        if (duk_is_number(ctx, -1)) {
            int fid = duk_get_int(ctx, -1);
            auto it = s_engine_instance->m_frame_buffers.find(fid);
            if (it != s_engine_instance->m_frame_buffers.end()) src_fb = it->second;
        }
        duk_pop(ctx);
        sync_js_frame_data(ctx, frame_arg, src_fb);
    }
    if (this_is_frame) {
        duk_pop(ctx);
    }

    if (src_fb.rgba.empty()) {
        duk_push_null(ctx);
        return 1;
    }

    std::unordered_map<std::string, float> uniforms;
    if (duk_is_object(ctx, uniforms_arg)) {
        duk_enum(ctx, uniforms_arg, DUK_ENUM_OWN_PROPERTIES_ONLY);
        while (duk_next(ctx, -1, 1)) {
            if (duk_is_number(ctx, -1)) {
                uniforms[duk_get_string(ctx, -2)] = (float)duk_get_number(ctx, -1);
            }
            duk_pop_2(ctx);
        }
        duk_pop(ctx);
    }

    ScriptFrameBuffer out_fb;
    out_fb.id = s_engine_instance->m_next_frame_id++;
    out_fb.width = src_fb.width;
    out_fb.height = src_fb.height;
    out_fb.channels = 4;
    out_fb.time = src_fb.time;
    out_fb.clip_id = src_fb.clip_id;

    if (has_custom_fn && fn_idx >= 0) {
        out_fb.rgba.resize(src_fb.rgba.size());
        for (int y = 0; y < src_fb.height; ++y) {
            for (int x = 0; x < src_fb.width; ++x) {
                int idx = (y * src_fb.width + x) * 4;
                duk_dup(ctx, fn_idx);
                duk_push_uint(ctx, src_fb.rgba[idx + 0]);
                duk_push_uint(ctx, src_fb.rgba[idx + 1]);
                duk_push_uint(ctx, src_fb.rgba[idx + 2]);
                duk_push_uint(ctx, src_fb.rgba[idx + 3]);
                duk_push_int(ctx, x);
                duk_push_int(ctx, y);
                if (duk_pcall(ctx, 6) == 0) {
                    if (duk_is_array(ctx, -1)) {
                        duk_get_prop_index(ctx, -1, 0); out_fb.rgba[idx + 0] = (uint8_t)duk_get_uint(ctx, -1); duk_pop(ctx);
                        duk_get_prop_index(ctx, -1, 1); out_fb.rgba[idx + 1] = (uint8_t)duk_get_uint(ctx, -1); duk_pop(ctx);
                        duk_get_prop_index(ctx, -1, 2); out_fb.rgba[idx + 2] = (uint8_t)duk_get_uint(ctx, -1); duk_pop(ctx);
                        duk_get_prop_index(ctx, -1, 3); out_fb.rgba[idx + 3] = (uint8_t)duk_get_uint(ctx, -1); duk_pop(ctx);
                    } else if (duk_is_object(ctx, -1)) {
                        duk_get_prop_string(ctx, -1, "r"); if (duk_is_number(ctx, -1)) out_fb.rgba[idx + 0] = (uint8_t)duk_get_uint(ctx, -1); duk_pop(ctx);
                        duk_get_prop_string(ctx, -1, "g"); if (duk_is_number(ctx, -1)) out_fb.rgba[idx + 1] = (uint8_t)duk_get_uint(ctx, -1); duk_pop(ctx);
                        duk_get_prop_string(ctx, -1, "b"); if (duk_is_number(ctx, -1)) out_fb.rgba[idx + 2] = (uint8_t)duk_get_uint(ctx, -1); duk_pop(ctx);
                        duk_get_prop_string(ctx, -1, "a"); if (duk_is_number(ctx, -1)) out_fb.rgba[idx + 3] = (uint8_t)duk_get_uint(ctx, -1); duk_pop(ctx);
                    }
                    duk_pop(ctx);
                } else {
                    duk_pop(ctx);
                    out_fb.rgba[idx + 0] = src_fb.rgba[idx + 0];
                    out_fb.rgba[idx + 1] = src_fb.rgba[idx + 1];
                    out_fb.rgba[idx + 2] = src_fb.rgba[idx + 2];
                    out_fb.rgba[idx + 3] = src_fb.rgba[idx + 3];
                }
            }
        }
    } else {
        run_shader_effect(shader_src, src_fb.rgba, src_fb.width, src_fb.height, uniforms, out_fb.rgba);
    }

    out_fb.applied_shader = shader_src;
    out_fb.applied_uniforms = uniforms;

    s_engine_instance->m_frame_buffers[out_fb.id] = out_fb;
    push_js_frame_object(ctx, out_fb);
    return 1;
}

duk_ret_t ScriptEngine::js_clip_set_frame(duk_context* ctx) {
    if (!s_engine_instance || !s_engine_instance->m_project) {
        duk_push_boolean(ctx, false);
        return 1;
    }
    std::string target_clip_id = "";
    duk_idx_t frame_arg = -1;

    duk_push_this(ctx);
    bool this_is_frame = false;
    if (duk_is_object(ctx, -1) && duk_has_prop_string(ctx, -1, "data") && duk_has_prop_string(ctx, -1, "width")) {
        this_is_frame = true;
    }
    duk_idx_t this_idx = duk_normalize_index(ctx, -1);

    if (this_is_frame) {
        frame_arg = this_idx;
        if (duk_is_string(ctx, 0)) {
            target_clip_id = duk_get_string(ctx, 0);
        }
    } else {
        duk_pop(ctx);
        if (duk_is_string(ctx, 0) && duk_is_object(ctx, 1)) {
            target_clip_id = duk_get_string(ctx, 0);
            frame_arg = 1;
        } else if (duk_is_object(ctx, 0) && duk_is_string(ctx, 1)) {
            frame_arg = 0;
            target_clip_id = duk_get_string(ctx, 1);
        } else if (duk_is_object(ctx, 0)) {
            frame_arg = 0;
        }
    }

    ScriptFrameBuffer fb;
    if (frame_arg >= 0 && duk_is_object(ctx, frame_arg)) {
        duk_get_prop_string(ctx, frame_arg, "id");
        if (duk_is_number(ctx, -1)) {
            int fid = duk_get_int(ctx, -1);
            auto it = s_engine_instance->m_frame_buffers.find(fid);
            if (it != s_engine_instance->m_frame_buffers.end()) fb = it->second;
        }
        duk_pop(ctx);
        sync_js_frame_data(ctx, frame_arg, fb);
    }
    if (this_is_frame) {
        duk_pop(ctx);
    }

    if (fb.rgba.empty()) {
        duk_push_boolean(ctx, false);
        return 1;
    }

    if (target_clip_id.empty() && !fb.clip_id.empty()) {
        target_clip_id = fb.clip_id;
    }
    if (target_clip_id.empty() || target_clip_id == "active" || target_clip_id == "selected") {
        target_clip_id = s_engine_instance->get_selected_clip_id();
    }

    Clip* target_clip = nullptr;
    if (!target_clip_id.empty() && target_clip_id != "active" && target_clip_id != "selected") {
        target_clip = s_engine_instance->m_project->find_clip_mut(target_clip_id);
    }
    if (!target_clip) {
        std::string sel = s_engine_instance->get_selected_clip_id();
        if (!sel.empty()) {
            target_clip = s_engine_instance->m_project->find_clip_mut(sel);
        }
    }
    if (!target_clip) {
        for (auto& trk : s_engine_instance->m_project->tracks) {
            if (trk.type == "video" && !trk.clips.empty()) {
                target_clip = &trk.clips.front();
                break;
            }
        }
    }
    if (!target_clip) {
        duk_push_boolean(ctx, false);
        return 1;
    }

    if (!fb.applied_shader.empty()) {
        std::string out_clip_id;
        if (s_engine_instance->render_shader_across_clip(target_clip, fb.applied_shader, fb.applied_uniforms, false, 0, 0.0, out_clip_id)) {
            duk_push_boolean(ctx, true);
            return 1;
        }
    }

    std::string png_path = TempManager::get_temp_dir() + "/sfx_frame_js_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) + ".png";
    if (!MediaDecoder::save_rgba_to_png(png_path, fb.width, fb.height, fb.rgba)) {
        duk_push_boolean(ctx, false);
        return 1;
    }

    s_engine_instance->push_undo_state();
    VideoAsset new_va;
    new_va.id = "asset_sfx_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count() % 1000000);
    new_va.path = png_path;
    new_va.width = fb.width;
    new_va.height = fb.height;
    new_va.duration = target_clip->duration;
    new_va.is_image = true;
    new_va.has_audio = false;
    new_va.scale_x = 1.0;
    new_va.scale_y = 1.0;
    new_va.scale = 1.0;
    s_engine_instance->m_project->video_assets.push_back(new_va);

    target_clip->asset_id = new_va.id;
    if (s_engine_instance && s_engine_instance->m_app) {
        s_engine_instance->m_app->get_media_decoder().invalidate_context(png_path);
    }
    s_engine_instance->notify_project_modified();

    duk_push_boolean(ctx, true);
    return 1;
}

duk_ret_t ScriptEngine::js_clip_create_from_frame(duk_context* ctx) {
    if (!s_engine_instance || !s_engine_instance->m_project) {
        duk_push_string(ctx, "");
        return 1;
    }
    duk_idx_t frame_arg = -1;
    int track_idx = 0;
    double start_time = s_engine_instance->get_playhead_time();
    double duration = 5.0;

    duk_push_this(ctx);
    bool this_is_frame = false;
    if (duk_is_object(ctx, -1) && duk_has_prop_string(ctx, -1, "data") && duk_has_prop_string(ctx, -1, "width")) {
        this_is_frame = true;
    }
    duk_idx_t this_idx = duk_normalize_index(ctx, -1);

    if (this_is_frame) {
        frame_arg = this_idx;
        if (duk_is_number(ctx, 0)) track_idx = duk_get_int(ctx, 0);
        if (duk_is_number(ctx, 1)) start_time = duk_get_number(ctx, 1);
        if (duk_is_number(ctx, 2)) duration = duk_get_number(ctx, 2);
    } else {
        duk_pop(ctx);
        if (duk_is_object(ctx, 0)) {
            frame_arg = 0;
            if (duk_is_number(ctx, 1)) track_idx = duk_get_int(ctx, 1);
            if (duk_is_number(ctx, 2)) start_time = duk_get_number(ctx, 2);
            if (duk_is_number(ctx, 3)) duration = duk_get_number(ctx, 3);
        }
    }

    ScriptFrameBuffer fb;
    if (frame_arg >= 0 && duk_is_object(ctx, frame_arg)) {
        duk_get_prop_string(ctx, frame_arg, "id");
        if (duk_is_number(ctx, -1)) {
            int fid = duk_get_int(ctx, -1);
            auto it = s_engine_instance->m_frame_buffers.find(fid);
            if (it != s_engine_instance->m_frame_buffers.end()) fb = it->second;
        }
        duk_pop(ctx);
        sync_js_frame_data(ctx, frame_arg, fb);
    }
    if (this_is_frame) {
        duk_pop(ctx);
    }

    if (fb.rgba.empty()) {
        duk_push_string(ctx, "");
        return 1;
    }

    if (!fb.applied_shader.empty()) {
        std::string new_clip_id;
        Clip* base_clip = nullptr;
        if (!fb.clip_id.empty() && fb.clip_id != "active" && fb.clip_id != "selected") {
            base_clip = s_engine_instance->m_project->find_clip_mut(fb.clip_id);
        }
        if (!base_clip && !s_engine_instance->get_selected_clip_id().empty()) {
            base_clip = s_engine_instance->m_project->find_clip_mut(s_engine_instance->get_selected_clip_id());
        }
        if (!base_clip) {
            for (auto& trk : s_engine_instance->m_project->tracks) {
                if (trk.type == "video" && !trk.clips.empty()) {
                    base_clip = &trk.clips.front();
                    break;
                }
            }
        }
        if (base_clip) {
            Clip temp_clip = *base_clip;
            temp_clip.duration = duration;
            if (s_engine_instance->render_shader_across_clip(&temp_clip, fb.applied_shader, fb.applied_uniforms, true, track_idx, start_time, new_clip_id)) {
                duk_push_string(ctx, new_clip_id.c_str());
                return 1;
            }
        }
    }

    std::string png_path = TempManager::get_temp_dir() + "/sfx_clip_js_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) + ".png";
    if (!MediaDecoder::save_rgba_to_png(png_path, fb.width, fb.height, fb.rgba)) {
        duk_push_string(ctx, "");
        return 1;
    }

    Project& proj = *s_engine_instance->m_project;
    s_engine_instance->push_undo_state();

    VideoAsset new_va;
    std::string ts = std::to_string(std::chrono::system_clock::now().time_since_epoch().count() % 1000000);
    new_va.id = "asset_sfx_" + ts;
    new_va.path = png_path;
    new_va.width = fb.width;
    new_va.height = fb.height;
    new_va.duration = duration;
    new_va.is_image = true;
    new_va.has_audio = false;
    new_va.scale_x = 1.0;
    new_va.scale_y = 1.0;
    new_va.scale = 1.0;
    proj.video_assets.push_back(new_va);

    if (track_idx < 0 || track_idx >= static_cast<int>(proj.tracks.size()) || proj.tracks[track_idx].type != "video") {
        track_idx = -1;
        for (size_t t = 0; t < proj.tracks.size(); ++t) {
            if (proj.tracks[t].type == "video") {
                track_idx = static_cast<int>(t);
                break;
            }
        }
        if (track_idx < 0) {
            Track trk;
            trk.id = "track_v" + std::to_string(proj.tracks.size() + 1);
            trk.name = "Video " + std::to_string(proj.tracks.size() + 1);
            trk.type = "video";
            proj.tracks.push_back(trk);
            track_idx = static_cast<int>(proj.tracks.size() - 1);
        }
    }

    Clip new_clip;
    new_clip.id = "clip_sfx_" + ts;
    new_clip.asset_id = new_va.id;
    new_clip.start_time = std::max(0.0, start_time);
    new_clip.duration = std::max(0.1, duration);
    new_clip.offset = 0.0;
    new_clip.scale_x = 1.0;
    new_clip.scale_y = 1.0;
    new_clip.scale = 1.0;
    proj.tracks[track_idx].clips.push_back(new_clip);

    if (s_engine_instance && s_engine_instance->m_app) {
        s_engine_instance->m_app->get_media_decoder().invalidate_context(png_path);
    }
    s_engine_instance->set_selected_clip_id(new_clip.id);
    s_engine_instance->notify_project_modified();

    duk_push_string(ctx, new_clip.id.c_str());
    return 1;
}

// ----------------------------------------------------------------------------
// JS Localization Dispatchers
// ----------------------------------------------------------------------------

duk_ret_t ScriptEngine::js_locale_get(duk_context* ctx) {
    const char* key = duk_is_string(ctx, 0) ? duk_get_string(ctx, 0) : "";
    const char* def = duk_is_string(ctx, 1) ? duk_get_string(ctx, 1) : key;
    std::string text = LocaleManager::instance().get_text(key ? key : "", def ? def : "");
    duk_push_string(ctx, text.c_str());
    return 1;
}

duk_ret_t ScriptEngine::js_locale_get_language(duk_context* ctx) {
    duk_push_string(ctx, LocaleManager::instance().get_current_language().c_str());
    return 1;
}

duk_ret_t ScriptEngine::js_locale_set_language(duk_context* ctx) {
    const char* lang = duk_require_string(ctx, 0);
    if (lang) {
        LocaleManager::instance().set_language(lang);
        SettingsLogic::instance().set_language(lang);
    }
    duk_push_boolean(ctx, true);
    return 1;
}

duk_ret_t ScriptEngine::js_locale_is_rtl(duk_context* ctx) {
    const char* lang = duk_is_string(ctx, 0) ? duk_get_string(ctx, 0) : "";
    duk_push_boolean(ctx, LocaleManager::instance().is_rtl(lang ? lang : ""));
    return 1;
}

duk_ret_t ScriptEngine::js_locale_register(duk_context* ctx) {
    if (duk_is_string(ctx, 0) && duk_is_object(ctx, 1)) {
        std::string lang = duk_get_string(ctx, 0);
        duk_enum(ctx, 1, DUK_ENUM_OWN_PROPERTIES_ONLY);
        while (duk_next(ctx, -1, 1)) {
            std::string key = duk_get_string(ctx, -2);
            std::string val = duk_safe_to_string(ctx, -1);
            LocaleManager::instance().register_string(lang, key, val);
            duk_pop_2(ctx);
        }
        duk_pop(ctx);
        duk_push_boolean(ctx, true);
        return 1;
    } else if (duk_is_object(ctx, 0)) {
        duk_enum(ctx, 0, DUK_ENUM_OWN_PROPERTIES_ONLY);
        while (duk_next(ctx, -1, 1)) {
            std::string lang = duk_get_string(ctx, -2);
            if (duk_is_object(ctx, -1)) {
                duk_enum(ctx, -1, DUK_ENUM_OWN_PROPERTIES_ONLY);
                while (duk_next(ctx, -1, 1)) {
                    std::string key = duk_get_string(ctx, -2);
                    std::string val = duk_safe_to_string(ctx, -1);
                    LocaleManager::instance().register_string(lang, key, val);
                    duk_pop_2(ctx);
                }
                duk_pop(ctx);
            }
            duk_pop_2(ctx);
        }
        duk_pop(ctx);
        duk_push_boolean(ctx, true);
        return 1;
    }
    duk_push_boolean(ctx, false);
    return 1;
}

// ----------------------------------------------------------------------------
// JS AI Library Dispatchers
// ----------------------------------------------------------------------------

duk_ret_t ScriptEngine::js_ai_is_available(duk_context* ctx) {
    bool avail = false;
    if (s_engine_instance && s_engine_instance->has_mock_ai_response()) {
        avail = true;
    } else {
        avail = SettingsLogic::instance().is_ai_enabled() && SettingsLogic::instance().is_authenticated();
    }
    duk_push_boolean(ctx, avail);
    return 1;
}

duk_ret_t ScriptEngine::js_ai_is_enabled(duk_context* ctx) {
    duk_push_boolean(ctx, SettingsLogic::instance().is_ai_enabled());
    return 1;
}

duk_ret_t ScriptEngine::js_ai_set_enabled(duk_context* ctx) {
    bool en = duk_get_boolean(ctx, 0);
    SettingsLogic::instance().set_ai_enabled(en);
    SettingsLogic::instance().save_to_disk();
    return 0;
}

duk_ret_t ScriptEngine::js_ai_has_api_key(duk_context* ctx) {
    duk_push_boolean(ctx, SettingsLogic::instance().is_authenticated());
    return 1;
}

duk_ret_t ScriptEngine::js_ai_set_api_key(duk_context* ctx) {
    const char* key = duk_is_string(ctx, 0) ? duk_get_string(ctx, 0) : "";
    SettingsLogic::instance().set_gemini_api_key(key ? key : "");
    SettingsLogic::instance().save_to_disk();
    return 0;
}

duk_ret_t ScriptEngine::js_ai_is_busy(duk_context* ctx) {
    (void)ctx;
    duk_push_boolean(ctx, false);
    return 1;
}

duk_ret_t ScriptEngine::js_ai_get_status(duk_context* ctx) {
    if (s_engine_instance && s_engine_instance->has_mock_ai_response()) {
        duk_push_string(ctx, "Mock Mode Active");
        return 1;
    }
    if (!SettingsLogic::instance().is_ai_enabled()) {
        duk_push_string(ctx, "Disabled");
        return 1;
    }
    if (!SettingsLogic::instance().is_authenticated()) {
        duk_push_string(ctx, "API Key Missing");
        return 1;
    }
    duk_push_string(ctx, "Ready");
    return 1;
}

duk_ret_t ScriptEngine::js_ai_set_mock_response(duk_context* ctx) {
    if (duk_is_string(ctx, 0)) {
        if (s_engine_instance) s_engine_instance->set_mock_ai_response(duk_get_string(ctx, 0));
    } else if (duk_is_object(ctx, 0)) {
        nlohmann::json j = js_to_json(ctx, 0);
        if (s_engine_instance) s_engine_instance->set_mock_ai_response(j.dump());
    }
    return 0;
}

duk_ret_t ScriptEngine::js_ai_clear_mock_response(duk_context* ctx) {
    (void)ctx;
    if (s_engine_instance) s_engine_instance->clear_mock_ai_response();
    return 0;
}

duk_ret_t ScriptEngine::js_ai_prompt(duk_context* ctx) {
    const char* prompt = duk_is_string(ctx, 0) ? duk_get_string(ctx, 0) : "";
    const char* sys_inst = duk_is_string(ctx, 1) ? duk_get_string(ctx, 1) : "You are an AI assistant in minicut video editor.";

    if (s_engine_instance && s_engine_instance->has_mock_ai_response()) {
        duk_push_string(ctx, s_engine_instance->get_mock_ai_response().c_str());
        return 1;
    }

    if (!SettingsLogic::instance().is_ai_enabled() || !SettingsLogic::instance().is_authenticated()) {
        duk_push_null(ctx);
        return 1;
    }

    std::string key = SettingsLogic::instance().get_gemini_api_key();
    std::string resp = AICore::query_gemini_api(prompt ? prompt : "", sys_inst ? sys_inst : "", key);
    if (resp.empty()) {
        duk_push_null(ctx);
        return 1;
    }

    duk_push_string(ctx, resp.c_str());
    return 1;
}

duk_ret_t ScriptEngine::js_ai_parse_json(duk_context* ctx) {
    const char* raw = duk_is_string(ctx, 0) ? duk_get_string(ctx, 0) : nullptr;
    if (!raw) {
        duk_push_null(ctx);
        return 1;
    }
    std::string cleaned = clean_json_markdown(raw);
    try {
        nlohmann::json j = nlohmann::json::parse(cleaned);
        json_to_js(ctx, j);
        return 1;
    } catch (...) {
        duk_push_null(ctx);
        return 1;
    }
}

duk_ret_t ScriptEngine::js_ai_to_json(duk_context* ctx) {
    int indent = duk_is_number(ctx, 1) ? duk_get_int(ctx, 1) : -1;
    nlohmann::json j = js_to_json(ctx, 0);
    std::string str = (indent >= 0) ? j.dump(indent) : j.dump();
    duk_push_string(ctx, str.c_str());
    return 1;
}

duk_ret_t ScriptEngine::js_ai_prompt_json(duk_context* ctx) {
    const char* prompt = duk_is_string(ctx, 0) ? duk_get_string(ctx, 0) : "";
    std::string schema_inst = "Respond strictly with valid JSON without any markdown formatting or explanations.";
    if (duk_is_string(ctx, 1)) {
        schema_inst = duk_get_string(ctx, 1);
    } else if (duk_is_object(ctx, 1)) {
        nlohmann::json schema = js_to_json(ctx, 1);
        schema_inst = "Respond strictly with valid JSON adhering to this schema/default structure: " + schema.dump();
    }

    std::string resp;
    if (s_engine_instance && s_engine_instance->has_mock_ai_response()) {
        resp = s_engine_instance->get_mock_ai_response();
    } else {
        if (!SettingsLogic::instance().is_ai_enabled() || !SettingsLogic::instance().is_authenticated()) {
            duk_push_null(ctx);
            return 1;
        }
        std::string key = SettingsLogic::instance().get_gemini_api_key();
        resp = AICore::query_gemini_api(prompt ? prompt : "", schema_inst, key);
    }

    if (resp.empty()) {
        duk_push_null(ctx);
        return 1;
    }

    std::string cleaned = clean_json_markdown(resp);
    try {
        nlohmann::json j = nlohmann::json::parse(cleaned);
        json_to_js(ctx, j);
        return 1;
    } catch (...) {
        duk_push_null(ctx);
        return 1;
    }
}

duk_ret_t ScriptEngine::js_ai_struct(duk_context* ctx) {
    nlohmann::json base = nlohmann::json::object();
    if (duk_is_object(ctx, 0)) {
        base = js_to_json(ctx, 0);
    }
    if (duk_is_string(ctx, 1)) {
        try {
            nlohmann::json overlay = nlohmann::json::parse(clean_json_markdown(duk_get_string(ctx, 1)));
            base.merge_patch(overlay);
        } catch (...) {}
    } else if (duk_is_object(ctx, 1)) {
        nlohmann::json overlay = js_to_json(ctx, 1);
        base.merge_patch(overlay);
    }
    json_to_js(ctx, base);
    return 1;
}

// ----------------------------------------------------------------------------
// JS Export Dispatchers
// ----------------------------------------------------------------------------

duk_ret_t ScriptEngine::js_export_get_audio_progress(duk_context* ctx) {
    if (s_engine_instance && s_engine_instance->m_app) {
        duk_push_number(ctx, s_engine_instance->m_app->get_export_manager().get_audio_progress());
    } else {
        duk_push_number(ctx, 0.0);
    }
    return 1;
}

duk_ret_t ScriptEngine::js_export_get_progress(duk_context* ctx) {
    if (s_engine_instance && s_engine_instance->m_app) {
        duk_push_number(ctx, s_engine_instance->m_app->get_export_manager().get_progress());
    } else {
        duk_push_number(ctx, 0.0);
    }
    return 1;
}

duk_ret_t ScriptEngine::js_export_is_exporting(duk_context* ctx) {
    if (s_engine_instance && s_engine_instance->m_app) {
        duk_push_boolean(ctx, s_engine_instance->m_app->get_export_manager().is_exporting());
    } else {
        duk_push_boolean(ctx, false);
    }
    return 1;
}

duk_ret_t ScriptEngine::js_export_get_status(duk_context* ctx) {
    if (s_engine_instance && s_engine_instance->m_app) {
        duk_push_string(ctx, s_engine_instance->m_app->get_export_manager().get_status_text().c_str());
    } else {
        duk_push_string(ctx, "Idle");
    }
    return 1;
}

// ----------------------------------------------------------------------------
// JS Project & Assets Dispatchers
// ----------------------------------------------------------------------------

duk_ret_t ScriptEngine::js_project_save(duk_context* ctx) {
    const char* path = duk_is_string(ctx, 0) ? duk_get_string(ctx, 0) : "";
    bool ok = false;
    if (s_engine_instance && s_engine_instance->m_on_save_project) {
        ok = s_engine_instance->m_on_save_project(path ? path : "");
    }
    duk_push_boolean(ctx, ok);
    return 1;
}

duk_ret_t ScriptEngine::js_project_load(duk_context* ctx) {
    const char* path = duk_require_string(ctx, 0);
    bool ok = false;
    if (s_engine_instance && s_engine_instance->m_on_load_project) {
        ok = s_engine_instance->m_on_load_project(path ? path : "");
    }
    duk_push_boolean(ctx, ok);
    return 1;
}

duk_ret_t ScriptEngine::js_project_import_video(duk_context* ctx) {
    const char* path = duk_require_string(ctx, 0);
    std::string id = "";
    if (s_engine_instance && s_engine_instance->m_clip_manager && s_engine_instance->m_project && path) {
        s_engine_instance->push_undo_state();
        id = s_engine_instance->m_clip_manager->import_video(*s_engine_instance->m_project, path);
        s_engine_instance->notify_project_modified();
    }
    duk_push_string(ctx, id.c_str());
    return 1;
}

duk_ret_t ScriptEngine::js_project_import_audio(duk_context* ctx) {
    const char* path = duk_require_string(ctx, 0);
    std::string id = "";
    if (s_engine_instance && s_engine_instance->m_clip_manager && s_engine_instance->m_project && path) {
        s_engine_instance->push_undo_state();
        id = s_engine_instance->m_clip_manager->import_audio(*s_engine_instance->m_project, path);
        s_engine_instance->notify_project_modified();
    }
    duk_push_string(ctx, id.c_str());
    return 1;
}

duk_ret_t ScriptEngine::js_project_import_image(duk_context* ctx) {
    const char* path = duk_require_string(ctx, 0);
    std::string id = "";
    if (s_engine_instance && s_engine_instance->m_clip_manager && s_engine_instance->m_project && path) {
        s_engine_instance->push_undo_state();
        id = s_engine_instance->m_clip_manager->import_image(*s_engine_instance->m_project, path);
        s_engine_instance->notify_project_modified();
    }
    duk_push_string(ctx, id.c_str());
    return 1;
}

duk_ret_t ScriptEngine::js_project_get_name(duk_context* ctx) {
    std::string name = (s_engine_instance && s_engine_instance->m_project) ? s_engine_instance->m_project->name : "";
    duk_push_string(ctx, name.c_str());
    return 1;
}

duk_ret_t ScriptEngine::js_project_set_name(duk_context* ctx) {
    const char* name = duk_require_string(ctx, 0);
    if (s_engine_instance && s_engine_instance->m_project && name) {
        s_engine_instance->m_project->name = name;
        s_engine_instance->notify_project_modified();
    }
    return 0;
}

duk_ret_t ScriptEngine::js_assets_create_text(duk_context* ctx) {
    const char* text = duk_require_string(ctx, 0);
    std::string id = "";
    if (s_engine_instance && s_engine_instance->m_project && text) {
        TextConfig cfg;
        cfg.text = text;
        if (duk_get_top(ctx) >= 2 && duk_is_object(ctx, 1)) {
            duk_get_prop_string(ctx, 1, "font_size"); if (duk_is_number(ctx, -1)) cfg.font_size = (float)duk_get_number(ctx, -1); duk_pop(ctx);
            duk_get_prop_string(ctx, 1, "font_path"); if (duk_is_string(ctx, -1)) cfg.font_path = duk_get_string(ctx, -1); duk_pop(ctx);
            duk_get_prop_string(ctx, 1, "use_outline"); if (duk_is_boolean(ctx, -1)) cfg.use_outline = duk_get_boolean(ctx, -1); duk_pop(ctx);
            duk_get_prop_string(ctx, 1, "use_bg"); if (duk_is_boolean(ctx, -1)) cfg.use_bg = duk_get_boolean(ctx, -1); duk_pop(ctx);
            duk_get_prop_string(ctx, 1, "justification"); if (duk_is_number(ctx, -1)) cfg.justification = duk_get_int(ctx, -1); duk_pop(ctx);
        }
        id = TextGenerator::create_text_asset(*s_engine_instance->m_project, cfg);
    }
    duk_push_string(ctx, id.c_str());
    return 1;
}

duk_ret_t ScriptEngine::js_assets_get_scale(duk_context* ctx) {
    const char* id = duk_get_string(ctx, 0);
    duk_push_object(ctx);
    double sx = 1.0, sy = 1.0;
    if (s_engine_instance && s_engine_instance->m_project && id) {
        const VideoAsset* va = s_engine_instance->m_project->find_video_asset(id);
        if (va) { sx = va->scale_x; sy = va->scale_y; }
    }
    duk_push_number(ctx, sx); duk_put_prop_string(ctx, -2, "x");
    duk_push_number(ctx, sy); duk_put_prop_string(ctx, -2, "y");
    return 1;
}

duk_ret_t ScriptEngine::js_assets_get_scale_x(duk_context* ctx) {
    const char* id = duk_get_string(ctx, 0);
    double sx = 1.0;
    if (s_engine_instance && s_engine_instance->m_project && id) {
        const VideoAsset* va = s_engine_instance->m_project->find_video_asset(id);
        if (va) sx = va->scale_x;
    }
    duk_push_number(ctx, sx);
    return 1;
}

duk_ret_t ScriptEngine::js_assets_get_scale_y(duk_context* ctx) {
    const char* id = duk_get_string(ctx, 0);
    double sy = 1.0;
    if (s_engine_instance && s_engine_instance->m_project && id) {
        const VideoAsset* va = s_engine_instance->m_project->find_video_asset(id);
        if (va) sy = va->scale_y;
    }
    duk_push_number(ctx, sy);
    return 1;
}

duk_ret_t ScriptEngine::js_assets_set_scale(duk_context* ctx) {
    const char* id = duk_get_string(ctx, 0);
    double sx = duk_get_number(ctx, 1);
    double sy = duk_is_number(ctx, 2) ? duk_get_number(ctx, 2) : sx;
    if (s_engine_instance && s_engine_instance->m_project && id) {
        VideoAsset* va = s_engine_instance->m_project->find_video_asset_mut(id);
        if (va) {
            s_engine_instance->push_undo_state();
            va->scale_x = std::max(0.01, sx);
            va->scale_y = std::max(0.01, sy);
            va->scale = va->scale_x;
            s_engine_instance->notify_project_modified();
        }
    }
    return 0;
}

duk_ret_t ScriptEngine::js_assets_set_scale_x(duk_context* ctx) {
    const char* id = duk_get_string(ctx, 0);
    double sx = duk_get_number(ctx, 1);
    if (s_engine_instance && s_engine_instance->m_project && id) {
        VideoAsset* va = s_engine_instance->m_project->find_video_asset_mut(id);
        if (va) {
            s_engine_instance->push_undo_state();
            va->scale_x = std::max(0.01, sx);
            va->scale = va->scale_x;
            s_engine_instance->notify_project_modified();
        }
    }
    return 0;
}

duk_ret_t ScriptEngine::js_assets_set_scale_y(duk_context* ctx) {
    const char* id = duk_get_string(ctx, 0);
    double sy = duk_get_number(ctx, 1);
    if (s_engine_instance && s_engine_instance->m_project && id) {
        VideoAsset* va = s_engine_instance->m_project->find_video_asset_mut(id);
        if (va) {
            s_engine_instance->push_undo_state();
            va->scale_y = std::max(0.01, sy);
            s_engine_instance->notify_project_modified();
        }
    }
    return 0;
}

// ----------------------------------------------------------------------------
// JS Minicut & Utilities Dispatchers
// ----------------------------------------------------------------------------

duk_ret_t ScriptEngine::js_minicut_log(duk_context* ctx) {
    duk_idx_t n = duk_get_top(ctx);
    for (duk_idx_t i = 0; i < n; ++i) {
        if (i > 0) std::cout << " ";
        std::cout << duk_safe_to_string(ctx, i);
    }
    std::cout << std::endl;
    return 0;
}

duk_ret_t ScriptEngine::js_minicut_set_status(duk_context* ctx) {
    const char* msg = duk_require_string(ctx, 0);
    double dur = duk_is_number(ctx, 1) ? duk_get_number(ctx, 1) : 4.0;
    if (s_engine_instance && msg) {
        s_engine_instance->set_status(msg, dur);
    }
    return 0;
}

duk_ret_t ScriptEngine::js_minicut_push_undo(duk_context* ctx) {
    (void)ctx;
    if (s_engine_instance) {
        s_engine_instance->push_undo_state();
    }
    return 0;
}

// ----------------------------------------------------------------------------
// JS Net Dispatchers
// ----------------------------------------------------------------------------

duk_ret_t ScriptEngine::js_net_get(duk_context* ctx) {
    const char* url = duk_require_string(ctx, 0);
    if (!url) {
        duk_push_null(ctx);
        return 1;
    }
    CURL* curl = curl_easy_init();
    if (!curl) {
        duk_push_null(ctx);
        return 1;
    }
    HttpResponse resp;
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "minicut-editor/1.0");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_string_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp.body);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, curl_header_write_cb);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &resp.headers);
    CURLcode res = curl_easy_perform(curl);
    if (res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp.status_code);
        resp.success = true;
    } else {
        resp.error = curl_easy_strerror(res);
    }
    curl_easy_cleanup(curl);
    push_js_http_response(ctx, resp);
    return 1;
}

duk_ret_t ScriptEngine::js_net_post(duk_context* ctx) {
    const char* url = duk_require_string(ctx, 0);
    const char* body = duk_is_string(ctx, 1) ? duk_get_string(ctx, 1) : "";
    if (!url) {
        duk_push_null(ctx);
        return 1;
    }
    CURL* curl = curl_easy_init();
    if (!curl) {
        duk_push_null(ctx);
        return 1;
    }
    HttpResponse resp;
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "minicut-editor/1.0");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_string_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp.body);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, curl_header_write_cb);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &resp.headers);
    CURLcode res = curl_easy_perform(curl);
    if (res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp.status_code);
        resp.success = true;
    } else {
        resp.error = curl_easy_strerror(res);
    }
    curl_easy_cleanup(curl);
    push_js_http_response(ctx, resp);
    return 1;
}

duk_ret_t ScriptEngine::js_net_download(duk_context* ctx) {
    const char* url = duk_require_string(ctx, 0);
    const char* dest = duk_require_string(ctx, 1);
    if (!url || !dest) {
        duk_push_boolean(ctx, false);
        return 1;
    }
    FILE* fp = fopen(dest, "wb");
    if (!fp) {
        duk_push_boolean(ctx, false);
        return 1;
    }
    CURL* curl = curl_easy_init();
    if (!curl) {
        fclose(fp);
        duk_push_boolean(ctx, false);
        return 1;
    }
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "minicut-editor/1.0");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_file_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    fclose(fp);
    duk_push_boolean(ctx, res == CURLE_OK);
    return 1;
}

duk_ret_t ScriptEngine::js_net_upload(duk_context* ctx) {
    const char* url = duk_require_string(ctx, 0);
    const char* file_path = duk_require_string(ctx, 1);
    if (!url || !file_path) {
        duk_push_null(ctx);
        return 1;
    }
    std::ifstream ifs(file_path, std::ios::binary);
    if (!ifs.is_open()) {
        duk_push_null(ctx);
        return 1;
    }
    std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    ifs.close();

    CURL* curl = curl_easy_init();
    if (!curl) {
        duk_push_null(ctx);
        return 1;
    }
    bool is_sftp = (std::string(url).rfind("sftp://", 0) == 0 || std::string(url).rfind("ftp://", 0) == 0);
    HttpResponse resp;
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "minicut-editor/1.0");

    FILE* up_fp = nullptr;
    if (is_sftp) {
        up_fp = std::fopen(file_path, "rb");
        if (!up_fp) {
            curl_easy_cleanup(curl);
            duk_push_null(ctx);
            return 1;
        }
        curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
        curl_easy_setopt(curl, CURLOPT_READDATA, up_fp);
        curl_off_t fsize = static_cast<curl_off_t>(fs::file_size(file_path));
        curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, fsize);
    } else {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, content.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(content.size()));
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_string_write_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp.body);
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, curl_header_write_cb);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &resp.headers);
    }

    CURLcode res = curl_easy_perform(curl);
    if (up_fp) std::fclose(up_fp);
    if (res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp.status_code);
        resp.success = true;
    } else {
        resp.error = curl_easy_strerror(res);
    }
    curl_easy_cleanup(curl);
    push_js_http_response(ctx, resp);
    return 1;
}

duk_ret_t ScriptEngine::js_net_request(duk_context* ctx) {
    if (!duk_is_object(ctx, 0)) {
        duk_push_null(ctx);
        return 1;
    }
    std::string url, method = "GET", body;
    long timeout = 15;
    duk_get_prop_string(ctx, 0, "url"); if (duk_is_string(ctx, -1)) url = duk_get_string(ctx, -1); duk_pop(ctx);
    duk_get_prop_string(ctx, 0, "method"); if (duk_is_string(ctx, -1)) method = duk_get_string(ctx, -1); duk_pop(ctx);
    duk_get_prop_string(ctx, 0, "body"); if (duk_is_string(ctx, -1)) body = duk_get_string(ctx, -1); duk_pop(ctx);
    duk_get_prop_string(ctx, 0, "timeout"); if (duk_is_number(ctx, -1)) timeout = duk_get_int(ctx, -1); duk_pop(ctx);

    if (url.empty()) {
        duk_push_null(ctx);
        return 1;
    }
    CURL* curl = curl_easy_init();
    if (!curl) {
        duk_push_null(ctx);
        return 1;
    }
    HttpResponse resp;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    if (method == "POST") {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    } else if (method == "PUT") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    } else if (method == "DELETE") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    }
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "minicut-editor/1.0");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_string_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp.body);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, curl_header_write_cb);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &resp.headers);
    CURLcode res = curl_easy_perform(curl);
    if (res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp.status_code);
        resp.success = true;
    } else {
        resp.error = curl_easy_strerror(res);
    }
    curl_easy_cleanup(curl);
    push_js_http_response(ctx, resp);
    return 1;
}

// ----------------------------------------------------------------------------
// JS API Registration
// ----------------------------------------------------------------------------

void ScriptEngine::register_js_api() {
    if (!m_ctx) return;

    auto reg_fn = [&](const char* name, duk_c_function fn, duk_idx_t nargs) {
        duk_push_c_function(m_ctx, fn, nargs);
        duk_put_prop_string(m_ctx, -2, name);
    };

    // 1. gui
    duk_push_object(m_ctx);
    reg_fn("text", js_gui_text, 1);
    reg_fn("text_colored", js_gui_text_colored, 5);
    reg_fn("text_disabled", js_gui_text_disabled, 1);
    reg_fn("button", js_gui_button, 2);
    reg_fn("small_button", js_gui_small_button, 2);
    reg_fn("slider_float", js_gui_slider_float, 5);
    reg_fn("slider_int", js_gui_slider_int, 5);
    reg_fn("drag_float", js_gui_drag_float, 6);
    reg_fn("drag_int", js_gui_drag_int, 6);
    reg_fn("input_text", js_gui_input_text, 3);
    reg_fn("checkbox", js_gui_checkbox, 3);
    reg_fn("same_line", js_gui_same_line, 0);
    reg_fn("separator", js_gui_separator, 0);
    reg_fn("spacing", js_gui_spacing, 0);
    reg_fn("tooltip", js_gui_tooltip, 1);
    reg_fn("set_next_item_width", js_gui_set_next_item_width, 1);
    reg_fn("file_dialog", js_gui_file_dialog, 1);
    reg_fn("native_file_dialog", js_gui_native_file_dialog, 1);
    reg_fn("builtin_file_dialog", js_gui_builtin_file_dialog, 1);
    reg_fn("popout_preview", js_gui_popout_preview, 1);
    reg_fn("is_preview_popped_out", js_gui_is_preview_popped_out, 0);
    reg_fn("set_preview_aspect_mode", js_gui_set_preview_aspect_mode, 1);
    reg_fn("get_preview_aspect_mode", js_gui_get_preview_aspect_mode, 0);
    reg_fn("set_transform_tool", js_gui_set_transform_tool, 1);
    reg_fn("get_transform_tool", js_gui_get_transform_tool, 0);
    duk_put_global_string(m_ctx, "gui");

    // 2. timeline
    duk_push_object(m_ctx);
    reg_fn("get_playhead", js_timeline_get_playhead, 0);
    reg_fn("set_playhead", js_timeline_set_playhead, 1);
    reg_fn("get_duration", js_timeline_get_duration, 0);
    reg_fn("get_track_count", js_timeline_get_track_count, 0);
    reg_fn("add_track", js_timeline_add_track, 2);
    reg_fn("place_video", js_timeline_place_video, 4);
    reg_fn("place_audio", js_timeline_place_audio, 4);
    reg_fn("place_image", js_timeline_place_image, 4);
    reg_fn("place_text_clip", js_timeline_place_text_clip, 4);
    reg_fn("get_selected_clip", js_timeline_get_selected_clip, 0);
    reg_fn("get_selected_clips", js_timeline_get_selected_clips, 0);
    reg_fn("select_clip", js_timeline_select_clip, 3);
    reg_fn("select_clips", js_timeline_select_clips, 1);
    reg_fn("clear_selection", js_timeline_clear_selection, 0);
    reg_fn("select_all", js_timeline_select_all, 0);
    reg_fn("delete_clip", js_timeline_delete_clip, 1);
    reg_fn("split_clip", js_timeline_split_clip, 2);
    reg_fn("move_track_up", js_timeline_move_track_up, 1);
    reg_fn("move_track_down", js_timeline_move_track_down, 1);
    reg_fn("move_track", js_timeline_move_track, 2);
    reg_fn("swap_tracks", js_timeline_swap_tracks, 2);
    reg_fn("set_track_name", js_timeline_set_track_name, 2);
    reg_fn("get_track_name", js_timeline_get_track_name, 1);
    reg_fn("add_keyframe", js_timeline_add_keyframe, 4);
    reg_fn("remove_keyframe", js_timeline_remove_keyframe, 3);
    reg_fn("clear_keyframes", js_timeline_clear_keyframes, 2);
    reg_fn("get_keyframes", js_timeline_get_keyframes, 2);
    reg_fn("evaluate_keyframe", js_timeline_evaluate_keyframe, 4);
    duk_put_global_string(m_ctx, "timeline");

    // 3. clip
    duk_push_object(m_ctx);
    reg_fn("get_position", js_clip_get_position, 1);
    reg_fn("set_position", js_clip_set_position, 3);
    reg_fn("get_scale", js_clip_get_scale, 1);
    reg_fn("get_scale_xy", js_clip_get_scale_xy, 1);
    reg_fn("get_scale_x", js_clip_get_scale_x, 1);
    reg_fn("get_scale_y", js_clip_get_scale_y, 1);
    reg_fn("set_scale", js_clip_set_scale, 3);
    reg_fn("set_scale_x", js_clip_set_scale_x, 2);
    reg_fn("set_scale_y", js_clip_set_scale_y, 2);
    reg_fn("get_rotation", js_clip_get_rotation, 1);
    reg_fn("set_rotation", js_clip_set_rotation, 2);
    reg_fn("get_opacity", js_clip_get_opacity, 1);
    reg_fn("set_opacity", js_clip_set_opacity, 2);
    reg_fn("get_volume", js_clip_get_volume, 1);
    reg_fn("set_volume", js_clip_set_volume, 2);
    reg_fn("get_time", js_clip_get_time, 1);
    reg_fn("set_time", js_clip_set_time, 4);
    reg_fn("get_info", js_clip_get_info, 1);
    reg_fn("get_transform", js_clip_get_transform, 1);
    reg_fn("set_transform", js_clip_set_transform, DUK_VARARGS);
    reg_fn("reset_transform", js_clip_reset_transform, 1);
    // SFX Frame manipulation on clip
    reg_fn("get_frame", js_clip_get_frame, DUK_VARARGS);
    reg_fn("set_frame", js_clip_set_frame, DUK_VARARGS);
    reg_fn("create_from_frame", js_clip_create_from_frame, DUK_VARARGS);
    reg_fn("get_selected_clip", js_timeline_get_selected_clip, 0);
    reg_fn("get_selected_id", js_timeline_get_selected_clip, 0);
    duk_put_global_string(m_ctx, "clip");

    // 4. project
    duk_push_object(m_ctx);
    reg_fn("save", js_project_save, 1);
    reg_fn("load", js_project_load, 1);
    reg_fn("import_video", js_project_import_video, 1);
    reg_fn("import_audio", js_project_import_audio, 1);
    reg_fn("import_image", js_project_import_image, 1);
    reg_fn("create_text_asset", js_assets_create_text, 2);
    reg_fn("get_name", js_project_get_name, 0);
    reg_fn("set_name", js_project_set_name, 1);
    duk_put_global_string(m_ctx, "project");

    duk_push_c_function(m_ctx, js_project_save, 1);
    duk_put_global_string(m_ctx, "project_save");
    duk_push_c_function(m_ctx, js_project_load, 1);
    duk_put_global_string(m_ctx, "project_load");

    // 5. assets
    duk_push_object(m_ctx);
    reg_fn("create_text", js_assets_create_text, 2);
    reg_fn("get_scale", js_assets_get_scale, 1);
    reg_fn("get_scale_x", js_assets_get_scale_x, 1);
    reg_fn("get_scale_y", js_assets_get_scale_y, 1);
    reg_fn("set_scale", js_assets_set_scale, 3);
    reg_fn("set_scale_x", js_assets_set_scale_x, 2);
    reg_fn("set_scale_y", js_assets_set_scale_y, 2);
    duk_put_global_string(m_ctx, "assets");

    // 6. minicut
    duk_push_object(m_ctx);
    reg_fn("log", js_minicut_log, DUK_VARARGS);
    reg_fn("set_status", js_minicut_set_status, 2);
    reg_fn("push_undo", js_minicut_push_undo, 0);
    reg_fn("get_selected_clip", js_timeline_get_selected_clip, 0);
    reg_fn("get_selected_id", js_timeline_get_selected_clip, 0);
    duk_put_global_string(m_ctx, "minicut");

    duk_push_object(m_ctx);
    reg_fn("log", js_minicut_log, DUK_VARARGS);
    duk_put_global_string(m_ctx, "console");
    duk_push_c_function(m_ctx, js_minicut_log, DUK_VARARGS);
    duk_put_global_string(m_ctx, "print");

    // 7. net
    duk_push_object(m_ctx);
    reg_fn("get", js_net_get, 1);
    reg_fn("post", js_net_post, 2);
    reg_fn("download", js_net_download, 2);
    reg_fn("upload", js_net_upload, 2);
    reg_fn("request", js_net_request, 1);
    duk_put_global_string(m_ctx, "net");

    // 8. export (also alias as exporter & minicut.export for reserved keyword safety in JS)
    duk_push_object(m_ctx);
    reg_fn("get_audio_progress", js_export_get_audio_progress, 0);
    reg_fn("get_progress", js_export_get_progress, 0);
    reg_fn("is_exporting", js_export_is_exporting, 0);
    reg_fn("get_status", js_export_get_status, 0);
    duk_dup_top(m_ctx);
    duk_put_global_string(m_ctx, "export");
    duk_dup_top(m_ctx);
    duk_put_global_string(m_ctx, "exporter");
    duk_get_global_string(m_ctx, "minicut");
    if (duk_is_object(m_ctx, -1)) {
        duk_dup(m_ctx, -2);
        duk_put_prop_string(m_ctx, -2, "export");
    }
    duk_pop(m_ctx);
    duk_pop(m_ctx);

    // 9. locale (callable function + properties)
    duk_push_c_function(m_ctx, js_locale_get, 2);
    reg_fn("get", js_locale_get, 2);
    reg_fn("tr", js_locale_get, 2);
    reg_fn("get_language", js_locale_get_language, 0);
    reg_fn("set_language", js_locale_set_language, 1);
    reg_fn("is_rtl", js_locale_is_rtl, 1);
    reg_fn("register", js_locale_register, 2);
    reg_fn("add", js_locale_register, 2);
    duk_dup_top(m_ctx);
    duk_put_global_string(m_ctx, "locale");

    duk_push_c_function(m_ctx, js_locale_get, 2);
    duk_put_global_string(m_ctx, "tr");

    duk_get_global_string(m_ctx, "minicut");
    if (duk_is_object(m_ctx, -1)) {
        duk_dup(m_ctx, -2);
        duk_put_prop_string(m_ctx, -2, "locale");
    }
    duk_pop(m_ctx); // pop minicut
    duk_pop(m_ctx); // pop locale

    // 10. ai Library
    duk_push_object(m_ctx);
    reg_fn("is_available", js_ai_is_available, 0);
    reg_fn("is_enabled", js_ai_is_enabled, 0);
    reg_fn("set_enabled", js_ai_set_enabled, 1);
    reg_fn("has_api_key", js_ai_has_api_key, 0);
    reg_fn("set_api_key", js_ai_set_api_key, 1);
    reg_fn("is_busy", js_ai_is_busy, 0);
    reg_fn("get_status", js_ai_get_status, 0);
    reg_fn("set_mock_response", js_ai_set_mock_response, 1);
    reg_fn("clear_mock_response", js_ai_clear_mock_response, 0);
    reg_fn("prompt", js_ai_prompt, 2);
    reg_fn("prompt_json", js_ai_prompt_json, 2);
    reg_fn("parse_json", js_ai_parse_json, 1);
    reg_fn("to_json", js_ai_to_json, 2);
    reg_fn("struct", js_ai_struct, 2);
    duk_dup_top(m_ctx);
    duk_put_global_string(m_ctx, "ai");

    duk_get_global_string(m_ctx, "minicut");
    if (duk_is_object(m_ctx, -1)) {
        duk_dup(m_ctx, -2);
        duk_put_prop_string(m_ctx, -2, "ai");
    }
    duk_pop(m_ctx); // pop minicut
    duk_pop(m_ctx); // pop ai

    // 11. shader
    duk_push_object(m_ctx);
    reg_fn("create", js_shader_create, 1);
    reg_fn("set_uniform", js_shader_set_uniform, 3);
    reg_fn("apply", js_shader_apply, DUK_VARARGS);
    duk_put_global_string(m_ctx, "shader");

    // 12. sfx
    duk_push_object(m_ctx);
    reg_fn("get_frame", js_clip_get_frame, DUK_VARARGS);
    reg_fn("apply_shader", js_shader_apply, DUK_VARARGS);
    reg_fn("reinsert_frame", js_clip_set_frame, DUK_VARARGS);
    reg_fn("create_clip", js_clip_create_from_frame, DUK_VARARGS);
    reg_fn("create_shader", js_shader_create, 1);
    duk_put_global_string(m_ctx, "sfx");

    // 13. default plugin object
    duk_push_object(m_ctx);
    duk_push_string(m_ctx, "default"); duk_put_prop_string(m_ctx, -2, "id");
    duk_push_string(m_ctx, "Default Plugin"); duk_put_prop_string(m_ctx, -2, "title");
    duk_put_global_string(m_ctx, "plugin");
}


