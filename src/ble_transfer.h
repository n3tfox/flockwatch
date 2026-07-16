#pragma once
#include <Arduino.h>

extern bool ble_transfer_active;
extern volatile bool ble_transfer_connected;
extern int ble_transfer_progress; // 0-100 percentage

void ble_transfer_start();
void ble_transfer_stop();
void ble_transfer_tick();
