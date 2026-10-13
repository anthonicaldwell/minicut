#include "ScriptEngine.h"
#include "Common.h"
#include "Timeline.h"
#include "ClipManager.h"
#include "MediaDecoder.h"
#include "imgui.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cstdlib>
#include <cstring>

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
                        std::function<void(const std::string&)> on_select_clip) {
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

    s_engine_instance = this;

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
            if (entry.is_regular_file() && entry.path().extension() == ".lua") {
                ScriptItem item;
                item.id = entry.path().stem().string();
                item.filename = entry.path().filename().string();
                item.path = entry.path().string();
                item.title = format_title_from_stem(item.id);
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
            if (entry.is_regular_file() && entry.path().extension() == ".lua") {
                ScriptItem item;
                item.id = entry.path().stem().string();
                item.filename = entry.path().filename().string();
                item.path = entry.path().string();
                item.title = format_title_from_stem(item.id);
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

void ScriptEngine::render_maya_shelf() {
    if (!m_shelf_visible) return;

    ImGuiIO& io = ImGui::GetIO();
    float menu_h = ImGui::GetFrameHeight();
    float total_w = io.DisplaySize.x;

    ImGui::SetNextWindowPos(ImVec2(0, menu_h), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(total_w, m_shelf_height), ImGuiCond_Always);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
                             ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_MenuBar;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 4));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.12f, 0.13f, 0.15f, 1.0f));

    if (ImGui::Begin("##MayaShelfDock", nullptr, flags)) {
        // Shelf Header Label (Maya style tab)
        ImGui::TextColored(ImVec4(0.24f, 0.68f, 0.91f, 1.0f), "SHELF");
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();

        if (m_autorun_scripts.empty()) {
            ImGui::TextDisabled("No autorun scripts in ~/.config/minicut/autorun/");
        } else {
            for (auto& script : m_autorun_scripts) {
                ImGui::PushID(script.id.c_str());

                bool clicked = false;
                if (script.icon_texture != 0) {
                    ImVec4 bg_col = script.is_panel_open ? ImVec4(0.24f, 0.68f, 0.91f, 0.4f) : ImVec4(0, 0, 0, 0);
                    clicked = ImGui::ImageButton(("##btn_" + script.id).c_str(), (ImTextureID)(intptr_t)script.icon_texture, ImVec2(22, 22), ImVec2(0,0), ImVec2(1,1), bg_col);
                } else {
                    // Maya style initial abbreviation
                    std::string abbr = script.title.substr(0, 2);
                    if (abbr.empty()) abbr = "FX";
                    std::transform(abbr.begin(), abbr.end(), abbr.begin(), ::toupper);
                    if (script.is_panel_open) {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.24f, 0.68f, 0.91f, 0.6f));
                    }
                    clicked = ImGui::Button(abbr.c_str(), ImVec2(28, 24));
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
        float right_w = 80.0f;
        if (ImGui::GetContentRegionAvail().x > right_w) {
            ImGui::SetCursorPosX(total_w - right_w);
            if (ImGui::SmallButton("⟳ Reload")) {
                reload_scripts();
                set_status("Scripts and Maya shelf reloaded");
            }
        }

        ImGui::End();
    }

    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

void ScriptEngine::render_scripts_menu() {
    if (m_menu_scripts.empty()) {
        ImGui::TextDisabled("No scripts in ~/.config/minicut/scripts/");
    } else {
        for (auto& script : m_menu_scripts) {
            if (ImGui::MenuItem(script.title.c_str(), nullptr, script.is_panel_open)) {
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
        if (ImGui::BeginMenu("Autorun Scripts (Shelf)")) {
            for (auto& script : m_autorun_scripts) {
                if (ImGui::MenuItem(script.title.c_str(), nullptr, script.is_panel_open)) {
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

    if (ImGui::MenuItem("Reload All Scripts", "F5")) {
        reload_scripts();
        set_status("Reloaded all scripts");
    }

    if (ImGui::BeginMenu("Directories")) {
        ImGui::TextDisabled("Scripts: %s", m_scripts_dir.c_str());
        ImGui::TextDisabled("Autorun: %s", m_autorun_dir.c_str());
        ImGui::TextDisabled("Icons:   %s", m_icons_dir.c_str());
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
    lua_pushcfunction(m_L, lua_timeline_get_selected_clip); lua_setfield(m_L, -2, "get_selected_clip");
    lua_pushcfunction(m_L, lua_timeline_select_clip);     lua_setfield(m_L, -2, "select_clip");
    lua_pushcfunction(m_L, lua_timeline_delete_clip);     lua_setfield(m_L, -2, "delete_clip");
    lua_pushcfunction(m_L, lua_timeline_split_clip);      lua_setfield(m_L, -2, "split_clip");
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
    lua_pushcfunction(m_L, lua_project_import_video); lua_setfield(m_L, -2, "import_video");
    lua_pushcfunction(m_L, lua_project_import_audio); lua_setfield(m_L, -2, "import_audio");
    lua_pushcfunction(m_L, lua_project_import_image); lua_setfield(m_L, -2, "import_image");
    lua_pushcfunction(m_L, lua_project_get_name);     lua_setfield(m_L, -2, "get_name");
    lua_pushcfunction(m_L, lua_project_set_name);     lua_setfield(m_L, -2, "set_name");
    lua_setglobal(m_L, "project");

    // 5. `minicut` Table
    lua_newtable(m_L);
    lua_pushcfunction(m_L, lua_minicut_log);        lua_setfield(m_L, -2, "log");
    lua_pushcfunction(m_L, lua_minicut_set_status); lua_setfield(m_L, -2, "set_status");
    lua_pushcfunction(m_L, lua_minicut_push_undo);  lua_setfield(m_L, -2, "push_undo");
    lua_setglobal(m_L, "minicut");

    // 6. Global default `plugin` Table
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
