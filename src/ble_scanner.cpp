#include "ble_scanner.h"
#include <NimBLEDevice.h>

bool ble_scanner_running = false;
uint32_t ble_packets_scanned = 0;

static NimBLEScan* pBLEScan = nullptr;

// Extern match processor in main
extern void process_device_match(const String& mac, const String& ssid, int rssi, int channel, const String& type, const String& reason);

class FlockAdvertisedDeviceCallbacks: public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice* advertisedDevice) override {
        ble_packets_scanned++;
        
        bool is_match = false;
        String reason = "";
        String name = advertisedDevice->haveName() ? advertisedDevice->getName().c_str() : "";
        String mac = advertisedDevice->getAddress().toString().c_str();
        mac.toUpperCase();
        
        // 1. Check for Flock Manufacturer ID 0x09C8 (registered to XUNTONG)
        if (advertisedDevice->haveManufacturerData()) {
            std::string mfg_data = advertisedDevice->getManufacturerData();
            if (mfg_data.length() >= 2) {
                uint16_t mfg_id = (uint8_t)mfg_data[0] | ((uint8_t)mfg_data[1] << 8); // Company IDs are Little Endian in BLE
                if (mfg_id == 0x09C8) {
                    is_match = true;
                    reason = "BLE_FLOCK_MFG_MATCH";
                }
            }
        }
        
        // 2. Check for Flock surveillance name heuristics
        if (!is_match && name.length() > 0) {
            String lower_name = name;
            lower_name.toLowerCase();
            if (lower_name.indexOf("flock") != -1 ||
                lower_name.indexOf("fs ext battery") != -1 ||
                lower_name.indexOf("penguin") != -1 ||
                lower_name.indexOf("pigvision") != -1) {
                is_match = true;
                reason = "BLE_FLOCK_NAME_MATCH";
            }
        }
        
        // 3. Check for SoundThinking/ShotSpotter Raven service UUID signatures
        if (!is_match) {
            // Check primary service UUID if present
            if (advertisedDevice->haveServiceUUID()) {
                NimBLEUUID uuid = advertisedDevice->getServiceUUID();
                String uuid_str = uuid.toString().c_str();
                uuid_str.toLowerCase();
                
                if (uuid_str.startsWith("00003100") || uuid_str.startsWith("00003200") ||
                    uuid_str.startsWith("00003300") || uuid_str.startsWith("00003400") ||
                    uuid_str.startsWith("00003500") || uuid_str.startsWith("00001809") ||
                    uuid_str.startsWith("00001819") || uuid_str.startsWith("0000180a")) {
                    is_match = true;
                    reason = "BLE_RAVEN_SERVICE_MATCH";
                }
            }
            
            // Check secondary/additional service UUIDs
            if (!is_match) {
                int service_count = advertisedDevice->getServiceUUIDCount();
                for (int i = 0; i < service_count; i++) {
                    NimBLEUUID uuid = advertisedDevice->getServiceUUID(i);
                    String uuid_str = uuid.toString().c_str();
                    uuid_str.toLowerCase();
                    
                    if (uuid_str.startsWith("00003100") || uuid_str.startsWith("00003200") ||
                        uuid_str.startsWith("00003300") || uuid_str.startsWith("00003400") ||
                        uuid_str.startsWith("00003500") || uuid_str.startsWith("00001809") ||
                        uuid_str.startsWith("00001819") || uuid_str.startsWith("0000180a")) {
                        is_match = true;
                        reason = "BLE_RAVEN_SERVICE_MATCH";
                        break;
                    }
                }
            }
        }
        
        if (is_match) {
            process_device_match(mac, name, advertisedDevice->getRSSI(), 0, "BLE", reason);
        }
    }
};

void ble_scanner_init() {
    if (!NimBLEDevice::getInitialized()) {
        NimBLEDevice::init("FlockWatch");
    }
    
    pBLEScan = NimBLEDevice::getScan();
    pBLEScan->setAdvertisedDeviceCallbacks(new FlockAdvertisedDeviceCallbacks(), true);
    pBLEScan->setActiveScan(true); // Active scan to request scan responses (names, mfg data)
    pBLEScan->setInterval(97);     // Standard interval/window
    pBLEScan->setWindow(49);
}

void ble_scanner_start(int duration_secs) {
    if (ble_scanner_running) return;
    
    ble_scanner_init();
    
    // Start scan in background (asynchronous)
    pBLEScan->start(duration_secs, nullptr, false);
    ble_scanner_running = true;
    Serial.println("[BLE] BLE scanner started.");
}

void ble_scanner_stop() {
    if (!ble_scanner_running) return;
    
    if (pBLEScan) {
        pBLEScan->stop();
    }
    ble_scanner_running = false;
    Serial.println("[BLE] BLE scanner stopped.");
}

void ble_scanner_tick() {
    // If scanning finished in background, restart it or update status
    if (ble_scanner_running && pBLEScan && !pBLEScan->isScanning()) {
        pBLEScan->start(5, nullptr, false);
    }
}
