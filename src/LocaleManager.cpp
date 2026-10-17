#include "LocaleManager.h"
#include "ArabicShaper.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <cstdlib>

namespace fs = std::filesystem;
using json = nlohmann::json;

LocaleManager& LocaleManager::instance() {
    static LocaleManager s_instance;
    return s_instance;
}

LocaleManager::LocaleManager() {
    populate_embedded_fallbacks();
    init();
}

std::vector<LocaleManager::LanguageInfo> LocaleManager::get_available_languages() const {
    return {
        { "en", "English" },
        { "ja", "日本語 (Japanese)" },
        { "ko", "한국어 (Korean)" },
        { "uk", "Українська (Ukrainian)" },
        { "ar", "العربية (Arabic)" }
    };
}

std::string LocaleManager::current_locale_name() const {
    for (const auto& lang : get_available_languages()) {
        if (lang.code == m_current_locale) {
            return lang.name;
        }
    }
    return m_current_locale;
}

void LocaleManager::init(const std::string& locales_dir) {
    if (!locales_dir.empty()) {
        m_locales_dir = locales_dir;
    } else {
        // Detect candidates: user configuration directory takes highest priority!
        std::vector<std::string> candidates;
        const char* home = std::getenv("HOME");
        if (home) {
            candidates.push_back(std::string(home) + "/.config/minicut/locales");
            candidates.push_back(std::string(home) + "/.local/share/minicut/locales");
        }
        candidates.push_back("locales");
        candidates.push_back("../locales");
        candidates.push_back("/home/kaiju/.local/bin/locales");
        candidates.push_back("/usr/local/share/minicut/locales");
        candidates.push_back("/usr/share/minicut/locales");

        for (const auto& c : candidates) {
            std::error_code ec;
            if (fs::exists(c, ec) && fs::is_directory(c, ec)) {
                m_locales_dir = c;
                break;
            }
        }
    }

    if (m_current_locale.empty()) {
        m_current_locale = "en";
    }
    set_locale(m_current_locale);
}

void LocaleManager::reload_locales() {
    init(m_locales_dir);
}

bool LocaleManager::set_locale(const std::string& lang_code) {
    std::string code = lang_code.empty() ? "en" : lang_code;
    if (code == "jp") code = "ja"; // Alias support

    m_current_locale = code;
    m_translations.clear();

    // 1. Populate with embedded fallback for this language
    auto emb_it = m_embedded.find(code);
    if (emb_it != m_embedded.end()) {
        m_translations = emb_it->second;
    }

    // 2. Try loading JSON file to override or supplement
    load_locale_file(code);

    // 3. Apply any custom plugin translations registered for this language
    auto cust_it = m_custom_translations.find(code);
    if (cust_it != m_custom_translations.end()) {
        for (const auto& [k, v] : cust_it->second) {
            m_translations[k] = v;
        }
    }

    // 4. If Arabic (RTL), shape all translations for right-to-left cursive presentation
    if (m_current_locale == "ar") {
        for (auto& [k, v] : m_translations) {
            v = ArabicShaper::process(v, true);
        }
    }
    return true;
}

void LocaleManager::register_string(const std::string& lang_code, const std::string& key, const std::string& value) {
    if (key.empty()) return;
    std::string code = lang_code.empty() ? m_current_locale : lang_code;
    if (code == "jp") code = "ja";
    m_custom_translations[code][key] = value;
    if (code == "ja") {
        m_custom_translations["jp"][key] = value;
    }
    if (code == m_current_locale) {
        m_translations[key] = (code == "ar") ? ArabicShaper::process(value, true) : value;
    }
}

void LocaleManager::register_dictionary(const std::string& lang_code, const std::unordered_map<std::string, std::string>& dict) {
    for (const auto& [k, v] : dict) {
        register_string(lang_code, k, v);
    }
}

void LocaleManager::load_locale_file(const std::string& code) {
    std::vector<fs::path> paths_to_try;
    const char* home = std::getenv("HOME");
    if (home) {
        paths_to_try.push_back(fs::path(home) / ".config" / "minicut" / "locales" / ("locale_" + code + ".json"));
        if (code == "ja") paths_to_try.push_back(fs::path(home) / ".config" / "minicut" / "locales" / "locale_jp.json");
        paths_to_try.push_back(fs::path(home) / ".local" / "share" / "minicut" / "locales" / ("locale_" + code + ".json"));
        if (code == "ja") paths_to_try.push_back(fs::path(home) / ".local" / "share" / "minicut" / "locales" / "locale_jp.json");
    }
    if (!m_locales_dir.empty()) {
        paths_to_try.push_back(fs::path(m_locales_dir) / ("locale_" + code + ".json"));
        if (code == "ja") paths_to_try.push_back(fs::path(m_locales_dir) / "locale_jp.json");
    }
    paths_to_try.push_back(fs::path("locales") / ("locale_" + code + ".json"));
    paths_to_try.push_back(fs::path("/home/kaiju/.local/bin/locales") / ("locale_" + code + ".json"));
    paths_to_try.push_back(fs::path("/usr/local/share/minicut/locales") / ("locale_" + code + ".json"));
    paths_to_try.push_back(fs::path("/usr/share/minicut/locales") / ("locale_" + code + ".json"));

    for (const auto& path : paths_to_try) {
        std::error_code ec;
        if (fs::exists(path, ec)) {
            try {
                std::ifstream ifs(path);
                if (ifs.is_open()) {
                    json j;
                    ifs >> j;
                    if (j.contains("translations") && j["translations"].is_object()) {
                        for (auto& [k, v] : j["translations"].items()) {
                            if (v.is_string()) {
                                m_translations[k] = v.get<std::string>();
                            }
                        }
                        return;
                    }
                }
            } catch (const std::exception& e) {
                std::cerr << "LocaleManager: Failed to parse " << path << ": " << e.what() << std::endl;
            }
        }
    }
}

std::string LocaleManager::get(const std::string& key, const std::string& fallback) const {
    auto it = m_translations.find(key);
    if (it != m_translations.end() && !it->second.empty()) {
        return it->second;
    }

    // Check custom English fallback
    auto cust_en = m_custom_translations.find("en");
    if (cust_en != m_custom_translations.end()) {
        auto ck = cust_en->second.find(key);
        if (ck != cust_en->second.end() && !ck->second.empty()) {
            return ck->second;
        }
    }

    // Check English embedded fallback
    auto en_it = m_embedded.find("en");
    if (en_it != m_embedded.end()) {
        auto key_it = en_it->second.find(key);
        if (key_it != en_it->second.end()) {
            return key_it->second;
        }
    }

    return fallback.empty() ? key : fallback;
}

const char* LocaleManager::tr(const char* key, const char* fallback) const {
    if (!key) return "";
    auto it = m_translations.find(key);
    if (it != m_translations.end() && !it->second.empty()) {
        return it->second.c_str();
    }

    auto cust_en = m_custom_translations.find("en");
    if (cust_en != m_custom_translations.end()) {
        auto ck = cust_en->second.find(key);
        if (ck != cust_en->second.end() && !ck->second.empty()) {
            return ck->second.c_str();
        }
    }

    auto en_it = m_embedded.find("en");
    if (en_it != m_embedded.end()) {
        auto key_it = en_it->second.find(key);
        if (key_it != en_it->second.end()) {
            return key_it->second.c_str();
        }
    }

    return fallback ? fallback : key;
}

void LocaleManager::populate_embedded_fallbacks() {
    // English
    m_embedded["en"] = {
        {"menu_file", "File"}, {"menu_edit", "Edit"}, {"menu_assets", "Assets"}, {"menu_project", "Project"}, {"menu_modifiers", "Modifiers"},
        {"menu_settings", "Settings"}, {"menu_help", "Help"},
        {"file_new", "New Project"}, {"file_open", "Open Project..."},
        {"file_save", "Save Project"}, {"file_save_as", "Save Project As..."},
        {"file_export", "Export Video..."}, {"file_exit", "Exit"},
        {"edit_undo", "Undo"}, {"edit_redo", "Redo"}, {"edit_cut", "Cut"},
        {"edit_copy", "Copy"}, {"edit_paste", "Paste"},
        {"edit_split", "Split Selected Clip"}, {"edit_delete", "Delete Selected Clip"},
        {"project_import_video", "Import Video..."}, {"project_import_audio", "Import Audio..."},
        {"project_import_image", "Import Image..."}, {"project_import_prefab", "Import Project as Prefab..."},
        {"project_generate_demo", "Generate Demo Media"}, {"project_cleanup_temp", "Cleanup Temporary Files"},
        {"settings_system_dialog", "Use System File Dialog"}, {"settings_language", "Language"},
        {"help_about", "About minicut..."},
        {"panel_clips", "Asset Browser"}, {"panel_preview", "Preview & Program"},
        {"panel_timeline", "Timeline"}, {"panel_context", "Context Inspector & Tools"},
        {"timeline_play", "Play"}, {"timeline_pause", "Pause"}, {"timeline_stop", "Stop"},
        {"timeline_add_video", "+ Video Track"}, {"timeline_add_audio", "+ Audio Track"}, {"timeline_add_subtitle", "+ Subtitle"},
        {"timeline_separate_audio", "Separate Audio Track"}, {"timeline_split_playhead", "Split at Playhead"},
        {"timeline_paste_here", "Paste Here"}, {"timeline_depth", "Render Depth"},
        {"timeline_move_front", "Move to Front (Depth 100)"}, {"timeline_move_back", "Move to Back (Depth 0)"},
        {"timeline_reset_transforms", "Reset Transforms / Volume"},
        {"timeline_delete_prefab", "Delete Prefab Block"}, {"timeline_dissolve_prefab", "Dissolve Prefab (Keep Clips)"},
        {"inspector_position_x", "Position X"}, {"inspector_position_y", "Position Y"},
        {"inspector_rotation", "Rotation"}, {"inspector_scale", "Scale"},
        {"inspector_scale_x", "Scale X"}, {"inspector_scale_y", "Scale Y"},
        {"inspector_opacity", "Opacity"}, {"inspector_volume", "Audio Gain / Volume"},
        {"inspector_track_depth", "Track Render Depth (0-100)"},
        {"inspector_fade_in", "Fade In (1s)"}, {"inspector_fade_out", "Fade Out (1s)"},
        {"inspector_ducking", "Audio Ducking"},
        {"status_temp_cleaned", "Temporary files cleaned"}, {"status_no_temp", "No temporary files to clean"},
        {"track_video", "Video"}, {"track_audio", "Audio"},
        {"timeline_move_up", "Move Track Up"}, {"timeline_move_down", "Move Track Down"},
        {"timeline_rename_track", "Rename Track"}, {"timeline_track_name", "Track Name"},
        {"edit_select_all", "Select All"},
        {"dialog_ok", "OK"}, {"dialog_cancel", "Cancel"},
        {"text_add_asset", "Add Text"}, {"text_dialog_title", "Create Text Asset"},
        {"text_font", "Font"}, {"text_font_size", "Font Size"}, {"text_font_color", "Font Color"},
        {"text_use_background", "Background"}, {"text_bg_color", "Background Color"},
        {"text_use_outline", "Outline"}, {"text_outline_color", "Outline Color"},
        {"text_outline_width", "Outline Width"}, {"text_letter_spacing", "Letter Spacing"},
        {"text_line_spacing", "Line Spacing"}, {"text_alignment", "Justification"},
        {"text_align_left", "Left"}, {"text_align_center", "Center"}, {"text_align_right", "Right"},
        {"text_entry_label", "Text Entry"}, {"text_preview", "Preview"},
        {"text_add_to_assets", "Add to Assets"}, {"text_add_to_timeline", "Add to Timeline"},
        {"menu_scripts", "Scripts"}, {"scripts_no_scripts", "No scripts found"},
        {"scripts_autorun_menu", "Autorun Scripts (Dock)"}, {"scripts_reload", "Reload All Scripts"},
        {"scripts_directories", "Directories"},
        {"menu_help_lua_api", "Lua API Reference (PDF)..."},
        {"menu_help_js_api", "JavaScript API Reference (PDF)..."},
        {"dock_title", "DOCK"}
    };

    // Japanese
    m_embedded["ja"] = {
        {"menu_file", "ファイル"}, {"menu_edit", "編集"}, {"menu_assets", "素材"}, {"menu_project", "プロジェクト"}, {"menu_modifiers", "修飾子"},
        {"menu_settings", "設定"}, {"menu_help", "ヘルプ"},
        {"file_new", "新規プロジェクト"}, {"file_open", "プロジェクトを開く..."},
        {"file_save", "プロジェクトを保存"}, {"file_save_as", "名前を付けて保存..."},
        {"file_export", "動画を書き出し..."}, {"file_exit", "終了"},
        {"edit_undo", "元に戻す"}, {"edit_redo", "やり直し"}, {"edit_cut", "切り取り"},
        {"edit_copy", "コピー"}, {"edit_paste", "貼り付け"},
        {"edit_split", "選択クリップを分割"}, {"edit_delete", "選択クリップを削除"},
        {"project_import_video", "動画を読み込み..."}, {"project_import_audio", "音声を読み込み..."},
        {"project_import_image", "画像を読み込み..."}, {"project_import_prefab", "プレハブとしてプロジェクトを読み込み..."},
        {"project_generate_demo", "デモメディアを生成"}, {"project_cleanup_temp", "一時ファイルをクリーンアップ"},
        {"settings_system_dialog", "システムファイルダイアログを使用"}, {"settings_language", "言語 (Language)"},
        {"help_about", "minicut について..."},
        {"panel_clips", "アセットブラウザ"}, {"panel_preview", "プレビューとプログラム"},
        {"panel_timeline", "タイムライン"}, {"panel_context", "コンテキストインスペクターとツール"},
        {"timeline_play", "再生"}, {"timeline_pause", "一時停止"}, {"timeline_stop", "停止"},
        {"timeline_add_video", "+ 動画トラック"}, {"timeline_add_audio", "+ 音声トラック"}, {"timeline_add_subtitle", "+ 字幕"},
        {"timeline_separate_audio", "音声トラックを分離"}, {"timeline_split_playhead", "再生ヘッド位置で分割"},
        {"timeline_paste_here", "ここに貼り付け"}, {"timeline_depth", "描画深度 (Depth)"},
        {"timeline_move_front", "最前面に移動 (深度 100)"}, {"timeline_move_back", "最背面に移動 (深度 0)"},
        {"timeline_reset_transforms", "変形と音量をリセット"},
        {"timeline_delete_prefab", "プレハブブロックを削除"}, {"timeline_dissolve_prefab", "プレハブを分解 (クリップを保持)"},
        {"inspector_position_x", "位置 X"}, {"inspector_position_y", "位置 Y"},
        {"inspector_rotation", "回転"}, {"inspector_scale", "拡大縮小"},
        {"inspector_scale_x", "拡大縮小 X"}, {"inspector_scale_y", "拡大縮小 Y"},
        {"inspector_opacity", "不透明度"}, {"inspector_volume", "音声ゲイン / 音量"},
        {"inspector_track_depth", "トラック描画深度 (0-100)"},
        {"inspector_fade_in", "フェードイン (1秒)"}, {"inspector_fade_out", "フェードアウト (1秒)"},
        {"inspector_ducking", "オーディオダッキング"},
        {"status_temp_cleaned", "一時ファイルを削除しました"}, {"status_no_temp", "削除する一時ファイルはありません"},
        {"track_video", "ビデオ"}, {"track_audio", "オーディオ"},
        {"timeline_move_up", "トラックを上に移動"}, {"timeline_move_down", "トラックを下に移動"},
        {"timeline_rename_track", "トラック名の変更"}, {"timeline_track_name", "トラック名"},
        {"edit_select_all", "すべて選択"},
        {"dialog_ok", "OK"}, {"dialog_cancel", "キャンセル"},
        {"text_add_asset", "テキストを追加"}, {"text_dialog_title", "テキストアセットの作成"},
        {"text_font", "フォント"}, {"text_font_size", "フォントサイズ"}, {"text_font_color", "文字色"},
        {"text_use_background", "背景"}, {"text_bg_color", "背景色"},
        {"text_use_outline", "アウトライン"}, {"text_outline_color", "アウトライン色"},
        {"text_outline_width", "アウトライン幅"}, {"text_letter_spacing", "文字間隔"},
        {"text_line_spacing", "行間隔"}, {"text_alignment", "配置"},
        {"text_align_left", "左揃え"}, {"text_align_center", "中央揃え"}, {"text_align_right", "右揃え"},
        {"text_entry_label", "テキスト入力"}, {"text_preview", "プレビュー"},
        {"text_add_to_assets", "アセットに追加"}, {"text_add_to_timeline", "タイムラインに追加"},
        {"menu_scripts", "スクリプト"}, {"scripts_no_scripts", "スクリプトが見つかりません"},
        {"scripts_autorun_menu", "自動実行スクリプト (ドック)"}, {"scripts_reload", "すべてのスクリプトを再読込"},
        {"scripts_directories", "ディレクトリ"},
        {"menu_help_lua_api", "Lua APIリファレンスマニュアル (PDF)..."},
        {"menu_help_js_api", "JavaScript APIリファレンスマニュアル (PDF)..."},
        {"dock_title", "ドック"}
    };
    m_embedded["jp"] = m_embedded["ja"];

    // Korean
    m_embedded["ko"] = {
        {"menu_file", "파일"}, {"menu_edit", "편집"}, {"menu_assets", "에셋"}, {"menu_project", "프로젝트"}, {"menu_modifiers", "수정자"},
        {"menu_settings", "설정"}, {"menu_help", "도움말"},
        {"file_new", "새 프로젝트"}, {"file_open", "프로젝트 열기..."},
        {"file_save", "프로젝트 저장"}, {"file_save_as", "다른 이름으로 저장..."},
        {"file_export", "동영상 내보내기..."}, {"file_exit", "종료"},
        {"edit_undo", "실행 취소"}, {"edit_redo", "다시 실행"}, {"edit_cut", "잘라내기"},
        {"edit_copy", "복사"}, {"edit_paste", "붙여넣기"},
        {"edit_split", "선택한 클립 분할"}, {"edit_delete", "선택한 클립 삭제"},
        {"project_import_video", "비디오 가져오기..."}, {"project_import_audio", "오디오 가져오기..."},
        {"project_import_image", "이미지 가져오기..."}, {"project_import_prefab", "프리팹으로 프로젝트 가져오기..."},
        {"project_generate_demo", "데모 미디어 생성"}, {"project_cleanup_temp", "임시 파일 정리"},
        {"settings_system_dialog", "시스템 파일 대화상자 사용"}, {"settings_language", "언어 (Language)"},
        {"help_about", "minicut 정보..."},
        {"panel_clips", "에셋 브라우저"}, {"panel_preview", "미리보기 및 프로그램"},
        {"panel_timeline", "타임라인"}, {"panel_context", "컨텍스트 인스펙터 및 도구"},
        {"timeline_play", "재생"}, {"timeline_pause", "일시 정지"}, {"timeline_stop", "정지"},
        {"timeline_add_video", "+ 비디오 트랙"}, {"timeline_add_audio", "+ 오디오 트랙"}, {"timeline_add_subtitle", "+ 자막"},
        {"timeline_separate_audio", "오디오 트랙 분리"}, {"timeline_split_playhead", "재생헤드 위치에서 분할"},
        {"timeline_paste_here", "여기에 붙여넣기"}, {"timeline_depth", "렌더링 깊이 (Depth)"},
        {"timeline_move_front", "맨 앞으로 이동 (깊이 100)"}, {"timeline_move_back", "맨 뒤로 이동 (깊이 0)"},
        {"timeline_reset_transforms", "변형 및 볼륨 초기화"},
        {"timeline_delete_prefab", "프리팹 블록 삭제"}, {"timeline_dissolve_prefab", "프리팹 분해 (클립 유지)"},
        {"inspector_position_x", "위치 X"}, {"inspector_position_y", "위치 Y"},
        {"inspector_rotation", "회전"}, {"inspector_scale", "크기 조절"},
        {"inspector_scale_x", "크기 조절 X"}, {"inspector_scale_y", "크기 조절 Y"},
        {"inspector_opacity", "불투명도"}, {"inspector_volume", "오디오 게인 / 볼륨"},
        {"inspector_track_depth", "트랙 렌더링 깊이 (0-100)"},
        {"inspector_fade_in", "페이드 인 (1초)"}, {"inspector_fade_out", "페이드 아웃 (1초)"},
        {"inspector_ducking", "오디오 더킹"},
        {"status_temp_cleaned", "임시 파일을 정리했습니다"}, {"status_no_temp", "정리할 임시 파일이 없습니다"},
        {"track_video", "비디오"}, {"track_audio", "오디오"},
        {"timeline_move_up", "트랙 위로 이동"}, {"timeline_move_down", "트랙 아래로 이동"},
        {"timeline_rename_track", "트랙 이름 바꾸기"}, {"timeline_track_name", "트랙 이름"},
        {"edit_select_all", "모두 선택"},
        {"dialog_ok", "확인"}, {"dialog_cancel", "취소"},
        {"text_add_asset", "텍스트 추가"}, {"text_dialog_title", "텍스트 에셋 생성"},
        {"text_font", "글꼴"}, {"text_font_size", "글꼴 크기"}, {"text_font_color", "글자 색상"},
        {"text_use_background", "배경"}, {"text_bg_color", "배경 색상"},
        {"text_use_outline", "외곽선"}, {"text_outline_color", "외곽선 색상"},
        {"text_outline_width", "외곽선 두께"}, {"text_letter_spacing", "자간"},
        {"text_line_spacing", "줄 간격"}, {"text_alignment", "정렬"},
        {"text_align_left", "왼쪽"}, {"text_align_center", "가운데"}, {"text_align_right", "오른쪽"},
        {"text_entry_label", "텍스트 입력"}, {"text_preview", "미리보기"},
        {"text_add_to_assets", "에셋에 추가"}, {"text_add_to_timeline", "타임라인에 추가"},
        {"menu_scripts", "스크립트"}, {"scripts_no_scripts", "스크립트가 없습니다"},
        {"scripts_autorun_menu", "자동 실행 스크립트 (도크)"}, {"scripts_reload", "모든 스크립트 다시 로드"},
        {"scripts_directories", "디렉터리"},
        {"menu_help_lua_api", "Lua API 참조 매뉴얼 (PDF)..."},
        {"menu_help_js_api", "JavaScript API 참조 매뉴얼 (PDF)..."},
        {"dock_title", "도크"}
    };

    // Ukrainian
    m_embedded["uk"] = {
        {"menu_file", "Файл"}, {"menu_edit", "Правка"}, {"menu_assets", "Ресурси"}, {"menu_project", "Проєкт"}, {"menu_modifiers", "Модифікатори"},
        {"menu_settings", "Налаштування"}, {"menu_help", "Довідка"},
        {"file_new", "Новий проєкт"}, {"file_open", "Відкрити проєкт..."},
        {"file_save", "Зберегти проєкт"}, {"file_save_as", "Зберегти проєкт як..."},
        {"file_export", "Експортувати відео..."}, {"file_exit", "Вихід"},
        {"edit_undo", "Скасувати"}, {"edit_redo", "Повторити"}, {"edit_cut", "Вирізати"},
        {"edit_copy", "Копіювати"}, {"edit_paste", "Вставити"},
        {"edit_split", "Розрізати вибраний кліп"}, {"edit_delete", "Видалити вибраний кліп"},
        {"project_import_video", "Імпортувати відео..."}, {"project_import_audio", "Імпортувати аудіо..."},
        {"project_import_image", "Імпортувати зображення..."}, {"project_import_prefab", "Імпортувати проєкт як префаб..."},
        {"project_generate_demo", "Створити демо-медіа"}, {"project_cleanup_temp", "Очистити тимчасові файли"},
        {"settings_system_dialog", "Використовувати системні діалоги"}, {"settings_language", "Мова (Language)"},
        {"help_about", "Про minicut..."},
        {"panel_clips", "Браузер ресурсів"}, {"panel_preview", "Попередній перегляд"},
        {"panel_timeline", "Часова шкала"}, {"panel_context", "Контекстний інспектор та інструменти"},
        {"timeline_play", "Відтворити"}, {"timeline_pause", "Пауза"}, {"timeline_stop", "Зупинити"},
        {"timeline_add_video", "+ Відеодоріжка"}, {"timeline_add_audio", "+ Аудіодоріжка"}, {"timeline_add_subtitle", "+ Субтитри"},
        {"timeline_separate_audio", "Відокремити аудіодоріжку"}, {"timeline_split_playhead", "Розрізати за курсором"},
        {"timeline_paste_here", "Вставити сюди"}, {"timeline_depth", "Глибина рендерингу"},
        {"timeline_move_front", "На передній план (глибина 100)"}, {"timeline_move_back", "На задній план (глибина 0)"},
        {"timeline_reset_transforms", "Скинути трансформації та гучність"},
        {"timeline_delete_prefab", "Видалити блок префабу"}, {"timeline_dissolve_prefab", "Розформувати префаб (залишити кліпи)"},
        {"inspector_position_x", "Позиція X"}, {"inspector_position_y", "Позиція Y"},
        {"inspector_rotation", "Поворот"}, {"inspector_scale", "Масштаб"},
        {"inspector_scale_x", "Масштаб X"}, {"inspector_scale_y", "Масштаб Y"},
        {"inspector_opacity", "Непрозорість"}, {"inspector_volume", "Підсилення / Гучність"},
        {"inspector_track_depth", "Глибина рендерингу доріжки (0-100)"},
        {"inspector_fade_in", "Плавне наростання (1с)"}, {"inspector_fade_out", "Плавне затухання (1с)"},
        {"inspector_ducking", "Аудіо-дакінг"},
        {"status_temp_cleaned", "Тимчасові файли успішно очищено"}, {"status_no_temp", "Немає тимчасових файлів для очищення"},
        {"track_video", "Відео"}, {"track_audio", "Аудіо"},
        {"timeline_move_up", "Перемістити доріжку вгору"}, {"timeline_move_down", "Перемістити доріжку вниз"},
        {"timeline_rename_track", "Перейменувати доріжку"}, {"timeline_track_name", "Назва доріжки"},
        {"edit_select_all", "Виділити все"},
        {"dialog_ok", "Гаразд"}, {"dialog_cancel", "Скасувати"},
        {"text_add_asset", "Додати текст"}, {"text_dialog_title", "Створення текстового ресурсу"},
        {"text_font", "Шрифт"}, {"text_font_size", "Розмір шрифту"}, {"text_font_color", "Колір шрифту"},
        {"text_use_background", "Тло"}, {"text_bg_color", "Колір тла"},
        {"text_use_outline", "Контур"}, {"text_outline_color", "Колір контуру"},
        {"text_outline_width", "Товщина контуру"}, {"text_letter_spacing", "Міжсимвольний інтервал"},
        {"text_line_spacing", "Міжрядковий інтервал"}, {"text_alignment", "Вирівнювання"},
        {"text_align_left", "Ліворуч"}, {"text_align_center", "По центру"}, {"text_align_right", "Праворуч"},
        {"text_entry_label", "Введення тексту"}, {"text_preview", "Попередній перегляд"},
        {"text_add_to_assets", "Додати до ресурсів"}, {"text_add_to_timeline", "Додати на таймлайн"},
        {"menu_scripts", "Скрипти"}, {"scripts_no_scripts", "Скрипти відсутні"},
        {"scripts_autorun_menu", "Скрипти автозапуску (Док)"}, {"scripts_reload", "Перезавантажити всі скрипти"},
        {"scripts_directories", "Каталоги"},
        {"menu_help_lua_api", "Довідник Lua API (PDF)..."},
        {"menu_help_js_api", "Довідник JavaScript API (PDF)..."},
        {"dock_title", "ДОК"}
    };

    // Arabic
    m_embedded["ar"] = {
        {"menu_file", "ملف"}, {"menu_edit", "تعديل"}, {"menu_assets", "الوسائط"}, {"menu_project", "مشروع"}, {"menu_modifiers", "المعدلات"},
        {"menu_settings", "إعدادات"}, {"menu_help", "مساعدة"},
        {"menu_settings", "إعدادات"}, {"menu_help", "مساعدة"},
        {"file_new", "مشروع جديد"}, {"file_open", "فتح مشروع..."},
        {"file_save", "حفظ المشروع"}, {"file_save_as", "حفظ المشروع كـ..."},
        {"file_export", "تصدير الفيديو..."}, {"file_exit", "خروج"},
        {"edit_undo", "تراجع"}, {"edit_redo", "إعادة"}, {"edit_cut", "قص"},
        {"edit_copy", "نسخ"}, {"edit_paste", "لصق"},
        {"edit_split", "تقسيم المقطع المحدد"}, {"edit_delete", "حذف المقطع المحدد"},
        {"project_import_video", "استيراد فيديو..."}, {"project_import_audio", "استيراد صوت..."},
        {"project_import_image", "استيراد صورة..."}, {"project_import_prefab", "استيراد مشروع كقالب..."},
        {"project_generate_demo", "إنشاء وسائط تجريبية"}, {"project_cleanup_temp", "تنظيف الملفات المؤقتة"},
        {"settings_system_dialog", "استخدام مربعات حوار النظام"}, {"settings_language", "اللغة (Language)"},
        {"help_about", "حول minicut..."},
        {"panel_clips", "متصفح الوسائط"}, {"panel_preview", "المعاينة والشاشة"},
        {"panel_timeline", "المخطط الزمني"}, {"panel_context", "لوحة الخصائص والأدوات"},
        {"timeline_play", "تشغيل"}, {"timeline_pause", "إيقاف مؤقت"}, {"timeline_stop", "إيقاف"},
        {"timeline_add_video", "+ مسار فيديو"}, {"timeline_add_audio", "+ مسار صوت"}, {"timeline_add_subtitle", "+ مسار ترجمة"},
        {"timeline_separate_audio", "فصل مسار الصوت"}, {"timeline_split_playhead", "تقسيم عند المؤشر"},
        {"timeline_paste_here", "لصق هنا"}, {"timeline_depth", "عمق العرض"},
        {"timeline_move_front", "إحضار إلى المقدمة (عمق 100)"}, {"timeline_move_back", "إرسال إلى الخلف (عمق 0)"},
        {"timeline_reset_transforms", "إعادة تعيين التحويلات ومستوى الصوت"},
        {"timeline_delete_prefab", "حذف كتلة القالب"}, {"timeline_dissolve_prefab", "فك القالب (الاحتفاظ بالمقاطع)"},
        {"inspector_position_x", "الموضع X"}, {"inspector_position_y", "الموضع Y"},
        {"inspector_rotation", "التدوير"}, {"inspector_scale", "المقياس"},
        {"inspector_scale_x", "المقياس X"}, {"inspector_scale_y", "المقياس Y"},
        {"inspector_opacity", "الشفافية"}, {"inspector_volume", "مستوى الصوت"},
        {"inspector_track_depth", "عمق عرض المسار (0-100)"},
        {"inspector_fade_in", "تلاشي للداخل (1 ثانية)"}, {"inspector_fade_out", "تلاشي للخارج (1 ثانية)"},
        {"inspector_ducking", "تخفيض الصوت التلقائي"},
        {"status_temp_cleaned", "تم تنظيف الملفات المؤقتة بنجاح"}, {"status_no_temp", "لا توجد ملفات مؤقتة للتنظيف"},
        {"track_video", "فيديو"}, {"track_audio", "صوت"},
        {"timeline_move_up", "تحريك المسار لأعلى"}, {"timeline_move_down", "تحريك المسار لأسفل"},
        {"timeline_rename_track", "إعادة تسمية المسار"}, {"timeline_track_name", "اسم المسار"},
        {"edit_select_all", "تحديد الكل"},
        {"dialog_ok", "موافق"}, {"dialog_cancel", "إلغاء"},
        {"text_add_asset", "إضافة نص"}, {"text_dialog_title", "إنشاء عنصر نصي"},
        {"text_font", "الخط"}, {"text_font_size", "حجم الخط"}, {"text_font_color", "لون الخط"},
        {"text_use_background", "خلفية"}, {"text_bg_color", "لون الخلفية"},
        {"text_use_outline", "تحديد الحواف"}, {"text_outline_color", "لون التحديد"},
        {"text_outline_width", "سمك التحديد"}, {"text_letter_spacing", "تباعد الحروف"},
        {"text_line_spacing", "تباعد الأسطر"}, {"text_alignment", "المحاذاة"},
        {"text_align_left", "يسار"}, {"text_align_center", "وسط"}, {"text_align_right", "يمين"},
        {"text_entry_label", "إدخال النص"}, {"text_preview", "معاينة"},
        {"text_add_to_assets", "إضافة إلى الوسائط"}, {"text_add_to_timeline", "إضافة إلى المخطط الزمني"},
        {"menu_scripts", "البرمجيات (Scripts)"}, {"scripts_no_scripts", "لا توجد سكربتات في المجلد"},
        {"scripts_autorun_menu", "سكربتات التشغيل التلقائي (منصة)"}, {"scripts_reload", "إعادة تحميل كافة السكربتات"},
        {"scripts_directories", "المسارات والمجلدات"},
        {"menu_help_lua_api", "دليل واجهة برمجة Lua (PDF)..."},
        {"menu_help_js_api", "دليل واجهة برمجة JavaScript (PDF)..."},
        {"dock_title", "منصة"}
    };
}
