#pragma once

#include "RenderBackend.h"

class RenderBackendVulkan : public RenderBackend {
public:
    RenderBackendVulkan();
    ~RenderBackendVulkan() override;

    RenderBackendType get_type() const override { return RenderBackendType::Vulkan; }
    const char* get_name() const override { return "Vulkan"; }
    bool is_supported() const override;

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
    bool m_vulkan_available = false;
};
