#include "ShaderManager.h"
#include "imgui.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <cstdlib>
#include <algorithm>

namespace fs = std::filesystem;

ShaderManager& ShaderManager::instance() {
    static ShaderManager s_instance;
    return s_instance;
}

std::string ShaderManager::get_gl_shaders_dir() const {
    const char* home = std::getenv("HOME");
    std::string base = home ? (std::string(home) + "/.config/minicut/shaders/gl") : "/tmp/minicut/shaders/gl";
    return base;
}

std::string ShaderManager::get_vulkan_shaders_dir() const {
    const char* home = std::getenv("HOME");
    std::string base = home ? (std::string(home) + "/.config/minicut/shaders/vulkan") : "/tmp/minicut/shaders/vulkan";
    return base;
}

void ShaderManager::init() {
    ensure_directories_and_defaults();
    scan_shaders();
}

void ShaderManager::ensure_directories_and_defaults() {
    std::string gl_dir = get_gl_shaders_dir();
    std::string vk_dir = get_vulkan_shaders_dir();

    std::error_code ec;
    fs::create_directories(gl_dir, ec);
    fs::create_directories(vk_dir, ec);

    auto write_file_if_missing = [](const std::string& path, const std::string& content) {
        if (!fs::exists(path)) {
            std::ofstream ofs(path);
            if (ofs.is_open()) {
                ofs << content;
            }
        }
    };

    // 1. OpenGL Shaders
    write_file_if_missing(gl_dir + "/default.vert",
        "#version 330 core\n"
        "layout(location = 0) in vec2 aPos;\n"
        "layout(location = 1) in vec2 aTexCoord;\n"
        "out vec2 TexCoord;\n"
        "void main() {\n"
        "    gl_Position = vec4(aPos, 0.0, 1.0);\n"
        "    TexCoord = aTexCoord;\n"
        "}\n");

    write_file_if_missing(gl_dir + "/ripple.vert",
        "#version 330 core\n"
        "layout(location = 0) in vec2 aPos;\n"
        "layout(location = 1) in vec2 aTexCoord;\n"
        "out vec2 TexCoord;\n"
        "void main() {\n"
        "    gl_Position = vec4(aPos, 0.0, 1.0);\n"
        "    TexCoord = aTexCoord;\n"
        "}\n");

    write_file_if_missing(gl_dir + "/ripple.frag",
        "#version 330 core\n"
        "// Shader: ripple\n"
        "in vec2 TexCoord;\n"
        "out vec4 FragColor;\n"
        "uniform sampler2D u_texture;\n"
        "uniform float u_time;\n"
        "uniform float u_frequency;\n"
        "uniform float u_amplitude;\n"
        "void main() {\n"
        "    vec2 p = TexCoord - 0.5;\n"
        "    float r = length(p);\n"
        "    float angle = atan(p.y, p.x);\n"
        "    float freq = (u_frequency > 0.0) ? u_frequency : 30.0;\n"
        "    float amp = (u_amplitude > 0.0) ? u_amplitude : 0.02;\n"
        "    r += sin(r * freq - u_time * 4.0) * amp;\n"
        "    vec2 uv = vec2(0.5 + r * cos(angle), 0.5 + r * sin(angle));\n"
        "    FragColor = texture(u_texture, uv);\n"
        "}\n");

    write_file_if_missing(gl_dir + "/grayscale.frag",
        "#version 330 core\n"
        "in vec2 TexCoord;\n"
        "out vec4 FragColor;\n"
        "uniform sampler2D u_texture;\n"
        "void main() {\n"
        "    vec4 col = texture(u_texture, TexCoord);\n"
        "    float gray = dot(col.rgb, vec3(0.299, 0.587, 0.114));\n"
        "    FragColor = vec4(vec3(gray), col.a);\n"
        "}\n");

    write_file_if_missing(gl_dir + "/invert.frag",
        "#version 330 core\n"
        "in vec2 TexCoord;\n"
        "out vec4 FragColor;\n"
        "uniform sampler2D u_texture;\n"
        "void main() {\n"
        "    vec4 col = texture(u_texture, TexCoord);\n"
        "    FragColor = vec4(vec3(1.0) - col.rgb, col.a);\n"
        "}\n");

    write_file_if_missing(gl_dir + "/vignette.frag",
        "#version 330 core\n"
        "in vec2 TexCoord;\n"
        "out vec4 FragColor;\n"
        "uniform sampler2D u_texture;\n"
        "void main() {\n"
        "    vec4 col = texture(u_texture, TexCoord);\n"
        "    vec2 uv = TexCoord - 0.5;\n"
        "    float dist = length(uv);\n"
        "    float vig = smoothstep(0.75, 0.25, dist);\n"
        "    FragColor = vec4(col.rgb * vig, col.a);\n"
        "}\n");

    // 2. Vulkan Shaders
    write_file_if_missing(vk_dir + "/default.vert",
        "#version 450\n"
        "layout(location = 0) in vec2 aPos;\n"
        "layout(location = 1) in vec2 aTexCoord;\n"
        "layout(location = 0) out vec2 TexCoord;\n"
        "void main() {\n"
        "    gl_Position = vec4(aPos, 0.0, 1.0);\n"
        "    TexCoord = aTexCoord;\n"
        "}\n");

    write_file_if_missing(vk_dir + "/ripple.vert",
        "#version 450\n"
        "layout(location = 0) in vec2 aPos;\n"
        "layout(location = 1) in vec2 aTexCoord;\n"
        "layout(location = 0) out vec2 TexCoord;\n"
        "void main() {\n"
        "    gl_Position = vec4(aPos, 0.0, 1.0);\n"
        "    TexCoord = aTexCoord;\n"
        "}\n");

    write_file_if_missing(vk_dir + "/ripple.frag",
        "#version 450\n"
        "// Shader: ripple\n"
        "layout(location = 0) in vec2 TexCoord;\n"
        "layout(location = 0) out vec4 FragColor;\n"
        "layout(binding = 0) uniform sampler2D u_texture;\n"
        "layout(push_constant) uniform PushConstants {\n"
        "    float u_time;\n"
        "    float u_frequency;\n"
        "    float u_amplitude;\n"
        "} pc;\n"
        "void main() {\n"
        "    vec2 p = TexCoord - 0.5;\n"
        "    float r = length(p);\n"
        "    float angle = atan(p.y, p.x);\n"
        "    float freq = (pc.u_frequency > 0.0) ? pc.u_frequency : 30.0;\n"
        "    float amp = (pc.u_amplitude > 0.0) ? pc.u_amplitude : 0.02;\n"
        "    r += sin(r * freq - pc.u_time * 4.0) * amp;\n"
        "    vec2 uv = vec2(0.5 + r * cos(angle), 0.5 + r * sin(angle));\n"
        "    FragColor = texture(u_texture, uv);\n"
        "}\n");

    write_file_if_missing(vk_dir + "/grayscale.frag",
        "#version 450\n"
        "layout(location = 0) in vec2 TexCoord;\n"
        "layout(location = 0) out vec4 FragColor;\n"
        "layout(binding = 0) uniform sampler2D u_texture;\n"
        "void main() {\n"
        "    vec4 col = texture(u_texture, TexCoord);\n"
        "    float gray = dot(col.rgb, vec3(0.299, 0.587, 0.114));\n"
        "    FragColor = vec4(vec3(gray), col.a);\n"
        "}\n");

    write_file_if_missing(vk_dir + "/invert.frag",
        "#version 450\n"
        "layout(location = 0) in vec2 TexCoord;\n"
        "layout(location = 0) out vec4 FragColor;\n"
        "layout(binding = 0) uniform sampler2D u_texture;\n"
        "void main() {\n"
        "    vec4 col = texture(u_texture, TexCoord);\n"
        "    FragColor = vec4(vec3(1.0) - col.rgb, col.a);\n"
        "}\n");

    write_file_if_missing(vk_dir + "/vignette.frag",
        "#version 450\n"
        "layout(location = 0) in vec2 TexCoord;\n"
        "layout(location = 0) out vec4 FragColor;\n"
        "layout(binding = 0) uniform sampler2D u_texture;\n"
        "void main() {\n"
        "    vec4 col = texture(u_texture, TexCoord);\n"
        "    vec2 uv = TexCoord - 0.5;\n"
        "    float dist = length(uv);\n"
        "    float vig = smoothstep(0.75, 0.25, dist);\n"
        "    FragColor = vec4(col.rgb * vig, col.a);\n"
        "}\n");
}

void ShaderManager::scan_shaders() {
    m_gl_shaders.clear();
    m_vulkan_shaders.clear();

    auto scan_dir = [](const std::string& dir, const std::string& type, std::vector<CustomShader>& list) {
        if (!fs::exists(dir)) return;
        std::unordered_map<std::string, CustomShader> map;

        for (const auto& entry : fs::directory_iterator(dir)) {
            if (!entry.is_regular_file()) continue;
            std::string ext = entry.path().extension().string();
            std::string stem = entry.path().stem().string();

            if (stem == "default") continue; // default pass-through helper

            if (ext == ".frag") {
                map[stem].name = stem;
                map[stem].type = type;
                map[stem].frag_path = entry.path().string();
            } else if (ext == ".vert") {
                map[stem].name = stem;
                map[stem].type = type;
                map[stem].vert_path = entry.path().string();
            }
        }

        for (auto& kv : map) {
            if (!kv.second.frag_path.empty()) {
                // If no matching .vert, fallback to default.vert if exists
                if (kv.second.vert_path.empty()) {
                    std::string def_vert = dir + "/default.vert";
                    if (fs::exists(def_vert)) {
                        kv.second.vert_path = def_vert;
                    }
                }
                list.push_back(std::move(kv.second));
            }
        }

        std::sort(list.begin(), list.end(), [](const CustomShader& a, const CustomShader& b) {
            return a.name < b.name;
        });
    };

    scan_dir(get_gl_shaders_dir(), "gl", m_gl_shaders);
    scan_dir(get_vulkan_shaders_dir(), "vulkan", m_vulkan_shaders);
}

bool ShaderManager::load_shader_source(CustomShader& shader) {
    if (!shader.frag_path.empty()) {
        std::ifstream ifs(shader.frag_path);
        if (ifs.is_open()) {
            std::stringstream ss;
            ss << ifs.rdbuf();
            std::string src = ss.str();
            if (src.find("// Shader: ") == std::string::npos && src.find(shader.name) == std::string::npos) {
                src = "// Shader: " + shader.name + "\n" + src;
            }
            shader.frag_src = src;
        }
    }
    if (!shader.vert_path.empty()) {
        std::ifstream ifs(shader.vert_path);
        if (ifs.is_open()) {
            std::stringstream ss;
            ss << ifs.rdbuf();
            shader.vert_src = ss.str();
        }
    }
    return !shader.frag_src.empty();
}

bool ShaderManager::find_shader(const std::string& name_or_stem, CustomShader& out_shader, const std::string& preferred_type) {
    std::string stem = fs::path(name_or_stem).stem().string();

    auto search = [&](const std::vector<CustomShader>& list) -> bool {
        for (const auto& sh : list) {
            if (sh.name == stem || sh.name == name_or_stem) {
                out_shader = sh;
                return true;
            }
        }
        return false;
    };

    if (preferred_type == "vulkan") {
        if (search(m_vulkan_shaders)) return true;
        if (search(m_gl_shaders)) return true;
    } else {
        if (search(m_gl_shaders)) return true;
        if (search(m_vulkan_shaders)) return true;
    }

    // Direct path lookup
    if (fs::exists(name_or_stem)) {
        out_shader.name = stem;
        out_shader.type = preferred_type;
        out_shader.frag_path = name_or_stem;
        std::string vert = fs::path(name_or_stem).replace_extension(".vert").string();
        if (fs::exists(vert)) out_shader.vert_path = vert;
        return true;
    }

    return false;
}

void ShaderManager::render_shaders_menu(std::function<void(const CustomShader&)> on_apply_shader,
                                        std::function<void(const std::string&)> on_status) {
    if (ImGui::BeginMenu("Shaders")) {
        // OpenGL Submenu
        if (ImGui::BeginMenu("OpenGL Shaders (gl/)")) {
            if (m_gl_shaders.empty()) {
                ImGui::TextDisabled("No .frag shaders in ~/.config/minicut/shaders/gl");
            } else {
                for (const auto& sh : m_gl_shaders) {
                    std::string label = sh.name;
                    if (!label.empty()) label[0] = std::toupper(label[0]);
                    if (ImGui::MenuItem(label.c_str())) {
                        CustomShader loaded = sh;
                        load_shader_source(loaded);
                        if (on_apply_shader) on_apply_shader(loaded);
                    }
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Open OpenGL Shaders Folder...")) {
                std::string cmd = "xdg-open \"" + get_gl_shaders_dir() + "\" > /dev/null 2>&1 &";
                std::system(cmd.c_str());
            }
            ImGui::EndMenu();
        }

        // Vulkan Submenu
        if (ImGui::BeginMenu("Vulkan Shaders (vulkan/)")) {
            if (m_vulkan_shaders.empty()) {
                ImGui::TextDisabled("No .frag shaders in ~/.config/minicut/shaders/vulkan");
            } else {
                for (const auto& sh : m_vulkan_shaders) {
                    std::string label = sh.name;
                    if (!label.empty()) label[0] = std::toupper(label[0]);
                    if (ImGui::MenuItem(label.c_str())) {
                        CustomShader loaded = sh;
                        load_shader_source(loaded);
                        if (on_apply_shader) on_apply_shader(loaded);
                    }
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Open Vulkan Shaders Folder...")) {
                std::string cmd = "xdg-open \"" + get_vulkan_shaders_dir() + "\" > /dev/null 2>&1 &";
                std::system(cmd.c_str());
            }
            ImGui::EndMenu();
        }

        ImGui::Separator();
        if (ImGui::MenuItem("Reload Shaders")) {
            scan_shaders();
            if (on_status) on_status("Reloaded all custom shaders from disk");
        }

        ImGui::EndMenu();
    }
}
