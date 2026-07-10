#pragma once
#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>
#include <vector>

extern bool storage_ok;

// Signature lists
extern std::vector<String> target_ouis;
extern std::vector<String> target_ssids;

bool storage_init();
bool storage_log_device(const String& mac, const String& ssid, int rssi, int channel, const String& type, const String& reason);
bool storage_clear_logs();
size_t storage_get_log_size();
void storage_load_signatures();
bool storage_match_oui(const String& mac_oui);
bool storage_match_ssid(const String& ssid);
String storage_get_all_logs();
