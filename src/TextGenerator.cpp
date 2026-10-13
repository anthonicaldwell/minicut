#include "TextGenerator.h"
#include "ArabicShaper.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_STROKER_H
#include <png.h>
#include <GL/glew.h>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <chrono>
#include <algorithm>
#include <cmath>

namespace fs = std::filesystem;

std::vector<std::pair<std::string, std::string>> TextGenerator::get_available_fonts() {
    std::vector<std::pair<std::string, std::string>> fonts;
    static const std::vector<std::pair<std::string, std::string>> candidates = {
        { "Space Grotesk", "/usr/share/fonts/truetype/space-grotesk/SpaceGrotesk-Regular.ttf" },
        { "Noto Sans", "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf" },
        { "Noto Sans Bold", "/usr/share/fonts/truetype/noto/NotoSans-Bold.ttf" },
        { "Noto Naskh Arabic", "/usr/share/fonts/truetype/noto/NotoNaskhArabic-Regular.ttf" },
        { "Noto Naskh Arabic Bold", "/usr/share/fonts/truetype/noto/NotoNaskhArabic-Bold.ttf" },
        { "DejaVu Sans", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf" },
        { "DejaVu Sans Bold", "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf" },
        { "Liberation Sans", "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf" },
        { "IBM Plex Sans", "/usr/share/fonts/truetype/ibm-plex/IBMPlexSans-Regular.ttf" },
    };

    for (const auto& pair : candidates) {
        if (fs::exists(pair.second)) {
            fonts.push_back(pair);
        }
    }

    if (fonts.empty()) {
        fonts.push_back({ "Default System Font", "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf" });
    }
    return fonts;
}

std::string TextGenerator::get_default_font_path() {
    auto fonts = get_available_fonts();
    if (!fonts.empty()) return fonts.front().second;
    return "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf";
}

std::string TextGenerator::get_arabic_font_path() {
    static const std::vector<std::string> candidates = {
        "/usr/share/fonts/truetype/noto/NotoNaskhArabic-Regular.ttf",
        "/usr/share/fonts/truetype/noto/NotoNaskhArabic-Bold.ttf",
        "/usr/share/fonts/truetype/noto/NotoNaskhArabicUI-Regular.ttf",
    };
    for (const auto& p : candidates) {
        if (fs::exists(p)) return p;
    }
    return get_default_font_path();
}

static void blend_pixel(uint8_t* dst, uint8_t src_r, uint8_t src_g, uint8_t src_b, uint8_t src_a) {
    if (src_a == 0) return;
    float sa = src_a / 255.0f;
    float da = dst[3] / 255.0f;
    float out_a = sa + da * (1.0f - sa);
    if (out_a > 0.001f) {
        float out_r = (src_r * sa + dst[0] * da * (1.0f - sa)) / out_a;
        float out_g = (src_g * sa + dst[1] * da * (1.0f - sa)) / out_a;
        float out_b = (src_b * sa + dst[2] * da * (1.0f - sa)) / out_a;
        dst[0] = static_cast<uint8_t>(std::clamp(out_r, 0.0f, 255.0f));
        dst[1] = static_cast<uint8_t>(std::clamp(out_g, 0.0f, 255.0f));
        dst[2] = static_cast<uint8_t>(std::clamp(out_b, 0.0f, 255.0f));
        dst[3] = static_cast<uint8_t>(std::clamp(out_a * 255.0f, 0.0f, 255.0f));
    }
}

bool TextGenerator::render_to_rgba(const TextConfig& config,
                                   int& out_w, int& out_h,
                                   std::vector<uint8_t>& out_rgba) {
    FT_Library ft;
    if (FT_Init_FreeType(&ft)) {
        return false;
    }

    std::string font_path = config.font_path;
    if (font_path.empty() || !fs::exists(font_path)) {
        font_path = get_default_font_path();
    }

    FT_Face face;
    if (FT_New_Face(ft, font_path.c_str(), 0, &face)) {
        FT_Done_FreeType(ft);
        return false;
    }

    // Secondary Arabic font face for fallback
    FT_Face arabic_face = nullptr;
    std::string ar_font = get_arabic_font_path();
    if (fs::exists(ar_font) && ar_font != font_path) {
        FT_New_Face(ft, ar_font.c_str(), 0, &arabic_face);
    }

    int pixel_size = static_cast<int>(std::max(10.0f, config.font_size));
    FT_Set_Pixel_Sizes(face, 0, pixel_size);
    if (arabic_face) {
        FT_Set_Pixel_Sizes(arabic_face, 0, pixel_size);
    }

    // Split text into lines
    std::vector<std::string> raw_lines;
    {
        std::stringstream ss(config.text.empty() ? " " : config.text);
        std::string line;
        while (std::getline(ss, line)) {
            raw_lines.push_back(line);
        }
    }
    if (raw_lines.empty()) raw_lines.push_back(" ");

    // Process lines for Arabic / RTL shaping
    std::vector<std::vector<char32_t>> processed_lines;
    for (const auto& raw : raw_lines) {
        std::string shaped_line = ArabicShaper::process(raw);
        processed_lines.push_back(ArabicShaper::utf8_to_utf32(shaped_line));
    }

    struct GlyphInfo {
        char32_t cp;
        FT_Face target_face;
        int x, y;
        int width, height;
        int bearing_x, bearing_y;
        int advance_x;
    };

    struct LineInfo {
        int width = 0;
        int max_ascent = 0;
        int max_descent = 0;
        std::vector<GlyphInfo> glyphs;
    };

    std::vector<LineInfo> lines_info;
    int max_line_width = 0;
    int line_height = static_cast<int>(pixel_size * std::max(0.8f, config.line_spacing));

    for (const auto& line_cps : processed_lines) {
        LineInfo li;
        int cur_x = 0;
        for (char32_t cp : line_cps) {
            FT_Face target = face;
            FT_UInt g_idx = FT_Get_Char_Index(face, cp);
            if (g_idx == 0 && arabic_face) {
                FT_UInt ar_idx = FT_Get_Char_Index(arabic_face, cp);
                if (ar_idx != 0) {
                    target = arabic_face;
                    g_idx = ar_idx;
                }
            }

            if (FT_Load_Glyph(target, g_idx, FT_LOAD_DEFAULT)) {
                cur_x += pixel_size / 2;
                continue;
            }

            GlyphInfo gi;
            gi.cp = cp;
            gi.target_face = target;
            gi.width = target->glyph->metrics.width >> 6;
            gi.height = target->glyph->metrics.height >> 6;
            gi.bearing_x = target->glyph->metrics.horiBearingX >> 6;
            gi.bearing_y = target->glyph->metrics.horiBearingY >> 6;
            gi.advance_x = (target->glyph->metrics.horiAdvance >> 6) + static_cast<int>(config.letter_spacing);
            gi.x = cur_x;
            gi.y = 0;

            if (gi.bearing_y > li.max_ascent) li.max_ascent = gi.bearing_y;
            int descent = gi.height - gi.bearing_y;
            if (descent > li.max_descent) li.max_descent = descent;

            cur_x += gi.advance_x;
            li.glyphs.push_back(gi);
        }
        li.width = cur_x;
        if (li.width > max_line_width) max_line_width = li.width;
        lines_info.push_back(li);
    }

    int outline_px = config.use_outline ? static_cast<int>(std::ceil(config.outline_thickness)) : 0;
    int pad_x = config.padding_x + outline_px;
    int pad_y = config.padding_y + outline_px;

    out_w = max_line_width + pad_x * 2;
    if (out_w < 64) out_w = 64;

    int total_text_h = static_cast<int>(lines_info.size()) * line_height;
    out_h = total_text_h + pad_y * 2;
    if (out_h < 32) out_h = 32;

    out_rgba.assign(out_w * out_h * 4, 0);

    // Fill background if requested
    if (config.use_bg) {
        uint8_t br = static_cast<uint8_t>(config.bg_color[0] * 255.0f);
        uint8_t bg = static_cast<uint8_t>(config.bg_color[1] * 255.0f);
        uint8_t bb = static_cast<uint8_t>(config.bg_color[2] * 255.0f);
        uint8_t ba = static_cast<uint8_t>(config.bg_color[3] * 255.0f);
        for (int i = 0; i < out_w * out_h; ++i) {
            out_rgba[i * 4 + 0] = br;
            out_rgba[i * 4 + 1] = bg;
            out_rgba[i * 4 + 2] = bb;
            out_rgba[i * 4 + 3] = ba;
        }
    }

    FT_Stroker stroker = nullptr;
    if (config.use_outline && config.outline_thickness > 0.0f) {
        FT_Stroker_New(ft, &stroker);
        FT_Stroker_Set(stroker,
                       static_cast<FT_Fixed>(config.outline_thickness * 64.0f),
                       FT_STROKER_LINECAP_ROUND,
                       FT_STROKER_LINEJOIN_ROUND,
                       0);
    }

    uint8_t fr = static_cast<uint8_t>(std::clamp(config.font_color[0] * 255.0f, 0.0f, 255.0f));
    uint8_t fg = static_cast<uint8_t>(std::clamp(config.font_color[1] * 255.0f, 0.0f, 255.0f));
    uint8_t fb = static_cast<uint8_t>(std::clamp(config.font_color[2] * 255.0f, 0.0f, 255.0f));
    uint8_t fa = static_cast<uint8_t>(std::clamp(config.font_color[3] * 255.0f, 0.0f, 255.0f));

    uint8_t or_c = static_cast<uint8_t>(std::clamp(config.outline_color[0] * 255.0f, 0.0f, 255.0f));
    uint8_t og_c = static_cast<uint8_t>(std::clamp(config.outline_color[1] * 255.0f, 0.0f, 255.0f));
    uint8_t ob_c = static_cast<uint8_t>(std::clamp(config.outline_color[2] * 255.0f, 0.0f, 255.0f));
    uint8_t oa_c = static_cast<uint8_t>(std::clamp(config.outline_color[3] * 255.0f, 0.0f, 255.0f));

    int base_y = pad_y + (line_height * 3 / 4);

    // Pass 1: Outline (if enabled)
    if (stroker) {
        for (size_t l_idx = 0; l_idx < lines_info.size(); ++l_idx) {
            const auto& li = lines_info[l_idx];
            int line_start_x = pad_x;
            if (config.justification == 1) { // Center
                line_start_x += (max_line_width - li.width) / 2;
            } else if (config.justification == 2) { // Right
                line_start_x += (max_line_width - li.width);
            }

            int baseline = base_y + static_cast<int>(l_idx * line_height);

            for (const auto& gi : li.glyphs) {
                FT_UInt g_idx = FT_Get_Char_Index(gi.target_face, gi.cp);
                if (FT_Load_Glyph(gi.target_face, g_idx, FT_LOAD_DEFAULT)) continue;

                FT_Glyph glyph;
                if (FT_Get_Glyph(gi.target_face->glyph, &glyph)) continue;

                FT_Glyph_StrokeBorder(&glyph, stroker, 0, 1);
                FT_Glyph_To_Bitmap(&glyph, FT_RENDER_MODE_NORMAL, nullptr, 1);
                FT_BitmapGlyph bglyph = reinterpret_cast<FT_BitmapGlyph>(glyph);

                int gx = line_start_x + gi.x + bglyph->left;
                int gy = baseline - bglyph->top;

                for (unsigned int r = 0; r < bglyph->bitmap.rows; ++r) {
                    int py = gy + r;
                    if (py < 0 || py >= out_h) continue;
                    for (unsigned int c = 0; c < bglyph->bitmap.width; ++c) {
                        int px = gx + c;
                        if (px < 0 || px >= out_w) continue;
                        uint8_t alpha = bglyph->bitmap.buffer[r * bglyph->bitmap.pitch + c];
                        if (alpha > 0) {
                            uint8_t final_a = static_cast<uint8_t>((alpha * oa_c) / 255);
                            blend_pixel(&out_rgba[(py * out_w + px) * 4], or_c, og_c, ob_c, final_a);
                        }
                    }
                }
                FT_Done_Glyph(glyph);
            }
        }
        FT_Stroker_Done(stroker);
    }

    // Pass 2: Main Text Glyphs
    for (size_t l_idx = 0; l_idx < lines_info.size(); ++l_idx) {
        const auto& li = lines_info[l_idx];
        int line_start_x = pad_x;
        if (config.justification == 1) { // Center
            line_start_x += (max_line_width - li.width) / 2;
        } else if (config.justification == 2) { // Right
            line_start_x += (max_line_width - li.width);
        }

        int baseline = base_y + static_cast<int>(l_idx * line_height);

        for (const auto& gi : li.glyphs) {
            FT_UInt g_idx = FT_Get_Char_Index(gi.target_face, gi.cp);
            if (FT_Load_Glyph(gi.target_face, g_idx, FT_LOAD_RENDER)) continue;

            FT_Bitmap& bitmap = gi.target_face->glyph->bitmap;
            int gx = line_start_x + gi.x + gi.target_face->glyph->bitmap_left;
            int gy = baseline - gi.target_face->glyph->bitmap_top;

            for (unsigned int r = 0; r < bitmap.rows; ++r) {
                int py = gy + r;
                if (py < 0 || py >= out_h) continue;
                for (unsigned int c = 0; c < bitmap.width; ++c) {
                    int px = gx + c;
                    if (px < 0 || px >= out_w) continue;
                    uint8_t alpha = bitmap.buffer[r * bitmap.pitch + c];
                    if (alpha > 0) {
                        uint8_t final_a = static_cast<uint8_t>((alpha * fa) / 255);
                        blend_pixel(&out_rgba[(py * out_w + px) * 4], fr, fg, fb, final_a);
                    }
                }
            }
        }
    }

    FT_Done_Face(face);
    if (arabic_face) FT_Done_Face(arabic_face);
    FT_Done_FreeType(ft);

    return true;
}

bool TextGenerator::save_to_png(const TextConfig& config,
                                const std::string& output_path,
                                int& out_w, int& out_h) {
    std::vector<uint8_t> rgba;
    if (!render_to_rgba(config, out_w, out_h, rgba)) {
        return false;
    }

    fs::path p(output_path);
    if (p.has_parent_path()) {
        fs::create_directories(p.parent_path());
    }

    FILE* fp = fopen(output_path.c_str(), "wb");
    if (!fp) return false;

    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png) { fclose(fp); return false; }

    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_write_struct(&png, nullptr);
        fclose(fp);
        return false;
    }

    if (setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &info);
        fclose(fp);
        return false;
    }

    png_init_io(png, fp);
    png_set_IHDR(png, info, out_w, out_h, 8, PNG_COLOR_TYPE_RGBA,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);

    std::vector<const uint8_t*> rows(out_h);
    for (int y = 0; y < out_h; ++y) {
        rows[y] = rgba.data() + y * out_w * 4;
    }

    png_write_image(png, const_cast<png_bytepp>(rows.data()));
    png_write_end(png, nullptr);
    png_destroy_write_struct(&png, &info);
    fclose(fp);
    return true;
}

std::string TextGenerator::create_text_asset(Project& project,
                                             const TextConfig& config,
                                             const std::string& asset_name) {
    const char* home = std::getenv("HOME");
    std::string temp_dir = home ? (std::string(home) + "/.config/minicut/temp") : "/tmp";
    fs::create_directories(temp_dir);

    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    std::string filename = "text_" + std::to_string(now_ms) + ".png";
    std::string full_path = (fs::path(temp_dir) / filename).string();

    int w = 0, h = 0;
    if (!save_to_png(config, full_path, w, h)) {
        return "";
    }

    std::string asset_id = "va_text_" + std::to_string(now_ms % 1000000);
    VideoAsset va;
    va.id = asset_id;
    va.path = full_path;
    va.width = w;
    va.height = h;
    va.duration = 5.0; // Default 5.0s for text graphic
    va.is_image = true;
    va.has_audio = false;

    // Generate thumbnail OpenGL texture from rendered file
    std::vector<uint8_t> rgba;
    int tw = 0, th = 0;
    if (render_to_rgba(config, tw, th, rgba) && tw > 0 && th > 0) {
        GLuint tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tw, th, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        va.thumbnail_texture = tex;
    }

    project.video_assets.push_back(va);
    return asset_id;
}
