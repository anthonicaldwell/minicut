#pragma once

#include "SettingsLogic.h"

class App;

class SettingsDialog {
public:
    SettingsDialog();
    ~SettingsDialog();

    void open();
    void close();
    bool is_open() const { return m_is_open; }

    void render(SettingsLogic& logic, App& app);

private:
    bool m_is_open = false;
    bool m_request_focus = false;
    char m_api_key_buf[256] = "";
    bool m_show_api_key = false;
    bool m_key_initialized = false;
};
