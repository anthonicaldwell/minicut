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
#include "imgui.h"
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
                item.id = entry.path().stem().string();
                item.filename = entry.path().filename().string();
                item.path = entry.path().string();
                item.title = format_title_from_stem(item.id);
                item.description = "Autorun script: " + item.filename;
                item.is_autorun = true;
                item.is_loaded = false;
                item.is_panel_open = false;
                item.is_javascript = (ext == ".js");
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
                item.id = entry.path().stem().string();
                item.filename = entry.path().filename().string();
                item.path = entry.path().string();
                item.title = format_title_from_stem(item.id);
                item.description = "Script: " + item.filename;
                item.is_autorun = false;
                item.is_loaded = false;
                item.is_panel_open = false;
                item.is_javascript = (ext == ".js");
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
    for (auto& item : m_autorun_scripts) {
        std::vector<std::string> candidates = {
            m_icons_dir + "/" + item.id + ".png",
            m_icons_dir + "/" + item.id + ".jpg",
            m_icons_dir + "/" + item.id + ".jpeg"
        };
        for (const auto& icon_path : candidates) {
            if (fs::exists(icon_path, ec)) {
                GLuint tex = MediaDecoder::create_thumbnail_texture(icon_path, 0.0);
                if (tex != 0) {
                    item.icon_texture = tex;
                    break;
                }
            }
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
                             ImGuiWindowFlags_NoSavedSettings;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 6.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 4.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.13f, 0.14f, 0.16f, 1.0f));

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
            if (ImGui::MenuItem(disp_title.c_str(), nullptr, script.is_panel_open)) {
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
                if (ImGui::MenuItem(disp_title.c_str(), nullptr, script.is_panel_open)) {
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
    lua_pushcfunction(m_L, lua_clip_set_scale);    lua_setfield(m_L, -2, "set_scale");
    lua_pushcfunction(m_L, lua_clip_get_rotation); lua_setfield(m_L, -2, "get_rotation");
    lua_pushcfunction(m_L, lua_clip_set_rotation); lua_setfield(m_L, -2, "set_rotation");
    lua_pushcfunction(m_L, lua_clip_get_opacity);  lua_setfield(m_L, -2, "get_opacity");
    lua_pushcfunction(m_L, lua_clip_set_opacity);  lua_setfield(m_L, -2, "set_opacity");
    lua_pushcfunction(m_L, lua_clip_get_volume);   lua_setfield(m_L, -2, "get_volume");
    lua_pushcfunction(m_L, lua_clip_set_volume);   lua_setfield(m_L, -2, "set_volume");
    lua_pushcfunction(m_L, lua_clip_get_time);     lua_setfield(m_L, -2, "get_time");
    lua_pushcfunction(m_L, lua_clip_set_time);     lua_setfield(m_L, -2, "set_time");
    lua_pushcfunction(m_L, lua_clip_get_info);     lua_setfield(m_L, -2, "get_info");
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
    lua_setglobal(m_L, "assets");

    // 6. `minicut` Table
    lua_newtable(m_L);
    lua_pushcfunction(m_L, lua_minicut_log);        lua_setfield(m_L, -2, "log");
    lua_pushcfunction(m_L, lua_minicut_set_status); lua_setfield(m_L, -2, "set_status");
    lua_pushcfunction(m_L, lua_minicut_push_undo);  lua_setfield(m_L, -2, "push_undo");
    lua_setglobal(m_L, "minicut");

    // 7. `net` Table (HTTP, download, upload)
    lua_newtable(m_L);
    lua_pushcfunction(m_L, lua_net_get);      lua_setfield(m_L, -2, "get");
    lua_pushcfunction(m_L, lua_net_post);     lua_setfield(m_L, -2, "post");
    lua_pushcfunction(m_L, lua_net_download); lua_setfield(m_L, -2, "download");
    lua_pushcfunction(m_L, lua_net_upload);   lua_setfield(m_L, -2, "upload");
    lua_pushcfunction(m_L, lua_net_request);  lua_setfield(m_L, -2, "request");
    lua_setglobal(m_L, "net");

    // 8. Global default `plugin` Table
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
        return 1;
    }
    const char* clip_id = luaL_checkstring(L, 1);
    const Clip* c = s_engine_instance->m_project->find_clip(clip_id);
    lua_pushnumber(L, c ? c->scale : 1.0);
    return 1;
}

int ScriptEngine::lua_clip_set_scale(lua_State* L) {
    if (!s_engine_instance || !s_engine_instance->m_project) return 0;
    const char* clip_id = luaL_checkstring(L, 1);
    double s = luaL_checknumber(L, 2);

    Clip* c = s_engine_instance->m_project->find_clip_mut(clip_id);
    if (c) {
        c->scale = std::max(0.01, s);
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
    lua_pushnumber(L, c->rotation);          lua_setfield(L, -2, "rotation");
    lua_pushnumber(L, c->opacity);           lua_setfield(L, -2, "opacity");
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

    if (http_code >= 400) {
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

    curl_mime* mime = curl_mime_init(curl);
    curl_mimepart* part = curl_mime_addpart(mime);
    curl_mime_name(part, field_name);
    curl_mime_filedata(part, filepath);

    for (const auto& kv : extra_fields) {
        curl_mimepart* p = curl_mime_addpart(mime);
        curl_mime_name(p, kv.first.c_str());
        curl_mime_data(p, kv.second.c_str(), CURL_ZERO_TERMINATED);
    }

    HttpResponse resp;
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);
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

    curl_mime_free(mime);
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
        if (c) s = c->scale;
    }
    duk_push_number(ctx, s);
    return 1;
}

duk_ret_t ScriptEngine::js_clip_set_scale(duk_context* ctx) {
    const char* id = duk_get_string(ctx, 0);
    double s = duk_get_number(ctx, 1);
    if (s_engine_instance && s_engine_instance->m_project && id) {
        Clip* c = s_engine_instance->m_project->find_clip_mut(id);
        if (c) {
            s_engine_instance->push_undo_state();
            c->scale = std::max(0.01, s);
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
            duk_push_number(ctx, c->rotation); duk_put_prop_string(ctx, -2, "rotation");
            duk_push_number(ctx, c->opacity); duk_put_prop_string(ctx, -2, "opacity");
            double vol = s_engine_instance->m_project->evaluate_keyframe(c->id, "volume", 0.0, 1.0);
            duk_push_number(ctx, vol); duk_put_prop_string(ctx, -2, "volume");
        }
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
    HttpResponse resp;
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, content.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(content.size()));
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
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
    reg_fn("set_scale", js_clip_set_scale, 3);
    reg_fn("get_rotation", js_clip_get_rotation, 1);
    reg_fn("set_rotation", js_clip_set_rotation, 2);
    reg_fn("get_opacity", js_clip_get_opacity, 1);
    reg_fn("set_opacity", js_clip_set_opacity, 2);
    reg_fn("get_volume", js_clip_get_volume, 1);
    reg_fn("set_volume", js_clip_set_volume, 2);
    reg_fn("get_time", js_clip_get_time, 1);
    reg_fn("set_time", js_clip_set_time, 4);
    reg_fn("get_info", js_clip_get_info, 1);
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
    duk_put_global_string(m_ctx, "assets");

    // 6. minicut
    duk_push_object(m_ctx);
    reg_fn("log", js_minicut_log, DUK_VARARGS);
    reg_fn("set_status", js_minicut_set_status, 2);
    reg_fn("push_undo", js_minicut_push_undo, 0);
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

    // 8. default plugin object
    duk_push_object(m_ctx);
    duk_push_string(m_ctx, "default"); duk_put_prop_string(m_ctx, -2, "id");
    duk_push_string(m_ctx, "Default Plugin"); duk_put_prop_string(m_ctx, -2, "title");
    duk_put_global_string(m_ctx, "plugin");
}


