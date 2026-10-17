#include "RenderBackendVulkan.h"
#include <SDL2/SDL_vulkan.h>
#include <iostream>
#include <vector>
#include <cstring>
#include <algorithm>

RenderBackendVulkan::RenderBackendVulkan() {
    m_vulkan_available = is_supported();
}

RenderBackendVulkan::~RenderBackendVulkan() {
    shutdown();
}

bool RenderBackendVulkan::is_supported() const {
    bool sdl_inited = (SDL_WasInit(SDL_INIT_VIDEO) != 0);
    if (!sdl_inited) {
        if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
            return false;
        }
    }

    bool available = false;
    if (SDL_Vulkan_LoadLibrary(nullptr) == 0) {
        auto getProcAddr = (PFN_vkGetInstanceProcAddr)SDL_Vulkan_GetVkGetInstanceProcAddr();
        if (getProcAddr) {
            auto createInst = (PFN_vkCreateInstance)getProcAddr(VK_NULL_HANDLE, "vkCreateInstance");
            if (createInst) {
                available = true;
            }
        }
        SDL_Vulkan_UnloadLibrary();
    }

    if (!sdl_inited) {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }
    return available;
}

uint32_t RenderBackendVulkan::find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(m_physical_device, &mem_props);
    for (uint32_t i = 0; i < mem_props.memoryTypeCount; ++i) {
        if ((type_filter & (1 << i)) && (mem_props.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return 0;
}

bool RenderBackendVulkan::init(SDL_Window* window, SDL_GLContext gl_context) {
    (void)gl_context;
    if (m_initialized) return true;

    if (SDL_Vulkan_LoadLibrary(nullptr) != 0) {
        std::cerr << "RenderBackendVulkan: Failed to load Vulkan loader: " << SDL_GetError() << std::endl;
        return false;
    }

    m_window = window;

    // 1. Enumerate Instance Extensions
    uint32_t ext_count = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &ext_count, nullptr);
    std::vector<VkExtensionProperties> available_exts(ext_count);
    vkEnumerateInstanceExtensionProperties(nullptr, &ext_count, available_exts.data());

    auto is_ext_supported = [&](const char* name) {
        for (const auto& p : available_exts) {
            if (std::strcmp(p.extensionName, name) == 0) return true;
        }
        return false;
    };

    std::vector<const char*> instance_extensions;
    if (window) {
        unsigned int sdl_ext_count = 0;
        if (SDL_Vulkan_GetInstanceExtensions(window, &sdl_ext_count, nullptr) && sdl_ext_count > 0) {
            std::vector<const char*> sdl_exts(sdl_ext_count);
            SDL_Vulkan_GetInstanceExtensions(window, &sdl_ext_count, sdl_exts.data());
            for (const char* e : sdl_exts) {
                instance_extensions.push_back(e);
            }
        }
    }

    if (instance_extensions.empty()) {
        if (is_ext_supported("VK_KHR_surface")) instance_extensions.push_back("VK_KHR_surface");
        if (is_ext_supported("VK_KHR_xcb_surface")) instance_extensions.push_back("VK_KHR_xcb_surface");
        if (is_ext_supported("VK_KHR_xlib_surface")) instance_extensions.push_back("VK_KHR_xlib_surface");
        if (is_ext_supported("VK_KHR_wayland_surface")) instance_extensions.push_back("VK_KHR_wayland_surface");
    }

    if (is_ext_supported("VK_KHR_get_physical_device_properties2")) {
        instance_extensions.push_back("VK_KHR_get_physical_device_properties2");
    }

    // 2. Create Vulkan Instance
    VkApplicationInfo app_info = {};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "minicut";
    app_info.applicationVersion = VK_MAKE_VERSION(1, 5, 0);
    app_info.pEngineName = "minicut_render";
    app_info.engineVersion = VK_MAKE_VERSION(1, 5, 0);
    app_info.apiVersion = VK_API_VERSION_1_2;

    VkInstanceCreateInfo create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.pApplicationInfo = &app_info;
    create_info.enabledExtensionCount = static_cast<uint32_t>(instance_extensions.size());
    create_info.ppEnabledExtensionNames = instance_extensions.data();

    VkResult err = vkCreateInstance(&create_info, m_allocator, &m_instance);
    if (err != VK_SUCCESS) {
        std::cerr << "RenderBackendVulkan: vkCreateInstance failed with error " << err << std::endl;
        SDL_Vulkan_UnloadLibrary();
        return false;
    }

    // 3. Select Physical Device
    uint32_t gpu_count = 0;
    vkEnumeratePhysicalDevices(m_instance, &gpu_count, nullptr);
    if (gpu_count == 0) {
        std::cerr << "RenderBackendVulkan: No Vulkan physical devices found." << std::endl;
        shutdown();
        return false;
    }

    std::vector<VkPhysicalDevice> gpus(gpu_count);
    vkEnumeratePhysicalDevices(m_instance, &gpu_count, gpus.data());

    m_physical_device = gpus[0];
    for (const auto& gpu : gpus) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(gpu, &props);
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            m_physical_device = gpu;
            break;
        }
    }

    // 4. Find Queue Family
    uint32_t queue_family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(m_physical_device, &queue_family_count, nullptr);
    std::vector<VkQueueFamilyProperties> queue_families(queue_family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(m_physical_device, &queue_family_count, queue_families.data());

    for (uint32_t i = 0; i < queue_family_count; ++i) {
        if (queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            m_queue_family = i;
            break;
        }
    }

    if (m_queue_family == (uint32_t)-1) {
        std::cerr << "RenderBackendVulkan: No graphics queue family found on physical device." << std::endl;
        shutdown();
        return false;
    }

    // 5. Create Logical Device
    std::vector<const char*> device_extensions;
    uint32_t dev_ext_count = 0;
    vkEnumerateDeviceExtensionProperties(m_physical_device, nullptr, &dev_ext_count, nullptr);
    std::vector<VkExtensionProperties> available_dev_exts(dev_ext_count);
    vkEnumerateDeviceExtensionProperties(m_physical_device, nullptr, &dev_ext_count, available_dev_exts.data());

    for (const auto& ext : available_dev_exts) {
        if (std::strcmp(ext.extensionName, "VK_KHR_swapchain") == 0) {
            device_extensions.push_back("VK_KHR_swapchain");
            break;
        }
    }

    float queue_priority[] = { 1.0f };
    VkDeviceQueueCreateInfo queue_info = {};
    queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info.queueFamilyIndex = m_queue_family;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = queue_priority;

    VkDeviceCreateInfo device_ci = {};
    device_ci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_ci.queueCreateInfoCount = 1;
    device_ci.pQueueCreateInfos = &queue_info;
    device_ci.enabledExtensionCount = static_cast<uint32_t>(device_extensions.size());
    device_ci.ppEnabledExtensionNames = device_extensions.data();

    err = vkCreateDevice(m_physical_device, &device_ci, m_allocator, &m_device);
    if (err != VK_SUCCESS) {
        std::cerr << "RenderBackendVulkan: vkCreateDevice failed with error " << err << std::endl;
        shutdown();
        return false;
    }

    vkGetDeviceQueue(m_device, m_queue_family, 0, &m_queue);

    // 6. Create Descriptor Pool
    VkDescriptorPoolSize pool_sizes[] = {
        { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
    };

    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = 1000 * IM_ARRAYSIZE(pool_sizes);
    pool_info.poolSizeCount = static_cast<uint32_t>(IM_ARRAYSIZE(pool_sizes));
    pool_info.pPoolSizes = pool_sizes;

    err = vkCreateDescriptorPool(m_device, &pool_info, m_allocator, &m_descriptor_pool);
    if (err != VK_SUCCESS) {
        std::cerr << "RenderBackendVulkan: Failed to create descriptor pool: " << err << std::endl;
        shutdown();
        return false;
    }

    // 7. Create Command Pool
    VkCommandPoolCreateInfo cmd_pool_info = {};
    cmd_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cmd_pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cmd_pool_info.queueFamilyIndex = m_queue_family;
    err = vkCreateCommandPool(m_device, &cmd_pool_info, m_allocator, &m_command_pool);
    if (err != VK_SUCCESS) {
        std::cerr << "RenderBackendVulkan: Failed to create command pool: " << err << std::endl;
        shutdown();
        return false;
    }

    // 8. Surface & Swapchain (if window provided with Vulkan surface capability)
    if (m_window) {
        if (SDL_Vulkan_CreateSurface(m_window, m_instance, &m_surface)) {
            VkBool32 wsi_supported = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(m_physical_device, m_queue_family, m_surface, &wsi_supported);
            if (wsi_supported) {
                int w = 0, h = 0;
                SDL_GetWindowSize(m_window, &w, &h);
                if (w <= 0) w = 1280;
                if (h <= 0) h = 720;

                m_window_data.Surface = m_surface;

                const VkFormat requestFormats[] = {
                    VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM,
                    VK_FORMAT_B8G8R8_UNORM, VK_FORMAT_R8G8B8_UNORM
                };
                m_window_data.SurfaceFormat = ImGui_ImplVulkanH_SelectSurfaceFormat(
                    m_physical_device, m_surface, requestFormats,
                    IM_ARRAYSIZE(requestFormats), VK_COLORSPACE_SRGB_NONLINEAR_KHR
                );

                VkPresentModeKHR present_modes[] = { VK_PRESENT_MODE_FIFO_KHR };
                m_window_data.PresentMode = ImGui_ImplVulkanH_SelectPresentMode(
                    m_physical_device, m_surface, present_modes, IM_ARRAYSIZE(present_modes)
                );

                ImGui_ImplVulkanH_CreateOrResizeWindow(
                    m_instance, m_physical_device, m_device, &m_window_data,
                    m_queue_family, m_allocator, w, h, m_min_image_count
                );

                ImGui_ImplVulkan_InitInfo init_info = {};
                init_info.Instance = m_instance;
                init_info.PhysicalDevice = m_physical_device;
                init_info.Device = m_device;
                init_info.QueueFamily = m_queue_family;
                init_info.Queue = m_queue;
                init_info.PipelineCache = m_pipeline_cache;
                init_info.DescriptorPool = m_descriptor_pool;
                init_info.RenderPass = m_window_data.RenderPass;
                init_info.Subpass = 0;
                init_info.MinImageCount = m_min_image_count;
                init_info.ImageCount = m_window_data.ImageCount;
                init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
                init_info.Allocator = m_allocator;

                if (ImGui_ImplVulkan_Init(&init_info)) {
                    m_has_swapchain = true;
                    std::cout << "RenderBackendVulkan: Vulkan 1.2+ pipeline initialized successfully with surface swapchain." << std::endl;
                }
            }
        } else {
            std::cout << "RenderBackendVulkan: Initialized in headless/offscreen device mode (no window surface bound)." << std::endl;
        }
    }

    if (m_window != nullptr && !m_has_swapchain) {
        std::cerr << "RenderBackendVulkan: Window provided without valid Vulkan swapchain surface. Falling back to OpenGL." << std::endl;
        m_initialized = true;
        shutdown();
        return false;
    }

    m_initialized = true;
    return true;
}

void RenderBackendVulkan::shutdown() {
    if (!m_initialized) return;

    if (m_device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_device);

        // Delete all textures
        auto textures_copy = m_textures;
        for (const auto& pair : textures_copy) {
            delete_texture(pair.first);
        }
        m_textures.clear();

        if (m_has_swapchain) {
            ImGui_ImplVulkan_Shutdown();
            ImGui_ImplVulkanH_DestroyWindow(m_instance, m_device, &m_window_data, m_allocator);
            m_has_swapchain = false;
            m_surface = VK_NULL_HANDLE;
        }

        if (m_surface != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(m_instance, m_surface, m_allocator);
            m_surface = VK_NULL_HANDLE;
        }

        if (m_command_pool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(m_device, m_command_pool, m_allocator);
            m_command_pool = VK_NULL_HANDLE;
        }

        if (m_descriptor_pool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(m_device, m_descriptor_pool, m_allocator);
            m_descriptor_pool = VK_NULL_HANDLE;
        }

        vkDestroyDevice(m_device, m_allocator);
        m_device = VK_NULL_HANDLE;
    }

    if (m_instance != VK_NULL_HANDLE) {
        vkDestroyInstance(m_instance, m_allocator);
        m_instance = VK_NULL_HANDLE;
    }

    SDL_Vulkan_UnloadLibrary();
    m_initialized = false;
}

void RenderBackendVulkan::new_frame() {
    if (m_has_swapchain) {
        ImGui_ImplVulkan_NewFrame();
    }
}

void RenderBackendVulkan::render_draw_data(ImDrawData* draw_data, int width, int height) {
    if (!m_has_swapchain || !draw_data) return;

    if (m_swapchain_rebuild) {
        int w = 0, h = 0;
        if (m_window) SDL_GetWindowSize(m_window, &w, &h);
        if (w > 0 && h > 0) {
            ImGui_ImplVulkan_SetMinImageCount(m_min_image_count);
            ImGui_ImplVulkanH_CreateOrResizeWindow(
                m_instance, m_physical_device, m_device, &m_window_data,
                m_queue_family, m_allocator, w, h, m_min_image_count
            );
            m_window_data.FrameIndex = 0;
            m_swapchain_rebuild = false;
        }
    }

    VkSemaphore image_acquired_sem = m_window_data.FrameSemaphores[m_window_data.SemaphoreIndex].ImageAcquiredSemaphore;
    VkSemaphore render_complete_sem = m_window_data.FrameSemaphores[m_window_data.SemaphoreIndex].RenderCompleteSemaphore;

    VkResult err = vkAcquireNextImageKHR(m_device, m_window_data.Swapchain, UINT64_MAX, image_acquired_sem, VK_NULL_HANDLE, &m_window_data.FrameIndex);
    if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR) {
        m_swapchain_rebuild = true;
        return;
    }
    if (err != VK_SUCCESS) return;

    ImGui_ImplVulkanH_Frame* fd = &m_window_data.Frames[m_window_data.FrameIndex];
    vkWaitForFences(m_device, 1, &fd->Fence, VK_TRUE, UINT64_MAX);
    vkResetFences(m_device, 1, &fd->Fence);

    vkResetCommandPool(m_device, fd->CommandPool, 0);
    VkCommandBufferBeginInfo begin_info = {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(fd->CommandBuffer, &begin_info);

    VkRenderPassBeginInfo rp_info = {};
    rp_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp_info.renderPass = m_window_data.RenderPass;
    rp_info.framebuffer = fd->Framebuffer;
    rp_info.renderArea.extent.width = m_window_data.Width;
    rp_info.renderArea.extent.height = m_window_data.Height;
    rp_info.clearValueCount = 1;
    VkClearValue clear_color = { {{0.14f, 0.15f, 0.16f, 1.0f}} };
    rp_info.pClearValues = &clear_color;
    vkCmdBeginRenderPass(fd->CommandBuffer, &rp_info, VK_SUBPASS_CONTENTS_INLINE);

    ImGui_ImplVulkan_RenderDrawData(draw_data, fd->CommandBuffer);

    vkCmdEndRenderPass(fd->CommandBuffer);

    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit_info = {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = &image_acquired_sem;
    submit_info.pWaitDstStageMask = &wait_stage;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &fd->CommandBuffer;
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = &render_complete_sem;

    vkEndCommandBuffer(fd->CommandBuffer);
    vkQueueSubmit(m_queue, 1, &submit_info, fd->Fence);
}

void RenderBackendVulkan::swap_window(SDL_Window* window) {
    (void)window;
    if (!m_has_swapchain || m_swapchain_rebuild) return;

    VkSemaphore render_complete_sem = m_window_data.FrameSemaphores[m_window_data.SemaphoreIndex].RenderCompleteSemaphore;
    VkPresentInfoKHR present_info = {};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = &render_complete_sem;
    present_info.swapchainCount = 1;
    present_info.pSwapchains = &m_window_data.Swapchain;
    present_info.pImageIndices = &m_window_data.FrameIndex;

    VkResult err = vkQueuePresentKHR(m_queue, &present_info);
    if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR) {
        m_swapchain_rebuild = true;
    }
    m_window_data.SemaphoreIndex = (m_window_data.SemaphoreIndex + 1) % m_window_data.SemaphoreCount;
}

uintptr_t RenderBackendVulkan::create_texture(int width, int height, const uint8_t* rgba) {
    if (!m_initialized || width <= 0 || height <= 0) return 0;

    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;

    // 1. Create Image
    VkImageCreateInfo image_info = {};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    image_info.extent = { static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1 };
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(m_device, &image_info, m_allocator, &image) != VK_SUCCESS) {
        return 0;
    }

    VkMemoryRequirements mem_reqs;
    vkGetImageMemoryRequirements(m_device, image, &mem_reqs);

    VkMemoryAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_reqs.size;
    alloc_info.memoryTypeIndex = find_memory_type(mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(m_device, &alloc_info, m_allocator, &memory) != VK_SUCCESS) {
        vkDestroyImage(m_device, image, m_allocator);
        return 0;
    }

    vkBindImageMemory(m_device, image, memory, 0);

    // 2. Upload Pixel Data (via staging buffer)
    size_t upload_size = static_cast<size_t>(width) * height * 4;
    VkBuffer staging_buf = VK_NULL_HANDLE;
    VkDeviceMemory staging_mem = VK_NULL_HANDLE;

    VkBufferCreateInfo buf_info = {};
    buf_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buf_info.size = upload_size;
    buf_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    buf_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkCreateBuffer(m_device, &buf_info, m_allocator, &staging_buf);

    VkMemoryRequirements buf_reqs;
    vkGetBufferMemoryRequirements(m_device, staging_buf, &buf_reqs);

    VkMemoryAllocateInfo buf_alloc = {};
    buf_alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    buf_alloc.allocationSize = buf_reqs.size;
    buf_alloc.memoryTypeIndex = find_memory_type(buf_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkAllocateMemory(m_device, &buf_alloc, m_allocator, &staging_mem);
    vkBindBufferMemory(m_device, staging_buf, staging_mem, 0);

    void* mapped = nullptr;
    vkMapMemory(m_device, staging_mem, 0, upload_size, 0, &mapped);
    if (rgba) {
        std::memcpy(mapped, rgba, upload_size);
    } else {
        std::memset(mapped, 0, upload_size);
    }
    vkUnmapMemory(m_device, staging_mem);

    // Single time command buffer
    VkCommandBufferAllocateInfo cmd_alloc = {};
    cmd_alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmd_alloc.commandPool = m_command_pool;
    cmd_alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmd_alloc.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(m_device, &cmd_alloc, &cmd);

    VkCommandBufferBeginInfo cb_begin = {};
    cb_begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cb_begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &cb_begin);

    // Transition Undefined -> Transfer Dst
    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    VkBufferImageCopy region = {};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = { static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1 };
    vkCmdCopyBufferToImage(cmd, staging_buf, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    // Transition Transfer Dst -> Shader Read Only
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submit_info = {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &cmd;
    vkQueueSubmit(m_queue, 1, &submit_info, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_queue);

    vkFreeCommandBuffers(m_device, m_command_pool, 1, &cmd);
    vkDestroyBuffer(m_device, staging_buf, m_allocator);
    vkFreeMemory(m_device, staging_mem, m_allocator);

    // 3. Create Image View
    VkImageViewCreateInfo view_info = {};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.layerCount = 1;
    if (vkCreateImageView(m_device, &view_info, m_allocator, &view) != VK_SUCCESS) {
        vkDestroyImage(m_device, image, m_allocator);
        vkFreeMemory(m_device, memory, m_allocator);
        return 0;
    }

    // 4. Create Sampler
    VkSamplerCreateInfo sampler_info = {};
    sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_info.magFilter = VK_FILTER_LINEAR;
    sampler_info.minFilter = VK_FILTER_LINEAR;
    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(m_device, &sampler_info, m_allocator, &sampler) != VK_SUCCESS) {
        vkDestroyImageView(m_device, view, m_allocator);
        vkDestroyImage(m_device, image, m_allocator);
        vkFreeMemory(m_device, memory, m_allocator);
        return 0;
    }

    // 5. ImGui Descriptor Set
    VkDescriptorSet ds = ImGui_ImplVulkan_AddTexture(sampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    VulkanTextureResource res;
    res.image = image;
    res.memory = memory;
    res.view = view;
    res.sampler = sampler;
    res.descriptor_set = ds;
    res.width = width;
    res.height = height;

    uintptr_t handle = reinterpret_cast<uintptr_t>(ds);
    if (handle == 0) {
        // Fallback handle if ImGui wasn't fully initialized with pipeline
        handle = reinterpret_cast<uintptr_t>(view);
    }
    m_textures[handle] = res;
    return handle;
}

void RenderBackendVulkan::update_texture(uintptr_t texture_id, int width, int height, const uint8_t* rgba) {
    if (!m_initialized || !texture_id || !rgba || width <= 0 || height <= 0) return;

    auto it = m_textures.find(texture_id);
    if (it == m_textures.end()) return;

    const auto& res = it->second;
    size_t upload_size = static_cast<size_t>(width) * height * 4;

    VkBuffer staging_buf = VK_NULL_HANDLE;
    VkDeviceMemory staging_mem = VK_NULL_HANDLE;

    VkBufferCreateInfo buf_info = {};
    buf_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buf_info.size = upload_size;
    buf_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    buf_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkCreateBuffer(m_device, &buf_info, m_allocator, &staging_buf);

    VkMemoryRequirements buf_reqs;
    vkGetBufferMemoryRequirements(m_device, staging_buf, &buf_reqs);

    VkMemoryAllocateInfo buf_alloc = {};
    buf_alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    buf_alloc.allocationSize = buf_reqs.size;
    buf_alloc.memoryTypeIndex = find_memory_type(buf_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkAllocateMemory(m_device, &buf_alloc, m_allocator, &staging_mem);
    vkBindBufferMemory(m_device, staging_buf, staging_mem, 0);

    void* mapped = nullptr;
    vkMapMemory(m_device, staging_mem, 0, upload_size, 0, &mapped);
    std::memcpy(mapped, rgba, upload_size);
    vkUnmapMemory(m_device, staging_mem);

    VkCommandBufferAllocateInfo cmd_alloc = {};
    cmd_alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmd_alloc.commandPool = m_command_pool;
    cmd_alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmd_alloc.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(m_device, &cmd_alloc, &cmd);

    VkCommandBufferBeginInfo cb_begin = {};
    cb_begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cb_begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &cb_begin);

    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = res.image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    VkBufferImageCopy region = {};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = { static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1 };
    vkCmdCopyBufferToImage(cmd, staging_buf, res.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submit_info = {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &cmd;
    vkQueueSubmit(m_queue, 1, &submit_info, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_queue);

    vkFreeCommandBuffers(m_device, m_command_pool, 1, &cmd);
    vkDestroyBuffer(m_device, staging_buf, m_allocator);
    vkFreeMemory(m_device, staging_mem, m_allocator);
}

void RenderBackendVulkan::delete_texture(uintptr_t texture_id) {
    if (!m_initialized || !texture_id) return;

    auto it = m_textures.find(texture_id);
    if (it == m_textures.end()) return;

    const auto& res = it->second;
    if (res.descriptor_set) {
        ImGui_ImplVulkan_RemoveTexture(res.descriptor_set);
    }
    if (res.sampler) {
        vkDestroySampler(m_device, res.sampler, m_allocator);
    }
    if (res.view) {
        vkDestroyImageView(m_device, res.view, m_allocator);
    }
    if (res.image) {
        vkDestroyImage(m_device, res.image, m_allocator);
    }
    if (res.memory) {
        vkFreeMemory(m_device, res.memory, m_allocator);
    }
    m_textures.erase(it);
}
