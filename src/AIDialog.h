#pragma once

#include "AICore.h"
#include <string>

class App;

class AIDialog {
public:
    AIDialog();
    ~AIDialog();

    void open();
    void close();
    bool is_open() const { return m_is_open; }

    void render(App& app);

private:
    bool m_is_open = false;
    bool m_request_focus = false;

    char m_prompt_buf[2048] = "";
    char m_output_buf[8192] = "";

    std::string m_status_msg;
    bool m_is_running = false;
};
