#pragma once

#include <string>

namespace JapaneseTransliteration {
    // Converts Kanji in Japanese text into Hiragana so that speech synthesizers
    // like eSpeak-ng can synthesize speech with accurate Japanese pronunciation.
    std::string kanji_to_hiragana(const std::string& input);

    // Returns true if text contains any Kanji characters (CJK ideographs)
    bool contains_kanji(const std::string& input);
}
