#pragma once

#include <string>
#include <cstdint>
#include <SDL2/SDL.h>
#include "imgui.h"

enum class RenderBackendType {
    OpenGL3,
    Vulkan
};

class RenderBackend {
public:
    virtual ~RenderBackend() = default;

    virtual RenderBackendType get_type() const = 0;
    virtual const char* get_name() const = 0;
    virtual bool is_supported() const = 0;

    virtual bool init(SDL_Window* window, SDL_GLContext gl_context = nullptr) = 0;
    virtual void shutdown() = 0;

    virtual void new_frame() = 0;
    virtual void render_draw_data(ImDrawData* draw_data, int width, int height) = 0;
    virtual void swap_window(SDL_Window* window) = 0;

    virtual uintptr_t create_texture(int width, int height, const uint8_t* rgba = nullptr) = 0;
    virtual void update_texture(uintptr_t texture_id, int width, int height, const uint8_t* rgba) = 0;
    virtual void delete_texture(uintptr_t texture_id) = 0;
};
