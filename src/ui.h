#pragma once
#include <Arduino.h>

enum UIState {
    UI_STATE_DASHBOARD,
    UI_STATE_MENU,
    UI_STATE_TRANSFER
};

extern UIState current_ui_state;
extern int screen_rotation; // 1 or 3

void ui_init();
void ui_update();
void ui_log_match(const String& mac, const String& ssid, int rssi, const String& type, const String& reason);
