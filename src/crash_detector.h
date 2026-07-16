#ifndef CRASH_DETECTOR_H
#define CRASH_DETECTOR_H

#include <Arduino.h>

/**
 * @brief Initialize crash detection. Checks reset reason and writes RTC dump to LittleFS.
 */
void crash_detector_init();

/**
 * @brief Periodically checks Serial for the symbol 'D' to trigger a crash log push.
 */
void crash_detector_tick();

/**
 * @brief Dumps the stored crash log from LittleFS over Serial.
 */
void crash_detector_dump();

#endif // CRASH_DETECTOR_H
