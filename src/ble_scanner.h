#pragma once
#include <Arduino.h>

extern bool ble_scanner_running;
extern uint32_t ble_packets_scanned;

void ble_scanner_init();
void ble_scanner_start(int duration_secs);
void ble_scanner_stop();
void ble_scanner_tick();
