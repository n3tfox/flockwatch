#include <Arduino.h>
#include <M5Unified.h>
#include "storage.h"
#include "wifi_sniffer.h"
#include "ble_scanner.h"
#include "ble_transfer.h"
#include "ui.h"

// Unique matches counters
uint32_t wifi_logged_count = 0;
uint32_t ble_logged_count = 0;

// Deduplication cache structure
struct DedupeDevice {
    String mac;
    uint32_t timestamp;
};
static std::vector<DedupeDevice> dedupe_cache;

// Dual-mode state machine variables
static uint32_t last_mode_swap = 0;
static bool current_mode_wifi = true;
static uint32_t last_channel_hop = 0;

// Match processor called by Sniffer and Scanner callbacks
void process_device_match(const String& mac, const String& ssid, int rssi, int channel, const String& type, const String& reason) {
    uint32_t now_ms = millis();
    
    // Clean up expired deduplication entries (>30 seconds old)
    for (auto it = dedupe_cache.begin(); it != dedupe_cache.end(); ) {
        if (now_ms - it->timestamp > 30000) {
            it = dedupe_cache.erase(it);
        } else {
            if (it->mac == mac) {
                return; // Duplicate found, ignore logging and alerts
            }
            ++it;
        }
    }
    
    // Enforce cache memory limit (max 100 entries)
    if (dedupe_cache.size() >= 100) {
        dedupe_cache.erase(dedupe_cache.begin()); // Evict oldest
    }
    
    // Add to cache
    dedupe_cache.push_back({mac, now_ms});
    
    // Write match to CSV logs on LittleFS
    bool logged = storage_log_device(mac, ssid, rssi, channel, type, reason);
    if (logged) {
        if (type.startsWith("WIFI")) {
            wifi_logged_count++;
        } else if (type == "BLE") {
            ble_logged_count++;
        }
    }
    
    // Double chirp tone on matched target
    M5.Speaker.tone(1800, 80);
    delay(100);
    M5.Speaker.tone(2200, 80);
    
    // Flash built-in red LED (GPIO 19 on M5StickC Plus 2)
    digitalWrite(19, HIGH);
    delay(50);
    digitalWrite(19, LOW);
    
    // Notify UI to update the last match panel
    ui_log_match(mac, ssid, rssi, type, reason);
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
    
    // 3. Initialize display UI
    ui_init();
    
    // 4. Default start with WiFi sniffer active
    wifi_sniffer_start();
    last_mode_swap = millis();
    last_channel_hop = millis();
    
    // Play startup chime
    M5.Speaker.tone(1000, 100);
    delay(120);
    M5.Speaker.tone(1500, 100);
    delay(120);
    M5.Speaker.tone(2000, 150);
}

void loop() {
    // Poll button inputs and refresh display
    ui_update();
    
    // Execute BLE Transfer server if in transfer mode
    if (current_ui_state == UI_STATE_TRANSFER) {
        ble_transfer_tick();
        return; 
    }
    
    // Alternate scanning between Wi-Fi and BLE when dashboard is active
    if (current_ui_state == UI_STATE_DASHBOARD) {
        uint32_t now_ms = millis();
        
        if (current_mode_wifi) {
            // Sniff WiFi on channel 1, 6, or 11
            if (now_ms - last_mode_swap >= 15000) { // 15 seconds of WiFi
                wifi_sniffer_stop();
                ble_scanner_start(5); // Switch to BLE scan for 5 seconds
                current_mode_wifi = false;
                last_mode_swap = now_ms;
                Serial.println("[MAIN] Swapping to BLE scanning.");
            } else {
                // Hop WiFi channels every 150ms
                if (now_ms - last_channel_hop >= 150) {
                    wifi_sniffer_hop();
                    last_channel_hop = now_ms;
                }
            }
        } else {
            // BLE scanning
            if (now_ms - last_mode_swap >= 5000) { // 5 seconds of BLE
                ble_scanner_stop();
                wifi_sniffer_start(); // Switch back to WiFi sniffing
                current_mode_wifi = true;
                last_mode_swap = now_ms;
                last_channel_hop = now_ms;
                Serial.println("[MAIN] Swapping to Wi-Fi sniffing.");
            } else {
                ble_scanner_tick();
            }
        }
    }
}
