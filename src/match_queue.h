#pragma once

#include <Arduino.h>

// Fixed-size match event for ISR/callback-safe queuing (no heap allocs on push).
struct MatchEvent {
    char mac[18];
    char ssid[33];
    char type[16];
    char reason[32];
    int rssi;
    int channel;
};

void match_queue_init();
bool match_queue_submit(const char* mac, const char* ssid, int rssi, int channel,
                        const char* type, const char* reason);
void match_queue_process();
void match_queue_clear_dedupe();
void match_queue_reset_stats();

extern uint32_t wifi_logged_count;
extern uint32_t ble_logged_count;
