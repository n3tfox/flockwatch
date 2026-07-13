#include "ble_transfer.h"
#include "storage.h"
#include <NimBLEDevice.h>
#include <LittleFS.h>

bool ble_transfer_active = false;
bool ble_transfer_connected = false;
int ble_transfer_progress = 0;

static NimBLEServer* pServer = nullptr;
static NimBLECharacteristic* pTxCharacteristic = nullptr;
static NimBLECharacteristic* pRxCharacteristic = nullptr;

static bool send_logs_requested = false;
static bool clear_logs_requested = false;
static bool ping_requested = false;

// Custom NUS UUIDs
#define SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define RX_CHARACTERISTIC_UUID "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define TX_CHARACTERISTIC_UUID "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

class LogServerCallbacks: public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pServer) override {
        ble_transfer_connected = true;
        ble_transfer_progress = 0;
        Serial.println("[BLE_TX] PC connected.");
    }
    
    void onDisconnect(NimBLEServer* pServer) override {
        ble_transfer_connected = false;
        ble_transfer_progress = 0;
        send_logs_requested = false;
        Serial.println("[BLE_TX] PC disconnected.");
        // Restart advertising so client can reconnect
        NimBLEDevice::startAdvertising();
    }
};

class RxCallbacks: public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pCharacteristic) override {
        std::string rxValue = pCharacteristic->getValue();
        if (rxValue.length() > 0) {
            String cmd = String(rxValue.c_str());
            cmd.trim();
            Serial.printf("[BLE_TX] Command received: %s\n", cmd.c_str());
            
            if (cmd == "GET_LOGS") {
                send_logs_requested = true;
            } else if (cmd == "CLEAR_LOGS") {
                clear_logs_requested = true;
            } else if (cmd == "PING") {
                ping_requested = true;
            }
        }
    }
};

static LogServerCallbacks serverCallbacks;
static RxCallbacks rxCallbacks;

void ble_transfer_start() {
    if (ble_transfer_active) return;
    
    // Stop scanning first to free the radio
    ble_transfer_progress = 0;
    
    // Generate device name with last 2 bytes of MAC
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char name_buf[25];
    snprintf(name_buf, sizeof(name_buf), "FlockWatch-%02X%02X", mac[4], mac[5]);
    
    if (!NimBLEDevice::getInitialized()) {
        NimBLEDevice::init(name_buf);
    }
    
    pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(&serverCallbacks, false);
    
    NimBLEService* pService = pServer->createService(SERVICE_UUID);
    
    pTxCharacteristic = pService->createCharacteristic(
        TX_CHARACTERISTIC_UUID,
        NIMBLE_PROPERTY::NOTIFY
    );
    
    pRxCharacteristic = pService->createCharacteristic(
        RX_CHARACTERISTIC_UUID,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
    );
    pRxCharacteristic->setCallbacks(&rxCallbacks);
    
    pService->start();
    
    NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setName(name_buf);
    pAdvertising->start();
    
    ble_transfer_active = true;
    Serial.printf("[BLE_TX] BLE Transfer Server started as '%s'\n", name_buf);
}

void ble_transfer_stop() {
    if (!ble_transfer_active) return;
    
    if (pServer) {
        NimBLEDevice::getAdvertising()->stop();
        // Disconnect clients
        size_t peers = pServer->getConnectedCount();
        for (size_t i = 0; i < peers; i++) {
            pServer->disconnect(pServer->getPeerInfo(i).getConnHandle());
        }
        // Free resources
        NimBLEDevice::deinit(true);
    }
    
    pServer = nullptr;
    pTxCharacteristic = nullptr;
    pRxCharacteristic = nullptr;
    ble_transfer_active = false;
    ble_transfer_connected = false;
    ble_transfer_progress = 0;
    send_logs_requested = false;
    clear_logs_requested = false;
    ping_requested = false;
    
    Serial.println("[BLE_TX] BLE Transfer Server stopped.");
}

void ble_transfer_tick() {
    if (!ble_transfer_active || !ble_transfer_connected) return;
    
    if (ping_requested) {
        ping_requested = false;
        pTxCharacteristic->setValue("PONG\n");
        pTxCharacteristic->notify();
        Serial.println("[BLE_TX] Sent PONG");
    }
    
    if (clear_logs_requested) {
        clear_logs_requested = false;
        bool res = storage_clear_logs();
        if (res) {
            pTxCharacteristic->setValue("LOGS_CLEARED\n");
        } else {
            pTxCharacteristic->setValue("ERROR_CLEAR_FAILED\n");
        }
        pTxCharacteristic->notify();
        Serial.println("[BLE_TX] Cleared logs and acknowledged");
    }
    
    if (send_logs_requested) {
        send_logs_requested = false;
        Serial.println("[BLE_TX] Starting log file streaming...");
        
        if (!LittleFS.exists("/logs.csv")) {
            pTxCharacteristic->setValue("[START:logs.csv:0]\n");
            pTxCharacteristic->notify();
            delay(50);
            pTxCharacteristic->setValue("[END]\n");
            pTxCharacteristic->notify();
            Serial.println("[BLE_TX] No log file exists. Sent empty.");
            return;
        }
        
        File file = LittleFS.open("/logs.csv", "r");
        if (!file) {
            pTxCharacteristic->setValue("ERROR_OPEN_FAILED\n");
            pTxCharacteristic->notify();
            return;
        }
        
        size_t total_size = file.size();
        char header[50];
        snprintf(header, sizeof(header), "[START:logs.csv:%zu]\n", total_size);
        pTxCharacteristic->setValue((uint8_t*)header, strlen(header));
        pTxCharacteristic->notify();
        delay(100); // Wait for PC to parse header
        
        size_t sent_bytes = 0;
        uint8_t buffer[200];
        
        while (file.available() && ble_transfer_connected) {
            int to_read = file.available();
            if (to_read > 200) to_read = 200;
            
            int bytes_read = file.read(buffer, to_read);
            if (bytes_read <= 0) break;
            
            pTxCharacteristic->setValue(buffer, bytes_read);
            pTxCharacteristic->notify();
            
            sent_bytes += bytes_read;
            ble_transfer_progress = (sent_bytes * 100) / total_size;
            
            // Wait 15ms to allow BLE transmission queue clearance
            delay(15); 
        }
        
        file.close();
        
        if (ble_transfer_connected) {
            delay(100); // Let last data packet clear
            pTxCharacteristic->setValue("[END]\n");
            pTxCharacteristic->notify();
            ble_transfer_progress = 100;
            Serial.println("[BLE_TX] Log file streamed completely.");
        } else {
            Serial.println("[BLE_TX] Transfer aborted due to disconnection.");
        }
    }
}
