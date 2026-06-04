#ifndef STORAGE_MANAGER_H
#define STORAGE_MANAGER_H
#define PASSWORD_FILE  "/password.txt"
#define TEMPLATES_INDEX "/templates.txt"
#define SOUNDS_FILE    "/sounds.txt"
#define THEME_FILE     "/theme.txt"
#define HOLIDAYS_FILE  "/holidays.txt"

#include <Arduino.h>
#include <LittleFS.h>
#include "Event.h"

// V1 event struct — for binary migration from old format
// This MUST match the old Event layout exactly (without duration field)
#pragma pack(push, 1)
struct EventV1 {
    uint16_t id;
    uint8_t hour;
    uint8_t minute;
    uint8_t weekdayMask;
    uint8_t folder;
    uint8_t track;
    bool enabled;
    char name[MAX_EVENT_NAME_LEN + 1];
    int8_t lastTriggeredMinute;
};
#pragma pack(pop)

// V2 event struct — for binary migration from v2 format (has duration, no interval)
#pragma pack(push, 1)
struct EventV2 {
    uint16_t id;
    uint8_t  hour;
    uint8_t  minute;
    uint8_t  weekdayMask;
    uint8_t  folder;
    uint8_t  track;
    bool     enabled;
    char     name[MAX_EVENT_NAME_LEN + 1];
    uint16_t duration;
    int8_t   lastTriggeredMinute;
};
#pragma pack(pop)

#define EVENTS_FILE      "/events.bin"
#define SCHOOL_NAME_FILE "/schoolname.txt"

class StorageManager {
public:
    bool begin();
    void saveEvents(uint16_t nextId,
                    uint8_t count,
                    Event* events);
    bool loadEvents(uint16_t &nextId,
                    uint8_t &count,
                    Event* events);
    void clearEvents();

    // School name
    void saveSchoolName(const char* name);
    bool loadSchoolName(char* name, size_t maxLen);

    // Sound names
    void saveSounds(const String &data);
    String loadSoundsJSON();
    String getSoundName(uint16_t track);
    void loadSoundsCache();
    uint16_t getSoundCount();

    // Theme
    void saveTheme(uint8_t theme);
    uint8_t loadTheme();

    // Colors
    void saveColors(uint8_t nR, uint8_t nG, uint8_t nB,
                    uint8_t cR, uint8_t cG, uint8_t cB);
    bool loadColors(uint8_t &nR, uint8_t &nG, uint8_t &nB,
                    uint8_t &cR, uint8_t &cG, uint8_t &cB);

    // Custom background
    bool saveCustomBg(uint8_t *data, size_t len, size_t offset, size_t total);
    bool hasCustomBg();

    // Holidays
    bool addHoliday(uint16_t year, uint8_t month, uint8_t day, const char* name = "");
    bool removeHoliday(const char* dateStr);
    bool isHoliday(uint16_t year, uint8_t month, uint8_t day);
    String loadHolidaysJSON();
    uint8_t getHolidayCount();
    void clearHolidays();

    // Password
    void savePassword(const char* password);
    bool loadPassword(char* password, size_t maxLen);

    // Templates
    bool saveTemplate(uint8_t id, const char* name,
                      uint16_t nextId, uint8_t count, Event* events);
    bool loadTemplate(uint8_t id, uint16_t &nextId,
                      uint8_t &count, Event* events);
    bool deleteTemplate(uint8_t id);
    String loadTemplatesJSON();
    uint8_t getTemplateCount();
    uint8_t getNextTemplateId();

    // WiFi credentials
    void saveWiFiCredentials(const char* ssid, const char* password);
    bool loadWiFiCredentials(char* ssid, size_t ssidLen, char* password, size_t passLen);
    bool hasWiFiCredentials();
    void clearWiFiCredentials();

    // WiFi mode preference (0=AP, 1=STA)
    void saveWiFiMode(uint8_t mode);
    uint8_t loadWiFiMode();

    // Setup wizard
    bool isFirstBoot();
    void markSetupDone();
    void resetSetup();

    // Environment profile
    void saveProfile(uint8_t profile);
    uint8_t loadProfile();

private:
    // Sound names cache (loaded into RAM on boot)
    String _soundsCache;
    bool _soundsCacheValid = false;
    uint16_t _soundsCount = 0;

    // Holiday cache (avoid re-reading file every second)
    uint16_t _holCacheYear  = 0;
    uint8_t  _holCacheMonth = 0;
    uint8_t  _holCacheDay   = 0;
    bool     _holCacheResult = false;
    bool     _holCacheValid  = false;
};

#endif