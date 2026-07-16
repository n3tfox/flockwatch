#include "wifi_sniffer.h"
#include "storage.h"
#include <WiFi.h>
#include <esp_wifi.h>

volatile bool wifi_sniffer_running = false;
uint8_t wifi_sniffer_channel = 1;
uint32_t wifi_packets_sniffed = 0;

#include "match_queue.h"

// Helper to parse SSID from 802.11 Information Elements (IE)
static bool parse_ssid_ie(const uint8_t* payload, int body_offset, int total_len, String& ssid) {
    int offset = body_offset;
    // Walk through TLV (Type-Length-Value) structures
    while (offset + 2 <= total_len) {
        uint8_t ie_id = payload[offset];
        uint8_t ie_len = payload[offset + 1];
        
        if (offset + 2 + ie_len > total_len) {
            break; // Out of bounds / corrupt frame
        }
        
        if (ie_id == 0) { // Element ID 0 is SSID
            ssid = "";
            for (int i = 0; i < ie_len; i++) {
                char c = (char)payload[offset + 2 + i];
                // Check if printable
                if (c >= 32 && c <= 126) {
                    ssid += c;
                } else {
                    ssid += '.';
                }
            }
            return true;
        }
        offset += 2 + ie_len;
    }
    return false;
}

// C-style WiFi sniffer callback
static void wifi_promiscuous_callback(void* buf, wifi_promiscuous_pkt_type_t type) {
    if (!wifi_sniffer_running) return;
    if (type != WIFI_PKT_MGMT) return; // Only care about management frames
    
    wifi_packets_sniffed++;
    
    wifi_promiscuous_pkt_t* pkt = (wifi_promiscuous_pkt_t*)buf;
    const uint8_t* payload = pkt->payload;
    int len = pkt->rx_ctrl.sig_len;
    
    if (len < 24) return;
    
    uint8_t frame_control = payload[0];
    uint8_t frame_type = (frame_control >> 2) & 0x03;
    uint8_t frame_subtype = (frame_control >> 4) & 0x0F;
    
    // Validate management frame type
    if (frame_type != 0) return; 
    
    // Extract Source MAC (SA) at offset 10
    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             payload[10], payload[11], payload[12], payload[13], payload[14], payload[15]);
    
    String mac_oui = String(mac_str).substring(0, 8); // e.g. "B4:1E:52"
    mac_oui.replace(":", "");
    
    String ssid = "";
    bool matches = false;
    String match_reason = "";
    
    if (frame_subtype == 4) { // Probe Request
        // Body starts at offset 24
        parse_ssid_ie(payload, 24, len, ssid);
        
        bool oui_match = storage_match_oui(mac_oui);
        bool ssid_match = (ssid.length() > 0) && storage_match_ssid(ssid);
        
        if (oui_match && ssid.length() == 0) {
            matches = true;
            match_reason = "WILDCARD_PROBE_OUI_MATCH";
        } else if (oui_match && ssid_match) {
            matches = true;
            match_reason = "PROBE_OUI_SSID_MATCH";
        } else if (oui_match) {
            matches = true;
            match_reason = "PROBE_OUI_ONLY_MATCH";
        } else if (ssid_match) {
            matches = true;
            match_reason = "PROBE_SSID_ONLY_MATCH";
        }
        
        if (matches) {
            match_queue_submit(mac_str, ssid.c_str(), pkt->rx_ctrl.rssi, wifi_sniffer_channel, "WIFI_CLIENT", match_reason.c_str());
        }
    } 
    else if (frame_subtype == 8 || frame_subtype == 5) { // Beacon or Probe Response
        // Body starts at offset 36 (after 12 bytes of fixed parameters)
        if (len < 36) return;
        parse_ssid_ie(payload, 36, len, ssid);
        
        bool oui_match = storage_match_oui(mac_oui);
        bool ssid_match = (ssid.length() > 0) && storage_match_ssid(ssid);
        
        if (oui_match && ssid_match) {
            matches = true;
            match_reason = "AP_OUI_SSID_MATCH";
        } else if (oui_match) {
            matches = true;
            match_reason = "AP_OUI_ONLY_MATCH";
        } else if (ssid_match) {
            matches = true;
            match_reason = "AP_SSID_ONLY_MATCH";
        }
        
        if (matches) {
            match_queue_submit(mac_str, ssid.c_str(), pkt->rx_ctrl.rssi, wifi_sniffer_channel, "WIFI_AP", match_reason.c_str());
        }
    }
}

void wifi_sniffer_start() {
    if (wifi_sniffer_running) return;
    
    WiFi.disconnect();
    WiFi.mode(WIFI_MODE_STA);
    
    // Set up filter for Management frames
    wifi_promiscuous_filter_t filter = {
        .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT
    };
    esp_wifi_set_promiscuous_filter(&filter);
    
    esp_wifi_set_promiscuous_rx_cb(wifi_promiscuous_callback);
    esp_wifi_set_promiscuous(true);
    
    // Start on channel 1
    wifi_sniffer_channel = 1;
    esp_wifi_set_channel(wifi_sniffer_channel, WIFI_SECOND_CHAN_NONE);
    
    wifi_sniffer_running = true;
    Serial.println("[SNIFFER] Wi-Fi Sniffer started.");
}

void wifi_sniffer_stop() {
    if (!wifi_sniffer_running) return;
    
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(nullptr);
    wifi_sniffer_running = false;
    Serial.println("[SNIFFER] Wi-Fi Sniffer stopped.");
}

void wifi_sniffer_hop() {
    if (!wifi_sniffer_running) return;
    
    // Hop between 1, 6, and 11
    if (wifi_sniffer_channel == 1) wifi_sniffer_channel = 6;
    else if (wifi_sniffer_channel == 6) wifi_sniffer_channel = 11;
    else wifi_sniffer_channel = 1;
    
    esp_wifi_set_channel(wifi_sniffer_channel, WIFI_SECOND_CHAN_NONE);
}
