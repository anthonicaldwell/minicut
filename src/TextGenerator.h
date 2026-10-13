#pragma once

#include "Common.h"
#include <string>
#include <vector>
#include <cstdint>
#include <utility>

struct TextConfig {
    std::string text = "Title Text";
    std::string font_path;                             // Empty = auto-detect default font
    float font_size = 48.0f;                           // Pixels / pt
    float font_color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };   // RGBA
    bool use_bg = false;
    float bg_color[4] = { 0.0f, 0.0f, 0.0f, 0.6f };     // RGBA
    bool use_outline = false;
    float outline_color[4] = { 0.0f, 0.0f, 0.0f, 1.0f };// RGBA
    float outline_thickness = 2.5f;                    // Pixels
    float letter_spacing = 0.0f;                       // Pixels
    float line_spacing = 1.25f;                        // Multiplier
    int justification = 0;                             // 0 = Left, 1 = Center, 2 = Right
    int padding_x = 24;
    int padding_y = 24;
};

class TextGenerator {
public:
    // Returns list of { Display Name, File Path } for system fonts
    static std::vector<std::pair<std::string, std::string>> get_available_fonts();
    static std::string get_default_font_path();
    static std::string get_arabic_font_path();

    // Renders text configuration to an RGBA pixel buffer
    static bool render_to_rgba(const TextConfig& config,
                               int& out_w, int& out_h,
                               std::vector<uint8_t>& out_rgba);

    // Renders text and writes transparent RGBA PNG to file
    static bool save_to_png(const TextConfig& config,
                            const std::string& output_path,
                            int& out_w, int& out_h);

    // High-level asset creator: writes PNG to temp dir and adds VideoAsset (is_image=true) to project
    static std::string create_text_asset(Project& project,
                                         const TextConfig& config,
                                         const std::string& asset_name = "");
};
