#include "App.h"
#include "LocaleManager.h"
#include "TempManager.h"
#include "SwapManager.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <backends/imgui_impl_sdl2.h>
#include <backends/imgui_impl_opengl3.h>
#include <nlohmann/json.hpp>
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <algorithm>
#include <csignal>

namespace fs = std::filesystem;

static Uint32 get_event_window_id(const SDL_Event& event) {
    switch (event.type) {
        case SDL_WINDOWEVENT: return event.window.windowID;
        case SDL_KEYDOWN:
        case SDL_KEYUP: return event.key.windowID;
        case SDL_TEXTEDITING: return event.edit.windowID;
        case SDL_TEXTINPUT: return event.text.windowID;
        case SDL_MOUSEMOTION: return event.motion.windowID;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP: return event.button.windowID;
        case SDL_MOUSEWHEEL: return event.wheel.windowID;
        default: return 0;
    }
}

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
    std::signal(SIGPIPE, SIG_IGN);
    SwapManager::instance().init();
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

    SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");

    m_window = SDL_CreateWindow(
        "Minicut - New Project",
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
            "desktop/minicut.png",
            "icons/minicut.png",
            "../desktop/minicut.png",
            "../icons/minicut.png",
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
    m_imgui_context = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Direct ImGui layout storage to ~/.config/minicut/imgui.ini to keep working directory clean
    const char* home = std::getenv("HOME");
    static std::string s_ini_path;
    if (home) {
        std::string cfg_dir = std::string(home) + "/.config/minicut";
        std::error_code ec;
        fs::create_directories(cfg_dir, ec);
        s_ini_path = cfg_dir + "/imgui.ini";
        if (!fs::exists(s_ini_path)) {
            for (const auto& tmpl : {"desktop/imgui.ini", "../desktop/imgui.ini", "imgui.ini"}) {
                if (fs::exists(tmpl)) {
                    fs::copy_file(tmpl, s_ini_path, fs::copy_options::overwrite_existing, ec);
                    break;
                }
            }
        }
        io.IniFilename = s_ini_path.c_str();
    }

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
        "/usr/share/fonts/truetype/nanum/NanumGothic.ttf",
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
    RenderBackendType preferred_backend = (m_settings_logic.get_render_backend() == "Vulkan") ? RenderBackendType::Vulkan : RenderBackendType::OpenGL3;
    RenderSystem::instance().init(m_window, m_gl_context, preferred_backend);

    // Init Preview Texture
    m_preview_texture = static_cast<GLuint>(RenderSystem::instance().create_texture(1280, 720, nullptr));

    // Generate Settings Gear Icon Texture
    generate_settings_gear_icon();

    // Generate Preview Toolbar Icons
    generate_preview_toolbar_icons();

    // Initialize ShaderManager (ensures ~/.config/minicut/shaders/gl and vulkan exist with starter shaders)
    ShaderManager::instance().init();

    // Init Audio Engine
    m_audio_engine.init();
    m_audio_engine.set_preview_volume_db(m_settings_logic.get_preview_volume_db());

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
    m_script_engine.set_app(this);
    update_window_title();

    return true;
}

void App::apply_breeze_dark_theme() {
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
    colors[ImGuiCol_TableHeaderBg]         = ImVec4(0.20f, 0.22f, 0.25f, 1.00f);
    colors[ImGuiCol_TableBorderStrong]     = ImVec4(0.28f, 0.30f, 0.34f, 1.00f);
    colors[ImGuiCol_TableBorderLight]      = ImVec4(0.22f, 0.24f, 0.27f, 1.00f);
    colors[ImGuiCol_TableRowBg]            = ImVec4(0.14f, 0.15f, 0.16f, 1.00f);
    colors[ImGuiCol_TableRowBgAlt]         = ImVec4(0.17f, 0.19f, 0.21f, 1.00f);
}

void App::apply_breeze_light_theme() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    // KDE Breeze Light Color Scheme
    colors[ImGuiCol_Text]                  = ImVec4(0.14f, 0.15f, 0.16f, 1.00f); // #232629
    colors[ImGuiCol_TextDisabled]          = ImVec4(0.50f, 0.54f, 0.58f, 1.00f);
    colors[ImGuiCol_WindowBg]              = ImVec4(0.94f, 0.94f, 0.95f, 1.00f); // #eff0f1
    colors[ImGuiCol_ChildBg]               = ImVec4(0.99f, 0.99f, 0.99f, 1.00f); // #fcfcfc
    colors[ImGuiCol_PopupBg]               = ImVec4(1.00f, 1.00f, 1.00f, 0.98f);
    colors[ImGuiCol_Border]                = ImVec4(0.74f, 0.75f, 0.76f, 1.00f); // #bcbebf
    colors[ImGuiCol_BorderShadow]          = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg]               = ImVec4(0.98f, 0.98f, 0.99f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]        = ImVec4(0.24f, 0.68f, 0.91f, 0.25f);
    colors[ImGuiCol_FrameBgActive]         = ImVec4(0.24f, 0.68f, 0.91f, 0.45f);
    colors[ImGuiCol_TitleBg]               = ImVec4(0.90f, 0.91f, 0.92f, 1.00f);
    colors[ImGuiCol_TitleBgActive]         = ImVec4(0.85f, 0.86f, 0.88f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed]      = ImVec4(0.90f, 0.91f, 0.92f, 0.75f);
    colors[ImGuiCol_MenuBarBg]             = ImVec4(0.92f, 0.93f, 0.94f, 1.00f);
    colors[ImGuiCol_ScrollbarBg]           = ImVec4(0.92f, 0.93f, 0.94f, 0.60f);
    colors[ImGuiCol_ScrollbarGrab]         = ImVec4(0.74f, 0.75f, 0.76f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.24f, 0.68f, 0.91f, 0.70f);
    colors[ImGuiCol_ScrollbarGrabActive]   = ImVec4(0.24f, 0.68f, 0.91f, 1.00f);
    colors[ImGuiCol_CheckMark]             = ImVec4(0.15f, 0.55f, 0.82f, 1.00f);
    colors[ImGuiCol_SliderGrab]            = ImVec4(0.24f, 0.68f, 0.91f, 1.00f);
    colors[ImGuiCol_SliderGrabActive]      = ImVec4(0.15f, 0.55f, 0.82f, 1.00f);
    colors[ImGuiCol_Button]                = ImVec4(0.88f, 0.89f, 0.90f, 1.00f);
    colors[ImGuiCol_ButtonHovered]         = ImVec4(0.24f, 0.68f, 0.91f, 0.60f);
    colors[ImGuiCol_ButtonActive]          = ImVec4(0.24f, 0.68f, 0.91f, 0.90f);
    colors[ImGuiCol_Header]                = ImVec4(0.24f, 0.68f, 0.91f, 0.25f);
    colors[ImGuiCol_HeaderHovered]         = ImVec4(0.24f, 0.68f, 0.91f, 0.50f);
    colors[ImGuiCol_HeaderActive]          = ImVec4(0.24f, 0.68f, 0.91f, 0.75f);
    colors[ImGuiCol_Separator]             = ImVec4(0.74f, 0.75f, 0.76f, 1.00f);
    colors[ImGuiCol_SeparatorHovered]      = ImVec4(0.24f, 0.68f, 0.91f, 0.78f);
    colors[ImGuiCol_SeparatorActive]       = ImVec4(0.24f, 0.68f, 0.91f, 1.00f);
    colors[ImGuiCol_ResizeGrip]            = ImVec4(0.74f, 0.75f, 0.76f, 0.50f);
    colors[ImGuiCol_ResizeGripHovered]     = ImVec4(0.24f, 0.68f, 0.91f, 0.70f);
    colors[ImGuiCol_ResizeGripActive]      = ImVec4(0.24f, 0.68f, 0.91f, 1.00f);
    colors[ImGuiCol_Tab]                   = ImVec4(0.88f, 0.89f, 0.90f, 1.00f);
    colors[ImGuiCol_TabHovered]            = ImVec4(0.24f, 0.68f, 0.91f, 0.70f);
    colors[ImGuiCol_TabActive]             = ImVec4(0.24f, 0.68f, 0.91f, 1.00f);
    colors[ImGuiCol_TabUnfocused]          = ImVec4(0.85f, 0.86f, 0.87f, 1.00f);
    colors[ImGuiCol_TabUnfocusedActive]    = ImVec4(0.88f, 0.89f, 0.90f, 1.00f);
    colors[ImGuiCol_TableHeaderBg]         = ImVec4(0.88f, 0.89f, 0.91f, 1.00f);
    colors[ImGuiCol_TableBorderStrong]     = ImVec4(0.74f, 0.75f, 0.76f, 1.00f);
    colors[ImGuiCol_TableBorderLight]      = ImVec4(0.82f, 0.83f, 0.84f, 1.00f);
    colors[ImGuiCol_TableRowBg]            = ImVec4(0.96f, 0.96f, 0.97f, 1.00f);
    colors[ImGuiCol_TableRowBgAlt]         = ImVec4(0.92f, 0.93f, 0.94f, 1.00f);
}

void App::apply_theme() {
    auto apply_to_current = [this]() {
        if (m_settings_logic.get_light_theme()) {
            apply_breeze_light_theme();
        } else {
            apply_breeze_dark_theme();
        }

        ImGuiStyle& style = ImGui::GetStyle();
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
    };

    if (m_imgui_context) {
        ImGui::SetCurrentContext(m_imgui_context);
        apply_to_current();
    }
    if (m_preview_imgui_ctx) {
        ImGui::SetCurrentContext(m_preview_imgui_ctx);
        apply_to_current();
    }
    if (m_export_imgui_ctx) {
        ImGui::SetCurrentContext(m_export_imgui_ctx);
        apply_to_current();
    }
    if (m_imgui_context) {
        ImGui::SetCurrentContext(m_imgui_context);
    }
}

void App::apply_kde_plasma_theme() {
    apply_theme();
}

void App::set_playhead_time(double t) {
    m_playhead_time = std::max(0.0, t);
    m_audio_engine.set_playhead_time(m_playhead_time);
    m_last_requested_playhead = m_playhead_time;
    m_media_decoder.request_frame(m_playhead_time, m_project);
}

void App::on_project_modified() {
    update_window_title();
    m_audio_engine.update_project_snapshot(m_project);
    m_last_requested_playhead = m_playhead_time;
    m_media_decoder.request_frame(m_playhead_time, m_project);
}

void App::set_selected_clip_id(const std::string& id) {
    m_selected_clip_id = id;
    m_keyframe_editor.set_selected_clip(id);
    m_timeline.select_clip(id, false, false);
}

Clip* App::find_selected_clip() {
    if (m_selected_clip_id.empty()) return nullptr;
    int t_idx = -1, c_idx = -1;
    return m_project.find_clip_mut(m_selected_clip_id, &t_idx, &c_idx);
}

bool App::is_selected_clip_visual() {
    Clip* c = find_selected_clip();
    if (!c) return false;
    for (const auto& track : m_project.tracks) {
        if (track.type == "video") {
            for (const auto& clip : track.clips) {
                if (clip.id == c->id) return true;
            }
        }
    }
    return false;
}

void App::set_status_notification(const std::string& msg, double duration) {
    m_status_notification = msg;
    m_status_notification_timer = duration;
}

void App::set_status_message(const std::string& msg, double duration) {
    set_status_notification(msg, duration);
}

void App::apply_custom_shader(const CustomShader& shader) {
    Clip* clip = find_selected_clip();
    if (!clip) {
        // If no clip is explicitly selected, find first visual clip on timeline
        for (auto& track : m_project.tracks) {
            if (track.type == "video" && !track.clips.empty()) {
                clip = &track.clips.front();
                break;
            }
        }
    }

    if (!clip) {
        set_status_notification("No clip selected to apply shader '" + shader.name + "'", 3.0);
        return;
    }

    std::unordered_map<std::string, float> uniforms;
    uniforms["time"] = static_cast<float>(m_playhead_time);
    uniforms["u_time"] = static_cast<float>(m_playhead_time);
    uniforms["frequency"] = 30.0f;
    uniforms["u_frequency"] = 30.0f;
    uniforms["amplitude"] = 0.02f;
    uniforms["u_amplitude"] = 0.02f;

    std::string out_clip_id;
    std::string shader_src = shader.frag_src.empty() ? shader.name : shader.frag_src;
    if (m_script_engine.render_shader_across_clip(clip, shader_src, uniforms, false, 0, 0.0, out_clip_id)) {
        set_status_notification("Applied " + shader.type + " shader '" + shader.name + "' to clip", 3.0);
        m_last_requested_playhead = m_playhead_time;
        m_media_decoder.request_frame(m_playhead_time, m_project);
    } else {
        set_status_notification("Failed to apply shader '" + shader.name + "'", 3.0);
    }
}

void ShaderManager::render_shaders_menu(App& app) {
    render_shaders_menu([&app](const CustomShader& cs) { app.apply_custom_shader(cs); },
                        [&app](const std::string& msg) { app.set_status_message(msg); });
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
        update_window_title();
        m_audio_engine.update_project_snapshot(m_project);
        m_last_requested_playhead = m_playhead_time;
        m_media_decoder.request_frame(m_playhead_time, m_project);
    }
}

void App::redo() {
    if (!m_redo_stack.empty()) {
        m_undo_stack.push_back(m_project);
        m_project = m_redo_stack.back();
        m_redo_stack.pop_back();
        update_window_title();
        m_audio_engine.update_project_snapshot(m_project);
        m_last_requested_playhead = m_playhead_time;
        m_media_decoder.request_frame(m_playhead_time, m_project);
    }
}

void App::new_project() {
    push_undo_state();
    m_project = Project();
    m_project.name = "";
    m_playhead_time = 0.0;
    m_selected_clip_id.clear();
    m_timeline.clear_selection();
    m_clip_manager.clear_selection();

    Track v1; v1.type = "video"; v1.name = "Video 1"; v1.id = "track_v1";
    Track v2; v2.type = "video"; v2.name = "Video 2"; v2.id = "track_v2";
    Track a1; a1.type = "audio"; a1.name = "Audio 1"; a1.id = "track_a1";
    Track a2; a2.type = "audio"; a2.name = "Audio 2"; a2.id = "track_a2";
    m_project.tracks.push_back(v1);
    m_project.tracks.push_back(v2);
    m_project.tracks.push_back(a1);
    m_project.tracks.push_back(a2);

    update_window_title();
    m_audio_engine.update_project_snapshot(m_project);
    m_last_requested_playhead = m_playhead_time;
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
    if (m_project_load_state.active.load()) {
        return;
    }

    if (m_project_load_state.worker_thread) {
        if (m_project_load_state.worker_thread->joinable()) {
            m_project_load_state.worker_thread->join();
        }
        delete m_project_load_state.worker_thread;
        m_project_load_state.worker_thread = nullptr;
    }

    m_project_load_state.project_filename = fs::path(path).filename().string();
    m_project_load_state.success = false;
    m_project_load_state.worker_done.store(false);
    m_project_load_state.active.store(true);
    m_project_load_state.set(0.05f, "Parsing project file...", m_project_load_state.project_filename);

    m_project_load_state.worker_thread = new std::thread([this, path]() {
        Project p;
        if (!ProjectSerializer::load_from_file(p, path)) {
            m_project_load_state.set(1.0f, "Failed to load project file", m_project_load_state.project_filename);
            m_project_load_state.success = false;
            m_project_load_state.worker_done.store(true);
            return;
        }

        m_project_load_state.set(0.15f, "Loading clips and media assets...", "");

        int total_items = static_cast<int>(p.video_assets.size() + p.audio_assets.size());
        if (total_items == 0) total_items = 1;
        int current_idx = 0;

        // Process and re-extract audio for video assets
        for (auto& va : p.video_assets) {
            current_idx++;
            float prog = 0.15f + 0.70f * (static_cast<float>(current_idx) / static_cast<float>(total_items));
            std::string item_name = fs::path(va.path).filename().string();
            if (item_name.empty()) item_name = va.id;

            m_project_load_state.set(prog, "Loading clips...", item_name);

            if (!va.is_image && !va.path.empty() && fs::exists(va.path)) {
                if (!va.has_audio || va.width <= 0) {
                    VideoAsset probed;
                    if (MediaDecoder::probe_video_file(va.path, probed)) {
                        va.has_audio = probed.has_audio;
                        if (va.width <= 0) va.width = probed.width;
                        if (va.height <= 0) va.height = probed.height;
                        if (va.fps <= 0.0) va.fps = probed.fps;
                        if (va.duration <= 0.0) va.duration = probed.duration;
                    }
                }
                if (va.has_audio) {
                    m_project_load_state.set(prog, "Re-extracting audio stream...", item_name);
                    m_audio_engine.preload_asset_audio(va.path);
                }
            }
        }

        // Process audio assets
        for (const auto& aa : p.audio_assets) {
            current_idx++;
            float prog = 0.15f + 0.70f * (static_cast<float>(current_idx) / static_cast<float>(total_items));
            std::string item_name = fs::path(aa.path).filename().string();
            if (item_name.empty()) item_name = aa.id;

            m_project_load_state.set(prog, "Re-extracting audio stream...", item_name);
            if (!aa.path.empty() && fs::exists(aa.path)) {
                m_audio_engine.preload_asset_audio(aa.path);
            }
        }

        m_project_load_state.set(0.95f, "Finalizing project timeline...", "");
        m_project_load_state.loaded_project = std::move(p);
        m_project_load_state.success = true;
        m_project_load_state.set(1.0f, "Project loaded successfully", "");
        m_project_load_state.worker_done.store(true);
    });
}

void App::poll_project_loading() {
    if (m_project_load_state.active.load() && m_project_load_state.worker_done.load()) {
        if (m_project_load_state.worker_thread) {
            if (m_project_load_state.worker_thread->joinable()) {
                m_project_load_state.worker_thread->join();
            }
            delete m_project_load_state.worker_thread;
            m_project_load_state.worker_thread = nullptr;
        }

        if (m_project_load_state.success) {
            push_undo_state();
            m_project = std::move(m_project_load_state.loaded_project);
            m_playhead_time = 0.0;
            m_selected_clip_id.clear();
            m_timeline.clear_selection();
            m_clip_manager.clear_selection();
            update_window_title();

            // Create OpenGL thumbnail textures on the main thread
            for (auto& va : m_project.video_assets) {
                if (va.thumbnail_texture == 0 && !va.path.empty() && fs::exists(va.path)) {
                    va.thumbnail_texture = MediaDecoder::create_thumbnail_texture(va.path, va.is_image ? 0.0 : std::min(1.0, va.duration * 0.2));
                }
            }

            m_audio_engine.update_project_snapshot(m_project);
            m_audio_engine.ensure_project_assets_loaded(m_project);
            m_last_requested_playhead = m_playhead_time;
            m_media_decoder.request_frame(m_playhead_time, m_project);
            set_status_notification("Project loaded: " + m_project.name, 3.5);
        } else {
            set_status_notification("Failed to load project: " + m_project_load_state.project_filename, 4.0);
        }

        m_project_load_state.active.store(false);
        m_project_load_state.worker_done.store(false);
    }
}

void App::render_project_load_modal() {
    if (!m_project_load_state.active.load()) return;

    ImGui::OpenPopup("Loading Project###ProjectLoadProgressModal");
    ImGui::SetNextWindowSize(ImVec2(460, 150), ImGuiCond_Always);
    float total_w = static_cast<float>(m_window_width);
    float total_h = static_cast<float>(m_window_height);
    ImGui::SetNextWindowPos(ImVec2(total_w * 0.5f, total_h * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("Loading Project###ProjectLoadProgressModal", nullptr,
                               ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Spacing();
        ImGui::Text("Project: %s", m_project_load_state.project_filename.c_str());
        ImGui::Spacing();

        float p = m_project_load_state.progress.load();
        std::string pct = std::to_string(static_cast<int>(p * 100.0f)) + "%";
        ImGui::ProgressBar(p, ImVec2(-1, 24), pct.c_str());
        ImGui::Spacing();

        std::string stage, item;
        m_project_load_state.get(stage, item);
        ImGui::TextColored(ImVec4(0.24f, 0.68f, 0.91f, 1.0f), "%s", stage.c_str());
        if (!item.empty()) {
            ImGui::TextDisabled("Item: %s", item.c_str());
        } else {
            ImGui::Spacing();
        }

        ImGui::EndPopup();
    }
}

void App::save_project(const std::string& path) {
    std::string target_path = path.empty() ? m_project.file_path : path;
    if (target_path.empty()) {
        save_project_as_browser();
        return;
    }
    ProjectSerializer::save_to_file(m_project, target_path);
    update_window_title();
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
        set_selected_clip_id(vc.id);

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
        set_selected_clip_id(ac.id);
    }

    m_audio_engine.update_project_snapshot(m_project);
    m_audio_engine.ensure_project_assets_loaded(m_project);
    m_last_requested_playhead = m_playhead_time;
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
            "Supported Video Files (*.mp4, *.mkv, *.mov, *.avi, *.webm, *.ogv, *.ogm)",
            "*.mp4 *.mkv *.mov *.avi *.webm *.ogv *.ogm",
            on_chosen
        );
        return;
    }

    m_file_browser.open(
        "Import Video Clip",
        FileBrowserMode::OpenFile,
        { { "Supported Video Files (*.mp4, *.mkv, *.mov, *.avi, *.webm, *.ogv, *.ogm)", { ".mp4", ".mkv", ".mov", ".avi", ".webm", ".ogv", ".ogm" } },
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

void App::generate_demo_media() {
    std::string test_v = "tests/assets/demo_colorbars.mp4";
    std::string test_a = "tests/assets/demo_tone.mp3";
    if (!fs::exists("tests/assets")) {
        test_v = "demo_colorbars.mp4";
        test_a = "demo_tone.mp3";
    }
    if (MediaDecoder::generate_test_media(test_v, test_a)) {
        push_undo_state();
        m_clip_manager.import_video(m_project, test_v);
        m_clip_manager.import_audio(m_project, test_a);
        m_audio_engine.update_project_snapshot(m_project);
    }
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
            "Any Supported Format (*.mp4 *.gif *.webp)",
            "*.mp4 *.gif *.webp",
            [this](const std::string& path) {
                m_export_manager.set_output_path(path);
            }
        );
        return;
    }

    m_file_browser.open(
        "Select Export Destination",
        FileBrowserMode::SaveFile,
        { { "Any Supported Format (*.mp4, *.gif, *.webp)", { ".mp4", ".gif", ".webp" } },
          { "MP4 Video (*.mp4)", { ".mp4" } },
          { "Animated GIF (*.gif)", { ".gif" } },
          { "Animated WebP (*.webp)", { ".webp" } },
          { "All Files (*.*)", { ".*" } } },
        default_file,
        "",
        [this](const std::string& path) {
            m_export_manager.set_output_path(path);
        },
        "Save"
    );
}

void App::process_events() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        Uint32 wid = get_event_window_id(event);
        if (m_export_window && wid == SDL_GetWindowID(m_export_window)) {
            if (m_export_imgui_ctx) {
                ImGui::SetCurrentContext(m_export_imgui_ctx);
                ImGui_ImplSDL2_ProcessEvent(&event);
            }
        } else if (m_preview_window && wid == SDL_GetWindowID(m_preview_window)) {
            if (m_preview_imgui_ctx) {
                ImGui::SetCurrentContext(m_preview_imgui_ctx);
                ImGui_ImplSDL2_ProcessEvent(&event);
            }
        } else {
            ImGui::SetCurrentContext(m_imgui_context);
            ImGui_ImplSDL2_ProcessEvent(&event);
        }

        if (event.type == SDL_QUIT) {
            m_running = false;
        } else if (event.type == SDL_WINDOWEVENT) {
            if (m_preview_window && event.window.windowID == SDL_GetWindowID(m_preview_window)) {
                if (event.window.event == SDL_WINDOWEVENT_CLOSE) {
                    set_preview_popped_out(false);
                }
            } else if (m_export_window && event.window.windowID == SDL_GetWindowID(m_export_window)) {
                if (event.window.event == SDL_WINDOWEVENT_CLOSE) {
                    if (m_export_manager.is_exporting()) {
                        m_export_manager.cancel_export();
                    }
                    m_export_manager.close_export_dialog();
                    m_export_manager.dismiss_status();
                }
            } else if (m_window && event.window.windowID == SDL_GetWindowID(m_window)) {
                if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
                    m_window_width = event.window.data1;
                    m_window_height = event.window.data2;
                }
            }
        } else if (event.type == SDL_KEYDOWN) {
            if (m_preview_window && event.key.windowID == SDL_GetWindowID(m_preview_window)) {
                if (event.key.keysym.sym == SDLK_ESCAPE) {
                    set_preview_popped_out(false);
                } else if (event.key.keysym.sym == SDLK_SPACE) {
                    toggle_playback();
                } else if (event.key.keysym.sym == SDLK_F11) {
                    Uint32 flags = SDL_GetWindowFlags(m_preview_window);
                    bool is_fullscreen = (flags & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
                    SDL_SetWindowFullscreen(m_preview_window, is_fullscreen ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
                }
            } else if (m_export_window && event.key.windowID == SDL_GetWindowID(m_export_window)) {
                if (event.key.keysym.sym == SDLK_ESCAPE) {
                    if (!m_export_manager.is_exporting()) {
                        m_export_manager.close_export_dialog();
                        m_export_manager.dismiss_status();
                    }
                }
            }
        } else if (event.type == SDL_TEXTEDITING) {
            if (ImGui::GetIO().WantTextInput) {
                m_ime_composition = event.edit.text;
                m_ime_cursor = event.edit.start;
                m_ime_selection_len = event.edit.length;
                m_text_preview_dirty = true;
            } else {
                m_ime_composition.clear();
            }
        } else if (event.type == SDL_TEXTINPUT) {
            if (ImGui::GetIO().WantTextInput) {
                m_ime_composition.clear();
                m_text_preview_dirty = true;
            } else {
                m_ime_composition.clear();
            }
        } else if (event.type == SDL_DROPFILE) {
            char* dropped_file = event.drop.file;
            if (dropped_file) {
                std::string path = dropped_file;
                SDL_free(dropped_file);
                if (fs::exists(path)) {
                    std::string ext = fs::path(path).extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                    if (ext == ".minicut") {
                        open_project(path);
                    } else if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".webp") {
                        push_undo_state();
                        m_clip_manager.start_async_import_image(path, 5.0, [this]() {
                            m_audio_engine.update_project_snapshot(m_project);
                        });
                    } else if (ext == ".mp3" || ext == ".wav" || ext == ".aac" || ext == ".ogg" || ext == ".flac" || ext == ".m4a" || ext == ".opus") {
                        push_undo_state();
                        m_clip_manager.start_async_import_audio(path, [this]() {
                            m_audio_engine.update_project_snapshot(m_project);
                            m_audio_engine.ensure_project_assets_loaded(m_project);
                        });
                    } else if (ext == ".mp4" || ext == ".mkv" || ext == ".mov" || ext == ".avi" || ext == ".webm" || ext == ".flv" || ext == ".wmv" || ext == ".m4v" || ext == ".ts" || ext == ".ogv" || ext == ".ogm") {
                        push_undo_state();
                        m_clip_manager.start_async_import_video(path, [this]() {
                            m_audio_engine.update_project_snapshot(m_project);
                            m_audio_engine.ensure_project_assets_loaded(m_project);
                        });
                    }
                }
            }
        }
    }
    if (m_imgui_context) {
        ImGui::SetCurrentContext(m_imgui_context);
    }
}

void App::update(double delta_time) {
    NativeFileDialog::poll_callbacks();
    m_script_engine.update(delta_time);

    // Prefab Hot-Reload check on disk
    if (m_settings_logic.get_prefab_hot_reload() && !m_project.prefabs.empty()) {
        m_prefab_poll_timer += static_cast<float>(delta_time);
        if (m_prefab_poll_timer >= m_settings_logic.get_prefab_reload_interval()) {
            m_prefab_poll_timer = 0.0f;
            m_clip_manager.check_and_reload_modified_prefabs(m_project, [this]() {
                on_project_modified();
                m_media_decoder.request_frame(m_playhead_time, m_project);
            });
        }
    }

    ImGuiIO& io = ImGui::GetIO();

    // Synchronize SDL text input state with Dear ImGui's text input focus.
    // When no text widget is active, deactivate OS IME so it does not intercept
    // timeline keyboard shortcuts across different language keyboard layouts.
    if (io.WantTextInput) {
        if (!SDL_IsTextInputActive()) {
            SDL_StartTextInput();
        }
    } else {
        if (SDL_IsTextInputActive()) {
            SDL_StopTextInput();
        }
        m_ime_composition.clear();
    }

    // ------------------------------------------------------------------------
    // Reliable Frame-Synchronized Keyboard Shortcuts
    // Only ignored if user is actively typing in a text field
    // ------------------------------------------------------------------------
    if (!io.WantTextInput) {
        // Escape: Clear all timeline and asset selections
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !is_any_dialog_open()) {
            m_timeline.clear_selection();
            set_selected_clip_id("");
            m_clip_manager.clear_selection();
        }

        // Spacebar: Play / Pause
        if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
            toggle_playback();
        }

        // Left / Right Arrows: Step 1 frame
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true)) {
            m_playhead_time = std::max(0.0, m_playhead_time - 1.0 / 30.0);
            m_audio_engine.set_playhead_time(m_playhead_time);
            m_last_requested_playhead = m_playhead_time;
            m_media_decoder.request_frame(m_playhead_time, m_project);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, true)) {
            m_playhead_time = m_playhead_time + 1.0 / 30.0;
            m_audio_engine.set_playhead_time(m_playhead_time);
            m_last_requested_playhead = m_playhead_time;
            m_media_decoder.request_frame(m_playhead_time, m_project);
        }

        // Home / End
        if (ImGui::IsKeyPressed(ImGuiKey_Home, false)) {
            m_playhead_time = 0.0;
            m_audio_engine.set_playhead_time(0.0);
            m_last_requested_playhead = 0.0;
            m_media_decoder.request_frame(0.0, m_project);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_End, false)) {
            m_playhead_time = m_project.get_total_duration();
            m_audio_engine.set_playhead_time(m_playhead_time);
            m_last_requested_playhead = m_playhead_time;
            m_media_decoder.request_frame(m_playhead_time, m_project);
        }

        // Transform Tool Hotkey: T (or Q) toggles universal transform gizmo
        if (!io.KeyCtrl && !io.KeyAlt && !io.KeyShift) {
            if (ImGui::IsKeyPressed(ImGuiKey_T, false) || ImGui::IsKeyPressed(ImGuiKey_Q, false)) {
                m_preview_transform_enabled = !m_preview_transform_enabled;
            }
        }

        // S: Split selected clip at playhead
        if (ImGui::IsKeyPressed(ImGuiKey_S, false) && !io.KeyCtrl) {
            push_undo_state();
            m_timeline.split_selected_clip(m_project, m_playhead_time);
            m_last_requested_playhead = m_playhead_time;
            m_media_decoder.request_frame(m_playhead_time, m_project);
        }

        // Delete / Backspace: Delete selected clips
        if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) {
            if (!m_timeline.get_selected_clip_ids().empty() || !m_selected_clip_id.empty()) {
                push_undo_state();
                m_timeline.delete_selected_clips(m_project);
                set_selected_clip_id("");
                m_last_requested_playhead = m_playhead_time;
                m_media_decoder.request_frame(m_playhead_time, m_project);
            }
        }

        // Ctrl + A: Select All clips
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false)) {
            m_timeline.select_all(m_project);
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
            m_timeline.copy_selected_clips(m_project);
        }

        // Ctrl + X: Cut
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_X, false)) {
            push_undo_state();
            m_timeline.cut_selected_clips(m_project);
            set_selected_clip_id("");
        }

        // Ctrl + V: Paste
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false)) {
            push_undo_state();
            double paste_t = m_timeline.get_mouse_hover_time();
            if (paste_t <= 0.0) paste_t = m_playhead_time;
            m_timeline.paste_clip(m_project, paste_t);
            m_last_requested_playhead = m_playhead_time;
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
        if (std::abs(m_playhead_time - m_last_requested_playhead) > 1e-5) {
            m_last_requested_playhead = m_playhead_time;
            m_media_decoder.request_frame(m_playhead_time, m_project);
        }
    }

    m_media_decoder.update_preview_texture(m_preview_texture, m_preview_tex_w, m_preview_tex_h, m_preview_tex_time);
    poll_project_loading();
}

void App::render_menu_bar() {
    float menu_font = SettingsLogic::instance().get_menu_font_size();
    float font_scale = (menu_font > 0.0f) ? (menu_font / 15.0f) : 1.0f;

    // More padding around menu area
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 7.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(14.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 6.0f));

    if (ImGui::BeginMainMenuBar()) {
        ImGui::SetWindowFontScale(font_scale);
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
            bool has_sel = !m_timeline.get_selected_clip_ids().empty() || !m_selected_clip_id.empty();
            if (ImGui::MenuItem(_TR("edit_cut"), "Ctrl+X", false, has_sel)) {
                push_undo_state();
                m_timeline.cut_selected_clips(m_project);
                m_selected_clip_id.clear();
                m_keyframe_editor.set_selected_clip("");
            }
            if (ImGui::MenuItem(_TR("edit_copy"), "Ctrl+C", false, has_sel)) {
                m_timeline.copy_selected_clips(m_project);
            }
            if (ImGui::MenuItem(_TR("edit_paste"), "Ctrl+V")) {
                push_undo_state();
                double paste_t = m_timeline.get_mouse_hover_time();
                if (paste_t <= 0.0) paste_t = m_playhead_time;
                m_timeline.paste_clip(m_project, paste_t, m_timeline.get_hovered_or_active_track());
                m_media_decoder.request_frame(m_playhead_time, m_project);
            }
            ImGui::Separator();
            if (ImGui::MenuItem(_TR("edit_select_all"), "Ctrl+A")) {
                m_timeline.select_all(m_project);
            }
            if (ImGui::MenuItem(_TR("edit_split"), "S", false, has_sel || !m_project.tracks.empty())) {
                push_undo_state();
                m_timeline.split_selected_clip(m_project, m_playhead_time);
                m_media_decoder.request_frame(m_playhead_time, m_project);
            }
            std::string del_label = (m_timeline.get_selected_clip_ids().size() > 1)
                ? (std::string(_TR("edit_delete")) + " (" + std::to_string(m_timeline.get_selected_clip_ids().size()) + ")")
                : std::string(_TR("edit_delete"));
            if (ImGui::MenuItem(del_label.c_str(), "Del", false, has_sel)) {
                push_undo_state();
                m_timeline.delete_selected_clips(m_project);
                m_selected_clip_id.clear();
                m_keyframe_editor.set_selected_clip("");
                m_media_decoder.request_frame(m_playhead_time, m_project);
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu(_TR("menu_assets"))) {
            if (ImGui::MenuItem(_TR("project_import_video"), "Ctrl+I")) { import_video_browser(); }
            if (ImGui::MenuItem(_TR("project_import_audio"))) { import_audio_browser(); }
            if (ImGui::MenuItem(_TR("project_import_image"))) { import_image_browser(); }
            if (ImGui::MenuItem(_TR("project_import_prefab"))) { import_prefab_browser(); }
            ImGui::Separator();
            if (ImGui::MenuItem(_TR("project_generate_demo"))) {
                generate_demo_media();
            }
            ImGui::Separator();
            if (ImGui::MenuItem(_TR("text_add_asset"))) { open_text_dialog(); }
            if (ImGui::MenuItem("Add Text-to-Speech...")) { open_tts_dialog(); }
            ImGui::Separator();
            if (ImGui::MenuItem("Add Subtitle...")) { add_subtitle_at_playhead(); }
            if (ImGui::MenuItem("Add Subtitle Track")) {
                m_timeline.add_track(m_project, "subtitle");
                on_project_modified();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu(_TR("menu_modifiers"))) {
            if (ImGui::BeginMenu("AI Tools")) {
                if (ImGui::MenuItem("AI Script Generator...")) { open_ai_dialog(); }
                ImGui::EndMenu();
            }
            ImGui::Separator();
            m_script_engine.render_scripts_menu();
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu(_TR("menu_help"))) {
            ImGui::MenuItem("minicut v" MINICUT_VERSION, nullptr, false, false);
            ImGui::Separator();

            std::string cur_loc = LocaleManager::instance().current_locale();
            if (cur_loc == "jp") cur_loc = "ja";

            auto open_api_doc = [&](const std::string& api_prefix) {
                const char* home = std::getenv("HOME");
                std::vector<std::string> candidates;
                if (!cur_loc.empty() && cur_loc != "en") {
                    std::string loc_pdf = api_prefix + "_" + cur_loc + ".pdf";
                    candidates.push_back("docs/" + loc_pdf);
                    candidates.push_back("../docs/" + loc_pdf);
                    if (home) {
                        candidates.push_back(std::string(home) + "/.config/minicut/docs/" + loc_pdf);
                        candidates.push_back(std::string(home) + "/.local/share/minicut/docs/" + loc_pdf);
                    }
                    candidates.push_back("/home/kaiju/.local/bin/docs/" + loc_pdf);
                }
                std::string base_pdf = api_prefix + ".pdf";
                candidates.push_back("docs/" + base_pdf);
                candidates.push_back("../docs/" + base_pdf);
                if (home) {
                    candidates.push_back(std::string(home) + "/.config/minicut/docs/" + base_pdf);
                    candidates.push_back(std::string(home) + "/.local/share/minicut/docs/" + base_pdf);
                }
                candidates.push_back("/home/kaiju/.local/bin/docs/" + base_pdf);

                for (const auto& p : candidates) {
                    if (!p.empty() && fs::exists(p)) {
                        std::string cmd = "xdg-open \"" + p + "\" >/dev/null 2>&1 &";
                        std::system(cmd.c_str());
                        break;
                    }
                }
            };

            if (ImGui::MenuItem(_TR("menu_help_lua_api"))) {
                open_api_doc("minicut_lua_api");
            }

            if (ImGui::MenuItem(_TR("menu_help_js_api"))) {
                open_api_doc("minicut_js_api");
            }

            ImGui::Separator();
            if (ImGui::MenuItem(_TR("help_about"))) { m_show_about = true; }
            ImGui::EndMenu();
        }

        // Right-aligned notifications & status indicators
        float win_w = ImGui::GetWindowWidth();

        std::string settings_btn_text = _TR("menu_settings");
        if (settings_btn_text.empty()) settings_btn_text = "Settings";

        float text_w = ImGui::CalcTextSize(settings_btn_text.c_str()).x;
        float settings_btn_h = std::max(24.0f, m_menu_bar_height * 0.72f);
        float icon_sz = (m_settings_icon_texture != 0) ? std::min(15.0f, settings_btn_h - 6.0f) : 0.0f;
        float icon_gap = (m_settings_icon_texture != 0) ? 9.0f : 0.0f;
        float settings_btn_w = 12.0f + icon_sz + icon_gap + text_w + 12.0f;
        float right_x = win_w - settings_btn_w - 12.0f;
        float settings_y = std::max(0.0f, (m_menu_bar_height - settings_btn_h) * 0.5f);

        // Reserved space for shader progress bar to the right of project name and time
        float shader_bar_w = 130.0f;
        float shader_bar_h = 16.0f;
        float shader_bar_x = right_x - shader_bar_w - 12.0f;

        std::string status = "Project: " + m_project.name + " | Time: " + format_timecode(m_playhead_time);
        float status_w = ImGui::CalcTextSize(status.c_str()).x;
        float status_x = shader_bar_x - status_w - 14.0f;

        if (status_x > 320.0f) {
            // Notifications to the left of Project Name & Time
            if (!m_status_notification.empty()) {
                float notif_w = ImGui::CalcTextSize(m_status_notification.c_str()).x;
                float notif_x = status_x - notif_w - 18.0f;
                if (notif_x > 320.0f) {
                    ImGui::SetCursorPosX(notif_x);
                    ImGui::TextColored(ImVec4(0.2f, 0.85f, 0.45f, 1.0f), "%s", m_status_notification.c_str());
                }
            }

            // Project Name & Time
            ImGui::SetCursorPosX(status_x);
            ImGui::TextDisabled("%s", status.c_str());
        }

        // Automatic shader progress bar to the right of project name and time
        // Remains hidden until used, permanently preserving its layout space
        if (m_script_engine.is_rendering_shader() && shader_bar_x > 320.0f) {
            float bar_y = std::max(0.0f, (m_menu_bar_height - shader_bar_h) * 0.5f);
            ImGui::SetCursorPos(ImVec2(shader_bar_x, bar_y));
            float p = m_script_engine.get_shader_progress();
            char bar_text[32];
            snprintf(bar_text, sizeof(bar_text), "Shader %d%%", static_cast<int>(p * 100.0f));
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.24f, 0.68f, 0.91f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.12f, 0.14f, 0.18f, 0.8f));
            ImGui::ProgressBar(p, ImVec2(shader_bar_w, shader_bar_h), bar_text);
            ImGui::PopStyleColor(2);
            ImGui::SameLine();
        }

        // Far-right Settings button with text and dark blue background
        ImGui::SetCursorPos(ImVec2(right_x, settings_y));
        ImVec2 p_min = ImGui::GetCursorScreenPos();
        ImVec2 p_max = ImVec2(p_min.x + settings_btn_w, p_min.y + settings_btn_h);

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.08f, 0.22f, 0.48f, 1.0f));         // Dark blue background
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.14f, 0.35f, 0.68f, 1.0f));  // Lighter blue on hover
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.20f, 0.45f, 0.82f, 1.0f));   // Bright blue when clicked
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.95f, 0.98f, 1.0f));           // Clear white text
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 3.0f));

        bool clicked = ImGui::Button("##SettingsBtn", ImVec2(settings_btn_w, settings_btn_h));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float cur_x = p_min.x + 12.0f;
        if (m_settings_icon_texture != 0) {
            float icon_y = p_min.y + (settings_btn_h - icon_sz) * 0.5f;
            dl->AddImage((ImTextureID)(intptr_t)m_settings_icon_texture,
                         ImVec2(cur_x, icon_y),
                         ImVec2(cur_x + icon_sz, icon_y + icon_sz));
            cur_x += icon_sz + icon_gap;
        }
        float text_y = p_min.y + (settings_btn_h - ImGui::GetFontSize()) * 0.5f;
        dl->AddText(ImVec2(cur_x, text_y), IM_COL32(245, 245, 250, 255), settings_btn_text.c_str());

        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(4);

        if (clicked) {
            open_settings_dialog();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", settings_btn_text.c_str());
        }

        ImGui::EndMainMenuBar();
    }
    ImGui::PopStyleVar(3);
}

void App::render_shelf_dock() {
    float menu_h = m_menu_bar_height > 0.0f ? m_menu_bar_height : ImGui::GetFrameHeight();
    m_script_engine.render_shelf_dock(menu_h);
}

void App::render_clip_panel() {
    std::string title = std::string(_TR("panel_clips")) + "###ClipsPanel";
    ImGui::Begin(title.c_str(), nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus);

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
        },
        // On add text
        [this]() {
            open_text_dialog();
        },
        // On add tts
        [this]() {
            open_tts_dialog();
        },
        // On generate demo
        [this]() {
            generate_demo_media();
        }
    );


    ImGui::End();
}


void App::render_popout_preview_window() {
    if (!m_preview_window || !m_preview_imgui_ctx) return;

    SDL_GL_MakeCurrent(m_preview_window, m_gl_context);
    ImGui::SetCurrentContext(m_preview_imgui_ctx);
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    int pw = 0, ph = 0;
    SDL_GL_GetDrawableSize(m_preview_window, &pw, &ph);

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(static_cast<float>(pw), static_cast<float>(ph)));
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoBringToFrontOnFocus;
    if (ImGui::Begin("DetachedPreviewWindowRoot", nullptr, flags)) {
        render_preview_contents(true);
        ImGui::End();
    }

    ImGui::Render();
    glViewport(0, 0, pw, ph);
    glClearColor(0.04f, 0.04f, 0.05f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SDL_GL_SwapWindow(m_preview_window);

    // Restore main context
    SDL_GL_MakeCurrent(m_window, m_gl_context);
    ImGui::SetCurrentContext(m_imgui_context);
}

void App::render_export_os_window() {
    if (!m_export_window || !m_export_imgui_ctx) return;

    // Update window title with progress if exporting
    if (m_export_manager.is_exporting()) {
        char buf[128];
        snprintf(buf, sizeof(buf), "Exporting Timeline (%.0f%%) - Minicut", m_export_manager.get_progress() * 100.0f);
        SDL_SetWindowTitle(m_export_window, buf);
    } else if (m_export_manager.is_finished()) {
        SDL_SetWindowTitle(m_export_window, "Export Finished - Minicut");
    } else if (m_export_manager.is_error()) {
        SDL_SetWindowTitle(m_export_window, "Export Error - Minicut");
    } else {
        SDL_SetWindowTitle(m_export_window, "Export Timeline Media - Minicut");
    }

    SDL_GL_MakeCurrent(m_export_window, m_gl_context);
    ImGui::SetCurrentContext(m_export_imgui_ctx);
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    int ew = 0, eh = 0;
    SDL_GL_GetDrawableSize(m_export_window, &ew, &eh);

    m_export_manager.render_ui(
        [this](const ExportConfig& cfg) {
            m_export_manager.start_export(m_project, cfg);
        },
        true // standalone_window
    );

    ImGui::Render();
    glViewport(0, 0, ew, eh);
    glClearColor(0.14f, 0.15f, 0.18f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SDL_GL_SwapWindow(m_export_window);

    // Restore main context
    SDL_GL_MakeCurrent(m_window, m_gl_context);
    ImGui::SetCurrentContext(m_imgui_context);
}

void App::render_preview_panel() {
    std::string title = std::string(_TR("panel_preview")) + "###PreviewPanel";
    ImGui::Begin(title.c_str(), nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus);

    if (m_preview_popped_out) {
        ImVec2 avail = ImGui::GetContentRegionAvail();
        float btn_w = 260.0f;
        float btn_h = 36.0f;
        float center_x = std::max(10.0f, (avail.x - btn_w) * 0.5f);
        float center_y = std::max(10.0f, (avail.y - btn_h) * 0.5f - 20.0f);
        ImGui::SetCursorPos(ImVec2(center_x, center_y));
        ImGui::TextColored(ImVec4(0.40f, 0.75f, 1.0f, 1.0f), "Video Preview is Detached (Separate Window)");
        ImGui::SetCursorPosX(center_x - 30.0f);
        ImGui::TextDisabled("Drag preview window across monitors. Press F11 for Fullscreen.");
        ImGui::SetCursorPosX(center_x);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f);
        if (ImGui::Button("Attach Preview Back (Dock)", ImVec2(btn_w, btn_h))) {
            set_preview_popped_out(false);
        }
    } else {
        render_preview_contents(false);
    }
    ImGui::End();
}

void App::render_preview_toolbar(bool is_detached) {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 3.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 0.0f));

    // 1. Universal Transform Toggle Button
    if (m_preview_transform_enabled) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.24f, 0.68f, 0.91f, 0.60f));
    }
    if (ImGui::ImageButton("##btn_transform", (ImTextureID)(intptr_t)m_icon_tex_transform, ImVec2(20, 20))) {
        m_preview_transform_enabled = !m_preview_transform_enabled;
    }
    if (m_preview_transform_enabled) {
        ImGui::PopStyleColor();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Transform Tool (T)\nToggle universal transform gizmo (drag inside to move, corners to scale, top pin to rotate).");
    }

    ImGui::SameLine();
    // 2. Reset Button
    if (ImGui::ImageButton("##btn_reset", (ImTextureID)(intptr_t)m_icon_tex_reset, ImVec2(20, 20))) {
        Clip* clip = find_selected_clip();
        if (clip && is_selected_clip_visual()) {
            push_undo_state();
            double r_t = clip->clip_relative_time(m_playhead_time);
            clip->pos_x = 0.0;
            clip->pos_y = 0.0;
            clip->scale = 1.0;
            clip->scale_x = 1.0;
            clip->scale_y = 1.0;
            clip->rotation = 0.0;
            if (!m_project.get_clip_keyframes(clip->id, "pos_x").empty()) {
                m_project.set_or_add_keyframe(clip->id, "pos_x", r_t, 0.0);
            }
            if (!m_project.get_clip_keyframes(clip->id, "pos_y").empty()) {
                m_project.set_or_add_keyframe(clip->id, "pos_y", r_t, 0.0);
            }
            if (!m_project.get_clip_keyframes(clip->id, "scale_x").empty()) {
                m_project.set_or_add_keyframe(clip->id, "scale_x", r_t, 1.0);
            }
            if (!m_project.get_clip_keyframes(clip->id, "scale_y").empty()) {
                m_project.set_or_add_keyframe(clip->id, "scale_y", r_t, 1.0);
            }
            if (!m_project.get_clip_keyframes(clip->id, "scale").empty()) {
                m_project.set_or_add_keyframe(clip->id, "scale", r_t, 1.0);
            }
            if (!m_project.get_clip_keyframes(clip->id, "rotation").empty()) {
                m_project.set_or_add_keyframe(clip->id, "rotation", r_t, 0.0);
            }
            on_project_modified();
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Reset Transform\nCenter and reset scale X/Y (1.0) & rotation (0°).");
    }

    ImGui::SameLine();
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();

    // Quick numerical properties for selected visual clip
    Clip* sel_clip = find_selected_clip();
    bool is_visual = sel_clip && is_selected_clip_visual();
    if (is_visual) {
        double rel_time = sel_clip->clip_relative_time(m_playhead_time);
        double cur_pos_x   = m_project.get_clip_param(*sel_clip, "pos_x", rel_time);
        double cur_pos_y   = m_project.get_clip_param(*sel_clip, "pos_y", rel_time);
        double cur_scale_x = m_project.get_clip_param(*sel_clip, "scale_x", rel_time);
        double cur_scale_y = m_project.get_clip_param(*sel_clip, "scale_y", rel_time);
        double cur_rot     = m_project.get_clip_param(*sel_clip, "rotation", rel_time);

        float px = static_cast<float>(cur_pos_x);
        float py = static_cast<float>(cur_pos_y);
        float sx = static_cast<float>(cur_scale_x);
        float sy = static_cast<float>(cur_scale_y);
        float rot = static_cast<float>(cur_rot);

        ImGui::Text("X:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(55.0f);
        if (ImGui::DragFloat("##tx", &px, 1.0f, -4000.0f, 4000.0f, "%.0f")) {
            push_undo_state();
            sel_clip->pos_x = px;
            if (!m_project.get_clip_keyframes(sel_clip->id, "pos_x").empty()) {
                m_project.set_or_add_keyframe(sel_clip->id, "pos_x", rel_time, px);
            }
            on_project_modified();
        }
        ImGui::SameLine();
        ImGui::Text("Y:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(55.0f);
        if (ImGui::DragFloat("##ty", &py, 1.0f, -4000.0f, 4000.0f, "%.0f")) {
            push_undo_state();
            sel_clip->pos_y = py;
            if (!m_project.get_clip_keyframes(sel_clip->id, "pos_y").empty()) {
                m_project.set_or_add_keyframe(sel_clip->id, "pos_y", rel_time, py);
            }
            on_project_modified();
        }
        ImGui::SameLine();
        ImGui::Text("SX:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(50.0f);
        if (ImGui::DragFloat("##tsx", &sx, 0.01f, 0.05f, 10.0f, "%.2f")) {
            push_undo_state();
            sel_clip->scale_x = std::max(0.01f, sx);
            sel_clip->scale = sel_clip->scale_x;
            if (!m_project.get_clip_keyframes(sel_clip->id, "scale_x").empty()) {
                m_project.set_or_add_keyframe(sel_clip->id, "scale_x", rel_time, sel_clip->scale_x);
            }
            if (!m_project.get_clip_keyframes(sel_clip->id, "scale").empty()) {
                m_project.set_or_add_keyframe(sel_clip->id, "scale", rel_time, sel_clip->scale_x);
            }
            on_project_modified();
        }
        ImGui::SameLine();
        ImGui::Text("SY:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(50.0f);
        if (ImGui::DragFloat("##tsy", &sy, 0.01f, 0.05f, 10.0f, "%.2f")) {
            push_undo_state();
            sel_clip->scale_y = std::max(0.01f, sy);
            if (!m_project.get_clip_keyframes(sel_clip->id, "scale_y").empty()) {
                m_project.set_or_add_keyframe(sel_clip->id, "scale_y", rel_time, sel_clip->scale_y);
            }
            on_project_modified();
        }
        ImGui::SameLine();
        ImGui::Text("R:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(52.0f);
        if (ImGui::DragFloat("##tr", &rot, 0.5f, -360.0f, 360.0f, "%.1f°")) {
            push_undo_state();
            sel_clip->rotation = rot;
            if (!m_project.get_clip_keyframes(sel_clip->id, "rotation").empty()) {
                m_project.set_or_add_keyframe(sel_clip->id, "rotation", rel_time, rot);
            }
            on_project_modified();
        }
    } else {
        ImGui::TextDisabled("No visual clip selected");
    }

    // Right-aligned Pop-Out / Dock button
    float pop_btn_w = 78.0f;
    float rem_w = ImGui::GetContentRegionAvail().x;
    if (rem_w > pop_btn_w + 10.0f) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (rem_w - pop_btn_w));
    }
    if (is_detached) {
        if (ImGui::Button("Dock In##PopIn", ImVec2(pop_btn_w, 24.0f))) {
            set_preview_popped_out(false);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Dock preview back into main application panel");
    } else {
        if (ImGui::Button("Pop Out##PopOut", ImVec2(pop_btn_w, 24.0f))) {
            set_preview_popped_out(true);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pop out preview into floating window for dual-monitor workflows");
    }

    ImGui::PopStyleVar(2);
    ImGui::Separator();
}

void App::render_preview_viewport() {
    float transport_h = 38.0f;
    ImVec2 avail = ImGui::GetContentRegionAvail();
    float video_area_h = avail.y - transport_h - 4.0f;
    if (video_area_h < 80.0f) video_area_h = 80.0f;

    ImVec2 vp_screen_pos = ImGui::GetCursorScreenPos();
    ImVec2 vp_screen_end = ImVec2(vp_screen_pos.x + avail.x, vp_screen_pos.y + video_area_h);

    // Padding area settings
    float pad = SettingsLogic::instance().get_preview_padding();
    float usable_w = std::max(40.0f, avail.x - 2.0f * pad);
    float usable_h = std::max(40.0f, video_area_h - 2.0f * pad);

    float aspect = 16.0f / 9.0f;
    switch (m_preview_aspect_idx) {
        case 0: aspect = static_cast<float>(m_project.get_largest_asset_aspect_ratio()); break;
        case 1: aspect = 16.0f / 9.0f; break;
        case 2: aspect = 9.0f / 16.0f; break;
        case 3: aspect = 4.0f / 3.0f; break;
        case 4: aspect = 1.0f / 1.0f; break;
        case 5: aspect = 21.0f / 9.0f; break;
        case 6: aspect = 4.0f / 5.0f; break;
        default: aspect = static_cast<float>(m_project.get_largest_asset_aspect_ratio()); break;
    }
    if (aspect <= 0.05f) aspect = 16.0f / 9.0f;

    float disp_w = usable_w;
    float disp_h = disp_w / aspect;

    if (disp_h > usable_h) {
        disp_h = usable_h;
        disp_w = disp_h * aspect;
    }

    float offset_x = pad + (usable_w - disp_w) * 0.5f;
    float offset_y = pad + (usable_h - disp_h) * 0.5f;

    // Draw preview window background padding area
    ImDrawList* dl = ImGui::GetWindowDrawList();
    int bg_mode = SettingsLogic::instance().get_preview_bg_mode();
    ImU32 bg_color = IM_COL32(20, 22, 26, 255); // Default dark
    if (bg_mode == 1) {
        bg_color = IM_COL32(0, 0, 0, 255); // Solid black
    } else if (bg_mode == 2) {
        bg_color = IM_COL32(13, 19, 32, 255); // Deep navy
    } else if (bg_mode == 3) {
        bg_color = IM_COL32(64, 64, 64, 255); // Neutral Gray
    } else if (bg_mode == 5) {
        const float* c = SettingsLogic::instance().get_preview_bg_custom_color();
        bg_color = ImGui::ColorConvertFloat4ToU32(ImVec4(c[0], c[1], c[2], c[3]));
    }

    if (bg_mode == 4) {
        // Checkerboard / transparency pattern
        dl->AddRectFilled(vp_screen_pos, vp_screen_end, IM_COL32(32, 32, 36, 255), 4.0f);
        float check_sz = 16.0f;
        ImU32 check_col1 = IM_COL32(48, 48, 54, 255);
        for (float y = vp_screen_pos.y; y < vp_screen_end.y; y += check_sz) {
            for (float x = vp_screen_pos.x; x < vp_screen_end.x; x += check_sz) {
                int ix = static_cast<int>((x - vp_screen_pos.x) / check_sz);
                int iy = static_cast<int>((y - vp_screen_pos.y) / check_sz);
                if ((ix + iy) % 2 == 1) {
                    ImVec2 p1(x, y);
                    ImVec2 p2(std::min(x + check_sz, vp_screen_end.x), std::min(y + check_sz, vp_screen_end.y));
                    dl->AddRectFilled(p1, p2, check_col1);
                }
            }
        }
    } else {
        dl->AddRectFilled(vp_screen_pos, vp_screen_end, bg_color, 4.0f);
    }
    dl->AddRect(vp_screen_pos, vp_screen_end, IM_COL32(45, 50, 60, 200), 4.0f);

    ImVec2 img_screen_pos = ImVec2(vp_screen_pos.x + offset_x, vp_screen_pos.y + offset_y);

    ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + offset_x, ImGui::GetCursorPosY() + offset_y));
    ImGui::Image((ImTextureID)(intptr_t)m_preview_texture, ImVec2(disp_w, disp_h));

    // Render active subtitles over video preview
    for (const auto& track : m_project.tracks) {
        if (track.type == "subtitle" && !track.muted) {
            for (const auto& clip : track.clips) {
                if (clip.contains_time(m_playhead_time) && !clip.subtitle_text.empty()) {
                    ImVec2 text_size = ImGui::CalcTextSize(clip.subtitle_text.c_str());
                    float box_pad = 10.0f;
                    float text_x = img_screen_pos.x + (disp_w - text_size.x) * 0.5f;
                    float text_y = img_screen_pos.y + disp_h - text_size.y - 24.0f;

                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    dl->AddRectFilled(ImVec2(text_x - box_pad, text_y - box_pad * 0.5f),
                                      ImVec2(text_x + text_size.x + box_pad, text_y + text_size.y + box_pad * 0.5f),
                                      IM_COL32(0, 0, 0, 195), 4.0f);
                    dl->AddText(ImVec2(text_x, text_y), IM_COL32(255, 255, 255, 255), clip.subtitle_text.c_str());
                }
            }
        }
    }

    // Render Interactive Transform Gizmo for selected visual clip
    Clip* sel_clip = find_selected_clip();
    bool is_visual = sel_clip && is_selected_clip_visual();
    if (m_preview_transform_enabled && is_visual && sel_clip->contains_time(m_playhead_time)) {
        double rel_time = sel_clip->clip_relative_time(m_playhead_time);
        double cur_pos_x   = m_project.get_clip_param(*sel_clip, "pos_x", rel_time);
        double cur_pos_y   = m_project.get_clip_param(*sel_clip, "pos_y", rel_time);
        double cur_scale_x = m_project.get_clip_param(*sel_clip, "scale_x", rel_time);
        double cur_scale_y = m_project.get_clip_param(*sel_clip, "scale_y", rel_time);
        double cur_rot     = m_project.get_clip_param(*sel_clip, "rotation", rel_time);

        int src_w = 1280, src_h = 720;
        double asset_sx = 1.0, asset_sy = 1.0;
        const VideoAsset* va = m_project.find_video_asset(sel_clip->asset_id);
        if (va) {
            if (va->width > 0 && va->height > 0) {
                src_w = va->width;
                src_h = va->height;
            }
            asset_sx = va->scale_x;
            asset_sy = va->scale_y;
        }

        double base_scale = std::min(1280.0 / src_w, 720.0 / src_h);
        double total_scale_x = base_scale * cur_scale_x * asset_sx;
        double total_scale_y = base_scale * cur_scale_y * asset_sy;
        double half_w = (src_w * total_scale_x) * 0.5;
        double half_h = (src_h * total_scale_y) * 0.5;

        double f_rad = cur_rot * (3.14159265358979323846 / 180.0);
        double cos_f = std::cos(f_rad);
        double sin_f = std::sin(f_rad);

        double cx_1280 = 1280.0 * 0.5 + cur_pos_x;
        double cy_720  = 720.0  * 0.5 + cur_pos_y;

        double corners_x[4] = { -half_w,  half_w,  half_w, -half_w };
        double corners_y[4] = { -half_h, -half_h,  half_h,  half_h };

        ImVec2 screen_corners[4];
        for (int k = 0; k < 4; ++k) {
            double px = cx_1280 + corners_x[k] * cos_f - corners_y[k] * sin_f;
            double py = cy_720  + corners_x[k] * sin_f + corners_y[k] * cos_f;
            screen_corners[k].x = img_screen_pos.x + static_cast<float>((px / 1280.0) * disp_w);
            screen_corners[k].y = img_screen_pos.y + static_cast<float>((py / 720.0) * disp_h);
        }

        ImVec2 screen_center;
        screen_center.x = img_screen_pos.x + static_cast<float>((cx_1280 / 1280.0) * disp_w);
        screen_center.y = img_screen_pos.y + static_cast<float>((cy_720 / 720.0) * disp_h);

        ImVec2 top_mid;
        top_mid.x = (screen_corners[0].x + screen_corners[1].x) * 0.5f;
        top_mid.y = (screen_corners[0].y + screen_corners[1].y) * 0.5f;

        float pin_dist = 28.0f;
        ImVec2 rot_pin(top_mid.x + static_cast<float>(sin_f) * pin_dist,
                       top_mid.y - static_cast<float>(cos_f) * pin_dist);

        ImVec2 mouse_pos = ImGui::GetIO().MousePos;
        bool is_window_hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);

        // Check hover on rotation pin
        bool hovered_rot_pin = is_window_hovered && (std::hypot(mouse_pos.x - rot_pin.x, mouse_pos.y - rot_pin.y) <= 10.0f);

        // Check hover on corners
        int hovered_corner = -1;
        if (is_window_hovered && !hovered_rot_pin) {
            for (int k = 0; k < 4; ++k) {
                if (std::hypot(mouse_pos.x - screen_corners[k].x, mouse_pos.y - screen_corners[k].y) <= 9.0f) {
                    hovered_corner = k;
                    break;
                }
            }
        }

        // Check point in quad
        auto pt_in_quad = [](const ImVec2& pt, const ImVec2* p) -> bool {
            float c[4];
            for (int i = 0; i < 4; ++i) {
                int n = (i + 1) % 4;
                c[i] = (p[n].x - p[i].x) * (pt.y - p[i].y) - (p[n].y - p[i].y) * (pt.x - p[i].x);
            }
            bool all_pos = (c[0] >= 0.0f && c[1] >= 0.0f && c[2] >= 0.0f && c[3] >= 0.0f);
            bool all_neg = (c[0] <= 0.0f && c[1] <= 0.0f && c[2] <= 0.0f && c[3] <= 0.0f);
            return all_pos || all_neg;
        };
        bool inside_quad = is_window_hovered && hovered_corner < 0 && !hovered_rot_pin && pt_in_quad(mouse_pos, screen_corners);

        // Mouse Drag Start
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && is_window_hovered) {
            if (hovered_rot_pin) {
                m_gizmo_drag_mode = GizmoDragMode::RotateCorner;
                m_gizmo_drag_start_mouse = mouse_pos;
                m_gizmo_orig_rotation = cur_rot;
                m_gizmo_orig_angle = std::atan2(mouse_pos.y - screen_center.y, mouse_pos.x - screen_center.x) * (180.0 / 3.141592653589793);
                push_undo_state();
            } else if (hovered_corner >= 0) {
                m_gizmo_drag_corner = hovered_corner;
                m_gizmo_drag_start_mouse = mouse_pos;
                m_gizmo_drag_mode = GizmoDragMode::ScaleCorner;
                m_gizmo_orig_scale = cur_scale_x;
                m_gizmo_orig_scale_x = cur_scale_x;
                m_gizmo_orig_scale_y = cur_scale_y;
                m_gizmo_orig_dist = std::max(1.0f, std::hypot(mouse_pos.x - screen_center.x, mouse_pos.y - screen_center.y));
                push_undo_state();
            } else if (inside_quad) {
                m_gizmo_drag_mode = GizmoDragMode::Translate;
                m_gizmo_drag_start_mouse = mouse_pos;
                m_gizmo_orig_pos_x = cur_pos_x;
                m_gizmo_orig_pos_y = cur_pos_y;
                push_undo_state();
            }
        }

        // Mouse Drag Execution
        if (m_gizmo_drag_mode != GizmoDragMode::None) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                if (m_gizmo_drag_mode == GizmoDragMode::Translate) {
                    float dx = mouse_pos.x - m_gizmo_drag_start_mouse.x;
                    float dy = mouse_pos.y - m_gizmo_drag_start_mouse.y;
                    double new_x = m_gizmo_orig_pos_x + static_cast<double>(dx * (1280.0f / disp_w));
                    double new_y = m_gizmo_orig_pos_y + static_cast<double>(dy * (720.0f / disp_h));
                    sel_clip->pos_x = new_x;
                    sel_clip->pos_y = new_y;
                    if (!m_project.get_clip_keyframes(sel_clip->id, "pos_x").empty()) {
                        m_project.set_or_add_keyframe(sel_clip->id, "pos_x", rel_time, new_x);
                    }
                    if (!m_project.get_clip_keyframes(sel_clip->id, "pos_y").empty()) {
                        m_project.set_or_add_keyframe(sel_clip->id, "pos_y", rel_time, new_y);
                    }
                    on_project_modified();
                    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
                } else if (m_gizmo_drag_mode == GizmoDragMode::ScaleCorner) {
                    float cur_dist = std::hypot(mouse_pos.x - screen_center.x, mouse_pos.y - screen_center.y);
                    double ratio = cur_dist / m_gizmo_orig_dist;
                    double new_scale_x = std::max(0.01, m_gizmo_orig_scale_x * ratio);
                    double new_scale_y = std::max(0.01, m_gizmo_orig_scale_y * ratio);
                    sel_clip->scale = new_scale_x;
                    sel_clip->scale_x = new_scale_x;
                    sel_clip->scale_y = new_scale_y;
                    if (!m_project.get_clip_keyframes(sel_clip->id, "scale_x").empty()) {
                        m_project.set_or_add_keyframe(sel_clip->id, "scale_x", rel_time, new_scale_x);
                    }
                    if (!m_project.get_clip_keyframes(sel_clip->id, "scale_y").empty()) {
                        m_project.set_or_add_keyframe(sel_clip->id, "scale_y", rel_time, new_scale_y);
                    }
                    if (!m_project.get_clip_keyframes(sel_clip->id, "scale").empty()) {
                        m_project.set_or_add_keyframe(sel_clip->id, "scale", rel_time, new_scale_x);
                    }
                    on_project_modified();
                    ImGui::SetMouseCursor((m_gizmo_drag_corner == 0 || m_gizmo_drag_corner == 2) ? ImGuiMouseCursor_ResizeNWSE : ImGuiMouseCursor_ResizeNESW);
                } else if (m_gizmo_drag_mode == GizmoDragMode::RotateCorner) {
                    double cur_angle = std::atan2(mouse_pos.y - screen_center.y, mouse_pos.x - screen_center.x) * (180.0 / 3.141592653589793);
                    double delta_angle = cur_angle - m_gizmo_orig_angle;
                    double new_rot = m_gizmo_orig_rotation + delta_angle;
                    sel_clip->rotation = new_rot;
                    if (!m_project.get_clip_keyframes(sel_clip->id, "rotation").empty()) {
                        m_project.set_or_add_keyframe(sel_clip->id, "rotation", rel_time, new_rot);
                    }
                    on_project_modified();
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                }
            } else {
                m_gizmo_drag_mode = GizmoDragMode::None;
                m_gizmo_drag_corner = -1;
            }
        } else {
            // Set hover cursor
            if (hovered_rot_pin) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            } else if (hovered_corner >= 0) {
                ImGui::SetMouseCursor((hovered_corner == 0 || hovered_corner == 2) ? ImGuiMouseCursor_ResizeNWSE : ImGuiMouseCursor_ResizeNESW);
            } else if (inside_quad) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
            }
        }

        // Draw Gizmo Overlay
        ImDrawList* dl = ImGui::GetWindowDrawList();
        // 1. Quad outline
        ImVec2 quad_pts[5] = { screen_corners[0], screen_corners[1], screen_corners[2], screen_corners[3], screen_corners[0] };
        dl->AddPolyline(quad_pts, 5, IM_COL32(61, 174, 233, 230), 0, 1.8f);

        // 2. Line to rotation pin
        dl->AddLine(top_mid, rot_pin, IM_COL32(61, 174, 233, 200), 1.5f);

        // 3. Rotation pin circle
        bool pin_active = (m_gizmo_drag_mode == GizmoDragMode::RotateCorner) || (hovered_rot_pin && m_gizmo_drag_mode == GizmoDragMode::None);
        dl->AddCircleFilled(rot_pin, 6.0f, pin_active ? IM_COL32(255, 220, 50, 255) : IM_COL32(61, 174, 233, 255));
        dl->AddCircle(rot_pin, 6.5f, IM_COL32(20, 25, 30, 255), 0, 1.5f);

        // 4. Center crosshair
        float ch = 7.0f;
        dl->AddLine(ImVec2(screen_center.x - ch, screen_center.y), ImVec2(screen_center.x + ch, screen_center.y), IM_COL32(255, 255, 255, 220), 1.2f);
        dl->AddLine(ImVec2(screen_center.x, screen_center.y - ch), ImVec2(screen_center.x, screen_center.y + ch), IM_COL32(255, 255, 255, 220), 1.2f);

        // 5. 4 Corner handle boxes
        for (int k = 0; k < 4; ++k) {
            bool corner_active = (m_gizmo_drag_mode == GizmoDragMode::ScaleCorner && m_gizmo_drag_corner == k) ||
                                 (hovered_corner == k && m_gizmo_drag_mode == GizmoDragMode::None);
            ImU32 fill_col = corner_active ? IM_COL32(255, 220, 50, 255) : IM_COL32(255, 255, 255, 255);
            ImVec2 p_min(screen_corners[k].x - 4.5f, screen_corners[k].y - 4.5f);
            ImVec2 p_max(screen_corners[k].x + 4.5f, screen_corners[k].y + 4.5f);
            dl->AddRectFilled(p_min, p_max, fill_col, 1.5f);
            dl->AddRect(p_min, p_max, IM_COL32(61, 174, 233, 255), 1.5f, 0, 1.5f);
        }
    }
}

void App::render_preview_transport() {
    float transport_h = 38.0f;
    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - transport_h - 4.0f);
    ImGui::Separator();

    if (ImGui::Button("|<<", ImVec2(32, 24))) {
        m_playhead_time = 0.0;
        m_audio_engine.set_playhead_time(0.0);
        m_last_requested_playhead = 0.0;
        m_media_decoder.request_frame(0.0, m_project);
    }
    ImGui::SameLine();
    if (ImGui::Button("<|", ImVec2(30, 24))) {
        m_playhead_time = std::max(0.0, m_playhead_time - 1.0 / 30.0);
        m_audio_engine.set_playhead_time(m_playhead_time);
        m_last_requested_playhead = m_playhead_time;
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
        m_last_requested_playhead = m_playhead_time;
        m_media_decoder.request_frame(m_playhead_time, m_project);
    }
    ImGui::SameLine();
    if (ImGui::Button(">>|", ImVec2(32, 24))) {
        m_playhead_time = m_project.get_total_duration();
        m_audio_engine.set_playhead_time(m_playhead_time);
        m_last_requested_playhead = m_playhead_time;
        m_media_decoder.request_frame(m_playhead_time, m_project);
    }
    ImGui::SameLine();
    ImGui::Checkbox("Loop", &m_loop_playback);

    ImGui::SameLine();
    ImGui::SetNextItemWidth(105.0f);
    char auto_ar_label[64];
    double largest_ar = m_project.get_largest_asset_aspect_ratio();
    snprintf(auto_ar_label, sizeof(auto_ar_label), "Auto (%.2f:1)", largest_ar);
    const char* aspect_options[] = {
        auto_ar_label,
        "16:9 Wide",
        "9:16 Shorts",
        "4:3 Standard",
        "1:1 Square",
        "21:9 Cinema",
        "4:5 Portrait"
    };
    ImGui::Combo("##preview_aspect", &m_preview_aspect_idx, aspect_options, IM_ARRAYSIZE(aspect_options));
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Preview Aspect Ratio\nDefault: Auto (adapts to timeline asset with largest aspect ratio)");
    }

    ImGui::SameLine();
    ImGui::Text("%s / %s", format_timecode(m_playhead_time).c_str(), format_timecode(m_project.get_total_duration()).c_str());

    // Scrub Slider & Preview Volume Adjustment (dB)
    ImGui::SameLine();
    float p_avail_w = ImGui::GetContentRegionAvail().x;
    float vol_slider_w = 90.0f;
    float scrub_w = p_avail_w - vol_slider_w - 15.0f;
    if (scrub_w > 50.0f) {
        ImGui::SetNextItemWidth(scrub_w);
        float t_val = static_cast<float>(m_playhead_time);
        if (ImGui::SliderFloat("##scrub", &t_val, 0.0f, static_cast<float>(m_project.get_total_duration()), "")) {
            m_playhead_time = t_val;
            m_audio_engine.set_playhead_time(m_playhead_time);
            m_last_requested_playhead = m_playhead_time;
            m_media_decoder.request_frame(m_playhead_time, m_project);
        }
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(vol_slider_w);
    float vol_db = m_audio_engine.get_preview_volume_db();
    char vol_str[32];
    if (vol_db <= -59.5f) {
        snprintf(vol_str, sizeof(vol_str), "Mute");
    } else {
        snprintf(vol_str, sizeof(vol_str), "%+.1f dB", vol_db);
    }
    if (ImGui::SliderFloat("##preview_vol_db", &vol_db, -60.0f, 6.0f, vol_str)) {
        m_audio_engine.set_preview_volume_db(vol_db);
        m_settings_logic.set_preview_volume_db(vol_db);
        m_settings_logic.save_to_disk();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Preview Audio Volume (-60 dB to +6 dB)\nDouble-click to enter value directly.");
    }
}

void App::render_preview_contents(bool is_detached) {
    render_preview_toolbar(is_detached);
    render_preview_viewport();
    render_preview_transport();
}

void App::render_timeline_panel() {
    std::string title = std::string(_TR("panel_timeline")) + "###TimelinePanel";
    ImGui::Begin(title.c_str(), nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoMove);

    m_timeline.render_ui(
        m_project, m_playhead_time, m_selected_clip_id,
        [this](const std::string& clip_id) {
            m_selected_clip_id = clip_id;
            m_keyframe_editor.set_selected_clip(clip_id);
            if (clip_id.empty()) {
                m_timeline.clear_selection();
            } else if (!m_timeline.is_clip_selected(clip_id)) {
                m_timeline.select_clip(clip_id, false, false);
            }
        },
        [this](double new_t) {
            m_playhead_time = new_t;
            m_audio_engine.set_playhead_time(m_playhead_time);
            m_last_requested_playhead = m_playhead_time;
            m_media_decoder.request_frame(m_playhead_time, m_project);
        },
        [this]() {
            push_undo_state();
            m_audio_engine.update_project_snapshot(m_project);
            m_last_requested_playhead = m_playhead_time;
            m_media_decoder.request_frame(m_playhead_time, m_project);
        },
        [this](const std::string& prefab_id, double insert_time) {
            push_undo_state();
            set_selected_clip_id("");
            m_clip_manager.instantiate_prefab_on_timeline(m_project, prefab_id, insert_time);
            m_audio_engine.update_project_snapshot(m_project);
            m_last_requested_playhead = m_playhead_time;
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
            } else if (tr.type == "subtitle" || !clip->subtitle_text.empty()) {
                title = "Context: Subtitle Inspector###ContextPanel";
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

    // Enhance scrollbar visibility and styling for Context Inspector
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 16.0f);
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0.12f, 0.14f, 0.18f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, ImVec4(0.35f, 0.58f, 0.82f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, ImVec4(0.45f, 0.70f, 0.95f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, ImVec4(0.55f, 0.80f, 1.00f, 1.00f));

    ImGui::Begin(title.c_str(), nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_AlwaysVerticalScrollbar);
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

    // If more content exists below the scroll view, show an obvious hint banner
    float scroll_y = ImGui::GetScrollY();
    float max_scroll_y = ImGui::GetScrollMaxY();
    if (max_scroll_y > 10.0f && scroll_y < max_scroll_y - 10.0f) {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.24f, 0.68f, 0.91f, 0.95f), "  ▼  Scroll down for more parameters  ▼");
    }

    ImGui::End();
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar();
}

void App::render_about_dialog() {
    if (m_show_about) {
        ImGui::OpenPopup("About minicut");
        m_show_about = false;
    }

    if (ImGui::BeginPopupModal("About minicut", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("minicut v%s - Modern C++17 Multi-Track Video Editor", MINICUT_VERSION);
        ImGui::TextDisabled("Built for Linux with KDE Plasma / Modern Desktop Environments");
        ImGui::Separator();
        ImGui::BulletText("OpenGL 3.3 Core Profile + Dear ImGui");
        ImGui::BulletText("FFmpeg (avformat, avcodec, swscale, swresample)");
        ImGui::BulletText("SDL2 Multi-channel Audio Mixing & Playback");
        ImGui::BulletText("Context-sensitive Image, Video, & Audio Transform Tools");
        ImGui::BulletText("Full Transform Keyframing (Position, Rotation, Scale, Opacity)");
        ImGui::BulletText("Non-blocking Asynchronous Export Pipeline (.mp4)");
        ImGui::BulletText("Full Interactive Graphical FileBrowser");
        ImGui::BulletText("Embedded Lua 5.1 / LuaJIT & JavaScript (Duktape) Engines");
        ImGui::BulletText("FreeType Text Generation with Full CJK & Arabic Shaping Support");
        ImGui::Separator();
        if (ImGui::Button("OK", ImVec2(100, 26))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

bool App::is_any_dialog_open() const {
    return m_show_about || m_show_text_dialog || m_file_browser.is_open() ||
           m_export_manager.is_dialog_open() || m_export_manager.is_exporting() ||
           m_timeline.is_rename_modal_open() || m_clip_manager.is_importing() ||
           m_settings_dialog.is_open() || m_tts_dialog.is_open() || m_ai_dialog.is_open() ||
           m_project_load_state.active.load();
}

void App::update_window_title() {
    if (!m_window) return;
    std::string proj_title = m_project.name;
    if (proj_title.empty() || proj_title == "Untitled Project") {
        proj_title = "New Project";
    }
    std::string full_title = "Minicut - " + proj_title;
    SDL_SetWindowTitle(m_window, full_title.c_str());
}

void App::load_settings() {
    m_settings_logic.load_from_disk();
    m_use_system_file_dialog = m_settings_logic.get_use_system_file_dialog();
    std::string lang = m_settings_logic.get_language();
    if (lang != "en" && lang != "ja" && lang != "jp" && lang != "ko" && lang != "uk" && lang != "ar") {
        lang = "en";
    }
    LocaleManager::instance().set_locale(lang);
    m_split_h = m_settings_logic.get_split_h();
    m_split_top_v = m_settings_logic.get_split_top_v();
    m_split_bottom_v = m_settings_logic.get_split_bottom_v();
    m_timeline.set_track_header_width(m_settings_logic.get_track_header_w());
}

void App::save_settings() {
    m_settings_logic.set_use_system_file_dialog(m_use_system_file_dialog);
    m_settings_logic.set_language(LocaleManager::instance().current_locale());
    m_settings_logic.set_split_h(m_split_h);
    m_settings_logic.set_split_top_v(m_split_top_v);
    m_settings_logic.set_split_bottom_v(m_split_bottom_v);
    m_settings_logic.set_track_header_w(m_timeline.get_track_header_width());
    m_settings_logic.save_to_disk();
}

void App::set_language(const std::string& code) {
    LocaleManager::instance().set_locale(code);
    m_settings_logic.set_language(code);
    save_settings();
}

void App::reload_subsystems() {
    // 1. Reload render backend preference
    std::string backend_pref = m_settings_logic.get_render_backend();
    RenderBackendType target_type = (backend_pref == "Vulkan") ? RenderBackendType::Vulkan : RenderBackendType::OpenGL3;
    RenderSystem::instance().set_backend(target_type);

    // 2. Reload theme & accessibility styling
    apply_theme();

    // 3. Reload audio engine snapshot & assets
    m_audio_engine.update_project_snapshot(m_project);
    m_audio_engine.ensure_project_assets_loaded(m_project);

    // 4. Reload scripts
    m_script_engine.reload_scripts();

    // 5. Reinitialize swap manager
    SwapManager::instance().reinitialize();

    // 6. Reload locales
    LocaleManager::instance().reload_locales();

    // 7. Request fresh preview frame
    m_media_decoder.request_frame(m_playhead_time, m_project);

    set_status_notification("Subsystems reloaded successfully (" + std::string(RenderSystem::instance().get_backend_name()) + ")", 3.0);
}

void App::add_subtitle_at_playhead() {
    push_undo_state();
    std::string new_id = m_timeline.add_subtitle_clip(m_project, m_playhead_time, 3.0, "New Subtitle");
    if (!new_id.empty()) {
        set_selected_clip_id(new_id);
    }
    m_media_decoder.request_frame(m_playhead_time, m_project);
    on_project_modified();
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
    // Suppress splitters if any modal, popup, file browser, context menu, or dialog is currently open
    bool any_popup = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
    if (is_any_dialog_open() || any_popup) {
        m_dragging_split_h = false;
        m_dragging_split_top_v = false;
        m_dragging_split_bottom_v = false;
    }

    ImVec2 mouse_pos = ImGui::GetMousePos();
    bool mouse_down = ImGui::IsMouseDown(0);

    float menu_h = m_menu_bar_height > 0.0f ? m_menu_bar_height : ImGui::GetFrameHeight();
    float shelf_h = m_script_engine.is_shelf_visible() ? m_script_engine.get_shelf_height() : 0.0f;
    float content_y = menu_h + shelf_h;
    float total_w = static_cast<float>(m_window_width);
    float total_h = static_cast<float>(m_window_height) - content_y;

    float top_h = total_h * m_split_h;
    float left_top_w = total_w * m_split_top_v;
    float left_bottom_w = total_w * m_split_bottom_v;

    const float thickness = 8.0f;

    // 1. Horizontal Splitter (between Top Row and Bottom Row)
    float split_y = content_y + top_h;
    bool hover_h = !is_any_dialog_open() && !any_popup &&
                   (mouse_pos.x >= 0.0f && mouse_pos.x <= total_w &&
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

    // 2. Vertical Splitter Top (between Clips & Assets and Preview)
    float split_top_x = left_top_w;
    bool hover_vt = !is_any_dialog_open() && !any_popup &&
                    (mouse_pos.x >= (split_top_x - thickness * 0.5f) && mouse_pos.x <= (split_top_x + thickness * 0.5f) &&
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

    // 3. Vertical Splitter Bottom (between Timeline and Context Inspector)
    float split_bottom_x = left_bottom_w;
    bool hover_vb = !is_any_dialog_open() && !any_popup &&
                    (mouse_pos.x >= (split_bottom_x - thickness * 0.5f) && mouse_pos.x <= (split_bottom_x + thickness * 0.5f) &&
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

    // Render splitters in a transparent background overlay window below all popups and context menus
    ImGui::SetNextWindowPos(ImVec2(0, content_y));
    ImGui::SetNextWindowSize(ImVec2(total_w, total_h));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs |
                             ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus;
    if (ImGui::Begin("##SplittersOverlay", nullptr, flags)) {
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        draw_list->AddLine(ImVec2(0.0f, split_y), ImVec2(total_w, split_y), col_h, (hover_h || m_dragging_split_h) ? 3.0f : 1.5f);
        draw_list->AddLine(ImVec2(split_top_x, content_y), ImVec2(split_top_x, split_y), col_vt, (hover_vt || m_dragging_split_top_v) ? 3.0f : 1.5f);
        draw_list->AddLine(ImVec2(split_bottom_x, split_y), ImVec2(split_bottom_x, content_y + total_h), col_vb, (hover_vb || m_dragging_split_bottom_v) ? 3.0f : 1.5f);
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

void App::render() {
    RenderSystem::instance().new_frame();
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

    // File Browser Dialog
    m_file_browser.render();

    // Settings Dialog
    m_settings_dialog.render(m_settings_logic, *this);

    // Text-to-Speech Dialog
    m_tts_dialog.render(*this);

    // AI Dialog
    m_ai_dialog.render(*this);

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

    // Project Load Progress Modal
    render_project_load_modal();

    // Text Asset Dialog
    render_text_dialog();

    // About Dialog
    render_about_dialog();

    // Universal IME auto-completion & composition overlay across all text entries and dialogs
    ImGuiContext* g = ImGui::GetCurrentContext();
    if (g && g->PlatformImeData.WantVisible && ImGui::GetIO().WantTextInput) {
        SDL_Rect ime_rect;
        ime_rect.x = static_cast<int>(g->PlatformImeData.InputPos.x);
        ime_rect.y = static_cast<int>(g->PlatformImeData.InputPos.y);
        ime_rect.w = 1;
        ime_rect.h = static_cast<int>(g->PlatformImeData.InputLineHeight > 0.0f ? g->PlatformImeData.InputLineHeight : ImGui::GetFontSize());
        SDL_SetTextInputRect(&ime_rect);

        if (!m_ime_composition.empty()) {
            ImVec2 pos = g->PlatformImeData.InputPos;
            float line_h = (g->PlatformImeData.InputLineHeight > 0.0f) ? g->PlatformImeData.InputLineHeight : ImGui::GetFontSize();
            ImDrawList* fg = ImGui::GetForegroundDrawList();
            ImVec2 text_sz = ImGui::CalcTextSize(m_ime_composition.c_str());

            ImVec2 box_min = ImVec2(pos.x, pos.y);
            ImVec2 box_max = ImVec2(pos.x + text_sz.x + 8.0f, pos.y + line_h);
            fg->AddRectFilled(box_min, box_max, IM_COL32(35, 45, 60, 240), 3.0f);
            fg->AddRect(box_min, box_max, IM_COL32(50, 150, 255, 255), 3.0f);
            fg->AddText(ImVec2(pos.x + 4.0f, pos.y + (line_h - text_sz.y) * 0.5f), IM_COL32(255, 255, 255, 255), m_ime_composition.c_str());
            fg->AddLine(ImVec2(pos.x + 2.0f, box_max.y - 1.0f), ImVec2(box_max.x - 2.0f, box_max.y - 1.0f), IM_COL32(80, 200, 255, 255), 2.0f);
        }
    }

    // Render ImGui & Swap Window via RenderSystem
    ImGui::Render();
    RenderSystem::instance().render_and_swap(ImGui::GetDrawData(), m_window_width, m_window_height);

    // Manage and render Detached Preview Window
    if (m_preview_popped_out) {
        if (!m_preview_window) {
            m_preview_window = SDL_CreateWindow(
                "Minicut Video Preview [Detached]",
                SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                960, 540,
                SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE
            );
            if (m_preview_window) {
                m_preview_imgui_ctx = ImGui::CreateContext(m_imgui_context->IO.Fonts);
                m_preview_imgui_ctx->IO.IniFilename = nullptr;
                ImGui::SetCurrentContext(m_preview_imgui_ctx);
                ImGui_ImplSDL2_InitForOpenGL(m_preview_window, m_gl_context);
                ImGui_ImplOpenGL3_Init("#version 330");
                apply_theme();
                ImGui::SetCurrentContext(m_imgui_context);
            }
        }
        if (m_preview_window) {
            render_popout_preview_window();
        }
    } else {
        if (m_preview_window) {
            if (m_preview_imgui_ctx) {
                ImGui::SetCurrentContext(m_preview_imgui_ctx);
                ImGui_ImplOpenGL3_Shutdown();
                ImGui_ImplSDL2_Shutdown();
                ImGui::DestroyContext(m_preview_imgui_ctx);
                m_preview_imgui_ctx = nullptr;
            }
            SDL_DestroyWindow(m_preview_window);
            m_preview_window = nullptr;
            ImGui::SetCurrentContext(m_imgui_context);
            SDL_GL_MakeCurrent(m_window, m_gl_context);
        }
    }

    // Manage and render Standalone Export Window
    if (m_export_manager.is_window_active()) {
        if (!m_export_window) {
            m_export_window = SDL_CreateWindow(
                "Export Timeline Media - Minicut",
                SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                580, 420,
                SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE
            );
            if (m_export_window) {
                m_export_imgui_ctx = ImGui::CreateContext(m_imgui_context->IO.Fonts);
                m_export_imgui_ctx->IO.IniFilename = nullptr;
                ImGui::SetCurrentContext(m_export_imgui_ctx);
                ImGui_ImplSDL2_InitForOpenGL(m_export_window, m_gl_context);
                ImGui_ImplOpenGL3_Init("#version 330");
                apply_theme();
                ImGui::SetCurrentContext(m_imgui_context);
            }
        }
        if (m_export_window) {
            render_export_os_window();
        }
    } else {
        if (m_export_window) {
            if (m_export_imgui_ctx) {
                ImGui::SetCurrentContext(m_export_imgui_ctx);
                ImGui_ImplOpenGL3_Shutdown();
                ImGui_ImplSDL2_Shutdown();
                ImGui::DestroyContext(m_export_imgui_ctx);
                m_export_imgui_ctx = nullptr;
            }
            SDL_DestroyWindow(m_export_window);
            m_export_window = nullptr;
            ImGui::SetCurrentContext(m_imgui_context);
            SDL_GL_MakeCurrent(m_window, m_gl_context);
        }
    }
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
    if (m_project_load_state.worker_thread) {
        if (m_project_load_state.worker_thread->joinable()) {
            m_project_load_state.worker_thread->join();
        }
        delete m_project_load_state.worker_thread;
        m_project_load_state.worker_thread = nullptr;
    }

    m_audio_engine.shutdown();
    SwapManager::instance().shutdown();

    if (m_preview_texture) {
        RenderSystem::instance().delete_texture(m_preview_texture);
        m_preview_texture = 0;
    }
    if (m_text_preview_texture) {
        RenderSystem::instance().delete_texture(m_text_preview_texture);
        m_text_preview_texture = 0;
    }
    if (m_settings_icon_texture) {
        RenderSystem::instance().delete_texture(m_settings_icon_texture);
        m_settings_icon_texture = 0;
    }
    cleanup_preview_toolbar_icons();

    if (m_preview_imgui_ctx) {
        ImGui::SetCurrentContext(m_preview_imgui_ctx);
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext(m_preview_imgui_ctx);
        m_preview_imgui_ctx = nullptr;
    }
    if (m_preview_window) {
        SDL_DestroyWindow(m_preview_window);
        m_preview_window = nullptr;
    }

    if (m_export_imgui_ctx) {
        ImGui::SetCurrentContext(m_export_imgui_ctx);
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext(m_export_imgui_ctx);
        m_export_imgui_ctx = nullptr;
    }
    if (m_export_window) {
        SDL_DestroyWindow(m_export_window);
        m_export_window = nullptr;
    }

    if (m_imgui_context) {
        ImGui::SetCurrentContext(m_imgui_context);
    }

    RenderSystem::instance().shutdown();
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

void App::open_text_dialog() {
    m_show_text_dialog = true;
    m_text_dialog_request_focus = true;
    m_text_preview_dirty = true;
    std::string loc = LocaleManager::instance().current_locale();
    if (LocaleManager::instance().is_rtl()) {
        m_text_config.justification = 2; // Right align for Arabic
        m_text_config.font_path = TextGenerator::get_arabic_font_path();
        if (m_text_input_buf[0] == '\0' || strcmp(m_text_input_buf, "Title Text") == 0) {
            strncpy(m_text_input_buf, "عنوان نصي", sizeof(m_text_input_buf) - 1);
        }
    } else if (loc == "ja" || loc == "jp") {
        m_text_config.font_path = TextGenerator::get_cjk_font_path();
        if (m_text_input_buf[0] == '\0' || strcmp(m_text_input_buf, "Title Text") == 0) {
            strncpy(m_text_input_buf, "タイトルテキスト", sizeof(m_text_input_buf) - 1);
        }
    } else if (loc == "ko") {
        m_text_config.font_path = TextGenerator::get_cjk_font_path();
        if (m_text_input_buf[0] == '\0' || strcmp(m_text_input_buf, "Title Text") == 0) {
            strncpy(m_text_input_buf, "제목 텍스트", sizeof(m_text_input_buf) - 1);
        }
    } else if (loc == "uk") {
        if (m_text_input_buf[0] == '\0' || strcmp(m_text_input_buf, "Title Text") == 0) {
            strncpy(m_text_input_buf, "Заголовок тексту", sizeof(m_text_input_buf) - 1);
        }
    }
}

void App::update_text_preview() {
    if (!m_text_preview_dirty) return;
    m_text_preview_dirty = false;
    std::string current_text = m_text_input_buf;
    if (!m_ime_composition.empty()) {
        current_text += m_ime_composition;
    }
    m_text_config.text = current_text;

    std::vector<uint8_t> rgba;
    int tw = 0, th = 0;
    if (TextGenerator::render_to_rgba(m_text_config, tw, th, rgba) && tw > 0 && th > 0) {
        if (!m_text_preview_texture) {
            glGenTextures(1, &m_text_preview_texture);
        }
        glBindTexture(GL_TEXTURE_2D, m_text_preview_texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tw, th, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        m_text_preview_w = tw;
        m_text_preview_h = th;
    }
}

void App::render_text_dialog() {
    if (!m_show_text_dialog) return;

    if (m_text_dialog_request_focus) {
        ImGui::SetNextWindowFocus();
        m_text_dialog_request_focus = false;
    }

    ImGui::SetNextWindowSize(ImVec2(620, 680), ImGuiCond_Appearing);
    std::string title = std::string(_TR("text_dialog_title")) + "###TextGeneratorModal";
    if (ImGui::Begin(title.c_str(), &m_show_text_dialog, ImGuiWindowFlags_NoCollapse)) {
        // Font selector
        auto fonts = TextGenerator::get_available_fonts();
        int cur_font_idx = 0;
        for (size_t i = 0; i < fonts.size(); ++i) {
            if (fonts[i].second == m_text_config.font_path) {
                cur_font_idx = static_cast<int>(i);
                break;
            }
        }
        const char* current_font_name = fonts.empty() ? "None" : fonts[cur_font_idx].first.c_str();
        if (ImGui::BeginCombo(_TR("text_font"), current_font_name)) {
            for (size_t i = 0; i < fonts.size(); ++i) {
                bool is_selected = (cur_font_idx == static_cast<int>(i));
                if (ImGui::Selectable(fonts[i].first.c_str(), is_selected)) {
                    m_text_config.font_path = fonts[i].second;
                    m_text_preview_dirty = true;
                }
                if (is_selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        // Font Size
        if (ImGui::SliderFloat(_TR("text_font_size"), &m_text_config.font_size, 12.0f, 144.0f, "%.0f px")) {
            m_text_preview_dirty = true;
        }

        // Font Color
        if (ImGui::ColorEdit4(_TR("text_font_color"), m_text_config.font_color, ImGuiColorEditFlags_AlphaBar)) {
            m_text_preview_dirty = true;
        }

        ImGui::Separator();

        // Background
        if (ImGui::Checkbox(_TR("text_use_background"), &m_text_config.use_bg)) {
            m_text_preview_dirty = true;
        }
        if (m_text_config.use_bg) {
            ImGui::SameLine();
            if (ImGui::ColorEdit4(_TR("text_bg_color"), m_text_config.bg_color, ImGuiColorEditFlags_AlphaBar)) {
                m_text_preview_dirty = true;
            }
        }

        // Outline
        if (ImGui::Checkbox(_TR("text_use_outline"), &m_text_config.use_outline)) {
            m_text_preview_dirty = true;
        }
        if (m_text_config.use_outline) {
            ImGui::SameLine();
            if (ImGui::ColorEdit4(_TR("text_outline_color"), m_text_config.outline_color)) {
                m_text_preview_dirty = true;
            }
            if (ImGui::SliderFloat(_TR("text_outline_width"), &m_text_config.outline_thickness, 0.5f, 15.0f, "%.1f px")) {
                m_text_preview_dirty = true;
            }
        }

        ImGui::Separator();

        // Spacing
        if (ImGui::SliderFloat(_TR("text_letter_spacing"), &m_text_config.letter_spacing, -5.0f, 30.0f, "%.1f px")) {
            m_text_preview_dirty = true;
        }
        if (ImGui::SliderFloat(_TR("text_line_spacing"), &m_text_config.line_spacing, 0.8f, 3.0f, "%.2f")) {
            m_text_preview_dirty = true;
        }

        // Justification
        ImGui::Text("%s:", _TR("text_alignment"));
        ImGui::SameLine();
        if (ImGui::RadioButton(_TR("text_align_left"), &m_text_config.justification, 0)) { m_text_preview_dirty = true; }
        ImGui::SameLine();
        if (ImGui::RadioButton(_TR("text_align_center"), &m_text_config.justification, 1)) { m_text_preview_dirty = true; }
        ImGui::SameLine();
        if (ImGui::RadioButton(_TR("text_align_right"), &m_text_config.justification, 2)) { m_text_preview_dirty = true; }

        ImGui::Separator();

        // Text entry
        ImGui::Text("%s:", _TR("text_entry_label"));
        if (LocaleManager::instance().is_rtl()) {
            ImGui::SameLine();
            ImGui::TextDisabled("(RTL Arabic mode active)");
        }
        if (!m_ime_composition.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.24f, 0.68f, 0.91f, 1.0f), "[%s]", m_ime_composition.c_str());
        }

        if (ImGui::InputTextMultiline("##text_input_content", m_text_input_buf, sizeof(m_text_input_buf), ImVec2(-1, 80))) {
            m_text_preview_dirty = true;
        }

        ImVec2 input_min = ImGui::GetItemRectMin();
        ImVec2 input_max = ImGui::GetItemRectMax();

        // Update SDL IME input rectangle to position OS candidate popup directly below text entry
        if (ImGui::IsItemActive()) {
            SDL_Rect r;
            r.x = static_cast<int>(input_min.x);
            r.y = static_cast<int>(input_max.y);
            r.w = static_cast<int>(input_max.x - input_min.x);
            r.h = 24;
            SDL_SetTextInputRect(&r);
        }

        // Draw active composition text right inside the text entry box if composing
        if (!m_ime_composition.empty()) {
            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            ImGuiStyle& style = ImGui::GetStyle();

            // Calculate position right after current text in the input box
            ImVec2 text_sz = ImGui::CalcTextSize(m_text_input_buf);
            float start_x = input_min.x + style.FramePadding.x + text_sz.x;
            float start_y = input_min.y + style.FramePadding.y;

            // Ensure within box bounds
            if (start_x < input_max.x - 10.0f) {
                ImVec2 comp_sz = ImGui::CalcTextSize(m_ime_composition.c_str());
                ImVec2 draw_pos = ImVec2(start_x + 2.0f, start_y);
                draw_list->AddText(draw_pos, IM_COL32(80, 190, 255, 255), m_ime_composition.c_str());

                // Draw standard IME composition underline
                float line_y = draw_pos.y + comp_sz.y + 1.0f;
                draw_list->AddLine(ImVec2(draw_pos.x, line_y),
                                   ImVec2(draw_pos.x + comp_sz.x, line_y),
                                   IM_COL32(60, 160, 240, 255), 2.0f);
            }
        }

        if (m_last_rendered_text != m_text_input_buf) {
            m_last_rendered_text = m_text_input_buf;
            m_text_preview_dirty = true;
        }

        // Live Preview Box
        ImGui::Separator();
        ImGui::Text("%s (%dx%d):", _TR("text_preview"), m_text_preview_w, m_text_preview_h);
        update_text_preview();

        ImVec2 avail = ImVec2(ImGui::GetContentRegionAvail().x, 140.0f);
        ImGui::BeginChild("##text_preview_box", avail, true, ImGuiWindowFlags_NoScrollbar);
        if (m_text_preview_texture && m_text_preview_w > 0 && m_text_preview_h > 0) {
            float aspect = static_cast<float>(m_text_preview_w) / static_cast<float>(m_text_preview_h);
            float disp_h = std::min(avail.y - 10.0f, static_cast<float>(m_text_preview_h));
            float disp_w = disp_h * aspect;
            if (disp_w > avail.x - 20.0f) {
                disp_w = avail.x - 20.0f;
                disp_h = disp_w / aspect;
            }
            float off_x = std::max(0.0f, (avail.x - disp_w) * 0.5f);
            float off_y = std::max(0.0f, (avail.y - disp_h) * 0.5f);
            ImGui::SetCursorPos(ImVec2(off_x, off_y));
            ImGui::Image((ImTextureID)(intptr_t)m_text_preview_texture, ImVec2(disp_w, disp_h));
        } else {
            ImGui::TextDisabled("No preview");
        }
        ImGui::EndChild();

        ImGui::Spacing();
        ImGui::Separator();

        // Buttons
        if (ImGui::Button(_TR("text_add_to_assets"), ImVec2(160, 32))) {
            if (!m_ime_composition.empty()) {
                strncat(m_text_input_buf, m_ime_composition.c_str(), sizeof(m_text_input_buf) - strlen(m_text_input_buf) - 1);
                m_ime_composition.clear();
            }
            m_text_config.text = m_text_input_buf;
            std::string id = TextGenerator::create_text_asset(m_project, m_text_config);
            if (!id.empty()) {
                m_clip_manager.select_asset(id, true, false);
                set_status_notification("Text asset created: " + id);
                on_project_modified();
            }
            m_show_text_dialog = false;
        }
        ImGui::SameLine();
        if (ImGui::Button(_TR("text_add_to_timeline"), ImVec2(180, 32))) {
            if (!m_ime_composition.empty()) {
                strncat(m_text_input_buf, m_ime_composition.c_str(), sizeof(m_text_input_buf) - strlen(m_text_input_buf) - 1);
                m_ime_composition.clear();
            }
            m_text_config.text = m_text_input_buf;
            std::string id = TextGenerator::create_text_asset(m_project, m_text_config);
            if (!id.empty()) {
                m_clip_manager.select_asset(id, true, false);
                add_asset_to_timeline(id, true, m_playhead_time);
                set_status_notification("Text asset added to timeline at " + format_timecode(m_playhead_time, false));
                on_project_modified();
            }
            m_show_text_dialog = false;
        }
        ImGui::SameLine();
        if (ImGui::Button(_TR("dialog_cancel"), ImVec2(100, 32)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            m_show_text_dialog = false;
        }

        ImGui::End();
    }
}

std::string App::create_text_asset(const std::string& text, const TextConfig& config, const std::string& name) {
    TextConfig cfg = config;
    cfg.text = text;
    std::string asset_id = TextGenerator::create_text_asset(m_project, cfg, name);
    if (!asset_id.empty()) {
        m_clip_manager.select_asset(asset_id, true, false);
        on_project_modified();
    }
    return asset_id;
}

void App::add_text_to_timeline(const std::string& asset_id, double insert_time) {
    add_asset_to_timeline(asset_id, true, insert_time);
}

void App::generate_settings_gear_icon() {
    const int w = 32;
    const int h = 32;
    std::vector<uint8_t> rgba(w * h * 4, 0);

    const float cx = 15.5f;
    const float cy = 15.5f;
    const float r_outer = 12.0f;
    const float r_inner = 8.5f;
    const float r_hole = 4.0f;
    const int teeth = 8;
    const float tooth_angle = (2.0f * 3.14159265f) / static_cast<float>(teeth);

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float dx = static_cast<float>(x) - cx;
            float dy = static_cast<float>(y) - cy;
            float dist = std::sqrt(dx * dx + dy * dy);
            float angle = std::atan2(dy, dx);
            if (angle < 0.0f) angle += 2.0f * 3.14159265f;

            float rel = std::fmod(angle, tooth_angle) / tooth_angle;
            // Trapezoidal tooth profile
            float tooth_val = 0.0f;
            if (rel >= 0.2f && rel <= 0.8f) {
                tooth_val = 1.0f;
            } else if (rel < 0.2f) {
                tooth_val = rel / 0.2f;
            } else {
                tooth_val = (1.0f - rel) / 0.2f;
            }

            float current_r = r_inner + (r_outer - r_inner) * tooth_val;
            float alpha_outer = std::clamp(current_r - dist + 0.6f, 0.0f, 1.0f);
            float alpha_hole = std::clamp(dist - r_hole + 0.6f, 0.0f, 1.0f);
            float alpha = alpha_outer * alpha_hole;

            if (alpha > 0.02f) {
                int idx = (y * w + x) * 4;
                rgba[idx + 0] = 225; // R
                rgba[idx + 1] = 230; // G
                rgba[idx + 2] = 238; // B
                rgba[idx + 3] = static_cast<uint8_t>(alpha * 255.0f);
            }
        }
    }

    if (m_settings_icon_texture != 0) {
        RenderSystem::instance().delete_texture(m_settings_icon_texture);
    }
    m_settings_icon_texture = static_cast<GLuint>(RenderSystem::instance().create_texture(w, h, rgba.data()));
}

void App::generate_preview_toolbar_icons() {
    const int w = 32;
    const int h = 32;

    auto set_px = [](std::vector<uint8_t>& buf, int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
        if (x >= 0 && x < 32 && y >= 0 && y < 32) {
            int idx = (y * 32 + x) * 4;
            float sa = a / 255.0f;
            float da = buf[idx + 3] / 255.0f;
            float out_a = sa + da * (1.0f - sa);
            if (out_a > 0.0f) {
                buf[idx + 0] = static_cast<uint8_t>((r * sa + buf[idx + 0] * da * (1.0f - sa)) / out_a);
                buf[idx + 1] = static_cast<uint8_t>((g * sa + buf[idx + 1] * da * (1.0f - sa)) / out_a);
                buf[idx + 2] = static_cast<uint8_t>((b * sa + buf[idx + 2] * da * (1.0f - sa)) / out_a);
                buf[idx + 3] = static_cast<uint8_t>(out_a * 255.0f);
            }
        }
    };

    auto draw_line = [&](std::vector<uint8_t>& buf, float x0, float y0, float x1, float y1, float thickness, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
        float dx = x1 - x0;
        float dy = y1 - y0;
        float len = std::hypot(dx, dy);
        if (len < 1e-4f) return;
        float ux = dx / len;
        float uy = dy / len;
        float nx = -uy;
        float ny = ux;
        float half_t = thickness * 0.5f;
        for (int y = 0; y < 32; ++y) {
            for (int x = 0; x < 32; ++x) {
                float px = x + 0.5f - x0;
                float py = y + 0.5f - y0;
                float proj = px * ux + py * uy;
                float perp = std::abs(px * nx + py * ny);
                if (proj >= -0.5f && proj <= len + 0.5f && perp <= half_t + 0.5f) {
                    float alpha = std::clamp(half_t + 0.5f - perp, 0.0f, 1.0f);
                    if (proj < 0.0f) alpha *= std::clamp(1.0f + proj, 0.0f, 1.0f);
                    if (proj > len) alpha *= std::clamp(1.0f - (proj - len), 0.0f, 1.0f);
                    if (alpha > 0.02f) {
                        set_px(buf, x, y, r, g, b, static_cast<uint8_t>(alpha * a));
                    }
                }
            }
        }
    };

    auto fill_circle = [&](std::vector<uint8_t>& buf, float cx, float cy, float radius, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
        for (int y = 0; y < 32; ++y) {
            for (int x = 0; x < 32; ++x) {
                float d = std::hypot(x + 0.5f - cx, y + 0.5f - cy);
                float alpha = std::clamp(radius + 0.5f - d, 0.0f, 1.0f);
                if (alpha > 0.02f) {
                    set_px(buf, x, y, r, g, b, static_cast<uint8_t>(alpha * a));
                }
            }
        }
    };

    auto draw_circle_outline = [&](std::vector<uint8_t>& buf, float cx, float cy, float radius, float thickness, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
        float half_t = thickness * 0.5f;
        for (int y = 0; y < 32; ++y) {
            for (int x = 0; x < 32; ++x) {
                float d = std::hypot(x + 0.5f - cx, y + 0.5f - cy);
                float dist_to_r = std::abs(d - radius);
                if (dist_to_r <= half_t + 0.5f) {
                    float alpha = std::clamp(half_t + 0.5f - dist_to_r, 0.0f, 1.0f);
                    if (alpha > 0.02f) {
                        set_px(buf, x, y, r, g, b, static_cast<uint8_t>(alpha * a));
                    }
                }
            }
        }
    };

    auto draw_rect = [&](std::vector<uint8_t>& buf, float x0, float y0, float x1, float y1, float thickness, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
        draw_line(buf, x0, y0, x1, y0, thickness, r, g, b, a);
        draw_line(buf, x1, y0, x1, y1, thickness, r, g, b, a);
        draw_line(buf, x1, y1, x0, y1, thickness, r, g, b, a);
        draw_line(buf, x0, y1, x0, y0, thickness, r, g, b, a);
    };

    // 1. Select Tool Icon (Cursor arrow)
    {
        std::vector<uint8_t> rgba(w * h * 4, 0);
        draw_line(rgba, 7.0f, 5.0f, 7.0f, 23.0f, 2.2f, 235, 240, 248);
        draw_line(rgba, 7.0f, 23.0f, 12.5f, 18.0f, 2.2f, 235, 240, 248);
        draw_line(rgba, 12.5f, 18.0f, 17.5f, 26.5f, 2.2f, 235, 240, 248);
        draw_line(rgba, 17.5f, 26.5f, 20.5f, 24.5f, 2.2f, 235, 240, 248);
        draw_line(rgba, 20.5f, 24.5f, 15.5f, 16.0f, 2.2f, 235, 240, 248);
        draw_line(rgba, 15.5f, 16.0f, 22.0f, 16.0f, 2.2f, 235, 240, 248);
        draw_line(rgba, 22.0f, 16.0f, 7.0f, 5.0f, 2.2f, 235, 240, 248);
        for (int y = 7; y <= 21; ++y) {
            for (int x = 8; x <= 18; ++x) {
                if (x <= 7 + (y - 5) && x >= 8 && y <= 16) {
                    set_px(rgba, x, y, 200, 225, 255, 180);
                }
            }
        }
        if (m_icon_tex_select) RenderSystem::instance().delete_texture(m_icon_tex_select);
        m_icon_tex_select = static_cast<GLuint>(RenderSystem::instance().create_texture(w, h, rgba.data()));
        m_icon_tex_transform = m_icon_tex_select;
    }

    // 2. Translate Tool Icon (4-way cross arrow)
    {
        std::vector<uint8_t> rgba(w * h * 4, 0);
        draw_line(rgba, 6.0f, 15.5f, 25.0f, 15.5f, 2.2f, 235, 240, 248);
        draw_line(rgba, 15.5f, 6.0f, 15.5f, 25.0f, 2.2f, 235, 240, 248);
        // Top arrow
        draw_line(rgba, 11.5f, 10.0f, 15.5f, 5.5f, 2.0f, 235, 240, 248);
        draw_line(rgba, 19.5f, 10.0f, 15.5f, 5.5f, 2.0f, 235, 240, 248);
        // Bottom arrow
        draw_line(rgba, 11.5f, 21.0f, 15.5f, 25.5f, 2.0f, 235, 240, 248);
        draw_line(rgba, 19.5f, 21.0f, 15.5f, 25.5f, 2.0f, 235, 240, 248);
        // Left arrow
        draw_line(rgba, 10.0f, 11.5f, 5.5f, 15.5f, 2.0f, 235, 240, 248);
        draw_line(rgba, 10.0f, 19.5f, 5.5f, 15.5f, 2.0f, 235, 240, 248);
        // Right arrow
        draw_line(rgba, 21.0f, 11.5f, 25.5f, 15.5f, 2.0f, 235, 240, 248);
        draw_line(rgba, 21.0f, 19.5f, 25.5f, 15.5f, 2.0f, 235, 240, 248);
        if (m_icon_tex_translate) RenderSystem::instance().delete_texture(m_icon_tex_translate);
        m_icon_tex_translate = static_cast<GLuint>(RenderSystem::instance().create_texture(w, h, rgba.data()));
    }

    // 3. Scale Tool Icon (Box with corner diagonal arrows)
    {
        std::vector<uint8_t> rgba(w * h * 4, 0);
        draw_rect(rgba, 9.0f, 9.0f, 22.0f, 22.0f, 1.8f, 160, 200, 245);
        // Top-left outward arrow
        draw_line(rgba, 12.0f, 12.0f, 5.0f, 5.0f, 2.0f, 235, 240, 248);
        draw_line(rgba, 5.0f, 10.0f, 5.0f, 5.0f, 2.0f, 235, 240, 248);
        draw_line(rgba, 10.0f, 5.0f, 5.0f, 5.0f, 2.0f, 235, 240, 248);
        // Bottom-right outward arrow
        draw_line(rgba, 19.0f, 19.0f, 26.0f, 26.0f, 2.0f, 235, 240, 248);
        draw_line(rgba, 21.0f, 26.0f, 26.0f, 26.0f, 2.0f, 235, 240, 248);
        draw_line(rgba, 26.0f, 21.0f, 26.0f, 26.0f, 2.0f, 235, 240, 248);
        if (m_icon_tex_scale) RenderSystem::instance().delete_texture(m_icon_tex_scale);
        m_icon_tex_scale = static_cast<GLuint>(RenderSystem::instance().create_texture(w, h, rgba.data()));
    }

    // 4. Rotate Tool Icon (Arc with arrow)
    {
        std::vector<uint8_t> rgba(w * h * 4, 0);
        const float cx = 15.5f;
        const float cy = 15.5f;
        const float r = 9.5f;
        for (float deg = 35.0f; deg <= 315.0f; deg += 3.0f) {
            float rad = deg * (3.14159265f / 180.0f);
            float px = cx + std::cos(rad) * r;
            float py = cy - std::sin(rad) * r;
            fill_circle(rgba, px, py, 1.2f, 235, 240, 248);
        }
        float tip_x = cx + std::cos(35.0f * 3.14159265f / 180.0f) * r;
        float tip_y = cy - std::sin(35.0f * 3.14159265f / 180.0f) * r;
        draw_line(rgba, tip_x - 1.0f, tip_y - 5.5f, tip_x, tip_y, 2.0f, 235, 240, 248);
        draw_line(rgba, tip_x + 5.5f, tip_y - 2.0f, tip_x, tip_y, 2.0f, 235, 240, 248);
        if (m_icon_tex_rotate) RenderSystem::instance().delete_texture(m_icon_tex_rotate);
        m_icon_tex_rotate = static_cast<GLuint>(RenderSystem::instance().create_texture(w, h, rgba.data()));
    }

    // 5. Reset Tool Icon (Crosshair target circle)
    {
        std::vector<uint8_t> rgba(w * h * 4, 0);
        draw_circle_outline(rgba, 15.5f, 15.5f, 9.0f, 1.8f, 235, 240, 248);
        fill_circle(rgba, 15.5f, 15.5f, 2.2f, 235, 240, 248);
        draw_line(rgba, 15.5f, 3.5f, 15.5f, 7.5f, 1.8f, 235, 240, 248);
        draw_line(rgba, 15.5f, 23.5f, 15.5f, 27.5f, 1.8f, 235, 240, 248);
        draw_line(rgba, 3.5f, 15.5f, 7.5f, 15.5f, 1.8f, 235, 240, 248);
        draw_line(rgba, 23.5f, 15.5f, 27.5f, 15.5f, 1.8f, 235, 240, 248);
        if (m_icon_tex_reset) RenderSystem::instance().delete_texture(m_icon_tex_reset);
        m_icon_tex_reset = static_cast<GLuint>(RenderSystem::instance().create_texture(w, h, rgba.data()));
    }

    // 6. Popout Tool Icon (Window with outward diagonal arrow)
    {
        std::vector<uint8_t> rgba(w * h * 4, 0);
        draw_rect(rgba, 5.0f, 10.0f, 20.0f, 26.0f, 1.8f, 170, 205, 245);
        draw_line(rgba, 5.0f, 14.0f, 20.0f, 14.0f, 1.5f, 170, 205, 245);
        draw_line(rgba, 13.0f, 17.0f, 26.0f, 5.0f, 2.2f, 235, 240, 248);
        draw_line(rgba, 20.0f, 5.0f, 26.0f, 5.0f, 2.2f, 235, 240, 248);
        draw_line(rgba, 26.0f, 11.0f, 26.0f, 5.0f, 2.2f, 235, 240, 248);
        if (m_icon_tex_popout) RenderSystem::instance().delete_texture(m_icon_tex_popout);
        m_icon_tex_popout = static_cast<GLuint>(RenderSystem::instance().create_texture(w, h, rgba.data()));
    }
}

void App::cleanup_preview_toolbar_icons() {
    if (m_icon_tex_select) { RenderSystem::instance().delete_texture(m_icon_tex_select); m_icon_tex_select = 0; }
    m_icon_tex_transform = 0;
    if (m_icon_tex_translate) { RenderSystem::instance().delete_texture(m_icon_tex_translate); m_icon_tex_translate = 0; }
    if (m_icon_tex_scale) { RenderSystem::instance().delete_texture(m_icon_tex_scale); m_icon_tex_scale = 0; }
    if (m_icon_tex_rotate) { RenderSystem::instance().delete_texture(m_icon_tex_rotate); m_icon_tex_rotate = 0; }
    if (m_icon_tex_reset) { RenderSystem::instance().delete_texture(m_icon_tex_reset); m_icon_tex_reset = 0; }
    if (m_icon_tex_popout) { RenderSystem::instance().delete_texture(m_icon_tex_popout); m_icon_tex_popout = 0; }
}



