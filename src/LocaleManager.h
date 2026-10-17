#pragma once

#include <string>
#include <unordered_map>
#include <vector>

class LocaleManager {
public:
    struct LanguageInfo {
        std::string code;
        std::string name;
    };

    static LocaleManager& instance();

    void init(const std::string& locales_dir = "");
    void reload_locales();
    bool set_locale(const std::string& lang_code);
    bool set_language(const std::string& lang_code) { return set_locale(lang_code); }
    std::string current_locale() const { return m_current_locale; }
    std::string get_current_language() const { return m_current_locale; }
    std::string current_locale_name() const;
    bool is_rtl(const std::string& lang_code = "") const {
        std::string code = lang_code.empty() ? m_current_locale : lang_code;
        return code == "ar" || code == "he" || code == "fa" || code == "ur";
    }

    // Translation lookup: returns translated string or fallback
    std::string get(const std::string& key, const std::string& fallback = "") const;
    std::string get_text(const std::string& key, const std::string& fallback = "") const { return get(key, fallback); }
    std::string get_string(const std::string& key, const std::string& fallback = "") const { return get(key, fallback); }
    const char* tr(const char* key, const char* fallback = nullptr) const;

    // Plugin / Script localization registration
    void register_string(const std::string& lang_code, const std::string& key, const std::string& value);
    void register_dictionary(const std::string& lang_code, const std::unordered_map<std::string, std::string>& dict);

    std::vector<LanguageInfo> get_available_languages() const;

private:
    LocaleManager();
    void load_locale_file(const std::string& code);
    void populate_embedded_fallbacks();

    std::string m_current_locale = "en";
    std::string m_locales_dir;
    std::unordered_map<std::string, std::string> m_translations;
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> m_embedded;
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> m_custom_translations;
};

#define _TR(key) LocaleManager::instance().tr(key, key)
