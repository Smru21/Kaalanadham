#include "RTCManager.h"
#include <time.h>

bool RTCManager::begin() {

    Wire.begin(RTC_SDA, RTC_SCL);

    if (!rtc.begin()) {
        Serial.println("[RTC] DS1307 not found!");
        return false;
    }

    if (!rtc.isrunning()) {
        Serial.println("[RTC] Was stopped. Setting compile time.");
        rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }

    _lastNTPSync = 0;
    _ntpSynced = false;
    _ntpState = 0;
    _ntpStart = 0;

    Serial.println("[RTC] Initialized OK.");
    return true;
}

void RTCManager::getTime(uint8_t &hour,
                          uint8_t &minute,
                          uint8_t &weekday)
{
    DateTime now = rtc.now();
    hour    = now.hour();
    minute  = now.minute();
    weekday = now.dayOfTheWeek();
}

void RTCManager::getDateTime(uint16_t &year,
                              uint8_t &month,
                              uint8_t &day,
                              uint8_t &hour,
                              uint8_t &minute,
                              uint8_t &weekday)
{
    DateTime now = rtc.now();
    year    = now.year();
    month   = now.month();
    day     = now.day();
    hour    = now.hour();
    minute  = now.minute();
    weekday = now.dayOfTheWeek();
}

void RTCManager::setTime(uint16_t year,
                          uint8_t month,
                          uint8_t day,
                          uint8_t hour,
                          uint8_t minute,
                          uint8_t second)
{
    rtc.adjust(DateTime(year, month, day,
                        hour, minute, second));

    Serial.print("[RTC] Time set to: ");
    Serial.printf("%04d-%02d-%02d %02d:%02d:%02d\n",
                  year, month, day, hour, minute, second);
}

bool RTCManager::syncFromNTP(long gmtOffset, int daylightOffset) {
    Serial.println("[RTC] Attempting NTP sync...");
    
    configTime(gmtOffset, daylightOffset, NTP_SERVER_1, NTP_SERVER_2);
    
    // Non-blocking approach: yield to system tasks during wait
    struct tm timeinfo;
    unsigned long startAttempt = millis();
    bool success = false;
    
    while (millis() - startAttempt < NTP_SYNC_TIMEOUT) {
        if (getLocalTime(&timeinfo, 100)) {  // 100ms timeout per attempt
            success = true;
            break;
        }
        // Feed watchdog and yield to other tasks
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    if (!success) {
        Serial.println("[RTC] NTP sync timeout!");
        return false;
    }
    
    // Update DS1307 with NTP time
    rtc.adjust(DateTime(
        timeinfo.tm_year + 1900,
        timeinfo.tm_mon + 1,
        timeinfo.tm_mday,
        timeinfo.tm_hour,
        timeinfo.tm_min,
        timeinfo.tm_sec
    ));
    
    _lastNTPSync = millis();
    _ntpSynced = true;
    
    Serial.printf("[RTC] NTP synced: %04d-%02d-%02d %02d:%02d:%02d\n",
                  timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                  timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    return true;
}
unsigned long RTCManager::lastNTPSync() {
    return _lastNTPSync;
}

bool RTCManager::isNTPSynced() {
    return _ntpSynced;
}

void RTCManager::startNTPSync(long gmtOffset, int daylightOffset) {
    if (_ntpState != 0) return;  // already in progress
    Serial.println("[RTC] Starting non-blocking NTP sync...");
    configTime(gmtOffset, daylightOffset, NTP_SERVER_1, NTP_SERVER_2);
    _ntpState = 1;
    _ntpStart = millis();
}

bool RTCManager::updateNTPSync() {
    if (_ntpState != 1) return false;

    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 10)) {  // 10ms timeout — very short, non-blocking
        rtc.adjust(DateTime(
            timeinfo.tm_year + 1900,
            timeinfo.tm_mon + 1,
            timeinfo.tm_mday,
            timeinfo.tm_hour,
            timeinfo.tm_min,
            timeinfo.tm_sec
        ));
        _lastNTPSync = millis();
        _ntpSynced = true;
        _ntpState = 0;
        Serial.printf("[RTC] NTP synced: %04d-%02d-%02d %02d:%02d:%02d\n",
            timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
            timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        return true;
    }

    if (millis() - _ntpStart > NTP_SYNC_TIMEOUT) {
        _ntpState = 0;
        Serial.println("[RTC] NTP sync timeout (non-blocking)");
    }

    return false;
}