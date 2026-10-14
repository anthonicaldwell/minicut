#pragma once

#include "RenderBackend.h"
#include <memory>
#include <string>

class RenderSystem {
public:
    RenderSystem();
    ~RenderSystem();

    static RenderSystem& instance();

    bool init(SDL_Window* window, SDL_GLContext gl_context, RenderBackendType preferred_type = RenderBackendType::OpenGL3);
    void shutdown();

    bool set_backend(RenderBackendType type);
    RenderBackendType get_backend_type() const { return m_current_type; }
    const char* get_backend_name() const;

    void ensure_context_focus();

    void new_frame();
    void render_and_swap(ImDrawData* draw_data, int width, int height);

    uintptr_t create_texture(int width, int height, const uint8_t* rgba = nullptr);
    void update_texture(uintptr_t texture_id, int width, int height, const uint8_t* rgba);
    void delete_texture(uintptr_t texture_id);

    RenderBackend* get_backend() { return m_backend.get(); }

private:
    std::unique_ptr<RenderBackend> create_backend_instance(RenderBackendType type);

    std::unique_ptr<RenderBackend> m_backend;
    RenderBackendType m_current_type = RenderBackendType::OpenGL3;

    SDL_Window* m_window = nullptr;
    SDL_GLContext m_gl_context = nullptr;
    ImGuiContext* m_imgui_context = nullptr;
    uintptr_t m_font_texture_id = 0;
};
