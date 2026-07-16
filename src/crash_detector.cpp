#include "crash_detector.h"
#include <esp_system.h>
#include <LittleFS.h>
#include <sys/stat.h>

#define CRASH_MAGIC 0xDEADC0DE

// Structure to capture CPU registers and panic metadata
struct CrashLog {
    uint32_t magic;
    uint32_t reset_reason;
    char reason[64];
    uint32_t pc;
    uint32_t exccause;
    uint32_t excvaddr;
    uint32_t regs[16];
};

// Mirroring ESP-IDF panic_info_t first two fields (reason and frame)
struct CustomPanicInfo {
    const char *reason;
    void *frame;
};

// Allocate the structure in RTC memory (survives software restarts)
RTC_NOINIT_ATTR CrashLog rtc_crash_log;

// Linker wrapped panic handler
extern "C" void __real_esp_panic_handler(void *info);

extern "C" void __wrap_esp_panic_handler(void *info) {
    rtc_crash_log.magic = CRASH_MAGIC;
    rtc_crash_log.reset_reason = (uint32_t)esp_reset_reason();
    
    if (info) {
        CustomPanicInfo *p_info = (CustomPanicInfo*)info;
        if (p_info->reason) {
            int i = 0;
            while (i < 63 && p_info->reason[i] != '\0') {
                rtc_crash_log.reason[i] = p_info->reason[i];
                i++;
            }
            rtc_crash_log.reason[i] = '\0';
        } else {
            rtc_crash_log.reason[0] = '\0';
        }
        
        if (p_info->frame) {
            XtExcFrame *f = (XtExcFrame*)p_info->frame;
            rtc_crash_log.pc = f->pc;
            rtc_crash_log.exccause = f->exccause;
            rtc_crash_log.excvaddr = f->excvaddr;
            rtc_crash_log.regs[0] = f->a0;
            rtc_crash_log.regs[1] = f->a1;
            rtc_crash_log.regs[2] = f->a2;
            rtc_crash_log.regs[3] = f->a3;
            rtc_crash_log.regs[4] = f->a4;
            rtc_crash_log.regs[5] = f->a5;
            rtc_crash_log.regs[6] = f->a6;
            rtc_crash_log.regs[7] = f->a7;
            rtc_crash_log.regs[8] = f->a8;
            rtc_crash_log.regs[9] = f->a9;
            rtc_crash_log.regs[10] = f->a10;
            rtc_crash_log.regs[11] = f->a11;
            rtc_crash_log.regs[12] = f->a12;
            rtc_crash_log.regs[13] = f->a13;
            rtc_crash_log.regs[14] = f->a14;
            rtc_crash_log.regs[15] = f->a15;
        } else {
            rtc_crash_log.pc = 0;
            rtc_crash_log.exccause = 0;
            rtc_crash_log.excvaddr = 0;
            for (int i = 0; i < 16; i++) {
                rtc_crash_log.regs[i] = 0;
            }
        }
    } else {
        rtc_crash_log.reason[0] = '\0';
        rtc_crash_log.pc = 0;
        rtc_crash_log.exccause = 0;
        rtc_crash_log.excvaddr = 0;
        for (int i = 0; i < 16; i++) {
            rtc_crash_log.regs[i] = 0;
        }
    }
    
    // Call the original panic handler to let the ESP32 print output and restart
    __real_esp_panic_handler(info);
}

static const char* get_reset_reason_str(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_POWERON: return "Power-on reset";
        case ESP_RST_EXT:     return "External pin reset";
        case ESP_RST_SW:      return "Software reset";
        case ESP_RST_PANIC:   return "Software panic/exception reset";
        case ESP_RST_INT_WDT: return "Interrupt watchdog reset";
        case ESP_RST_TASK_WDT:return "Task watchdog reset";
        case ESP_RST_WDT:     return "Other watchdog reset";
        case ESP_RST_DEEPSLEEP: return "Deep sleep wake-up reset";
        case ESP_RST_BROWNOUT: return "Brownout reset";
        case ESP_RST_SDIO:    return "SDIO reset";
        default:              return "Unknown reset reason";
    }
}

void crash_detector_init() {
    esp_reset_reason_t reason = esp_reset_reason();
    bool panic_detected = (reason == ESP_RST_PANIC || reason == ESP_RST_INT_WDT || reason == ESP_RST_TASK_WDT);
    
    if (panic_detected || rtc_crash_log.magic == CRASH_MAGIC) {
        Serial.println("[CRASH] Crash detected from previous boot!");
        
        // Write details to LittleFS to survive full power cycles
        File file = LittleFS.open("/crash_log.txt", "w");
        if (file) {
            file.printf("=== CRASH LOG ===\n");
            file.printf("Reset Reason: %d (%s)\n", (int)reason, get_reset_reason_str(reason));
            if (rtc_crash_log.magic == CRASH_MAGIC) {
                file.printf("Panic Reason: %s\n", rtc_crash_log.reason[0] != '\0' ? rtc_crash_log.reason : "Unknown");
                file.printf("PC: 0x%08X\n", rtc_crash_log.pc);
                file.printf("ExcCause: %u\n", rtc_crash_log.exccause);
                file.printf("ExcVAddr: 0x%08X\n", rtc_crash_log.excvaddr);
                for (int i = 0; i < 16; i++) {
                    file.printf("A%d: 0x%08X\n", i, rtc_crash_log.regs[i]);
                }
            } else {
                file.printf("Panic handler details not captured in RTC memory (power cycle or corruption).\n");
            }
            file.printf("=================\n");
            file.close();
            Serial.println("[CRASH] Saved crash log to /crash_log.txt");
        } else {
            Serial.println("[CRASH] Failed to open /crash_log.txt for writing!");
        }
    }
    
    // Clear the magic so we don't duplicate logs on future normal boots
    rtc_crash_log.magic = 0;
}

void crash_detector_dump() {
    Serial.println("--- BEGIN CRASH LOG ---");
    struct stat st;
    if (stat("/littlefs/crash_log.txt", &st) == 0) {
        File file = LittleFS.open("/crash_log.txt", "r");
        if (file) {
            while (file.available()) {
                Serial.write(file.read());
            }
            file.close();
        } else {
            Serial.println("Error: Failed to open /crash_log.txt for reading.");
        }
    } else {
        Serial.println("No crash log found. System is healthy!");
    }
    Serial.println("--- END CRASH LOG ---");
}

void crash_detector_tick() {
    if (Serial.available()) {
        char c = Serial.read();
        if (c == 'D') {
            crash_detector_dump();
        }
    }
}
