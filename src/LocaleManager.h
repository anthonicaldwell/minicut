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
    std::string current_locale() const { return m_current_locale; }
    std::string current_locale_name() const;
    bool is_rtl() const { return m_current_locale == "ar"; }

    // Translation lookup: returns translated string or fallback
    std::string get(const std::string& key, const std::string& fallback = "") const;
    const char* tr(const char* key, const char* fallback = nullptr) const;

    std::vector<LanguageInfo> get_available_languages() const;

private:
    LocaleManager();
    void load_locale_file(const std::string& code);
    void populate_embedded_fallbacks();

    std::string m_current_locale = "en";
    std::string m_locales_dir;
    std::unordered_map<std::string, std::string> m_translations;
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> m_embedded;
};

#define _TR(key) LocaleManager::instance().tr(key, key)
