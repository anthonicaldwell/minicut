#pragma once

#include "RenderBackend.h"
#include <vulkan/vulkan.h>
#include "backends/imgui_impl_vulkan.h"
#include <unordered_map>
#include <vector>

struct VulkanTextureResource {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
    int width = 0;
    int height = 0;
};

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

    VkInstance get_instance() const { return m_instance; }
    VkDevice get_device() const { return m_device; }
    VkPhysicalDevice get_physical_device() const { return m_physical_device; }
    VkQueue get_queue() const { return m_queue; }

private:
    uint32_t find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties);
    void setup_render_pass();

    bool m_initialized = false;
    bool m_vulkan_available = false;
    bool m_has_swapchain = false;

    SDL_Window* m_window = nullptr;
    VkAllocationCallbacks* m_allocator = nullptr;
    VkInstance m_instance = VK_NULL_HANDLE;
    VkPhysicalDevice m_physical_device = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    uint32_t m_queue_family = (uint32_t)-1;
    VkQueue m_queue = VK_NULL_HANDLE;
    VkPipelineCache m_pipeline_cache = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptor_pool = VK_NULL_HANDLE;
    VkCommandPool m_command_pool = VK_NULL_HANDLE;

    // Window surface and swapchain
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    ImGui_ImplVulkanH_Window m_window_data{};
    uint32_t m_min_image_count = 2;
    bool m_swapchain_rebuild = false;

    // Texture tracking
    std::unordered_map<uintptr_t, VulkanTextureResource> m_textures;
};
