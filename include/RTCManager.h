#ifndef RTC_MANAGER_H
#define RTC_MANAGER_H

#include <Arduino.h>
#include <RTClib.h>
#include "config.h"

class RTCManager {
private:
    RTC_DS1307 rtc;
    unsigned long _lastNTPSync;
    bool _ntpSynced;
    uint8_t _ntpState;       // 0=idle, 1=waiting for result
    unsigned long _ntpStart;

public:
    bool begin();

    // Quick time fetch (for scheduler & clock display)
    void getTime(uint8_t &hour,
                 uint8_t &minute,
                 uint8_t &weekday);

    // Full date+time fetch (for date/time display screen)
    void getDateTime(uint16_t &year,
                     uint8_t &month,
                     uint8_t &day,
                     uint8_t &hour,
                     uint8_t &minute,
                     uint8_t &weekday);

    // Set RTC (from web interface or serial debug)
    void setTime(uint16_t year,
                 uint8_t month,
                 uint8_t day,
                 uint8_t hour,
                 uint8_t minute,
                 uint8_t second);

    // NTP sync
    bool syncFromNTP(long gmtOffset = NTP_GMT_OFFSET, int daylightOffset = NTP_DAYLIGHT_OFFSET);
    unsigned long lastNTPSync();
    bool isNTPSynced();
    void startNTPSync(long gmtOffset = NTP_GMT_OFFSET, int daylightOffset = NTP_DAYLIGHT_OFFSET);
    bool updateNTPSync();  // call from loop(), returns true when sync completes
};

#endif