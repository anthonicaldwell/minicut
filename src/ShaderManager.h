#pragma once

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <functional>

class App;

struct CustomShader {
    std::string name;       // Display name or file stem, e.g. "ripple"
    std::string type;       // "gl" or "vulkan"
    std::string vert_path;  // Path to .vert file if exists
    std::string frag_path;  // Path to .frag file
    std::string vert_src;
    std::string frag_src;
};

class ShaderManager {
public:
    static ShaderManager& instance();

    void init();
    void scan_shaders();

    const std::vector<CustomShader>& get_gl_shaders() const { return m_gl_shaders; }
    const std::vector<CustomShader>& get_vulkan_shaders() const { return m_vulkan_shaders; }

    std::string get_gl_shaders_dir() const;
    std::string get_vulkan_shaders_dir() const;

    bool find_shader(const std::string& name_or_stem, CustomShader& out_shader, const std::string& preferred_type = "gl");
    bool load_shader_source(CustomShader& shader);

    void render_shaders_menu(std::function<void(const CustomShader&)> on_apply_shader,
                             std::function<void(const std::string&)> on_status);
    void render_shaders_menu(App& app);

private:
    ShaderManager() = default;
    ~ShaderManager() = default;

    void ensure_directories_and_defaults();

    std::vector<CustomShader> m_gl_shaders;
    std::vector<CustomShader> m_vulkan_shaders;
};
