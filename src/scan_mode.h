#pragma once

#include <Arduino.h>

enum ScanMode {
    SCAN_MODE_IDLE = 0,
    SCAN_MODE_WIFI,
    SCAN_MODE_BLE
};

void scan_mode_init();
void scan_mode_tick();
ScanMode scan_mode_get();
extern bool config_interlacing;

// Dashboard menu / lifecycle hooks
void scan_mode_start_wifi();
void scan_mode_start_ble();
void scan_mode_stop_all();
void scan_mode_resume_default();
