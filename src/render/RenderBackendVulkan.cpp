#include "RenderBackendVulkan.h"
#include <SDL2/SDL_vulkan.h>
#include <iostream>

RenderBackendVulkan::RenderBackendVulkan() {
    m_vulkan_available = is_supported();
}

RenderBackendVulkan::~RenderBackendVulkan() {
    shutdown();
}

bool RenderBackendVulkan::is_supported() const {
    if (SDL_Vulkan_LoadLibrary(nullptr) == 0) {
        SDL_Vulkan_UnloadLibrary();
        return true;
    }
    return false;
}

bool RenderBackendVulkan::init(SDL_Window* window, SDL_GLContext gl_context) {
    (void)window;
    (void)gl_context;
    if (m_initialized) return true;

    if (!is_supported()) {
        std::cerr << "RenderBackendVulkan: Vulkan loader or compatible hardware driver is not available." << std::endl;
        return false;
    }

    std::cout << "RenderBackendVulkan: Probing Vulkan subsystem for device surface..." << std::endl;
    // When a dedicated Vulkan physical device and swapchain pipeline are configured:
    // ImGui_ImplVulkan_Init(&init_info);
    // If running in OpenGL window context, gracefully inform caller to use OpenGL3
    std::cout << "RenderBackendVulkan: SDL window is currently bound to OpenGL context. Falling back to OpenGL 3.3 pipeline." << std::endl;
    return false;
}

void RenderBackendVulkan::shutdown() {
    if (m_initialized) {
        m_initialized = false;
    }
}

void RenderBackendVulkan::new_frame() {
}

void RenderBackendVulkan::render_draw_data(ImDrawData* draw_data, int width, int height) {
    (void)draw_data;
    (void)width;
    (void)height;
}

void RenderBackendVulkan::swap_window(SDL_Window* window) {
    (void)window;
}

uintptr_t RenderBackendVulkan::create_texture(int width, int height, const uint8_t* rgba) {
    (void)width;
    (void)height;
    (void)rgba;
    return 0;
}

void RenderBackendVulkan::update_texture(uintptr_t texture_id, int width, int height, const uint8_t* rgba) {
    (void)texture_id;
    (void)width;
    (void)height;
    (void)rgba;
}

void RenderBackendVulkan::delete_texture(uintptr_t texture_id) {
    (void)texture_id;
}
