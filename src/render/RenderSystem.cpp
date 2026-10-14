#include "RenderSystem.h"
#include "RenderBackendOpenGL3.h"
#include "RenderBackendVulkan.h"
#include <iostream>

RenderSystem::RenderSystem() = default;

RenderSystem::~RenderSystem() {
    shutdown();
}

RenderSystem& RenderSystem::instance() {
    static RenderSystem s_instance;
    return s_instance;
}

std::unique_ptr<RenderBackend> RenderSystem::create_backend_instance(RenderBackendType type) {
    if (type == RenderBackendType::Vulkan) {
        return std::make_unique<RenderBackendVulkan>();
    }
    return std::make_unique<RenderBackendOpenGL3>();
}

bool RenderSystem::init(SDL_Window* window, SDL_GLContext gl_context, RenderBackendType preferred_type) {
    m_window = window;
    m_gl_context = gl_context;
    m_imgui_context = ImGui::GetCurrentContext();

    // Try preferred backend
    m_backend = create_backend_instance(preferred_type);
    if (!m_backend || !m_backend->init(window, gl_context)) {
        if (preferred_type != RenderBackendType::OpenGL3) {
            std::cout << "RenderSystem: Falling back to default OpenGL 3.3 backend..." << std::endl;
            m_backend = create_backend_instance(RenderBackendType::OpenGL3);
            if (!m_backend || !m_backend->init(window, gl_context)) {
                std::cerr << "RenderSystem: Failed to initialize fallback OpenGL 3.3 backend!" << std::endl;
                return false;
            }
            m_current_type = RenderBackendType::OpenGL3;
        } else {
            return false;
        }
    } else {
        m_current_type = preferred_type;
    }

    ensure_context_focus();
    return true;
}

void RenderSystem::shutdown() {
    if (m_backend) {
        if (m_font_texture_id) {
            m_backend->delete_texture(m_font_texture_id);
            m_font_texture_id = 0;
        }
        m_backend->shutdown();
        m_backend.reset();
    }
}

bool RenderSystem::set_backend(RenderBackendType type) {
    if (type == m_current_type && m_backend) return true;

    ensure_context_focus();

    std::cout << "RenderSystem: Switching backend to " << (type == RenderBackendType::Vulkan ? "Vulkan" : "OpenGL 3.3") << "..." << std::endl;

    auto new_backend = create_backend_instance(type);
    if (!new_backend || !new_backend->init(m_window, m_gl_context)) {
        std::cerr << "RenderSystem: Backend initialization failed; maintaining current backend." << std::endl;
        return false;
    }

    if (m_backend) {
        if (m_font_texture_id) {
            m_backend->delete_texture(m_font_texture_id);
            m_font_texture_id = 0;
        }
        m_backend->shutdown();
    }

    m_backend = std::move(new_backend);
    m_current_type = type;

    // Restore context focus and re-upload font texture
    ensure_context_focus();
    return true;
}

const char* RenderSystem::get_backend_name() const {
    if (m_backend) return m_backend->get_name();
    return (m_current_type == RenderBackendType::Vulkan) ? "Vulkan" : "OpenGL 3.3";
}

void RenderSystem::ensure_context_focus() {
    // Address ImGui context focus between memory pools
    if (m_imgui_context) {
        ImGui::SetCurrentContext(m_imgui_context);
    }

    if (!m_backend) return;

    ImGuiContext* g = ImGui::GetCurrentContext();
    if (!g) return;

    ImGuiIO& io = ImGui::GetIO();

    // Verify font atlas texture validity
    if (!io.Fonts->IsBuilt()) {
        io.Fonts->Build();
    }

    if (io.Fonts->TexID == 0 || m_font_texture_id == 0) {
        unsigned char* pixels = nullptr;
        int width = 0, height = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        if (pixels && width > 0 && height > 0) {
            if (m_font_texture_id) {
                m_backend->delete_texture(m_font_texture_id);
            }
            m_font_texture_id = m_backend->create_texture(width, height, pixels);
            io.Fonts->SetTexID(static_cast<ImTextureID>(m_font_texture_id));
        }
    }
}

void RenderSystem::new_frame() {
    ensure_context_focus();
    if (m_backend) {
        m_backend->new_frame();
    }
}

void RenderSystem::render_and_swap(ImDrawData* draw_data, int width, int height) {
    ensure_context_focus();
    if (m_backend) {
        m_backend->render_draw_data(draw_data, width, height);
        m_backend->swap_window(m_window);
    }
}

uintptr_t RenderSystem::create_texture(int width, int height, const uint8_t* rgba) {
    if (m_backend) {
        return m_backend->create_texture(width, height, rgba);
    }
    return 0;
}

void RenderSystem::update_texture(uintptr_t texture_id, int width, int height, const uint8_t* rgba) {
    if (m_backend) {
        m_backend->update_texture(texture_id, width, height, rgba);
    }
}

void RenderSystem::delete_texture(uintptr_t texture_id) {
    if (m_backend) {
        m_backend->delete_texture(texture_id);
    }
}
