#include "storage.h"
#include <ArduinoJson.h>

bool storage_ok = false;

std::vector<String> target_ouis;
std::vector<String> target_ssids;

// Default signatures
const char* default_signatures_json = 
"{\n"
"  \"ouis\": [\n"
"    \"B41E52\",\n"
"    \"E4AAEA\",\n"
"    \"826BF2\"\n"
"  ],\n"
"  \"ssids\": [\n"
"    \"flock\",\n"
"    \"flck\",\n"
"    \"test_flck\"\n"
"  ]\n"
"}";

bool storage_init() {
    // Mount LittleFS (it will automatically format if it fails or doesn't exist)
    if (!LittleFS.begin(true)) {
        Serial.println("[STORAGE] LittleFS mount failed!");
        storage_ok = false;
        return false;
    }
    
    Serial.println("[STORAGE] LittleFS mounted successfully.");
    storage_ok = true;
    
    // Check if signatures file exists, if not write defaults
    if (!LittleFS.exists("/signatures.json")) {
        File file = LittleFS.open("/signatures.json", "w");
        if (file) {
            file.print(default_signatures_json);
            file.close();
            Serial.println("[STORAGE] Wrote default signatures.json");
        }
    }
    
    // Load signatures
    storage_load_signatures();
    return true;
}

void storage_load_signatures() {
    target_ouis.clear();
    target_ssids.clear();
    
    if (!storage_ok) return;
    
    File file = LittleFS.open("/signatures.json", "r");
    if (!file) {
        Serial.println("[STORAGE] Failed to open signatures.json for reading, using internal defaults.");
        // Load fallback defaults
        target_ouis.push_back("B41E52");
        target_ouis.push_back("E4AAEA");
        target_ouis.push_back("826BF2");
        target_ssids.push_back("flock");
        target_ssids.push_back("flck");
        target_ssids.push_back("test_flck");
        return;
    }
    
    StaticJsonDocument<1024> doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    
    if (error) {
        Serial.println("[STORAGE] Failed to parse signatures.json, using defaults.");
        target_ouis.push_back("B41E52");
        target_ouis.push_back("E4AAEA");
        target_ouis.push_back("826BF2");
        target_ssids.push_back("flock");
        target_ssids.push_back("flck");
        target_ssids.push_back("test_flck");
        return;
    }
    
    JsonArray ouis = doc["ouis"];
    for (JsonVariant oui : ouis) {
        String oui_str = oui.as<String>();
        oui_str.toUpperCase();
        // Remove colons
        oui_str.replace(":", "");
        target_ouis.push_back(oui_str);
    }
    
    JsonArray ssids = doc["ssids"];
    for (JsonVariant ssid : ssids) {
        String ssid_str = ssid.as<String>();
        ssid_str.toLowerCase();
        target_ssids.push_back(ssid_str);
    }
    
    Serial.printf("[STORAGE] Loaded %d OUIs and %d SSIDs from signatures.json\n", target_ouis.size(), target_ssids.size());
}

bool storage_log_device(const String& mac, const String& ssid, int rssi, int channel, const String& type, const String& reason) {
    if (!storage_ok) return false;
    
    // Check if we need to write header first
    bool write_header = !LittleFS.exists("/logs.csv");
    
    File file = LittleFS.open("/logs.csv", "a");
    if (!file) {
        Serial.println("[STORAGE] Failed to open logs.csv for appending!");
        return false;
    }
    
    if (write_header) {
        file.println("timestamp,mac,ssid,rssi,channel,type,reason");
    }
    
    unsigned long timestamp = millis();
    file.printf("%lu,%s,%s,%d,%d,%s,%s\n", timestamp, mac.c_str(), ssid.c_str(), rssi, channel, type.c_str(), reason.c_str());
    file.close();
    
    Serial.printf("[STORAGE] Logged: MAC=%s, SSID=%s, RSSI=%d, Ch=%d, Type=%s, Reason=%s\n", 
                  mac.c_str(), ssid.c_str(), rssi, channel, type.c_str(), reason.c_str());
    return true;
}

bool storage_clear_logs() {
    if (!storage_ok) return false;
    if (LittleFS.exists("/logs.csv")) {
        return LittleFS.remove("/logs.csv");
    }
    return true;
}

size_t storage_get_log_size() {
    if (!storage_ok) return 0;
    if (!LittleFS.exists("/logs.csv")) return 0;
    File file = LittleFS.open("/logs.csv", "r");
    if (!file) return 0;
    size_t sz = file.size();
    file.close();
    return sz;
}

bool storage_match_oui(const String& mac_oui) {
    String clean_oui = mac_oui;
    clean_oui.toUpperCase();
    clean_oui.replace(":", "");
    // Extract first 6 chars just in case full MAC is passed
    if (clean_oui.length() > 6) {
        clean_oui = clean_oui.substring(0, 6);
    }
    
    for (const String& oui : target_ouis) {
        if (clean_oui.startsWith(oui)) return true;
    }
    return false;
}

bool storage_match_ssid(const String& ssid) {
    String clean_ssid = ssid;
    clean_ssid.toLowerCase();
    
    for (const String& pattern : target_ssids) {
        if (clean_ssid.indexOf(pattern) != -1) return true;
    }
    return false;
}

String storage_get_all_logs() {
    if (!storage_ok || !LittleFS.exists("/logs.csv")) return "";
    File file = LittleFS.open("/logs.csv", "r");
    if (!file) return "";
    String content = file.readString();
    file.close();
    return content;
}
