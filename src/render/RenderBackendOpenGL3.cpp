#include "RenderBackendOpenGL3.h"
#include "backends/imgui_impl_opengl3.h"
#include <iostream>
#include <vector>

RenderBackendOpenGL3::RenderBackendOpenGL3() = default;

RenderBackendOpenGL3::~RenderBackendOpenGL3() {
    shutdown();
}

bool RenderBackendOpenGL3::init(SDL_Window* window, SDL_GLContext gl_context) {
    (void)window;
    (void)gl_context;
    if (m_initialized) return true;

    // Initialize GLEW
    glewExperimental = GL_TRUE;
    GLenum err = glewInit();
    if (err != GLEW_OK) {
        std::cerr << "RenderBackendOpenGL3: Failed to initialize GLEW: "
                  << glewGetErrorString(err) << std::endl;
        return false;
    }

    if (!ImGui_ImplOpenGL3_Init("#version 330")) {
        std::cerr << "RenderBackendOpenGL3: Failed to initialize ImGui OpenGL3 backend." << std::endl;
        return false;
    }

    m_initialized = true;
    return true;
}

void RenderBackendOpenGL3::shutdown() {
    if (m_initialized) {
        ImGui_ImplOpenGL3_Shutdown();
        m_initialized = false;
    }
}

void RenderBackendOpenGL3::new_frame() {
    if (m_initialized) {
        ImGui_ImplOpenGL3_NewFrame();
    }
}

void RenderBackendOpenGL3::render_draw_data(ImDrawData* draw_data, int width, int height) {
    if (!m_initialized) return;

    glViewport(0, 0, width, height);
    glClearColor(0.14f, 0.15f, 0.16f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    ImGui_ImplOpenGL3_RenderDrawData(draw_data);
}

void RenderBackendOpenGL3::swap_window(SDL_Window* window) {
    if (window) {
        SDL_GL_SwapWindow(window);
    }
}

uintptr_t RenderBackendOpenGL3::create_texture(int width, int height, const uint8_t* rgba) {
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    if (rgba) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    } else {
        std::vector<uint8_t> blank(width * height * 4, 0);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, blank.data());
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    return static_cast<uintptr_t>(tex);
}

void RenderBackendOpenGL3::update_texture(uintptr_t texture_id, int width, int height, const uint8_t* rgba) {
    if (!texture_id || !rgba) return;

    GLuint tex = static_cast<GLuint>(texture_id);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void RenderBackendOpenGL3::delete_texture(uintptr_t texture_id) {
    if (texture_id) {
        GLuint tex = static_cast<GLuint>(texture_id);
        glDeleteTextures(1, &tex);
    }
}
