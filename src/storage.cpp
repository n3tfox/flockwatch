#include "storage.h"
#include <ArduinoJson.h>

bool storage_ok = false;
bool config_wrap_logs = true; // Default is wrapping
bool log_has_wrapped = false;
bool log_is_full_stopped = false;
bool beep_pending = false;

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

struct __attribute__((packed)) LogRecord {
    uint32_t sequence_num;
    uint32_t timestamp;
    char mac[18];
    char ssid[33];
    int16_t rssi;
    uint8_t channel;
    char type[16];
    char reason[32];
};

static size_t dynamic_max_records = 1000;
static size_t write_index = 0;
static uint32_t next_sequence_num = 1;

static void copy_field(char* dest, size_t dest_size, const char* src) {
    if (!src) {
        dest[0] = '\0';
        return;
    }
    strncpy(dest, src, dest_size - 1);
    dest[dest_size - 1] = '\0';
}

static bool preallocate_log_file(size_t file_size) {
    File file = LittleFS.open("/logs.bin", "w");
    if (!file) {
        Serial.println("[STORAGE] Failed to open /logs.bin for preallocation!");
        return false;
    }
    
    uint8_t buffer[512] = {0};
    size_t written = 0;
    while (written < file_size) {
        size_t to_write = file_size - written;
        if (to_write > 512) to_write = 512;
        if (file.write(buffer, to_write) != to_write) {
            Serial.println("[STORAGE] Preallocation write failed!");
            file.close();
            LittleFS.remove("/logs.bin");
            return false;
        }
        written += to_write;
    }
    file.close();
    Serial.printf("[STORAGE] Preallocated /logs.bin with size %zu bytes\n", file_size);
    return true;
}

static uint32_t read_sequence_num_at(size_t idx) {
    if (idx >= dynamic_max_records) return 0;
    File file = LittleFS.open("/logs.bin", "r");
    if (!file) return 0;
    if (!file.seek(idx * sizeof(LogRecord), SeekSet)) {
        file.close();
        return 0;
    }
    uint32_t seq = 0;
    file.read((uint8_t*)&seq, sizeof(seq));
    file.close();
    return seq;
}

static size_t find_wrap_point() {
    size_t L = 0;
    size_t R = dynamic_max_records - 1;
    
    uint32_t seq_0 = read_sequence_num_at(0);
    uint32_t seq_last = read_sequence_num_at(R);
    
    if (seq_0 == 0) {
        // Binary search for the first 0
        size_t ans = 0;
        while (L <= R) {
            size_t mid = L + (R - L) / 2;
            if (read_sequence_num_at(mid) == 0) {
                ans = mid;
                if (mid == 0) break;
                R = mid - 1;
            } else {
                L = mid + 1;
            }
        }
        return ans;
    }
    
    if (seq_0 <= seq_last) {
        return 0;
    }
    
    while (L < R) {
        size_t mid = L + (R - L) / 2;
        uint32_t seq_mid = read_sequence_num_at(mid);
        
        if (seq_mid >= seq_0) {
            L = mid + 1;
        } else {
            R = mid;
        }
    }
    return L;
}

static bool read_record_at(size_t idx, LogRecord& record) {
    if (idx >= dynamic_max_records) return false;
    File file = LittleFS.open("/logs.bin", "r");
    if (!file) return false;
    if (!file.seek(idx * sizeof(LogRecord), SeekSet)) {
        file.close();
        return false;
    }
    size_t bytes_read = file.read((uint8_t*)&record, sizeof(LogRecord));
    file.close();
    return (bytes_read == sizeof(LogRecord));
}

static bool write_record_at(size_t idx, const LogRecord& record) {
    if (idx >= dynamic_max_records) return false;
    File file = LittleFS.open("/logs.bin", "r+");
    if (!file) {
        Serial.println("[STORAGE] Failed to open /logs.bin for writing!");
        return false;
    }
    if (!file.seek(idx * sizeof(LogRecord), SeekSet)) {
        Serial.println("[STORAGE] Seek failed during write!");
        file.close();
        return false;
    }
    size_t written = file.write((const uint8_t*)&record, sizeof(LogRecord));
    file.close();
    return (written == sizeof(LogRecord));
}

bool storage_init() {
    if (!LittleFS.begin(true)) {
        Serial.println("[STORAGE] LittleFS mount failed!");
        storage_ok = false;
        return false;
    }
    
    Serial.println("[STORAGE] LittleFS mounted successfully.");
    storage_ok = true;
    
    // Clean up old CSV files to free space since we transitioned to binary
    if (LittleFS.exists("/logs.csv")) {
        LittleFS.remove("/logs.csv");
    }
    if (LittleFS.exists("/logs.old.csv")) {
        LittleFS.remove("/logs.old.csv");
    }
    
    // Calculate storage and max records
    size_t total = LittleFS.totalBytes();
    size_t used = LittleFS.usedBytes();
    
    size_t bin_log_size = 0;
    if (LittleFS.exists("/logs.bin")) {
        File f = LittleFS.open("/logs.bin", "r");
        if (f) {
            bin_log_size = f.size();
            f.close();
        }
    }
    
    size_t non_log_used = 0;
    if (used > bin_log_size) {
        non_log_used = used - bin_log_size;
    }
    
    // Reserve safety margin of 64 KB
    const size_t SAFETY_MARGIN = 64 * 1024;
    size_t available_log_space = 0;
    if (total > (non_log_used + SAFETY_MARGIN)) {
        available_log_space = total - non_log_used - SAFETY_MARGIN;
    }
    
    // Cap log file size to 1.5 MB to avoid too long preallocation time
    if (available_log_space > 1536 * 1024) {
        available_log_space = 1536 * 1024;
    }
    
    dynamic_max_records = available_log_space / sizeof(LogRecord);
    if (dynamic_max_records < 100) {
        dynamic_max_records = 100;
    }
    
    if (LittleFS.exists("/logs.bin")) {
        File f = LittleFS.open("/logs.bin", "r");
        if (f) {
            size_t actual_size = f.size();
            f.close();
            if (actual_size >= sizeof(LogRecord)) {
                dynamic_max_records = actual_size / sizeof(LogRecord);
                Serial.printf("[STORAGE] Found existing /logs.bin with %zu records capacity\n", dynamic_max_records);
            }
        }
    } else {
        size_t bytes_to_allocate = dynamic_max_records * sizeof(LogRecord);
        preallocate_log_file(bytes_to_allocate);
    }
    
    // Initialize pointers
    write_index = find_wrap_point();
    if (write_index > 0) {
        uint32_t prev_seq = read_sequence_num_at(write_index - 1);
        next_sequence_num = prev_seq + 1;
        // If we are at a wrap point but seq is not 0 at write_index, we have wrapped
        uint32_t current_seq = read_sequence_num_at(write_index);
        if (current_seq > 0) {
            log_has_wrapped = true;
        }
    } else {
        uint32_t seq_0 = read_sequence_num_at(0);
        if (seq_0 > 0) {
            uint32_t prev_seq = read_sequence_num_at(dynamic_max_records - 1);
            next_sequence_num = prev_seq + 1;
            log_has_wrapped = true;
        } else {
            next_sequence_num = 1;
            log_has_wrapped = false;
        }
    }
    
    Serial.printf("[STORAGE] Next write index: %zu, Next sequence: %u, Wrapped: %s\n", 
                  write_index, next_sequence_num, log_has_wrapped ? "Yes" : "No");
    
    // Check if signatures file exists, if not write defaults
    if (!LittleFS.exists("/signatures.json")) {
        File file = LittleFS.open("/signatures.json", "w");
        if (file) {
            file.print(default_signatures_json);
            file.close();
            Serial.println("[STORAGE] Wrote default signatures.json");
        }
    }
    
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
    if (log_is_full_stopped) return false;
    
    size_t total = LittleFS.totalBytes();
    size_t used = LittleFS.usedBytes();
    size_t free_bytes = (total >= used) ? (total - used) : 0;
    const size_t SAFETY_MARGIN = 16 * 1024;
    
    if (free_bytes < SAFETY_MARGIN) {
        log_is_full_stopped = true;
        beep_pending = true;
        Serial.printf("[STORAGE] Storage critically low (%zu bytes free). Stopped recording.\n", free_bytes);
        return false;
    }
    
    if (!config_wrap_logs && log_has_wrapped) {
        log_is_full_stopped = true;
        beep_pending = true;
        Serial.println("[STORAGE] Log file full (no wrap). Stopped recording.");
        return false;
    }
    
    LogRecord record;
    record.sequence_num = next_sequence_num;
    record.timestamp = millis();
    copy_field(record.mac, sizeof(record.mac), mac.c_str());
    copy_field(record.ssid, sizeof(record.ssid), ssid.c_str());
    record.rssi = rssi;
    record.channel = channel;
    copy_field(record.type, sizeof(record.type), type.c_str());
    copy_field(record.reason, sizeof(record.reason), reason.c_str());
    
    if (write_record_at(write_index, record)) {
        size_t prev_index = write_index;
        write_index = (write_index + 1) % dynamic_max_records;
        next_sequence_num++;
        
        if (write_index == 0 && prev_index == dynamic_max_records - 1) {
            log_has_wrapped = true;
        }
        
        Serial.printf("[STORAGE] Logged record (seq=%u) to binary index %zu\n", record.sequence_num, prev_index);
        return true;
    }
    return false;
}

bool storage_clear_logs() {
    if (!storage_ok) return false;
    
    if (LittleFS.exists("/logs.bin")) {
        LittleFS.remove("/logs.bin");
    }
    
    log_has_wrapped = false;
    log_is_full_stopped = false;
    beep_pending = false;
    write_index = 0;
    next_sequence_num = 1;
    
    size_t bytes_to_allocate = dynamic_max_records * sizeof(LogRecord);
    return preallocate_log_file(bytes_to_allocate);
}

size_t storage_get_log_size() {
    return storage_get_active_record_count() * sizeof(LogRecord);
}

bool storage_match_oui(const String& mac_oui) {
    String clean_oui = mac_oui;
    clean_oui.toUpperCase();
    clean_oui.replace(":", "");
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
    if (!storage_ok) return "";
    size_t num_records = storage_get_active_record_count();
    if (num_records == 0) return "";
    
    String content = "timestamp,mac,ssid,rssi,channel,type,reason\n";
    for (size_t i = 0; i < num_records; ++i) {
        content += storage_get_csv_line(i);
    }
    return content;
}

size_t storage_get_active_record_count() {
    if (!storage_ok) return 0;
    return log_has_wrapped ? dynamic_max_records : write_index;
}

size_t storage_get_csv_total_size() {
    if (!storage_ok) return 0;
    size_t num_records = storage_get_active_record_count();
    if (num_records == 0) return 0;
    
    size_t total_size = strlen("timestamp,mac,ssid,rssi,channel,type,reason\n");
    
    for (size_t i = 0; i < num_records; ++i) {
        size_t physical_idx = (log_has_wrapped ? (write_index + i) : i) % dynamic_max_records;
        LogRecord rec;
        if (read_record_at(physical_idx, rec)) {
            char buf[250];
            int len = snprintf(buf, sizeof(buf), "%u,%s,%s,%d,%d,%s,%s\n", 
                               rec.timestamp, rec.mac, rec.ssid, rec.rssi, rec.channel, rec.type, rec.reason);
            if (len > 0) {
                total_size += len;
            }
        }
    }
    return total_size;
}

String storage_get_csv_line(size_t relative_idx) {
    if (!storage_ok) return "";
    size_t num_records = storage_get_active_record_count();
    if (relative_idx >= num_records) return "";
    
    size_t physical_idx = (log_has_wrapped ? (write_index + relative_idx) : relative_idx) % dynamic_max_records;
    LogRecord rec;
    if (read_record_at(physical_idx, rec)) {
        char buf[250];
        snprintf(buf, sizeof(buf), "%u,%s,%s,%d,%d,%s,%s\n", 
                 rec.timestamp, rec.mac, rec.ssid, rec.rssi, rec.channel, rec.type, rec.reason);
        return String(buf);
    }
    return "";
}
