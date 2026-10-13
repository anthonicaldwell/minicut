#include "ArabicShaper.h"
#include <unordered_map>
#include <algorithm>

bool ArabicShaper::is_arabic_codepoint(char32_t cp) {
    return (cp >= 0x0600 && cp <= 0x06FF) ||
           (cp >= 0x0750 && cp <= 0x077F) ||
           (cp >= 0x08A0 && cp <= 0x08FF) ||
           (cp >= 0xFB50 && cp <= 0xFDFF) ||
           (cp >= 0xFE70 && cp <= 0xFEFF);
}

bool ArabicShaper::has_arabic(const std::string& utf8_text) {
    auto cps = utf8_to_utf32(utf8_text);
    for (char32_t cp : cps) {
        if (is_arabic_codepoint(cp)) return true;
    }
    return false;
}

bool ArabicShaper::is_tashkeel(char32_t cp) {
    return (cp >= 0x064B && cp <= 0x065F) || (cp == 0x0670);
}

const ArabicShaper::GlyphForms* ArabicShaper::get_forms(char32_t cp) {
    static const std::unordered_map<char32_t, GlyphForms> table = {
        // cp,      { isolated, final,   initial, medial,  joins_right, joins_left }
        { 0x0621,   { 0xFE80,   0xFE80,  0xFE80,  0xFE80,  false,       false } }, // Hamza
        { 0x0622,   { 0xFE81,   0xFE82,  0xFE81,  0xFE82,  true,        false } }, // Alef Madda
        { 0x0623,   { 0xFE83,   0xFE84,  0xFE83,  0xFE84,  true,        false } }, // Alef Hamza Above
        { 0x0624,   { 0xFE85,   0xFE86,  0xFE85,  0xFE86,  true,        false } }, // Waw Hamza
        { 0x0625,   { 0xFE87,   0xFE88,  0xFE87,  0xFE88,  true,        false } }, // Alef Hamza Below
        { 0x0626,   { 0xFE89,   0xFE8A,  0xFE8B,  0xFE8C,  true,        true  } }, // Yeh Hamza
        { 0x0627,   { 0xFE8D,   0xFE8E,  0xFE8D,  0xFE8E,  true,        false } }, // Alef
        { 0x0628,   { 0xFE8F,   0xFE90,  0xFE91,  0xFE92,  true,        true  } }, // Beh
        { 0x0629,   { 0xFE93,   0xFE94,  0xFE93,  0xFE94,  true,        false } }, // Teh Marbuta
        { 0x062A,   { 0xFE95,   0xFE96,  0xFE97,  0xFE98,  true,        true  } }, // Teh
        { 0x062B,   { 0xFE99,   0xFE9A,  0xFE9B,  0xFE9C,  true,        true  } }, // Theh
        { 0x062C,   { 0xFE9D,   0xFE9E,  0xFE9F,  0xFEA0,  true,        true  } }, // Jeem
        { 0x062D,   { 0xFEA1,   0xFEA2,  0xFEA3,  0xFEA4,  true,        true  } }, // Hah
        { 0x062E,   { 0xFEA5,   0xFEA6,  0xFEA7,  0xFEA8,  true,        true  } }, // Khah
        { 0x062F,   { 0xFEA9,   0xFEAA,  0xFEA9,  0xFEAA,  true,        false } }, // Dal
        { 0x0630,   { 0xFEAB,   0xFEAC,  0xFEAB,  0xFEAC,  true,        false } }, // Thal
        { 0x0631,   { 0xFEAD,   0xFEAE,  0xFEAD,  0xFEAE,  true,        false } }, // Reh
        { 0x0632,   { 0xFEAF,   0xFEB0,  0xFEAF,  0xFEB0,  true,        false } }, // Zain
        { 0x0633,   { 0xFEB1,   0xFEB2,  0xFEB3,  0xFEB4,  true,        true  } }, // Seen
        { 0x0634,   { 0xFEB5,   0xFEB6,  0xFEB7,  0xFEB8,  true,        true  } }, // Sheen
        { 0x0635,   { 0xFEB9,   0xFEBA,  0xFEBB,  0xFEBC,  true,        true  } }, // Sad
        { 0x0636,   { 0xFEBD,   0xFEBE,  0xFEBF,  0xFEC0,  true,        true  } }, // Dad
        { 0x0637,   { 0xFEC1,   0xFEC2,  0xFEC3,  0xFEC4,  true,        true  } }, // Tah
        { 0x0638,   { 0xFEC5,   0xFEC6,  0xFEC7,  0xFEC8,  true,        true  } }, // Zah
        { 0x0639,   { 0xFEC9,   0xFECA,  0xFECB,  0xFECC,  true,        true  } }, // Ain
        { 0x063A,   { 0xFECD,   0xFECE,  0xFECF,  0xFED0,  true,        true  } }, // Ghain
        { 0x0640,   { 0x0640,   0x0640,  0x0640,  0x0640,  true,        true  } }, // Tatweel
        { 0x0641,   { 0xFED1,   0xFED2,  0xFED3,  0xFED4,  true,        true  } }, // Feh
        { 0x0642,   { 0xFED5,   0xFED6,  0xFED7,  0xFED8,  true,        true  } }, // Qaf
        { 0x0643,   { 0xFED9,   0xFEDA,  0xFEDB,  0xFEDC,  true,        true  } }, // Kaf
        { 0x0644,   { 0xFEDD,   0xFEDE,  0xFEDF,  0xFEE0,  true,        true  } }, // Lam
        { 0x0645,   { 0xFEE1,   0xFEE2,  0xFEE3,  0xFEE4,  true,        true  } }, // Meem
        { 0x0646,   { 0xFEE5,   0xFEE6,  0xFEE7,  0xFEE8,  true,        true  } }, // Noon
        { 0x0647,   { 0xFEE9,   0xFEEA,  0xFEEB,  0xFEEC,  true,        true  } }, // Heh
        { 0x0648,   { 0xFEED,   0xFEEE,  0xFEED,  0xFEEE,  true,        false } }, // Waw
        { 0x0649,   { 0xFEEF,   0xFEF0,  0xFEEF,  0xFEF0,  true,        false } }, // Alef Maksura
        { 0x064A,   { 0xFEF1,   0xFEF2,  0xFEF3,  0xFEF4,  true,        true  } }, // Yeh
    };

    auto it = table.find(cp);
    if (it != table.end()) return &it->second;
    return nullptr;
}

std::vector<char32_t> ArabicShaper::utf8_to_utf32(const std::string& str) {
    std::vector<char32_t> out;
    size_t i = 0;
    while (i < str.size()) {
        uint8_t c = static_cast<uint8_t>(str[i]);
        char32_t cp = 0;
        if (c < 0x80) {
            cp = c;
            i += 1;
        } else if ((c & 0xE0) == 0xC0 && i + 1 < str.size()) {
            cp = ((c & 0x1F) << 6) | (static_cast<uint8_t>(str[i + 1]) & 0x3F);
            i += 2;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < str.size()) {
            cp = ((c & 0x0F) << 12) | ((static_cast<uint8_t>(str[i + 1]) & 0x3F) << 6) | (static_cast<uint8_t>(str[i + 2]) & 0x3F);
            i += 3;
        } else if ((c & 0xF8) == 0xF0 && i + 3 < str.size()) {
            cp = ((c & 0x07) << 18) | ((static_cast<uint8_t>(str[i + 1]) & 0x3F) << 12) | ((static_cast<uint8_t>(str[i + 2]) & 0x3F) << 6) | (static_cast<uint8_t>(str[i + 3]) & 0x3F);
            i += 4;
        } else {
            i += 1;
        }
        out.push_back(cp);
    }
    return out;
}

std::string ArabicShaper::utf32_to_utf8(const std::vector<char32_t>& cps) {
    std::string out;
    for (char32_t cp : cps) {
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

std::string ArabicShaper::shape_and_bidi(const std::string& utf8_text) {
    std::vector<char32_t> input = utf8_to_utf32(utf8_text);
    if (input.empty()) return "";

    // 1. Process Lam-Alef Ligatures
    std::vector<char32_t> text;
    for (size_t i = 0; i < input.size(); ++i) {
        if (input[i] == 0x0644 && (i + 1 < input.size())) { // Lam
            char32_t next = input[i + 1];
            char32_t lig = 0;
            if (next == 0x0622) lig = 0xFEF5; // Lam + Alef Madda
            else if (next == 0x0623) lig = 0xFEF7; // Lam + Alef Hamza Above
            else if (next == 0x0625) lig = 0xFEF9; // Lam + Alef Hamza Below
            else if (next == 0x0627) lig = 0xFEFB; // Lam + Alef
            if (lig != 0) {
                text.push_back(lig);
                i++; // Skip the combined alef
                continue;
            }
        }
        text.push_back(input[i]);
    }

    // 2. Shape Arabic Glyphs
    std::vector<char32_t> shaped(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        char32_t cp = text[i];
        const GlyphForms* cur = get_forms(cp);
        if (!cur) {
            // Check if it's already a Lam-Alef ligature (0xFEF5, 0xFEF7, 0xFEF9, 0xFEFB)
            if (cp >= 0xFEF5 && cp <= 0xFEFB && (cp % 2 != 0)) {
                // Determine if preceded by a letter that joins left
                bool prev_joins = false;
                if (i > 0) {
                    const GlyphForms* prev_forms = get_forms(text[i - 1]);
                    if (prev_forms && prev_forms->joins_left) prev_joins = true;
                }
                shaped[i] = prev_joins ? (cp + 1) : cp; // Final form is odd + 1
            } else {
                shaped[i] = cp;
            }
            continue;
        }

        // Determine if previous connects to cur
        bool connect_prev = false;
        if (i > 0 && cur->joins_right) {
            size_t p = i - 1;
            while (p > 0 && is_tashkeel(text[p])) p--;
            const GlyphForms* prev_forms = get_forms(text[p]);
            if (prev_forms && prev_forms->joins_left) {
                connect_prev = true;
            }
        }

        // Determine if next connects to cur
        bool connect_next = false;
        if (i + 1 < text.size() && cur->joins_left) {
            size_t n = i + 1;
            while (n + 1 < text.size() && is_tashkeel(text[n])) n++;
            const GlyphForms* next_forms = get_forms(text[n]);
            if (next_forms && next_forms->joins_right) {
                connect_next = true;
            }
        }

        if (connect_prev && connect_next) {
            shaped[i] = cur->medial_form;
        } else if (connect_prev) {
            shaped[i] = cur->final_form;
        } else if (connect_next) {
            shaped[i] = cur->initial_form;
        } else {
            shaped[i] = cur->isolated;
        }
    }

    // 3. BiDi Reversal: Segment into runs
    // Arabic runs (letters + tashkeel + presentation forms) are reversed.
    // Numbers, Latin, and spaces outside Arabic words maintain direction.
    std::vector<char32_t> result;
    size_t idx = 0;
    while (idx < shaped.size()) {
        if (is_arabic_codepoint(shaped[idx])) {
            // Find extent of Arabic run (including spaces between Arabic words)
            size_t run_end = idx;
            while (run_end < shaped.size()) {
                if (is_arabic_codepoint(shaped[run_end])) {
                    run_end++;
                } else if (shaped[run_end] == ' ' && run_end + 1 < shaped.size() && is_arabic_codepoint(shaped[run_end + 1])) {
                    run_end++; // Include space if followed by more Arabic
                } else {
                    break;
                }
            }

            // Reverse the Arabic run for visual LTR renderers
            std::vector<char32_t> arabic_run(shaped.begin() + idx, shaped.begin() + run_end);
            std::reverse(arabic_run.begin(), arabic_run.end());
            result.insert(result.end(), arabic_run.begin(), arabic_run.end());
            idx = run_end;
        } else {
            // Non-Arabic run (Latin, punctuation, numbers)
            result.push_back(shaped[idx]);
            idx++;
        }
    }

    return utf32_to_utf8(result);
}

std::string ArabicShaper::process(const std::string& utf8_text, bool force_rtl) {
    if (utf8_text.empty()) return "";
    if (force_rtl || has_arabic(utf8_text)) {
        return shape_and_bidi(utf8_text);
    }
    return utf8_text;
}
