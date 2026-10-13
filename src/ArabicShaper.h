#pragma once

#include <string>
#include <vector>
#include <cstdint>

class ArabicShaper {
public:
    // Check if codepoint is an Arabic character
    static bool is_arabic_codepoint(char32_t cp);

    // Check if text contains any Arabic characters
    static bool has_arabic(const std::string& utf8_text);

    // Shapes Arabic characters to presentation forms and reverses Arabic runs for RTL rendering
    static std::string shape_and_bidi(const std::string& utf8_text);

    // Shapes text for RTL display if Arabic is present or if force_rtl is true
    static std::string process(const std::string& utf8_text, bool force_rtl = false);

    // Convert UTF-8 to UTF-32
    static std::vector<char32_t> utf8_to_utf32(const std::string& str);

    // Convert UTF-32 to UTF-8
    static std::string utf32_to_utf8(const std::vector<char32_t>& cps);

private:
    struct GlyphForms {
        char32_t isolated;
        char32_t final_form;
        char32_t initial_form;
        char32_t medial_form;
        bool joins_right; // Can connect with preceding char
        bool joins_left;  // Can connect with succeeding char
    };

    static const GlyphForms* get_forms(char32_t cp);
    static bool is_tashkeel(char32_t cp);
};
