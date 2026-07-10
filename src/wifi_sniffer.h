#pragma once
#include <Arduino.h>

extern bool wifi_sniffer_running;
extern uint8_t wifi_sniffer_channel;
extern uint32_t wifi_packets_sniffed;

void wifi_sniffer_start();
void wifi_sniffer_stop();
void wifi_sniffer_hop();
