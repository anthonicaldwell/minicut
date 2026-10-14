#pragma once

#include <string>
#include <functional>

class App;

class TTSDialog {
public:
    TTSDialog();
    ~TTSDialog();

    void open();
    void close();
    bool is_open() const { return m_is_open; }

    void render(App& app);

private:
    bool m_is_open = false;
    bool m_request_focus = false;
    char m_text_buf[2048] = "Welcome to minicut video editor.";
    float m_rate = 1.0f;
    float m_pitch = 1.0f;
    int m_gemini_voice_idx = 0;
    int m_system_voice_idx = 0;

    bool m_is_generating = false;
    std::string m_last_output_wav;
    std::string m_status_msg;
};
