#include "scan_mode.h"
#include "wifi_sniffer.h"
#include "ble_scanner.h"
#include "storage.h"
#include <Arduino.h>

bool config_interlacing = true; // Default is interlacing

static ScanMode current_mode = SCAN_MODE_WIFI;
static bool auto_alternate = true;
static uint32_t last_mode_swap = 0;
static uint32_t last_channel_hop = 0;

static constexpr uint32_t CHANNEL_HOP_MS = 150;

void scan_mode_init() {
    current_mode = SCAN_MODE_WIFI;
    auto_alternate = true;
    last_mode_swap = millis();
    last_channel_hop = millis();
    if (!log_is_full_stopped) {
        wifi_sniffer_start();
    }
}

void scan_mode_stop_all() {
    wifi_sniffer_stop();
    ble_scanner_stop();
    current_mode = SCAN_MODE_IDLE;
    auto_alternate = false;
}

void scan_mode_start_wifi() {
    ble_scanner_stop();
    if (!log_is_full_stopped) {
        wifi_sniffer_start();
    }
    current_mode = SCAN_MODE_WIFI;
    auto_alternate = false;
    last_mode_swap = millis();
    last_channel_hop = millis();
}

void scan_mode_start_ble() {
    wifi_sniffer_stop();
    if (!log_is_full_stopped) {
        ble_scanner_start(5);
    }
    current_mode = SCAN_MODE_BLE;
    auto_alternate = false;
    last_mode_swap = millis();
}

void scan_mode_resume_default() {
    auto_alternate = true;
    current_mode = SCAN_MODE_WIFI;
    last_mode_swap = millis();
    last_channel_hop = millis();
    ble_scanner_stop();
    if (!log_is_full_stopped) {
        wifi_sniffer_start();
    }
}

ScanMode scan_mode_get() {
    return current_mode;
}

void scan_mode_tick() {
    // If the storage logging has stopped due to full log, stop all active scanning
    if (log_is_full_stopped) {
        if (wifi_sniffer_running) wifi_sniffer_stop();
        if (ble_scanner_running) ble_scanner_stop();
        current_mode = SCAN_MODE_IDLE;
        return;
    }

    uint32_t now_ms = millis();
    uint32_t wifi_dwell = config_interlacing ? 1000 : 15000;
    uint32_t ble_dwell = config_interlacing ? 1000 : 5000;

    if (!auto_alternate) {
        // If not auto-alternating, we run the selected mode
        if (current_mode == SCAN_MODE_WIFI) {
            if (!wifi_sniffer_running) {
                wifi_sniffer_start();
            }
            if (now_ms - last_channel_hop >= CHANNEL_HOP_MS) {
                wifi_sniffer_hop();
                last_channel_hop = now_ms;
            }
        } else if (current_mode == SCAN_MODE_BLE) {
            if (!ble_scanner_running) {
                ble_scanner_start(5);
            }
            ble_scanner_tick();
        }
        return;
    }

    if (current_mode == SCAN_MODE_WIFI) {
        if (!wifi_sniffer_running) {
            wifi_sniffer_start();
        }
        if (now_ms - last_mode_swap >= wifi_dwell) {
            wifi_sniffer_stop();
            ble_scanner_start(5);
            current_mode = SCAN_MODE_BLE;
            last_mode_swap = now_ms;
            Serial.println("[SCAN] Swapping to BLE scanning.");
        } else if (now_ms - last_channel_hop >= CHANNEL_HOP_MS) {
            wifi_sniffer_hop();
            last_channel_hop = now_ms;
        }
    } else if (current_mode == SCAN_MODE_BLE) {
        if (!ble_scanner_running) {
            ble_scanner_start(5);
        }
        if (now_ms - last_mode_swap >= ble_dwell) {
            ble_scanner_stop();
            wifi_sniffer_start();
            current_mode = SCAN_MODE_WIFI;
            last_mode_swap = now_ms;
            last_channel_hop = now_ms;
            Serial.println("[SCAN] Swapping to Wi-Fi sniffing.");
        } else {
            ble_scanner_tick();
        }
    }
}
