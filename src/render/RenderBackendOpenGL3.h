#pragma once

#include "RenderBackend.h"
#include <GL/glew.h>

class RenderBackendOpenGL3 : public RenderBackend {
public:
    RenderBackendOpenGL3();
    ~RenderBackendOpenGL3() override;

    RenderBackendType get_type() const override { return RenderBackendType::OpenGL3; }
    const char* get_name() const override { return "OpenGL 3.3"; }
    bool is_supported() const override { return true; }

    bool init(SDL_Window* window, SDL_GLContext gl_context = nullptr) override;
    void shutdown() override;

    void new_frame() override;
    void render_draw_data(ImDrawData* draw_data, int width, int height) override;
    void swap_window(SDL_Window* window) override;

    uintptr_t create_texture(int width, int height, const uint8_t* rgba = nullptr) override;
    void update_texture(uintptr_t texture_id, int width, int height, const uint8_t* rgba) override;
    void delete_texture(uintptr_t texture_id) override;

private:
    bool m_initialized = false;
};
