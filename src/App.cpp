#include "App.h"
#include "LocaleManager.h"
#include "TempManager.h"
#include <imgui.h>
#include <backends/imgui_impl_sdl2.h>
#include <backends/imgui_impl_opengl3.h>
#include <nlohmann/json.hpp>
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

App::App() {
    m_clip_manager.set_audio_engine(&m_audio_engine);
    m_export_manager.set_browse_callback([this]() {
        export_path_browser();
    });
}

App::~App() {
    cleanup();
}

bool App::init(int argc, char* argv[]) {
    SDL_SetHint(SDL_HINT_APP_NAME, "minicut");

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
        std::cerr << "SDL_Init Error: " << SDL_GetError() << std::endl;
        return false;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    m_window = SDL_CreateWindow(
        "minicut - Multi-Track Video Editor",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        m_window_width, m_window_height,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI
    );
    if (!m_window) {
        std::cerr << "Failed to create window: " << SDL_GetError() << std::endl;
        return false;
    }

    // Set Window Icon
    {
        DecodedVideoContext icon_ctx;
        const char* home = std::getenv("HOME");
        std::string user_icon = home ? (std::string(home) + "/.local/share/icons/hicolor/256x256/apps/minicut.png") : "";
        std::vector<std::string> icon_candidates = {
            "minicut.png",
            "../minicut.png",
            user_icon,
            "/usr/local/share/icons/hicolor/256x256/apps/minicut.png",
            "/usr/share/icons/hicolor/256x256/apps/minicut.png"
        };
        for (const auto& icon_path : icon_candidates) {
            if (!icon_path.empty() && fs::exists(icon_path) && icon_ctx.open(icon_path)) {
                std::vector<uint8_t> icon_rgba;
                if (icon_ctx.decode_frame_at(0.0, 64, 64, icon_rgba)) {
                    SDL_Surface* icon_surf = SDL_CreateRGBSurfaceFrom(
                        icon_rgba.data(), 64, 64, 32, 64 * 4,
                        0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000
                    );
                    if (icon_surf) {
                        SDL_SetWindowIcon(m_window, icon_surf);
                        SDL_FreeSurface(icon_surf);
                    }
                }
                break;
            }
        }
    }

    m_gl_context = SDL_GL_CreateContext(m_window);
    if (!m_gl_context) {
        std::cerr << "Failed to create OpenGL context: " << SDL_GetError() << std::endl;
        return false;
    }
    SDL_GL_MakeCurrent(m_window, m_gl_context);
    SDL_GL_SetSwapInterval(1); // Enable VSync

    glewExperimental = GL_TRUE;
    GLenum err = glewInit();
    if (err != GLEW_OK) {
        std::cerr << "GLEW Init Error: " << glewGetErrorString(err) << std::endl;
        return false;
    }

    // Init ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Load persisted settings (language, dialog preference, view section split ratios)
    load_settings();

    // Multi-Language Font Loading: Latin, Cyrillic (Ukrainian), Japanese, Korean, Arabic
    std::vector<std::string> base_candidates = {
        "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf"
    };
    std::vector<std::string> cjk_candidates = {
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/opentype/noto/NotoSerifCJK-Regular.ttc",
        "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf"
    };
    std::vector<std::string> arabic_candidates = {
        "/usr/share/fonts/truetype/noto/NotoSansArabicUI-Regular.ttf",
        "/usr/share/fonts/truetype/noto/NotoSansArabic-Regular.ttf",
        "/usr/share/fonts/truetype/noto/NotoSansArabic-Bold.ttf",
        "/usr/share/fonts/truetype/noto/NotoNaskhArabicUI-Regular.ttf"
    };

    std::string base_font, cjk_font, arabic_font;
    for (const auto& f : base_candidates) { if (fs::exists(f)) { base_font = f; break; } }
    for (const auto& f : cjk_candidates) { if (fs::exists(f)) { cjk_font = f; break; } }
    for (const auto& f : arabic_candidates) { if (fs::exists(f)) { arabic_font = f; break; } }

    if (!base_font.empty()) {
        ImFontConfig cfg;
        cfg.OversampleH = 1;
        cfg.OversampleV = 1;
        // 1. Base font with Latin and Cyrillic (Ukrainian)
        io.Fonts->AddFontFromFileTTF(base_font.c_str(), 15.0f, &cfg, io.Fonts->GetGlyphRangesCyrillic());

        // 2. Merge CJK font for Japanese and Korean
        if (!cjk_font.empty()) {
            cfg.MergeMode = true;
            io.Fonts->AddFontFromFileTTF(cjk_font.c_str(), 15.0f, &cfg, io.Fonts->GetGlyphRangesJapanese());
            io.Fonts->AddFontFromFileTTF(cjk_font.c_str(), 15.0f, &cfg, io.Fonts->GetGlyphRangesKorean());
        }

        // 3. Merge Arabic font
        if (!arabic_font.empty()) {
            cfg.MergeMode = true;
            static const ImWchar arabic_ranges[] = {
                0x0600, 0x06FF, // Arabic
                0x0750, 0x077F, // Arabic Supplement
                0xFB50, 0xFDFF, // Arabic Presentation Forms-A
                0xFE70, 0xFEFF, // Arabic Presentation Forms-B
                0,
            };
            io.Fonts->AddFontFromFileTTF(arabic_font.c_str(), 15.0f, &cfg, arabic_ranges);
        }
    } else {
        io.Fonts->AddFontDefault();
    }

    apply_kde_plasma_theme();

    ImGui_ImplSDL2_InitForOpenGL(m_window, m_gl_context);
    ImGui_ImplOpenGL3_Init("#version 330");

    // Init Preview Texture
    glGenTextures(1, &m_preview_texture);
    glBindTexture(GL_TEXTURE_2D, m_preview_texture);
    std::vector<uint8_t> blank(1280 * 720 * 4, 20);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1280, 720, 0, GL_RGBA, GL_UNSIGNED_BYTE, blank.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // Init Audio Engine
    m_audio_engine.init();

    // Default tracks in project
    new_project();

    // If an initial file was passed via command line (e.g. desktop file association)
    if (argc > 1 && argv[1] != nullptr) {
        std::string initial_path = argv[1];
        if (!initial_path.empty() && fs::exists(initial_path)) {
            if (initial_path.size() >= 8 && initial_path.substr(initial_path.size() - 8) == ".minicut") {
                open_project(initial_path);
            } else {
                std::string ext = initial_path.substr(initial_path.find_last_of("."));
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".webp") {
                    m_clip_manager.import_image(m_project, initial_path);
                } else if (ext == ".mp3" || ext == ".wav" || ext == ".aac" || ext == ".ogg" || ext == ".flac") {
                    m_clip_manager.import_audio(m_project, initial_path);
                } else {
                    m_clip_manager.import_video(m_project, initial_path);
                }
            }
        }
    }

    // Init Lua Script Engine & Tool Shelf Dock
    m_script_engine.init(
        &m_project,
        &m_timeline,
        &m_clip_manager,
        &m_playhead_time,
        &m_selected_clip_id,
        [this]() { push_undo_state(); },
        [this]() { on_project_modified(); },
        [this](double t) { set_playhead_time(t); },
        [this](const std::string& msg, double d) { set_status_notification(msg, d); },
        [this](const std::string& id) { set_selected_clip_id(id); },
        [this](const std::string& path) -> bool {
            std::string target = path.empty() ? m_project.file_path : path;
            if (target.empty()) target = "untitled.minicut";
            save_project(target);
            set_status_notification("Project saved: " + target);
            return true;
        },
        [this](const std::string& path) -> bool {
            if (path.empty()) return false;
            open_project(path);
            set_status_notification("Project loaded: " + path);
            return true;
        }
    );

    return true;
}

void App::apply_kde_plasma_theme() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    // KDE Breeze Dark Color Scheme
    colors[ImGuiCol_Text]                  = ImVec4(0.93f, 0.94f, 0.95f, 1.00f);
    colors[ImGuiCol_TextDisabled]          = ImVec4(0.55f, 0.58f, 0.62f, 1.00f);
    colors[ImGuiCol_WindowBg]              = ImVec4(0.14f, 0.15f, 0.16f, 1.00f); // #232629
    colors[ImGuiCol_ChildBg]               = ImVec4(0.19f, 0.21f, 0.23f, 1.00f); // #31363b
    colors[ImGuiCol_PopupBg]               = ImVec4(0.19f, 0.21f, 0.23f, 0.98f);
    colors[ImGuiCol_Border]                = ImVec4(0.28f, 0.30f, 0.34f, 1.00f); // #474d56
    colors[ImGuiCol_BorderShadow]          = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg]               = ImVec4(0.16f, 0.18f, 0.20f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]        = ImVec4(0.24f, 0.68f, 0.91f, 0.35f); // Breeze Blue Hover
    colors[ImGuiCol_FrameBgActive]         = ImVec4(0.24f, 0.68f, 0.91f, 0.60f);
    colors[ImGuiCol_TitleBg]               = ImVec4(0.14f, 0.15f, 0.16f, 1.00f);
    colors[ImGuiCol_TitleBgActive]         = ImVec4(0.19f, 0.21f, 0.23f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed]      = ImVec4(0.14f, 0.15f, 0.16f, 0.75f);
    colors[ImGuiCol_MenuBarBg]             = ImVec4(0.16f, 0.18f, 0.20f, 1.00f);
    colors[ImGuiCol_ScrollbarBg]           = ImVec4(0.14f, 0.15f, 0.16f, 0.60f);
    colors[ImGuiCol_ScrollbarGrab]         = ImVec4(0.28f, 0.30f, 0.34f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.24f, 0.68f, 0.91f, 0.70f);
    colors[ImGuiCol_ScrollbarGrabActive]   = ImVec4(0.24f, 0.68f, 0.91f, 1.00f);
    colors[ImGuiCol_CheckMark]             = ImVec4(0.24f, 0.68f, 0.91f, 1.00f); // #3daee9
    colors[ImGuiCol_SliderGrab]            = ImVec4(0.24f, 0.68f, 0.91f, 1.00f);
    colors[ImGuiCol_SliderGrabActive]      = ImVec4(0.35f, 0.75f, 0.95f, 1.00f);
    colors[ImGuiCol_Button]                = ImVec4(0.22f, 0.24f, 0.26f, 1.00f);
    colors[ImGuiCol_ButtonHovered]         = ImVec4(0.24f, 0.68f, 0.91f, 0.70f);
    colors[ImGuiCol_ButtonActive]          = ImVec4(0.24f, 0.68f, 0.91f, 1.00f);
    colors[ImGuiCol_Header]                = ImVec4(0.24f, 0.68f, 0.91f, 0.35f);
    colors[ImGuiCol_HeaderHovered]         = ImVec4(0.24f, 0.68f, 0.91f, 0.65f);
    colors[ImGuiCol_HeaderActive]          = ImVec4(0.24f, 0.68f, 0.91f, 0.90f);
    colors[ImGuiCol_Separator]             = ImVec4(0.28f, 0.30f, 0.34f, 1.00f);
    colors[ImGuiCol_SeparatorHovered]      = ImVec4(0.24f, 0.68f, 0.91f, 0.78f);
    colors[ImGuiCol_SeparatorActive]       = ImVec4(0.24f, 0.68f, 0.91f, 1.00f);
    colors[ImGuiCol_ResizeGrip]            = ImVec4(0.28f, 0.30f, 0.34f, 0.50f);
    colors[ImGuiCol_ResizeGripHovered]     = ImVec4(0.24f, 0.68f, 0.91f, 0.70f);
    colors[ImGuiCol_ResizeGripActive]      = ImVec4(0.24f, 0.68f, 0.91f, 1.00f);
    colors[ImGuiCol_Tab]                   = ImVec4(0.18f, 0.20f, 0.22f, 1.00f);
    colors[ImGuiCol_TabHovered]            = ImVec4(0.24f, 0.68f, 0.91f, 0.70f);
    colors[ImGuiCol_TabActive]             = ImVec4(0.24f, 0.68f, 0.91f, 1.00f);
    colors[ImGuiCol_TabUnfocused]          = ImVec4(0.15f, 0.16f, 0.18f, 1.00f);
    colors[ImGuiCol_TabUnfocusedActive]    = ImVec4(0.20f, 0.22f, 0.24f, 1.00f);

    style.WindowRounding    = 4.0f;
    style.ChildRounding     = 4.0f;
    style.FrameRounding     = 3.0f;
    style.PopupRounding     = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.GrabRounding      = 3.0f;
    style.TabRounding       = 3.0f;
    style.WindowBorderSize  = 1.0f;
    style.FrameBorderSize   = 1.0f;
    style.ItemSpacing       = ImVec2(8.0f, 5.0f);
    style.FramePadding      = ImVec2(6.0f, 4.0f);
}

void App::set_playhead_time(double t) {
    m_playhead_time = std::max(0.0, t);
    m_audio_engine.set_playhead_time(m_playhead_time);
    m_media_decoder.request_frame(m_playhead_time, m_project);
}

void App::on_project_modified() {
    m_audio_engine.update_project_snapshot(m_project);
    m_media_decoder.request_frame(m_playhead_time, m_project);
}

void App::set_selected_clip_id(const std::string& id) {
    m_selected_clip_id = id;
    m_keyframe_editor.set_selected_clip(id);
}

void App::set_status_notification(const std::string& msg, double duration) {
    m_status_notification = msg;
    m_status_notification_timer = duration;
}

void App::push_undo_state() {
    m_undo_stack.push_back(m_project);
    m_redo_stack.clear();
    if (m_undo_stack.size() > 50) {
        m_undo_stack.erase(m_undo_stack.begin());
    }
}

void App::undo() {
    if (!m_undo_stack.empty()) {
        m_redo_stack.push_back(m_project);
        m_project = m_undo_stack.back();
        m_undo_stack.pop_back();
        m_audio_engine.update_project_snapshot(m_project);
        m_media_decoder.request_frame(m_playhead_time, m_project);
    }
}

void App::redo() {
    if (!m_redo_stack.empty()) {
        m_undo_stack.push_back(m_project);
        m_project = m_redo_stack.back();
        m_redo_stack.pop_back();
        m_audio_engine.update_project_snapshot(m_project);
        m_media_decoder.request_frame(m_playhead_time, m_project);
    }
}

void App::new_project() {
    push_undo_state();
    m_project = Project();
    m_project.name = "Untitled Project";
    m_playhead_time = 0.0;
    m_selected_clip_id.clear();

    Track v1; v1.type = "video"; v1.name = "Video 1"; v1.id = "track_v1";
    Track v2; v2.type = "video"; v2.name = "Video 2"; v2.id = "track_v2";
    Track a1; a1.type = "audio"; a1.name = "Audio 1"; a1.id = "track_a1";
    Track a2; a2.type = "audio"; a2.name = "Audio 2"; a2.id = "track_a2";
    m_project.tracks.push_back(v1);
    m_project.tracks.push_back(v2);
    m_project.tracks.push_back(a1);
    m_project.tracks.push_back(a2);

    m_audio_engine.update_project_snapshot(m_project);
    m_media_decoder.request_frame(m_playhead_time, m_project);
}

void App::toggle_playback() {
    m_is_playing = !m_is_playing;
    if (m_is_playing) {
        m_audio_engine.set_playhead_time(m_playhead_time);
        m_audio_engine.update_project_snapshot(m_project);
        m_audio_engine.ensure_project_assets_loaded(m_project);
    }
    m_audio_engine.set_playing(m_is_playing);
}

void App::open_project(const std::string& path) {
    Project p;
    if (ProjectSerializer::load_from_file(p, path)) {
        push_undo_state();
        m_project = p;
        m_playhead_time = 0.0;
        m_selected_clip_id.clear();
        m_audio_engine.update_project_snapshot(m_project);
        m_audio_engine.ensure_project_assets_loaded(m_project);
        m_media_decoder.request_frame(m_playhead_time, m_project);
    }
}

void App::save_project(const std::string& path) {
    std::string target_path = path.empty() ? m_project.file_path : path;
    if (target_path.empty()) {
        save_project_as_browser();
        return;
    }
    ProjectSerializer::save_to_file(m_project, target_path);
}

void App::add_asset_to_timeline(const std::string& asset_id, bool is_video, double insert_time) {
    push_undo_state();
    std::string ts = std::to_string(std::chrono::system_clock::now().time_since_epoch().count() % 1000000);

    if (is_video) {
        const VideoAsset* va = m_project.find_video_asset(asset_id);
        if (!va) return;
        double dur = va->duration > 0.0 ? va->duration : 5.0;

        // 1. Put Video Clip on first video track
        Track* v_track = nullptr;
        for (auto& tr : m_project.tracks) {
            if (tr.type == "video") {
                v_track = &tr;
                break;
            }
        }
        if (!v_track) {
            Track vt;
            vt.type = "video";
            vt.name = "Video 1";
            vt.id = "track_v1";
            m_project.tracks.insert(m_project.tracks.begin(), vt);
            v_track = &m_project.tracks.front();
        }

        Clip vc;
        vc.id = "clip_v_" + ts;
        vc.asset_id = asset_id;
        vc.start_time = insert_time;
        vc.duration = dur;
        vc.offset = 0.0;
        v_track->clips.push_back(vc);
        m_selected_clip_id = vc.id;
        m_keyframe_editor.set_selected_clip(vc.id);

        // 2. If video contains audio and is not a static image, put separated audio on audio track!
        if (va->has_audio && !va->is_image) {
            Track* a_track = nullptr;
            for (auto& tr : m_project.tracks) {
                if (tr.type == "audio") {
                    a_track = &tr;
                    break;
                }
            }
            if (!a_track) {
                Track at;
                at.type = "audio";
                at.name = "Audio 1";
                at.id = "track_a" + std::to_string(m_project.tracks.size() + 1);
                m_project.tracks.push_back(at);
                a_track = &m_project.tracks.back();
            }

            Clip ac;
            ac.id = "clip_a_" + ts;
            ac.asset_id = asset_id;
            ac.start_time = insert_time;
            ac.duration = dur;
            ac.offset = 0.0;
            a_track->clips.push_back(ac);
        }
    } else {
        const AudioAsset* aa = m_project.find_audio_asset(asset_id);
        if (!aa) return;
        double dur = aa->duration > 0.0 ? aa->duration : 5.0;

        Track* a_track = nullptr;
        for (auto& tr : m_project.tracks) {
            if (tr.type == "audio") {
                a_track = &tr;
                break;
            }
        }
        if (!a_track) {
            Track at;
            at.type = "audio";
            at.name = "Audio 1";
            at.id = "track_a" + std::to_string(m_project.tracks.size() + 1);
            m_project.tracks.push_back(at);
            a_track = &m_project.tracks.back();
        }

        Clip ac;
        ac.id = "clip_a_" + ts;
        ac.asset_id = asset_id;
        ac.start_time = insert_time;
        ac.duration = dur;
        ac.offset = 0.0;
        a_track->clips.push_back(ac);
        m_selected_clip_id = ac.id;
        m_keyframe_editor.set_selected_clip(ac.id);
    }

    m_audio_engine.update_project_snapshot(m_project);
    m_audio_engine.ensure_project_assets_loaded(m_project);
    m_media_decoder.request_frame(m_playhead_time, m_project);
}

void App::open_project_browser() {
    if (m_use_system_file_dialog && NativeFileDialog::is_available()) {
        NativeFileDialog::open_file(
            "Open minicut Project",
            "",
            "minicut Projects (*.minicut)",
            "*.minicut",
            [this](const std::string& path) {
                open_project(path);
            }
        );
        return;
    }

    m_file_browser.open(
        "Open .minicut Project",
        FileBrowserMode::OpenFile,
        { { "minicut Projects (*.minicut)", { ".minicut" } }, { "All Files (*.*)", { ".*" } } },
        "",
        ".minicut",
        [this](const std::string& path) {
            open_project(path);
        }
    );
}

void App::save_project_as_browser() {
    if (m_use_system_file_dialog && NativeFileDialog::is_available()) {
        NativeFileDialog::save_file(
            "Save Project As",
            m_project.name + ".minicut",
            "minicut Projects (*.minicut)",
            "*.minicut",
            [this](const std::string& path) {
                save_project(path);
            }
        );
        return;
    }

    m_file_browser.open(
        "Save Project As",
        FileBrowserMode::SaveFile,
        { { "minicut Projects (*.minicut)", { ".minicut" } }, { "All Files (*.*)", { ".*" } } },
        m_project.name + ".minicut",
        ".minicut",
        [this](const std::string& path) {
            save_project(path);
        }
    );
}

void App::import_video_browser() {
    auto on_chosen = [this](const std::string& path) {
        push_undo_state();
        m_clip_manager.start_async_import_video(path, [this]() {
            m_audio_engine.update_project_snapshot(m_project);
            m_audio_engine.ensure_project_assets_loaded(m_project);
        });
    };

    if (m_use_system_file_dialog && NativeFileDialog::is_available()) {
        NativeFileDialog::open_file(
            "Import Video Clip",
            "",
            "Supported Video Files (*.mp4, *.mkv, *.mov, *.avi, *.webm)",
            "*.mp4 *.mkv *.mov *.avi *.webm",
            on_chosen
        );
        return;
    }

    m_file_browser.open(
        "Import Video Clip",
        FileBrowserMode::OpenFile,
        { { "Supported Video Files (*.mp4, *.mkv, *.mov, *.avi, *.webm)", { ".mp4", ".mkv", ".mov", ".avi", ".webm" } },
          { "All Files (*.*)", { ".*" } } },
        "",
        "",
        on_chosen
    );
}

void App::import_image_browser() {
    auto on_chosen = [this](const std::string& path) {
        push_undo_state();
        m_clip_manager.start_async_import_image(path, 5.0, [this]() {
            m_audio_engine.update_project_snapshot(m_project);
        });
    };

    if (m_use_system_file_dialog && NativeFileDialog::is_available()) {
        NativeFileDialog::open_file(
            "Import Image Clip",
            "",
            "Supported Image Files (*.png, *.jpg, *.jpeg, *.bmp, *.webp)",
            "*.png *.jpg *.jpeg *.bmp *.webp",
            on_chosen
        );
        return;
    }

    m_file_browser.open(
        "Import Image Clip",
        FileBrowserMode::OpenFile,
        { { "Supported Image Files (*.png, *.jpg, *.jpeg, *.bmp, *.webp)", { ".png", ".jpg", ".jpeg", ".bmp", ".webp" } },
          { "All Files (*.*)", { ".*" } } },
        "",
        "",
        on_chosen
    );
}

void App::import_audio_browser() {
    auto on_chosen = [this](const std::string& path) {
        push_undo_state();
        m_clip_manager.start_async_import_audio(path, [this]() {
            m_audio_engine.update_project_snapshot(m_project);
            m_audio_engine.ensure_project_assets_loaded(m_project);
        });
    };

    if (m_use_system_file_dialog && NativeFileDialog::is_available()) {
        NativeFileDialog::open_file(
            "Import Audio Clip",
            "",
            "Supported Audio Files (*.mp3, *.wav, *.aac, *.ogg, *.flac, *.m4a)",
            "*.mp3 *.wav *.aac *.ogg *.flac *.m4a",
            on_chosen
        );
        return;
    }

    m_file_browser.open(
        "Import Audio Clip",
        FileBrowserMode::OpenFile,
        { { "Supported Audio Files (*.mp3, *.wav, *.aac, *.ogg, *.flac, *.m4a)", { ".mp3", ".wav", ".aac", ".ogg", ".flac", ".m4a" } },
          { "All Files (*.*)", { ".*" } } },
        "",
        "",
        on_chosen
    );
}

void App::import_prefab_browser() {
    auto on_chosen = [this](const std::string& path) {
        push_undo_state();
        m_clip_manager.start_async_import_prefab(path, [this]() {
            m_audio_engine.update_project_snapshot(m_project);
            m_audio_engine.ensure_project_assets_loaded(m_project);
        });
    };

    if (m_use_system_file_dialog && NativeFileDialog::is_available()) {
        NativeFileDialog::open_file(
            "Import Project as Prefab Clip",
            "",
            "minicut Projects (*.minicut)",
            "*.minicut",
            on_chosen
        );
        return;
    }

    m_file_browser.open(
        "Import Project as Prefab Clip",
        FileBrowserMode::OpenFile,
        { { "minicut Projects (*.minicut)", { ".minicut" } },
          { "All Files (*.*)", { ".*" } } },
        "",
        "",
        on_chosen
    );
}

void App::export_path_browser() {
    std::string default_file = "minicut_export.mp4";
    if (!m_export_manager.get_output_path().empty()) {
        default_file = m_export_manager.get_output_path();
    }

    if (m_use_system_file_dialog && NativeFileDialog::is_available()) {
        NativeFileDialog::save_file(
            "Select Export Destination",
            default_file,
            "MP4 Video (*.mp4)",
            "*.mp4",
            [this](const std::string& path) {
                m_export_manager.set_output_path(path);
            }
        );
        return;
    }

    m_file_browser.open(
        "Select Export Destination",
        FileBrowserMode::SaveFile,
        { { "MP4 Video (*.mp4)", { ".mp4" } }, { "All Files (*.*)", { ".*" } } },
        default_file,
        ".mp4",
        [this](const std::string& path) {
            m_export_manager.set_output_path(path);
        },
        "Save"
    );
}

void App::process_events() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL2_ProcessEvent(&event);

        if (event.type == SDL_QUIT) {
            m_running = false;
        } else if (event.type == SDL_WINDOWEVENT) {
            if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
                m_window_width = event.window.data1;
                m_window_height = event.window.data2;
            }
        }
    }
}

void App::update(double delta_time) {
    NativeFileDialog::poll_callbacks();
    m_script_engine.update(delta_time);

    ImGuiIO& io = ImGui::GetIO();

    // ------------------------------------------------------------------------
    // Reliable Frame-Synchronized Keyboard Shortcuts
    // Only ignored if user is actively typing in a text field
    // ------------------------------------------------------------------------
    if (!io.WantTextInput) {
        // Spacebar: Play / Pause
        if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
            toggle_playback();
        }

        // Left / Right Arrows: Step 1 frame
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true)) {
            m_playhead_time = std::max(0.0, m_playhead_time - 1.0 / 30.0);
            m_audio_engine.set_playhead_time(m_playhead_time);
            m_media_decoder.request_frame(m_playhead_time, m_project);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, true)) {
            m_playhead_time = m_playhead_time + 1.0 / 30.0;
            m_audio_engine.set_playhead_time(m_playhead_time);
            m_media_decoder.request_frame(m_playhead_time, m_project);
        }

        // Home / End
        if (ImGui::IsKeyPressed(ImGuiKey_Home, false)) {
            m_playhead_time = 0.0;
            m_audio_engine.set_playhead_time(0.0);
            m_media_decoder.request_frame(0.0, m_project);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_End, false)) {
            m_playhead_time = m_project.get_total_duration();
            m_audio_engine.set_playhead_time(m_playhead_time);
            m_media_decoder.request_frame(m_playhead_time, m_project);
        }

        // S: Split selected clip at playhead
        if (ImGui::IsKeyPressed(ImGuiKey_S, false) && !io.KeyCtrl) {
            push_undo_state();
            m_timeline.split_selected_clip(m_project, m_playhead_time);
            m_media_decoder.request_frame(m_playhead_time, m_project);
        }

        // Delete / Backspace: Delete selected clip
        if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) {
            push_undo_state();
            m_timeline.delete_selected_clip(m_project);
            m_selected_clip_id.clear();
            m_keyframe_editor.set_selected_clip("");
            m_media_decoder.request_frame(m_playhead_time, m_project);
        }

        // Ctrl + Z: Undo
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false) && !io.KeyShift) {
            undo();
        }

        // Ctrl + Y or Ctrl + Shift + Z: Redo
        if ((io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, false)) ||
            (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false))) {
            redo();
        }

        // Ctrl + C: Copy
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false)) {
            m_timeline.copy_selected_clip(m_project);
        }

        // Ctrl + X: Cut
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_X, false)) {
            push_undo_state();
            m_timeline.cut_selected_clip(m_project);
            m_selected_clip_id.clear();
            m_keyframe_editor.set_selected_clip("");
        }

        // Ctrl + V: Paste
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false)) {
            push_undo_state();
            double paste_t = m_timeline.get_mouse_hover_time();
            if (paste_t <= 0.0) paste_t = m_playhead_time;
            m_timeline.paste_clip(m_project, paste_t);
            m_media_decoder.request_frame(m_playhead_time, m_project);
        }

        // Ctrl + N: New Project
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_N, false)) {
            new_project();
        }

        // Ctrl + O: Open Project
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O, false)) {
            open_project_browser();
        }

        // Ctrl + S: Save Project
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false) && !io.KeyShift) {
            save_project();
        }

        // Ctrl + Shift + S: Save Project As
        if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
            save_project_as_browser();
        }

        // Ctrl + I: Import Video
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_I, false)) {
            import_video_browser();
        }

        // Ctrl + E: Export Video
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_E, false)) {
            m_export_manager.open_export_dialog();
        }
    }

    if (m_is_playing) {
        if (m_audio_engine.is_device_active()) {
            m_playhead_time = m_audio_engine.get_playhead_time();
        } else {
            m_playhead_time += delta_time;
            m_audio_engine.set_playhead_time(m_playhead_time);
        }
        double total_dur = m_project.get_total_duration();
        if (m_playhead_time >= total_dur) {
            if (m_loop_playback) {
                m_playhead_time = 0.0;
                m_audio_engine.set_playhead_time(0.0);
            } else {
                m_is_playing = false;
                m_audio_engine.set_playing(false);
            }
        }
        m_media_decoder.request_frame(m_playhead_time, m_project);
    }

    m_media_decoder.update_preview_texture(m_preview_texture, m_preview_tex_w, m_preview_tex_h, m_preview_tex_time);
}

void App::render_menu_bar() {
    if (ImGui::BeginMainMenuBar()) {
        m_menu_bar_height = ImGui::GetWindowSize().y;
        if (ImGui::BeginMenu(_TR("menu_file"))) {
            if (ImGui::MenuItem(_TR("file_new"), "Ctrl+N")) { new_project(); }
            if (ImGui::MenuItem(_TR("file_open"), "Ctrl+O")) { open_project_browser(); }
            if (ImGui::MenuItem(_TR("file_save"), "Ctrl+S")) { save_project(); }
            if (ImGui::MenuItem(_TR("file_save_as"), "Ctrl+Shift+S")) { save_project_as_browser(); }
            ImGui::Separator();
            if (ImGui::MenuItem(_TR("file_export"), "Ctrl+E")) { m_export_manager.open_export_dialog(); }
            ImGui::Separator();
            if (ImGui::MenuItem(_TR("file_exit"), "Ctrl+Q")) { m_running = false; }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu(_TR("menu_edit"))) {
            if (ImGui::MenuItem(_TR("edit_undo"), "Ctrl+Z", false, !m_undo_stack.empty())) { undo(); }
            if (ImGui::MenuItem(_TR("edit_redo"), "Ctrl+Y", false, !m_redo_stack.empty())) { redo(); }
            ImGui::Separator();
            if (ImGui::MenuItem(_TR("edit_cut"), "Ctrl+X", false, !m_selected_clip_id.empty())) {
                push_undo_state();
                m_timeline.cut_selected_clip(m_project);
                m_selected_clip_id.clear();
                m_keyframe_editor.set_selected_clip("");
            }
            if (ImGui::MenuItem(_TR("edit_copy"), "Ctrl+C", false, !m_selected_clip_id.empty())) {
                m_timeline.copy_selected_clip(m_project);
            }
            if (ImGui::MenuItem(_TR("edit_paste"), "Ctrl+V")) {
                push_undo_state();
                double paste_t = m_timeline.get_mouse_hover_time();
                if (paste_t <= 0.0) paste_t = m_playhead_time;
                m_timeline.paste_clip(m_project, paste_t, m_timeline.get_hovered_or_active_track());
                m_media_decoder.request_frame(m_playhead_time, m_project);
            }
            ImGui::Separator();
            if (ImGui::MenuItem(_TR("edit_split"), "S", false, !m_selected_clip_id.empty())) {
                push_undo_state();
                m_timeline.split_selected_clip(m_project, m_playhead_time);
                m_media_decoder.request_frame(m_playhead_time, m_project);
            }
            if (ImGui::MenuItem(_TR("edit_delete"), "Del", false, !m_selected_clip_id.empty())) {
                push_undo_state();
                m_timeline.delete_selected_clip(m_project);
                m_selected_clip_id.clear();
                m_keyframe_editor.set_selected_clip("");
                m_media_decoder.request_frame(m_playhead_time, m_project);
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu(_TR("menu_project"))) {
            if (ImGui::MenuItem(_TR("project_import_video"), "Ctrl+I")) { import_video_browser(); }
            if (ImGui::MenuItem(_TR("project_import_audio"))) { import_audio_browser(); }
            if (ImGui::MenuItem(_TR("project_import_image"))) { import_image_browser(); }
            if (ImGui::MenuItem(_TR("project_import_prefab"))) { import_prefab_browser(); }
            ImGui::Separator();
            if (ImGui::MenuItem(_TR("project_generate_demo"))) {
                std::string test_v = "demo_colorbars.mp4";
                std::string test_a = "demo_tone.mp3";
                if (MediaDecoder::generate_test_media(test_v, test_a)) {
                    push_undo_state();
                    m_clip_manager.import_video(m_project, test_v);
                    m_clip_manager.import_audio(m_project, test_a);
                    m_audio_engine.update_project_snapshot(m_project);
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem(_TR("project_cleanup_temp"))) {
                cleanup_temp_files();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu(_TR("menu_settings"))) {
            bool use_sys = m_use_system_file_dialog;
            if (ImGui::MenuItem(_TR("settings_system_dialog"), nullptr, &use_sys)) {
                m_use_system_file_dialog = use_sys;
                save_settings();
            }
            if (ImGui::BeginMenu(_TR("settings_language"))) {
                std::string cur_lang = LocaleManager::instance().current_locale();
                for (const auto& lang : LocaleManager::instance().get_available_languages()) {
                    if (ImGui::MenuItem(lang.name.c_str(), nullptr, cur_lang == lang.code)) {
                        set_language(lang.code);
                    }
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Scripts")) {
            m_script_engine.render_scripts_menu();
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu(_TR("menu_help"))) {
            if (ImGui::MenuItem("Lua API Reference (PDF)...")) {
                const char* home = std::getenv("HOME");
                std::vector<std::string> candidates = {
                    "docs/minicut_lua_api.pdf",
                    "../docs/minicut_lua_api.pdf",
                    home ? (std::string(home) + "/.config/minicut/docs/minicut_lua_api.pdf") : "",
                    home ? (std::string(home) + "/.local/share/minicut/docs/minicut_lua_api.pdf") : "",
                    "/home/kaiju/.local/bin/docs/minicut_lua_api.pdf"
                };
                for (const auto& p : candidates) {
                    if (!p.empty() && fs::exists(p)) {
                        std::string cmd = "xdg-open \"" + p + "\" >/dev/null 2>&1 &";
                        std::system(cmd.c_str());
                        break;
                    }
                }
            }
            if (ImGui::MenuItem(_TR("help_about"))) { m_show_about = true; }
            ImGui::EndMenu();
        }

        // Right-aligned notifications & status indicators
        if (!m_status_notification.empty()) {
            ImGui::SetCursorPosX(ImGui::GetWindowWidth() - ImGui::CalcTextSize(m_status_notification.c_str()).x - 220.0f);
            ImGui::TextColored(ImVec4(0.2f, 0.85f, 0.45f, 1.0f), "%s", m_status_notification.c_str());
        }

        float avail = ImGui::GetContentRegionAvail().x;
        if (avail > 200.0f) {
            std::string status = "Project: " + m_project.name + " | Time: " + format_timecode(m_playhead_time);
            ImGui::SetCursorPosX(ImGui::GetWindowWidth() - ImGui::CalcTextSize(status.c_str()).x - 20.0f);
            ImGui::TextDisabled("%s", status.c_str());
        }

        ImGui::EndMainMenuBar();
    }
}

void App::render_shelf_dock() {
    float menu_h = m_menu_bar_height > 0.0f ? m_menu_bar_height : ImGui::GetFrameHeight();
    m_script_engine.render_shelf_dock(menu_h);
}

void App::render_clip_panel() {
    std::string title = std::string(_TR("panel_clips")) + "###ClipsPanel";
    ImGui::Begin(title.c_str(), nullptr, ImGuiWindowFlags_NoCollapse);

    m_clip_manager.render_ui(
        m_project, m_playhead_time,
        // On add to timeline
        [this](const std::string& asset_id, bool is_video) {
            add_asset_to_timeline(asset_id, is_video, m_playhead_time);
        },
        // On add prefab to timeline
        [this](const std::string& prefab_id, double insert_time) {
            push_undo_state();
            m_clip_manager.instantiate_prefab_on_timeline(m_project, prefab_id, insert_time);
            m_audio_engine.update_project_snapshot(m_project);
            m_media_decoder.request_frame(m_playhead_time, m_project);
        },
        // On project changed
        [this]() {
            push_undo_state();
            m_audio_engine.update_project_snapshot(m_project);
            m_media_decoder.request_frame(m_playhead_time, m_project);
        },
        // On import video
        [this]() {
            import_video_browser();
        },
        // On import image
        [this]() {
            import_image_browser();
        },
        // On import audio
        [this]() {
            import_audio_browser();
        },
        // On import prefab
        [this]() {
            import_prefab_browser();
        }
    );

    ImGui::End();
}

void App::render_preview_panel() {
    std::string title = std::string(_TR("panel_preview")) + "###PreviewPanel";
    ImGui::Begin(title.c_str(), nullptr, ImGuiWindowFlags_NoCollapse);

    ImVec2 avail = ImGui::GetContentRegionAvail();
    float transport_h = 38.0f;
    float video_area_h = avail.y - transport_h;
    if (video_area_h < 100.0f) video_area_h = 100.0f;

    float aspect = 16.0f / 9.0f;
    float disp_w = avail.x;
    float disp_h = disp_w / aspect;

    if (disp_h > video_area_h) {
        disp_h = video_area_h;
        disp_w = disp_h * aspect;
    }

    float offset_x = (avail.x - disp_w) * 0.5f;
    float offset_y = (video_area_h - disp_h) * 0.5f;

    ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + offset_x, ImGui::GetCursorPosY() + offset_y));
    ImGui::Image((ImTextureID)(intptr_t)m_preview_texture, ImVec2(disp_w, disp_h));

    // Transport Controls
    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - transport_h - 4.0f);
    ImGui::Separator();

    if (ImGui::Button("|<<", ImVec2(32, 24))) {
        m_playhead_time = 0.0;
        m_audio_engine.set_playhead_time(0.0);
        m_media_decoder.request_frame(0.0, m_project);
    }
    ImGui::SameLine();
    if (ImGui::Button("<|", ImVec2(30, 24))) {
        m_playhead_time = std::max(0.0, m_playhead_time - 1.0 / 30.0);
        m_audio_engine.set_playhead_time(m_playhead_time);
        m_media_decoder.request_frame(m_playhead_time, m_project);
    }
    ImGui::SameLine();
    if (ImGui::Button(m_is_playing ? "||" : ">", ImVec2(36, 24))) {
        toggle_playback();
    }
    ImGui::SameLine();
    if (ImGui::Button("|>", ImVec2(30, 24))) {
        m_playhead_time = m_playhead_time + 1.0 / 30.0;
        m_audio_engine.set_playhead_time(m_playhead_time);
        m_media_decoder.request_frame(m_playhead_time, m_project);
    }
    ImGui::SameLine();
    if (ImGui::Button(">>|", ImVec2(32, 24))) {
        m_playhead_time = m_project.get_total_duration();
        m_audio_engine.set_playhead_time(m_playhead_time);
        m_media_decoder.request_frame(m_playhead_time, m_project);
    }
    ImGui::SameLine();
    ImGui::Checkbox("Loop", &m_loop_playback);

    ImGui::SameLine();
    ImGui::Text("%s / %s", format_timecode(m_playhead_time).c_str(), format_timecode(m_project.get_total_duration()).c_str());

    // Scrub Slider
    ImGui::SameLine();
    float scrub_w = ImGui::GetContentRegionAvail().x - 10.0f;
    if (scrub_w > 80.0f) {
        ImGui::SetNextItemWidth(scrub_w);
        float t_val = static_cast<float>(m_playhead_time);
        if (ImGui::SliderFloat("##scrub", &t_val, 0.0f, static_cast<float>(m_project.get_total_duration()), "")) {
            m_playhead_time = t_val;
            m_audio_engine.set_playhead_time(m_playhead_time);
            m_media_decoder.request_frame(m_playhead_time, m_project);
        }
    }

    ImGui::End();
}

void App::render_timeline_panel() {
    std::string title = std::string(_TR("panel_timeline")) + "###TimelinePanel";
    ImGui::Begin(title.c_str(), nullptr, ImGuiWindowFlags_NoCollapse);

    m_timeline.render_ui(
        m_project, m_playhead_time, m_selected_clip_id,
        [this](const std::string& clip_id) {
            m_selected_clip_id = clip_id;
            m_keyframe_editor.set_selected_clip(clip_id);
        },
        [this](double new_t) {
            m_playhead_time = new_t;
            m_audio_engine.set_playhead_time(m_playhead_time);
            m_media_decoder.request_frame(m_playhead_time, m_project);
        },
        [this]() {
            push_undo_state();
            m_audio_engine.update_project_snapshot(m_project);
            m_media_decoder.request_frame(m_playhead_time, m_project);
        },
        [this](const std::string& prefab_id, double insert_time) {
            push_undo_state();
            m_clip_manager.instantiate_prefab_on_timeline(m_project, prefab_id, insert_time);
            m_audio_engine.update_project_snapshot(m_project);
            m_media_decoder.request_frame(m_playhead_time, m_project);
        }
    );

    ImGui::End();
}

void App::render_context_panel() {
    std::string title = std::string(_TR("panel_context")) + "###ContextPanel";
    if (!m_selected_clip_id.empty()) {
        int t_idx = -1, c_idx = -1;
        const Clip* clip = m_project.find_clip(m_selected_clip_id, &t_idx, &c_idx);
        if (clip && t_idx >= 0) {
            const Track& tr = m_project.tracks[t_idx];
            if (tr.type == "audio") {
                title = "Context: Audio Tools & Keyframes###ContextPanel";
            } else {
                const VideoAsset* va = m_project.find_video_asset(clip->asset_id);
                if (va && va->is_image) {
                    title = "Context: Image Tools & Transform###ContextPanel";
                } else {
                    title = "Context: Video Tools & Transform###ContextPanel";
                }
            }
        }
    }

    ImGui::Begin(title.c_str(), nullptr, ImGuiWindowFlags_NoCollapse);
    m_keyframe_editor.render_ui(
        m_project, m_playhead_time,
        [this](double new_t) {
            m_playhead_time = new_t;
            m_audio_engine.set_playhead_time(m_playhead_time);
            m_media_decoder.request_frame(m_playhead_time, m_project);
        },
        [this]() {
            push_undo_state();
            m_audio_engine.update_project_snapshot(m_project);
            m_media_decoder.request_frame(m_playhead_time, m_project);
        }
    );
    ImGui::End();
}

void App::render_about_dialog() {
    if (m_show_about) {
        ImGui::OpenPopup("About minicut");
        m_show_about = false;
    }

    if (ImGui::BeginPopupModal("About minicut", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("minicut - Modern C++17 Multi-Track Video Editor");
        ImGui::TextDisabled("Built for Linux / Kubuntu 26.04.1 LTS with KDE Plasma");
        ImGui::Separator();
        ImGui::BulletText("OpenGL 3.3 Core Profile + Dear ImGui");
        ImGui::BulletText("FFmpeg (avformat, avcodec, swscale, swresample)");
        ImGui::BulletText("SDL2 Multi-channel Audio Mixing & Playback");
        ImGui::BulletText("Context-sensitive Image, Video, & Audio Transform Tools");
        ImGui::BulletText("Full Transform Keyframing (Position, Rotation, Scale, Opacity)");
        ImGui::BulletText("Non-blocking Asynchronous Export Pipeline (.mp4)");
        ImGui::BulletText("Full Interactive Graphical FileBrowser");
        ImGui::Separator();
        if (ImGui::Button("OK", ImVec2(100, 26))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void App::load_settings() {
    const char* home = std::getenv("HOME");
    if (!home) return;
    fs::path cfg_path = fs::path(home) / ".config" / "minicut" / "settings.json";
    std::error_code ec;
    if (fs::exists(cfg_path, ec)) {
        try {
            std::ifstream ifs(cfg_path);
            if (ifs.is_open()) {
                nlohmann::json j;
                ifs >> j;
                m_use_system_file_dialog = j.value("use_system_file_dialog", true);
                std::string lang = j.value("language", "en");
                if (lang != "en" && lang != "ja" && lang != "jp" && lang != "ko" && lang != "uk" && lang != "ar") {
                    lang = "en";
                }
                LocaleManager::instance().set_locale(lang);
                m_split_h = std::clamp(j.value("split_h", 0.46f), 0.15f, 0.85f);
                m_split_top_v = std::clamp(j.value("split_top_v", 0.28f), 0.15f, 0.75f);
                m_split_bottom_v = std::clamp(j.value("split_bottom_v", 0.65f), 0.20f, 0.85f);
                m_timeline.set_track_header_width(j.value("track_header_w", 145.0f));
            }
        } catch (...) {}
    } else {
        LocaleManager::instance().set_locale("en");
        save_settings();
    }
}

void App::save_settings() {
    const char* home = std::getenv("HOME");
    if (!home) return;
    fs::path cfg_dir = fs::path(home) / ".config" / "minicut";
    std::error_code ec;
    fs::create_directories(cfg_dir, ec);
    fs::path cfg_path = cfg_dir / "settings.json";

    nlohmann::json j;
    j["use_system_file_dialog"] = m_use_system_file_dialog;
    j["language"] = LocaleManager::instance().current_locale();
    j["split_h"] = m_split_h;
    j["split_top_v"] = m_split_top_v;
    j["split_bottom_v"] = m_split_bottom_v;
    j["track_header_w"] = m_timeline.get_track_header_width();

    try {
        std::ofstream ofs(cfg_path);
        if (ofs.is_open()) {
            ofs << j.dump(2);
        }
    } catch (...) {}
}

void App::set_language(const std::string& code) {
    LocaleManager::instance().set_locale(code);
    save_settings();
}

void App::cleanup_temp_files() {
    uintmax_t bytes_freed = 0;
    size_t count = TempManager::cleanup_temp_files(&bytes_freed);
    double mb = bytes_freed / (1024.0 * 1024.0);
    std::ostringstream ss;
    if (count > 0) {
        ss << _TR("status_temp_cleaned") << ": " << count << " files (" << std::fixed << std::setprecision(1) << mb << " MB)";
    } else {
        ss << _TR("status_no_temp");
    }
    m_status_notification = ss.str();
    m_status_notification_timer = 5.0;
}

void App::render_splitters() {
    ImDrawList* fg_draw = ImGui::GetForegroundDrawList();
    ImVec2 mouse_pos = ImGui::GetMousePos();
    bool mouse_down = ImGui::IsMouseDown(0);

    float menu_h = ImGui::GetFrameHeight();
    float shelf_h = m_script_engine.is_shelf_visible() ? m_script_engine.get_shelf_height() : 0.0f;
    float content_y = menu_h + shelf_h;
    float total_w = static_cast<float>(m_window_width);
    float total_h = static_cast<float>(m_window_height) - content_y;

    float top_h = total_h * m_split_h;
    float left_top_w = total_w * m_split_top_v;
    float left_bottom_w = total_w * m_split_bottom_v;

    const float thickness = 6.0f;

    // 1. Horizontal Splitter (between Top Row and Bottom Row)
    float split_y = content_y + top_h;
    bool hover_h = (mouse_pos.x >= 0.0f && mouse_pos.x <= total_w &&
                    mouse_pos.y >= (split_y - thickness * 0.5f) && mouse_pos.y <= (split_y + thickness * 0.5f));
    if (m_dragging_split_h) {
        if (!mouse_down) {
            m_dragging_split_h = false;
            save_settings();
        } else {
            float new_ratio = (mouse_pos.y - content_y) / total_h;
            m_split_h = std::clamp(new_ratio, 0.15f, 0.85f);
        }
    } else if (hover_h && ImGui::IsMouseClicked(0)) {
        m_dragging_split_h = true;
    }
    if (hover_h || m_dragging_split_h) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
    }
    ImU32 col_h = m_dragging_split_h ? IM_COL32(61, 174, 233, 255) : (hover_h ? IM_COL32(61, 174, 233, 180) : IM_COL32(35, 40, 48, 200));
    fg_draw->AddLine(ImVec2(0.0f, split_y), ImVec2(total_w, split_y), col_h, (hover_h || m_dragging_split_h) ? 3.0f : 1.5f);

    // 2. Vertical Splitter Top (between Clips & Assets and Preview)
    float split_top_x = left_top_w;
    bool hover_vt = (mouse_pos.x >= (split_top_x - thickness * 0.5f) && mouse_pos.x <= (split_top_x + thickness * 0.5f) &&
                     mouse_pos.y >= content_y && mouse_pos.y <= split_y);
    if (m_dragging_split_top_v) {
        if (!mouse_down) {
            m_dragging_split_top_v = false;
            save_settings();
        } else {
            float new_ratio = mouse_pos.x / total_w;
            m_split_top_v = std::clamp(new_ratio, 0.15f, 0.75f);
        }
    } else if (hover_vt && ImGui::IsMouseClicked(0)) {
        m_dragging_split_top_v = true;
    }
    if (hover_vt || m_dragging_split_top_v) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }
    ImU32 col_vt = m_dragging_split_top_v ? IM_COL32(61, 174, 233, 255) : (hover_vt ? IM_COL32(61, 174, 233, 180) : IM_COL32(35, 40, 48, 200));
    fg_draw->AddLine(ImVec2(split_top_x, content_y), ImVec2(split_top_x, split_y), col_vt, (hover_vt || m_dragging_split_top_v) ? 3.0f : 1.5f);

    // 3. Vertical Splitter Bottom (between Timeline and Context Inspector)
    float split_bottom_x = left_bottom_w;
    bool hover_vb = (mouse_pos.x >= (split_bottom_x - thickness * 0.5f) && mouse_pos.x <= (split_bottom_x + thickness * 0.5f) &&
                     mouse_pos.y >= split_y && mouse_pos.y <= (content_y + total_h));
    if (m_dragging_split_bottom_v) {
        if (!mouse_down) {
            m_dragging_split_bottom_v = false;
            save_settings();
        } else {
            float new_ratio = mouse_pos.x / total_w;
            m_split_bottom_v = std::clamp(new_ratio, 0.20f, 0.85f);
        }
    } else if (hover_vb && ImGui::IsMouseClicked(0)) {
        m_dragging_split_bottom_v = true;
    }
    if (hover_vb || m_dragging_split_bottom_v) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }
    ImU32 col_vb = m_dragging_split_bottom_v ? IM_COL32(61, 174, 233, 255) : (hover_vb ? IM_COL32(61, 174, 233, 180) : IM_COL32(35, 40, 48, 200));
    fg_draw->AddLine(ImVec2(split_bottom_x, split_y), ImVec2(split_bottom_x, content_y + total_h), col_vb, (hover_vb || m_dragging_split_bottom_v) ? 3.0f : 1.5f);
}

void App::render() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    render_menu_bar();
    render_shelf_dock();

    float menu_h = m_menu_bar_height > 0.0f ? m_menu_bar_height : ImGui::GetFrameHeight();
    float shelf_h = m_script_engine.is_shelf_visible() ? m_script_engine.get_shelf_height() : 0.0f;
    float content_y = menu_h + shelf_h;
    float total_w = static_cast<float>(m_window_width);
    float total_h = static_cast<float>(m_window_height) - content_y;

    float top_h = total_h * m_split_h;
    float bottom_h = total_h - top_h;

    float left_top_w = total_w * m_split_top_v;
    float right_top_w = total_w - left_top_w;

    float left_bottom_w = total_w * m_split_bottom_v;
    float right_bottom_w = total_w - left_bottom_w;

    // Panel 1: Clip Panel (Top-Left)
    ImGui::SetNextWindowPos(ImVec2(0, content_y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(left_top_w, top_h), ImGuiCond_Always);
    render_clip_panel();

    // Panel 2: Preview Panel (Top-Right, wider)
    ImGui::SetNextWindowPos(ImVec2(left_top_w, content_y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(right_top_w, top_h), ImGuiCond_Always);
    render_preview_panel();

    // Panel 3: Timeline Panel (Bottom-Left)
    ImGui::SetNextWindowPos(ImVec2(0, content_y + top_h), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(left_bottom_w, bottom_h), ImGuiCond_Always);
    render_timeline_panel();

    // Panel 4: Context Panel (Audio, Video, Image Tools & Transforms) (Bottom-Right)
    ImGui::SetNextWindowPos(ImVec2(left_bottom_w, content_y + top_h), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(right_bottom_w, bottom_h), ImGuiCond_Always);
    render_context_panel();

    // Allow view section resizing via interactive splitters
    render_splitters();

    // Render Plugin Script Panels
    m_script_engine.render_script_panels(total_w, total_h, content_y);

    // Export Manager dialog & progress bar
    m_export_manager.render_ui([this](const ExportConfig& cfg) {
        m_export_manager.start_export(m_project, cfg);
    });

    // File Browser Dialog
    m_file_browser.render();

    // Import Progress Modal
    if (m_clip_manager.is_importing()) {
        ImGui::SetNextWindowSize(ImVec2(380, 110), ImGuiCond_Always);
        ImGui::SetNextWindowPos(ImVec2(total_w * 0.5f, (total_h + menu_h) * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        if (ImGui::Begin("Importing Media Asset...", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("File: %s", m_clip_manager.get_import_filename().c_str());
            float p = m_clip_manager.get_import_progress();
            std::string pct = std::to_string(static_cast<int>(p * 100.0f)) + "%";
            ImGui::ProgressBar(p, ImVec2(-1, 20), pct.c_str());
            ImGui::TextDisabled("%s", m_clip_manager.get_import_status().c_str());
            ImGui::End();
        }
    }

    // About Dialog
    render_about_dialog();

    // Render ImGui
    ImGui::Render();
    glViewport(0, 0, m_window_width, m_window_height);
    glClearColor(0.14f, 0.15f, 0.16f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SDL_GL_SwapWindow(m_window);
}

void App::run() {
    uint64_t last_time = SDL_GetPerformanceCounter();
    while (m_running) {
        uint64_t current_time = SDL_GetPerformanceCounter();
        double delta_time = static_cast<double>(current_time - last_time) / SDL_GetPerformanceFrequency();
        last_time = current_time;

        process_events();
        update(delta_time);
        render();
    }
}

void App::cleanup() {
    m_audio_engine.shutdown();

    if (m_preview_texture) {
        glDeleteTextures(1, &m_preview_texture);
        m_preview_texture = 0;
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    if (m_gl_context) {
        SDL_GL_DeleteContext(m_gl_context);
        m_gl_context = nullptr;
    }
    if (m_window) {
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
    }
    SDL_Quit();
}
