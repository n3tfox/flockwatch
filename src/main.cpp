#include <Arduino.h>
#include <M5Unified.h>
#include "storage.h"
#include "wifi_sniffer.h"
#include "ble_scanner.h"
#include "ble_transfer.h"
#include "ui.h"
#include "match_queue.h"
#include "scan_mode.h"
#include "crash_detector.h"

// Non-blocking indicator variables
static bool match_indicator_pending = false;
static uint32_t match_indicator_start = 0;
static int match_indicator_step = -1;

static bool full_beep_pending = false;
static uint32_t full_beep_start = 0;
static int full_beep_step = -1;

void trigger_match_indicator() {
    match_indicator_pending = true;
    match_indicator_start = millis();
    match_indicator_step = 0;
}

void trigger_full_beep() {
    full_beep_pending = true;
    full_beep_start = millis();
    full_beep_step = 0;
}

static void update_indicators() {
    // 1. Process match indicators
    if (match_indicator_pending) {
        uint32_t elapsed = millis() - match_indicator_start;
        if (elapsed < 50) {
            digitalWrite(19, HIGH);
        } else {
            digitalWrite(19, LOW);
        }

        if (match_indicator_step == 0) {
            M5.Speaker.tone(1800, 80);
            match_indicator_step = 1;
        } else if (match_indicator_step == 1 && elapsed >= 100) {
            M5.Speaker.tone(2200, 80);
            match_indicator_step = 2;
        } else if (match_indicator_step == 2 && elapsed >= 180) {
            match_indicator_pending = false;
        }
    }

    // 2. Process warning full beeps
    if (beep_pending) {
        beep_pending = false;
        trigger_full_beep();
    }

    if (full_beep_pending) {
        uint32_t elapsed = millis() - full_beep_start;
        if (full_beep_step == 0) {
            M5.Speaker.tone(1500, 150);
            full_beep_step = 1;
        } else if (full_beep_step == 1 && elapsed >= 300) {
            M5.Speaker.tone(1500, 150);
            full_beep_step = 2;
        } else if (full_beep_step == 2 && elapsed >= 450) {
            full_beep_pending = false;
        }
    }
}

void setup() {
    // 1. Initialize M5Unified
    auto cfg = M5.config();
    M5.begin(cfg);
    
    // Configure Speaker/Buzzer volume (0-255 scale)
    M5.Speaker.setVolume(128);
    
    // Configure Serial logging
    Serial.begin(115200);
    Serial.println("[MAIN] FlockWatch Native Firmware starting...");
    
    // Configure LED pin
    pinMode(19, OUTPUT);
    digitalWrite(19, LOW);
    
    // 2. Initialize LittleFS storage
    storage_init();
    
    // Initialize crash detector (needs storage_init to have run)
    crash_detector_init();
    
    // Initialize match queue
    match_queue_init();
    
    // 3. Initialize display UI
    ui_init();
    
    // 4. Initialize scan mode
    scan_mode_init();
    
    // Play startup chime
    M5.Speaker.tone(1000, 100);
    delay(120);
    M5.Speaker.tone(1500, 100);
    delay(120);
    M5.Speaker.tone(2000, 150);
}

void loop() {
    // Poll serial input for crash log dump requests
    crash_detector_tick();

    // Process queued match events
    match_queue_process();

    // Process non-blocking beeps and LEDs
    update_indicators();

    // Poll button inputs and refresh display
    ui_update();
    
    // Execute BLE Transfer server if in transfer mode
    if (current_ui_state == UI_STATE_TRANSFER) {
        ble_transfer_tick();
        return; 
    }
    
    // Tick scanning when dashboard is active
    if (current_ui_state == UI_STATE_DASHBOARD) {
        scan_mode_tick();
    }
}
