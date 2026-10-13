#include "LocaleManager.h"
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

    set_locale(m_current_locale);
}

bool LocaleManager::set_locale(const std::string& lang_code) {
    std::string code = lang_code;
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
    return true;
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
        {"menu_file", "File"}, {"menu_edit", "Edit"}, {"menu_project", "Project"},
        {"menu_settings", "Settings"}, {"menu_help", "Help"},
        {"file_new", "New Project"}, {"file_open", "Open Project..."},
        {"file_save", "Save Project"}, {"file_save_as", "Save Project As..."},
        {"file_export", "Export Video (.mp4)..."}, {"file_exit", "Exit"},
        {"edit_undo", "Undo"}, {"edit_redo", "Redo"}, {"edit_cut", "Cut"},
        {"edit_copy", "Copy"}, {"edit_paste", "Paste"},
        {"edit_split", "Split Selected Clip"}, {"edit_delete", "Delete Selected Clip"},
        {"project_import_video", "Import Video..."}, {"project_import_audio", "Import Audio..."},
        {"project_import_image", "Import Image..."}, {"project_import_prefab", "Import Project as Prefab..."},
        {"project_generate_demo", "Generate Demo Media"}, {"project_cleanup_temp", "Cleanup Temporary Files"},
        {"settings_system_dialog", "Use System File Dialog"}, {"settings_language", "Language"},
        {"help_about", "About minicut..."},
        {"panel_clips", "Clips & Assets"}, {"panel_preview", "Preview & Program"},
        {"panel_timeline", "Timeline"}, {"panel_context", "Context Inspector & Tools"},
        {"timeline_play", "Play"}, {"timeline_pause", "Pause"}, {"timeline_stop", "Stop"},
        {"timeline_add_video", "+ Video Track"}, {"timeline_add_audio", "+ Audio Track"},
        {"timeline_separate_audio", "Separate Audio Track"}, {"timeline_split_playhead", "Split at Playhead"},
        {"timeline_paste_here", "Paste Here"}, {"timeline_depth", "Render Depth"},
        {"timeline_move_front", "Move to Front (Depth 100)"}, {"timeline_move_back", "Move to Back (Depth 0)"},
        {"timeline_reset_transforms", "Reset Transforms / Volume"},
        {"timeline_delete_prefab", "Delete Prefab Block"}, {"timeline_dissolve_prefab", "Dissolve Prefab (Keep Clips)"},
        {"inspector_position_x", "Position X"}, {"inspector_position_y", "Position Y"},
        {"inspector_rotation", "Rotation"}, {"inspector_scale", "Scale"},
        {"inspector_opacity", "Opacity"}, {"inspector_volume", "Audio Gain / Volume"},
        {"inspector_track_depth", "Track Render Depth (0-100)"},
        {"inspector_fade_in", "Fade In (1s)"}, {"inspector_fade_out", "Fade Out (1s)"},
        {"inspector_ducking", "Audio Ducking"},
        {"status_temp_cleaned", "Temporary files cleaned"}, {"status_no_temp", "No temporary files to clean"}
    };

    // Japanese
    m_embedded["ja"] = {
        {"menu_file", "ファイル"}, {"menu_edit", "編集"}, {"menu_project", "プロジェクト"},
        {"menu_settings", "設定"}, {"menu_help", "ヘルプ"},
        {"file_new", "新規プロジェクト"}, {"file_open", "プロジェクトを開く..."},
        {"file_save", "プロジェクトを保存"}, {"file_save_as", "名前を付けて保存..."},
        {"file_export", "動画を書き出し (.mp4)..."}, {"file_exit", "終了"},
        {"edit_undo", "元に戻す"}, {"edit_redo", "やり直し"}, {"edit_cut", "切り取り"},
        {"edit_copy", "コピー"}, {"edit_paste", "貼り付け"},
        {"edit_split", "選択クリップを分割"}, {"edit_delete", "選択クリップを削除"},
        {"project_import_video", "動画を読み込み..."}, {"project_import_audio", "音声を読み込み..."},
        {"project_import_image", "画像を読み込み..."}, {"project_import_prefab", "プレハブとしてプロジェクトを読み込み..."},
        {"project_generate_demo", "デモメディアを生成"}, {"project_cleanup_temp", "一時ファイルをクリーンアップ"},
        {"settings_system_dialog", "システムファイルダイアログを使用"}, {"settings_language", "言語 (Language)"},
        {"help_about", "minicut について..."},
        {"panel_clips", "クリップと素材"}, {"panel_preview", "プレビューとプログラム"},
        {"panel_timeline", "タイムライン"}, {"panel_context", "コンテキストインスペクターとツール"},
        {"timeline_play", "再生"}, {"timeline_pause", "一時停止"}, {"timeline_stop", "停止"},
        {"timeline_add_video", "+ 動画トラック"}, {"timeline_add_audio", "+ 音声トラック"},
        {"timeline_separate_audio", "音声トラックを分離"}, {"timeline_split_playhead", "再生ヘッド位置で分割"},
        {"timeline_paste_here", "ここに貼り付け"}, {"timeline_depth", "描画深度 (Depth)"},
        {"timeline_move_front", "最前面に移動 (深度 100)"}, {"timeline_move_back", "最背面に移動 (深度 0)"},
        {"timeline_reset_transforms", "変形と音量をリセット"},
        {"timeline_delete_prefab", "プレハブブロックを削除"}, {"timeline_dissolve_prefab", "プレハブを分解 (クリップを保持)"},
        {"inspector_position_x", "位置 X"}, {"inspector_position_y", "位置 Y"},
        {"inspector_rotation", "回転"}, {"inspector_scale", "拡大縮小"},
        {"inspector_opacity", "不透明度"}, {"inspector_volume", "音声ゲイン / 音量"},
        {"inspector_track_depth", "トラック描画深度 (0-100)"},
        {"inspector_fade_in", "フェードイン (1秒)"}, {"inspector_fade_out", "フェードアウト (1秒)"},
        {"inspector_ducking", "オーディオダッキング"},
        {"status_temp_cleaned", "一時ファイルを削除しました"}, {"status_no_temp", "削除する一時ファイルはありません"}
    };
    m_embedded["jp"] = m_embedded["ja"];

    // Korean
    m_embedded["ko"] = {
        {"menu_file", "파일"}, {"menu_edit", "편집"}, {"menu_project", "프로젝트"},
        {"menu_settings", "설정"}, {"menu_help", "도움말"},
        {"file_new", "새 프로젝트"}, {"file_open", "프로젝트 열기..."},
        {"file_save", "프로젝트 저장"}, {"file_save_as", "다른 이름으로 저장..."},
        {"file_export", "동영상 내보내기 (.mp4)..."}, {"file_exit", "종료"},
        {"edit_undo", "실행 취소"}, {"edit_redo", "다시 실행"}, {"edit_cut", "잘라내기"},
        {"edit_copy", "복사"}, {"edit_paste", "붙여넣기"},
        {"edit_split", "선택한 클립 분할"}, {"edit_delete", "선택한 클립 삭제"},
        {"project_import_video", "비디오 가져오기..."}, {"project_import_audio", "오디오 가져오기..."},
        {"project_import_image", "이미지 가져오기..."}, {"project_import_prefab", "프리팹으로 프로젝트 가져오기..."},
        {"project_generate_demo", "데모 미디어 생성"}, {"project_cleanup_temp", "임시 파일 정리"},
        {"settings_system_dialog", "시스템 파일 대화상자 사용"}, {"settings_language", "언어 (Language)"},
        {"help_about", "minicut 정보..."},
        {"panel_clips", "클립 및 에셋"}, {"panel_preview", "미리보기 및 프로그램"},
        {"panel_timeline", "타임라인"}, {"panel_context", "컨텍스트 인스펙터 및 도구"},
        {"timeline_play", "재생"}, {"timeline_pause", "일시 정지"}, {"timeline_stop", "정지"},
        {"timeline_add_video", "+ 비디오 트랙"}, {"timeline_add_audio", "+ 오디오 트랙"},
        {"timeline_separate_audio", "오디오 트랙 분리"}, {"timeline_split_playhead", "재생헤드 위치에서 분할"},
        {"timeline_paste_here", "여기에 붙여넣기"}, {"timeline_depth", "렌더링 깊이 (Depth)"},
        {"timeline_move_front", "맨 앞으로 이동 (깊이 100)"}, {"timeline_move_back", "맨 뒤로 이동 (깊이 0)"},
        {"timeline_reset_transforms", "변형 및 볼륨 초기화"},
        {"timeline_delete_prefab", "프리팹 블록 삭제"}, {"timeline_dissolve_prefab", "프리팹 분해 (클립 유지)"},
        {"inspector_position_x", "위치 X"}, {"inspector_position_y", "위치 Y"},
        {"inspector_rotation", "회전"}, {"inspector_scale", "크기 조절"},
        {"inspector_opacity", "불투명도"}, {"inspector_volume", "오디오 게인 / 볼륨"},
        {"inspector_track_depth", "트랙 렌더링 깊이 (0-100)"},
        {"inspector_fade_in", "페이드 인 (1초)"}, {"inspector_fade_out", "페이드 아웃 (1초)"},
        {"inspector_ducking", "오디오 더킹"},
        {"status_temp_cleaned", "임시 파일을 정리했습니다"}, {"status_no_temp", "정리할 임시 파일이 없습니다"}
    };

    // Ukrainian
    m_embedded["uk"] = {
        {"menu_file", "Файл"}, {"menu_edit", "Правка"}, {"menu_project", "Проєкт"},
        {"menu_settings", "Налаштування"}, {"menu_help", "Довідка"},
        {"file_new", "Новий проєкт"}, {"file_open", "Відкрити проєкт..."},
        {"file_save", "Зберегти проєкт"}, {"file_save_as", "Зберегти проєкт як..."},
        {"file_export", "Експортувати відео (.mp4)..."}, {"file_exit", "Вихід"},
        {"edit_undo", "Скасувати"}, {"edit_redo", "Повторити"}, {"edit_cut", "Вирізати"},
        {"edit_copy", "Копіювати"}, {"edit_paste", "Вставити"},
        {"edit_split", "Розрізати вибраний кліп"}, {"edit_delete", "Видалити вибраний кліп"},
        {"project_import_video", "Імпортувати відео..."}, {"project_import_audio", "Імпортувати аудіо..."},
        {"project_import_image", "Імпортувати зображення..."}, {"project_import_prefab", "Імпортувати проєкт як префаб..."},
        {"project_generate_demo", "Створити демо-медіа"}, {"project_cleanup_temp", "Очистити тимчасові файли"},
        {"settings_system_dialog", "Використовувати системні діалоги"}, {"settings_language", "Мова (Language)"},
        {"help_about", "Про minicut..."},
        {"panel_clips", "Кліпи та ресурси"}, {"panel_preview", "Попередній перегляд"},
        {"panel_timeline", "Часова шкала"}, {"panel_context", "Контекстний інспектор та інструменти"},
        {"timeline_play", "Відтворити"}, {"timeline_pause", "Пауза"}, {"timeline_stop", "Зупинити"},
        {"timeline_add_video", "+ Відеодоріжка"}, {"timeline_add_audio", "+ Аудіодоріжка"},
        {"timeline_separate_audio", "Відокремити аудіодоріжку"}, {"timeline_split_playhead", "Розрізати за курсором"},
        {"timeline_paste_here", "Вставити сюди"}, {"timeline_depth", "Глибина рендерингу"},
        {"timeline_move_front", "На передній план (глибина 100)"}, {"timeline_move_back", "На задній план (глибина 0)"},
        {"timeline_reset_transforms", "Скинути трансформації та гучність"},
        {"timeline_delete_prefab", "Видалити блок префабу"}, {"timeline_dissolve_prefab", "Розформувати префаб (залишити кліпи)"},
        {"inspector_position_x", "Позиція X"}, {"inspector_position_y", "Позиція Y"},
        {"inspector_rotation", "Поворот"}, {"inspector_scale", "Масштаб"},
        {"inspector_opacity", "Непрозорість"}, {"inspector_volume", "Підсилення / Гучність"},
        {"inspector_track_depth", "Глибина рендерингу доріжки (0-100)"},
        {"inspector_fade_in", "Плавне наростання (1с)"}, {"inspector_fade_out", "Плавне затухання (1с)"},
        {"inspector_ducking", "Аудіо-дакінг"},
        {"status_temp_cleaned", "Тимчасові файли успішно очищено"}, {"status_no_temp", "Немає тимчасових файлів для очищення"}
    };

    // Arabic
    m_embedded["ar"] = {
        {"menu_file", "ملف"}, {"menu_edit", "تعديل"}, {"menu_project", "مشروع"},
        {"menu_settings", "إعدادات"}, {"menu_help", "مساعدة"},
        {"file_new", "مشروع جديد"}, {"file_open", "فتح مشروع..."},
        {"file_save", "حفظ المشروع"}, {"file_save_as", "حفظ المشروع كـ..."},
        {"file_export", "تصدير الفيديو (.mp4)..."}, {"file_exit", "خروج"},
        {"edit_undo", "تراجع"}, {"edit_redo", "إعادة"}, {"edit_cut", "قص"},
        {"edit_copy", "نسخ"}, {"edit_paste", "لصق"},
        {"edit_split", "تقسيم المقطع المحدد"}, {"edit_delete", "حذف المقطع المحدد"},
        {"project_import_video", "استيراد فيديو..."}, {"project_import_audio", "استيراد صوت..."},
        {"project_import_image", "استيراد صورة..."}, {"project_import_prefab", "استيراد مشروع كقالب..."},
        {"project_generate_demo", "إنشاء وسائط تجريبية"}, {"project_cleanup_temp", "تنظيف الملفات المؤقتة"},
        {"settings_system_dialog", "استخدام مربعات حوار النظام"}, {"settings_language", "اللغة (Language)"},
        {"help_about", "حول minicut..."},
        {"panel_clips", "المقاطع والوسائط"}, {"panel_preview", "المعاينة والشاشة"},
        {"panel_timeline", "المخطط الزمني"}, {"panel_context", "لوحة الخصائص والأدوات"},
        {"timeline_play", "تشغيل"}, {"timeline_pause", "إيقاف مؤقت"}, {"timeline_stop", "إيقاف"},
        {"timeline_add_video", "+ مسار فيديو"}, {"timeline_add_audio", "+ مسار صوت"},
        {"timeline_separate_audio", "فصل مسار الصوت"}, {"timeline_split_playhead", "تقسيم عند المؤشر"},
        {"timeline_paste_here", "لصق هنا"}, {"timeline_depth", "عمق العرض"},
        {"timeline_move_front", "إحضار إلى المقدمة (عمق 100)"}, {"timeline_move_back", "إرسال إلى الخلف (عمق 0)"},
        {"timeline_reset_transforms", "إعادة تعيين التحويلات ومستوى الصوت"},
        {"timeline_delete_prefab", "حذف كتلة القالب"}, {"timeline_dissolve_prefab", "فك القالب (الاحتفاظ بالمقاطع)"},
        {"inspector_position_x", "الموضع X"}, {"inspector_position_y", "الموضع Y"},
        {"inspector_rotation", "التدوير"}, {"inspector_scale", "المقياس"},
        {"inspector_opacity", "الشفافية"}, {"inspector_volume", "مستوى الصوت"},
        {"inspector_track_depth", "عمق عرض المسار (0-100)"},
        {"inspector_fade_in", "تلاشي للداخل (1 ثانية)"}, {"inspector_fade_out", "تلاشي للخارج (1 ثانية)"},
        {"inspector_ducking", "تخفيض الصوت التلقائي"},
        {"status_temp_cleaned", "تم تنظيف الملفات المؤقتة بنجاح"}, {"status_no_temp", "لا توجد ملفات مؤقتة للتنظيف"}
    };
}
