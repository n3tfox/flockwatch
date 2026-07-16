#include "ui.h"
#include "storage.h"
#include "wifi_sniffer.h"
#include "ble_scanner.h"
#include "ble_transfer.h"
#include "scan_mode.h"
#include <M5Unified.h>

UIState current_ui_state = UI_STATE_DASHBOARD;
int screen_rotation = 1; // 1 = landscape (USB right), 3 = flipped landscape (USB left)

// LGFX Sprite canvas for flicker-free double buffering
static LGFX_Sprite* canvas_ptr = nullptr;
#define canvas (*canvas_ptr)

// Last matched device info
static String last_mac = "None";
static String last_ssid = "None";
static int last_rssi = 0;
static String last_type = "None";
static String last_reason = "None";
static uint32_t last_match_time = 0;

// Stats
#include "match_queue.h"

// Menu variables
static int menu_index = 0;
static const int menu_count = 7;
static const char* menu_items[menu_count] = {
    "Interlacing",
    "Wrap Logs",
    "Start BLE Transfer",
    "Rotate Screen",
    "Clear Logs",
    "Power Off",
    "Exit Settings"
};

void ui_init() {
    canvas_ptr = new LGFX_Sprite(&M5.Display);
    M5.Display.setRotation(screen_rotation);
    canvas.createSprite(240, 135);
    canvas.setTextColor(TFT_WHITE, TFT_BLACK);
}

static void draw_dashboard() {
    canvas.fillScreen(TFT_BLACK);
    
    // Theme Colors
    uint16_t c_cyan = canvas.color565(0, 220, 255);
    uint16_t c_grey = canvas.color565(80, 80, 80);
    uint16_t c_light_grey = canvas.color565(180, 180, 180);
    uint16_t c_green = canvas.color565(0, 230, 100);
    uint16_t c_orange = canvas.color565(255, 140, 0);

    // Get battery status
    int bat = M5.Power.getBatteryLevel();
    bool chg = M5.Power.isCharging();
    
    // 1. Header (y: 0 to 18)
    canvas.setTextSize(1.5);
    canvas.setTextColor(c_cyan, TFT_BLACK);
    canvas.drawString("FLOCKWATCH", 6, 4);
    
    // Status indicators in header
    String status_ind = "";
    if (wifi_sniffer_running) status_ind += "[W]";
    if (ble_scanner_running) status_ind += "[B]";
    canvas.setTextColor(c_green, TFT_BLACK);
    canvas.drawString(status_ind, 100, 4);
    
    // Battery text
    char bat_buf[15];
    if (chg) {
        snprintf(bat_buf, sizeof(bat_buf), "%d%% CHG", bat);
        canvas.setTextColor(c_green, TFT_BLACK);
    } else {
        snprintf(bat_buf, sizeof(bat_buf), "%d%%", bat);
        canvas.setTextColor(TFT_WHITE, TFT_BLACK);
    }
    canvas.drawString(bat_buf, 195, 4);
    
    // Header divider line
    canvas.drawFastHLine(0, 18, 240, c_grey);

    // 2. Body Area (y: 22 to 110)
    // Vertical split divider at x = 108
    canvas.drawFastVLine(108, 18, 92, c_grey);

    // Left Panel: Stats (x: 0 to 104)
    canvas.setTextSize(1.2);
    canvas.setTextColor(c_light_grey, TFT_BLACK);
    canvas.drawString("UNIQUE", 6, 24);
    
    canvas.setTextColor(TFT_WHITE, TFT_BLACK);
    char buf_wifi[20];
    char buf_ble[20];
    snprintf(buf_wifi, sizeof(buf_wifi), "WiFi: %u", wifi_logged_count);
    snprintf(buf_ble, sizeof(buf_ble), "BLE:  %u", ble_logged_count);
    canvas.drawString(buf_wifi, 6, 40);
    canvas.drawString(buf_ble, 6, 54);
    
    canvas.setTextColor(c_light_grey, TFT_BLACK);
    canvas.drawString("RUNNING", 6, 74);
    
    canvas.setTextColor(c_cyan, TFT_BLACK);
    if (wifi_sniffer_running) {
        char ch_buf[15];
        snprintf(ch_buf, sizeof(ch_buf), "WiFi (Ch%d)", wifi_sniffer_channel);
        canvas.drawString(ch_buf, 6, 90);
    } else if (ble_scanner_running) {
        canvas.drawString("BLE Scan", 6, 90);
    } else {
        canvas.drawString("Idle", 6, 90);
    }

    // Right Panel: Match Details (x: 114 to 240)
    canvas.setTextSize(1.2);
    canvas.setTextColor(c_orange, TFT_BLACK);
    canvas.drawString("LAST MATCH:", 114, 24);

    if (last_mac != "None") {
        canvas.setTextColor(TFT_WHITE, TFT_BLACK);
        
        // Truncate SSID if too long for the 126px width
        String disp_ssid = last_ssid;
        if (disp_ssid.length() == 0) disp_ssid = "<wildcard>";
        else if (disp_ssid.length() > 14) disp_ssid = disp_ssid.substring(0, 12) + "..";
        
        char ssid_row[35];
        char mac_row[35];
        char rssi_row[35];
        char reason_row[35];
        
        snprintf(ssid_row, sizeof(ssid_row), "SSID: %s", disp_ssid.c_str());
        snprintf(mac_row, sizeof(mac_row), "MAC:  %s", last_mac.c_str());
        snprintf(rssi_row, sizeof(rssi_row), "RSSI: %d dBm (%s)", last_rssi, last_type.c_str());
        snprintf(reason_row, sizeof(reason_row), "%s", last_reason.c_str());
        
        canvas.drawString(ssid_row, 114, 40);
        canvas.drawString(mac_row, 114, 54);
        canvas.drawString(rssi_row, 114, 68);
        
        // Make reason stand out
        canvas.setTextColor(c_cyan, TFT_BLACK);
        canvas.drawString(reason_row, 114, 82);
        
        // Time elapsed since match
        uint32_t elapsed_sec = (millis() - last_match_time) / 1000;
        char time_buf[20];
        if (elapsed_sec < 60) {
            snprintf(time_buf, sizeof(time_buf), "%ds ago", elapsed_sec);
        } else {
            snprintf(time_buf, sizeof(time_buf), "%dm ago", elapsed_sec / 60);
        }
        canvas.setTextColor(c_light_grey, TFT_BLACK);
        canvas.drawString(time_buf, 114, 96);
    } else {
        canvas.setTextColor(c_grey, TFT_BLACK);
        canvas.drawString("No matched", 114, 48);
        canvas.drawString("surveillance", 114, 62);
        canvas.drawString("signals yet.", 114, 76);
    }

    // 3. Footer Area (y: 115 to 135)
    canvas.drawFastHLine(0, 114, 240, c_grey);
    canvas.setTextSize(1.1);
    canvas.setTextColor(c_light_grey, TFT_BLACK);
    canvas.drawString("M5: Settings | Side: Reset Count", 6, 120);

    // Push buffer to display
    canvas.pushSprite(0, 0);
}

static void draw_menu() {
    canvas.fillScreen(TFT_BLACK);
    
    uint16_t c_cyan = canvas.color565(0, 220, 255);
    uint16_t c_grey = canvas.color565(80, 80, 80);
    uint16_t c_light_grey = canvas.color565(180, 180, 180);
    uint16_t c_green = canvas.color565(0, 230, 100);
    uint16_t c_orange = canvas.color565(255, 140, 0);
    
    // Header
    canvas.setTextSize(1.5);
    canvas.setTextColor(c_cyan, TFT_BLACK);
    canvas.drawString("SETTINGS MENU", 6, 4);
    
    // Display Wrap Issue in Header
    canvas.setTextSize(1.1);
    if (log_is_full_stopped) {
        canvas.setTextColor(TFT_RED, TFT_BLACK);
        canvas.drawString("WRAP: STOPPED", 140, 6);
    } else if (log_has_wrapped) {
        canvas.setTextColor(c_orange, TFT_BLACK);
        canvas.drawString("WRAP: WRAPPED", 140, 6);
    } else {
        canvas.setTextColor(c_green, TFT_BLACK);
        canvas.drawString("WRAP: OK", 170, 6);
    }
    
    canvas.drawFastHLine(0, 18, 240, c_grey);
    
    // Render menu items
    canvas.setTextSize(1.2);
    for (int i = 0; i < menu_count; i++) {
        int y = 24 + (i * 14);
        
        if (i == menu_index) {
            canvas.setTextColor(TFT_BLACK, c_cyan);
            canvas.drawString("> ", 6, y);
            canvas.drawString(menu_items[i], 18, y);
        } else {
            canvas.setTextColor(TFT_WHITE, TFT_BLACK);
            canvas.drawString("  ", 6, y);
            canvas.drawString(menu_items[i], 18, y);
        }
        
        // Draw state status on toggles
        canvas.setTextColor(c_light_grey, (i == menu_index) ? c_cyan : TFT_BLACK);
        if (i == 0) {
            canvas.drawString(config_interlacing ? "[ON]" : "[OFF]", 170, y);
        } else if (i == 1) {
            canvas.drawString(config_wrap_logs ? "[ON]" : "[OFF]", 170, y);
        } else if (i == 3) {
            char rot_buf[10];
            snprintf(rot_buf, sizeof(rot_buf), "[Rot:%d]", screen_rotation);
            canvas.drawString(rot_buf, 170, y);
        }
    }
    
    // Footer
    canvas.drawFastHLine(0, 114, 240, c_grey);
    canvas.setTextSize(1.0);
    canvas.setTextColor(c_light_grey, TFT_BLACK);
    canvas.drawString("Side: Scroll | M5 Button: Select", 6, 120);

    // Push buffer to display
    canvas.pushSprite(0, 0);
}

static void draw_transfer() {
    canvas.fillScreen(TFT_BLACK);
    
    uint16_t c_cyan = canvas.color565(0, 220, 255);
    uint16_t c_grey = canvas.color565(80, 80, 80);
    uint16_t c_light_grey = canvas.color565(180, 180, 180);
    uint16_t c_green = canvas.color565(0, 230, 100);
    
    // Header
    canvas.setTextSize(1.5);
    canvas.setTextColor(c_cyan, TFT_BLACK);
    canvas.drawString("BLE LOG TRANSFER", 6, 4);
    canvas.drawFastHLine(0, 18, 240, c_grey);
    
    canvas.setTextSize(1.1);
    canvas.setTextColor(TFT_WHITE, TFT_BLACK);
    
    uint8_t wifi_mac[6];
    esp_read_mac(wifi_mac, ESP_MAC_WIFI_STA);
    char name_buf[45];
    snprintf(name_buf, sizeof(name_buf), "Name: FlockWatch-%02X%02X", wifi_mac[4], wifi_mac[5]);
    canvas.drawString(name_buf, 6, 24);
    
    uint8_t bt_mac[6];
    esp_read_mac(bt_mac, ESP_MAC_BT);
    char mac_buf[45];
    snprintf(mac_buf, sizeof(mac_buf), "BT MAC: %02X:%02X:%02X:%02X:%02X:%02X", 
             bt_mac[0], bt_mac[1], bt_mac[2], bt_mac[3], bt_mac[4], bt_mac[5]);
    canvas.drawString(mac_buf, 6, 38);
    
    // Show connection status
    if (!ble_transfer_connected) {
        canvas.setTextColor(c_light_grey, TFT_BLACK);
        canvas.drawString("Status: Advertising (NUS)...", 6, 54);
    } else {
        canvas.setTextColor(c_green, TFT_BLACK);
        canvas.drawString("Status: Connected to PC", 6, 54);
        
        // Progress bar
        canvas.setTextColor(TFT_WHITE, TFT_BLACK);
        char prog_buf[25];
        snprintf(prog_buf, sizeof(prog_buf), "Transferring: %d%%", ble_transfer_progress);
        canvas.drawString(prog_buf, 6, 70);
        
        // Visual progress bar
        canvas.drawRect(6, 85, 228, 12, c_grey);
        int w = (ble_transfer_progress * 224) / 100;
        canvas.fillRect(8, 87, w, 8, c_green);
    }
    
    // Footer
    canvas.drawFastHLine(0, 114, 240, c_grey);
    canvas.setTextSize(1.1);
    canvas.setTextColor(c_light_grey, TFT_BLACK);
    canvas.drawString("M5 Button: Cancel / Stop Server", 6, 120);

    // Push buffer to display
    canvas.pushSprite(0, 0);
}

void ui_update() {
    M5.update();
    
    static bool screen_on = true;
    
    // Short press of Power Button (BtnPWR) toggles screen sleep state
    if (M5.BtnPWR.wasPressed()) {
        screen_on = !screen_on;
        if (screen_on) {
            M5.Display.wakeup();
            M5.Display.setBrightness(128);
        } else {
            M5.Display.setBrightness(0);
            M5.Display.sleep();
        }
        M5.Speaker.tone(1200, 30);
    }
    
    if (!screen_on) return;
    
    bool state_changed = false;
    
    // Handle button actions globally based on states
    if (current_ui_state == UI_STATE_DASHBOARD) {
        if (M5.BtnA.wasPressed()) { // Front button
            current_ui_state = UI_STATE_MENU;
            menu_index = 0;
            M5.Speaker.tone(1500, 50);
            state_changed = true;
        }
        else if (M5.BtnB.wasPressed()) { // Side button
            // Reset counters and clear logs
            storage_clear_logs();
            match_queue_reset_stats();
            last_mac = "None";
            last_ssid = "None";
            last_rssi = 0;
            last_type = "None";
            last_reason = "None";
            scan_mode_init();
            M5.Speaker.tone(800, 50);
            state_changed = true;
        }
    } 
    else if (current_ui_state == UI_STATE_MENU) {
        if (M5.BtnB.wasPressed()) { // Side button to scroll
            menu_index = (menu_index + 1) % menu_count;
            M5.Speaker.tone(1000, 30);
            state_changed = true;
        }
        else if (M5.BtnA.wasPressed()) { // Front button to select
            M5.Speaker.tone(1500, 50);
            state_changed = true;
            
            if (menu_index == 0) { // Interlacing toggle
                config_interlacing = !config_interlacing;
                scan_mode_init();
            }
            else if (menu_index == 1) { // Wrap Logs toggle
                config_wrap_logs = !config_wrap_logs;
            }
            else if (menu_index == 2) { // Start BLE Transfer
                wifi_sniffer_stop();
                ble_scanner_stop();
                ble_transfer_start();
                current_ui_state = UI_STATE_TRANSFER;
                return;
            }
            else if (menu_index == 3) { // Rotate Screen
                if (screen_rotation == 1) screen_rotation = 3;
                else screen_rotation = 1;
                M5.Display.setRotation(screen_rotation);
            }
            else if (menu_index == 4) { // Clear Logs
                storage_clear_logs();
                match_queue_reset_stats();
                last_mac = "None";
                last_ssid = "None";
                last_rssi = 0;
                last_type = "None";
                last_reason = "None";
                scan_mode_init();
                // Play clear logs tone sequence
                M5.Speaker.tone(600, 100);
                delay(100);
                M5.Speaker.tone(400, 100);
            }
            else if (menu_index == 5) { // Power Off
                // Play power down tune
                M5.Speaker.tone(1000, 100);
                delay(100);
                M5.Speaker.tone(500, 150);
                delay(150);
                M5.Power.powerOff();
            }
            else if (menu_index == 6) { // Exit Settings
                current_ui_state = UI_STATE_DASHBOARD;
            }
        }
    }
    else if (current_ui_state == UI_STATE_TRANSFER) {
        if (M5.BtnA.wasPressed()) { // Front button to exit BLE transfer
            ble_transfer_stop();
            M5.Speaker.tone(1200, 50);
            current_ui_state = UI_STATE_DASHBOARD;
            
            // Resume default scan mode after exiting transfer
            scan_mode_resume_default();
            return;
        }
    }
    
    // Throttle rendering: draw every 100ms or immediately on button press state change
    static uint32_t last_ui_draw = 0;
    uint32_t now = millis();
    if (state_changed || (now - last_ui_draw >= 100)) {
        last_ui_draw = now;
        if (current_ui_state == UI_STATE_DASHBOARD) {
            draw_dashboard();
        } else if (current_ui_state == UI_STATE_MENU) {
            draw_menu();
        } else if (current_ui_state == UI_STATE_TRANSFER) {
            draw_transfer();
        }
    }
}

void ui_log_match(const String& mac, const String& ssid, int rssi, const String& type, const String& reason) {
    last_mac = mac;
    last_ssid = ssid;
    last_rssi = rssi;
    last_type = type;
    last_reason = reason;
    last_match_time = millis();
}
