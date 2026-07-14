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

enum TransferState {
    TRANSFER_STATE_IDLE = 0,
    TRANSFER_STATE_START,
    TRANSFER_STATE_SENDING,
    TRANSFER_STATE_END
};

static TransferState transfer_state = TRANSFER_STATE_IDLE;
static File transfer_file;
static size_t transfer_total_size = 0;
static size_t transfer_sent_bytes = 0;
static uint32_t transfer_last_chunk_ms = 0;
static constexpr uint32_t CHUNK_INTERVAL_MS = 15;

static void reset_transfer_state() {
    if (transfer_file) {
        transfer_file.close();
    }
    transfer_state = TRANSFER_STATE_IDLE;
    transfer_sent_bytes = 0;
    transfer_total_size = 0;
}

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
    // 1. Detect disconnect and clean up
    if (!ble_transfer_active || !ble_transfer_connected) {
        if (transfer_state != TRANSFER_STATE_IDLE) {
            reset_transfer_state();
            Serial.println("[BLE_TX] Connection lost, reset transfer state.");
        }
        return;
    }
    
    // 2. Handle ping
    if (ping_requested) {
        ping_requested = false;
        pTxCharacteristic->setValue("PONG\n");
        pTxCharacteristic->notify();
        Serial.println("[BLE_TX] Sent PONG");
    }
    
    // 3. Handle log clear
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
    
    // 4. Handle log sending trigger
    if (send_logs_requested) {
        send_logs_requested = false;
        if (transfer_state == TRANSFER_STATE_IDLE) {
            transfer_state = TRANSFER_STATE_START;
            Serial.println("[BLE_TX] Starting log file streaming (non-blocking)...");
        }
    }
    
    // 5. Run the streaming state machine
    switch (transfer_state) {
        case TRANSFER_STATE_IDLE:
            break;
            
        case TRANSFER_STATE_START: {
            if (!LittleFS.exists("/logs.csv")) {
                pTxCharacteristic->setValue("[START:logs.csv:0]\n");
                pTxCharacteristic->notify();
                transfer_state = TRANSFER_STATE_END; // Skip directly to end
                transfer_last_chunk_ms = millis();
                Serial.println("[BLE_TX] No log file exists. Sent empty start.");
                break;
            }
            
            transfer_file = LittleFS.open("/logs.csv", "r");
            if (!transfer_file) {
                pTxCharacteristic->setValue("ERROR_OPEN_FAILED\n");
                pTxCharacteristic->notify();
                transfer_state = TRANSFER_STATE_IDLE;
                Serial.println("[BLE_TX] Failed to open logs.csv for streaming.");
                break;
            }
            
            transfer_total_size = transfer_file.size();
            char header[50];
            snprintf(header, sizeof(header), "[START:logs.csv:%zu]\n", transfer_total_size);
            pTxCharacteristic->setValue((uint8_t*)header, strlen(header));
            pTxCharacteristic->notify();
            
            transfer_sent_bytes = 0;
            ble_transfer_progress = 0;
            // Delay 100ms non-blockingly before sending the first chunk
            transfer_last_chunk_ms = millis() + 85; 
            transfer_state = TRANSFER_STATE_SENDING;
            break;
        }
        
        case TRANSFER_STATE_SENDING: {
            // Check chunk interval timer (15ms)
            if (millis() - transfer_last_chunk_ms < CHUNK_INTERVAL_MS) {
                break; 
            }
            
            if (transfer_file && transfer_file.available()) {
                uint8_t buffer[200];
                int to_read = transfer_file.available();
                if (to_read > 200) to_read = 200;
                
                int bytes_read = transfer_file.read(buffer, to_read);
                if (bytes_read > 0) {
                    pTxCharacteristic->setValue(buffer, bytes_read);
                    pTxCharacteristic->notify();
                    transfer_sent_bytes += bytes_read;
                    if (transfer_total_size > 0) {
                        ble_transfer_progress = (transfer_sent_bytes * 100) / transfer_total_size;
                    }
                }
                transfer_last_chunk_ms = millis();
            } else {
                transfer_state = TRANSFER_STATE_END;
                // Wait 100ms non-blockingly before sending [END]
                transfer_last_chunk_ms = millis() + 85;
            }
            break;
        }
        
        case TRANSFER_STATE_END: {
            if (millis() - transfer_last_chunk_ms < CHUNK_INTERVAL_MS) {
                break;
            }
            
            pTxCharacteristic->setValue("[END]\n");
            pTxCharacteristic->notify();
            ble_transfer_progress = 100;
            
            reset_transfer_state();
            Serial.println("[BLE_TX] Log file streamed completely (non-blocking).");
            break;
        }
    }
}
