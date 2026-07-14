#include "match_queue.h"
#include "storage.h"
#include "ui.h"
#include <M5Unified.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <vector>

uint32_t wifi_logged_count = 0;
uint32_t ble_logged_count = 0;

static constexpr size_t MATCH_QUEUE_CAPACITY = 16;

static MatchEvent match_queue[MATCH_QUEUE_CAPACITY];
static volatile size_t queue_head = 0;
static volatile size_t queue_tail = 0;
static portMUX_TYPE queue_mux = portMUX_INITIALIZER_UNLOCKED;

struct DedupeDevice {
    char mac[18];
    uint32_t timestamp;
};
static constexpr size_t DEDUPE_CACHE_CAPACITY = 128;
static DedupeDevice dedupe_cache[DEDUPE_CACHE_CAPACITY];
static size_t dedupe_write_idx = 0;
static size_t dedupe_count = 0;

static void copy_field(char* dest, size_t dest_size, const char* src) {
    if (!src) {
        dest[0] = '\0';
        return;
    }
    strncpy(dest, src, dest_size - 1);
    dest[dest_size - 1] = '\0';
}

static bool queue_is_full() {
    return ((queue_tail + 1) % MATCH_QUEUE_CAPACITY) == queue_head;
}

static bool queue_pop(MatchEvent* out) {
    portENTER_CRITICAL(&queue_mux);
    if (queue_head == queue_tail) {
        portEXIT_CRITICAL(&queue_mux);
        return false;
    }
    *out = match_queue[queue_head];
    queue_head = (queue_head + 1) % MATCH_QUEUE_CAPACITY;
    portEXIT_CRITICAL(&queue_mux);
    return true;
}

void match_queue_init() {
    queue_head = 0;
    queue_tail = 0;
    match_queue_clear_dedupe();
}

bool match_queue_submit(const char* mac, const char* ssid, int rssi, int channel,
                        const char* type, const char* reason) {
    portENTER_CRITICAL(&queue_mux);
    if (queue_is_full()) {
        portEXIT_CRITICAL(&queue_mux);
        return false;
    }

    MatchEvent* evt = &match_queue[queue_tail];
    copy_field(evt->mac, sizeof(evt->mac), mac);
    copy_field(evt->ssid, sizeof(evt->ssid), ssid ? ssid : "");
    copy_field(evt->type, sizeof(evt->type), type);
    copy_field(evt->reason, sizeof(evt->reason), reason);
    evt->rssi = rssi;
    evt->channel = channel;

    queue_tail = (queue_tail + 1) % MATCH_QUEUE_CAPACITY;
    portEXIT_CRITICAL(&queue_mux);
    return true;
}

static bool is_duplicate_mac(const char* mac, uint32_t now_ms) {
    size_t limit = dedupe_count < DEDUPE_CACHE_CAPACITY ? dedupe_count : DEDUPE_CACHE_CAPACITY;
    for (size_t i = 0; i < limit; ++i) {
        const DedupeDevice& entry = dedupe_cache[i];
        if (now_ms - entry.timestamp <= 30000) {
            if (strncmp(entry.mac, mac, sizeof(entry.mac)) == 0) {
                return true;
            }
        }
    }
    return false;
}

static void add_dedupe_entry(const char* mac, uint32_t now_ms) {
    DedupeDevice& entry = dedupe_cache[dedupe_write_idx];
    copy_field(entry.mac, sizeof(entry.mac), mac);
    entry.timestamp = now_ms;
    
    dedupe_write_idx = (dedupe_write_idx + 1) % DEDUPE_CACHE_CAPACITY;
    if (dedupe_count < DEDUPE_CACHE_CAPACITY) {
        dedupe_count++;
    }
}

static void process_match(const MatchEvent& evt) {
    uint32_t now_ms = millis();

    if (is_duplicate_mac(evt.mac, now_ms)) {
        return;
    }
    add_dedupe_entry(evt.mac, now_ms);

    String mac(evt.mac);
    String ssid(evt.ssid);
    String type(evt.type);
    String reason(evt.reason);

    bool logged = storage_log_device(mac, ssid, evt.rssi, evt.channel, type, reason);
    if (logged) {
        if (type.startsWith("WIFI")) {
            wifi_logged_count++;
        } else if (type == "BLE") {
            ble_logged_count++;
        }
    }

    extern void trigger_match_indicator();
    trigger_match_indicator();

    ui_log_match(mac, ssid, evt.rssi, type, reason);
}

void match_queue_process() {
    MatchEvent evt;
    while (queue_pop(&evt)) {
        process_match(evt);
    }
}

void match_queue_clear_dedupe() {
    dedupe_count = 0;
    dedupe_write_idx = 0;
}

void match_queue_reset_stats() {
    wifi_logged_count = 0;
    ble_logged_count = 0;
    match_queue_clear_dedupe();
}
