#ifndef EVENT_H
#define EVENT_H

#include <Arduino.h>
#include "config.h"

#define MAX_EVENT_NAME_LEN 15
#define EVENT_FORMAT_V2 0xA2
#define EVENT_FORMAT_V3 0xA3

// ── One-time event encoding ──────────────────────────────
// weekdayMask bit 7 = one-time flag
// folder      = day of month (1-31)
// intervalMinutes = (year-2024)*12 + (month-1)  → encodes year+month
// All other fields behave normally
#define ONETIME_FLAG     0x80   // bit 7 of weekdayMask      // ← NEW

class Event {
public:
    uint16_t id;
    uint8_t  hour;
    uint8_t  minute;
    uint8_t  weekdayMask;
    uint8_t  folder;
    uint8_t  track;
    bool     enabled;
    char     name[MAX_EVENT_NAME_LEN + 1];
    uint16_t duration;

    uint8_t  eventType;
    uint16_t intervalMinutes;
    uint16_t endMinute;

    // Runtime only (NOT stored)
    int8_t   lastTriggeredMinute;

    Event() {
        id = 0;
        hour = 0;
        minute = 0;
        weekdayMask = 0;
        folder = 0;
        track = 0;
        enabled = false;
        name[0] = '\0';
        duration = 0;
        eventType = EVENT_TYPE_FIXED;
        intervalMinutes = 0;
        endMinute = 0;
        lastTriggeredMinute = -1;
    }

    Event(uint16_t _id,
          uint8_t _hour,
          uint8_t _minute,
          uint8_t _weekdayMask,
          uint8_t _folder,
          uint8_t _track,
          const char* _name = "",
          uint16_t _duration = 0,
          uint8_t _eventType = EVENT_TYPE_FIXED,
          uint16_t _intervalMinutes = 0,
          uint16_t _endMinute = 0)
    {
        id = _id;
        hour = _hour;
        minute = _minute;
        weekdayMask = _weekdayMask;
        folder = _folder;
        track = _track;
        enabled = true;
        duration = _duration;
        eventType = _eventType;
        intervalMinutes = _intervalMinutes;
        endMinute = _endMinute;
        lastTriggeredMinute = -1;

        strncpy(name, _name, MAX_EVENT_NAME_LEN);
        name[MAX_EVENT_NAME_LEN] = '\0';
    }

    // ── One-time event helpers ──────────────────────────── // ← NEW

    bool isOneTime() const {                                   // ← NEW
        return (weekdayMask & ONETIME_FLAG) != 0;             // ← NEW
    }                                                          // ← NEW

    // Encode a calendar date into this event                  // ← NEW
    void setOneTimeDate(uint16_t year, uint8_t month,         // ← NEW
                        uint8_t day) {                         // ← NEW
        weekdayMask = ONETIME_FLAG;                            // ← NEW
        folder      = day;                                     // ← NEW
        // encode year+month into intervalMinutes              // ← NEW
        // year range 2024-2058 (34 years), month 1-12        // ← NEW
        uint16_t y = (year >= 2024) ? (year - 2024) : 0;     // ← NEW
        if (y > 34) y = 34;                                   // ← NEW
        intervalMinutes = y * 12 + (month - 1);               // ← NEW
        eventType = EVENT_TYPE_FIXED;                          // ← NEW
        endMinute = 0;                                         // ← NEW
    }                                                          // ← NEW

    // Decode stored date back to year/month/day              // ← NEW
    void getOneTimeDate(uint16_t &year, uint8_t &month,       // ← NEW
                        uint8_t &day) const {                  // ← NEW
        day   = folder;                                        // ← NEW
        uint16_t encoded = intervalMinutes;                    // ← NEW
        year  = 2024 + (encoded / 12);                        // ← NEW
        month = (encoded % 12) + 1;                           // ← NEW
    }                                                          // ← NEW

    // Check if this one-time event matches today             // ← NEW
    bool isOnTimeDate(uint16_t year, uint8_t month,           // ← NEW
                      uint8_t day) const {                     // ← NEW
        if (!isOneTime()) return false;                        // ← NEW
        uint16_t ey; uint8_t em, ed;                          // ← NEW
        getOneTimeDate(ey, em, ed);                            // ← NEW
        return (ey == year && em == month && ed == day);      // ← NEW
    }                                                          // ← NEW

    // ── Existing helpers (unchanged) ───────────────────────

    bool isActiveToday(uint8_t today) {
        if (isOneTime()) return false;  // ← CHANGED: one-time uses date not weekday
        return weekdayMask & (1 << today);
    }

    bool hasName() {
        return name[0] != '\0';
    }

    bool isInterval() {
        if (isOneTime()) return false;  // ← CHANGED: one-time is never interval
        return eventType == EVENT_TYPE_INTERVAL;
    }

    uint16_t startMinute() {
        return (uint16_t)hour * 60 + minute;
    }

    bool isIntervalTrigger(uint8_t nowHour, uint8_t nowMinute) {
        if (isOneTime()) return false;                         // ← NEW guard
        if (eventType != EVENT_TYPE_INTERVAL) return false;
        if (intervalMinutes == 0) return false;

        uint16_t nowMin = (uint16_t)nowHour * 60 + nowMinute;
        uint16_t startMin = startMinute();

        if (nowMin < startMin) return false;
        if (endMinute > 0 && nowMin > endMinute) return false;

        uint16_t elapsed = nowMin - startMin;
        return (elapsed % intervalMinutes) == 0;
    }

    uint16_t nextTriggerAfter(uint16_t nowMin) {
        if (isOneTime()) {                                     // ← NEW guard
            uint16_t evMin = startMinute();
            return (evMin > nowMin) ? evMin : 9999;
        }
        if (eventType != EVENT_TYPE_INTERVAL || intervalMinutes == 0) {
            uint16_t evMin = startMinute();
            return (evMin > nowMin) ? evMin : 9999;
        }

        uint16_t startMin = this->startMinute();
        uint16_t endMin = (endMinute > 0) ? endMinute : 1439;

        if (nowMin >= endMin) return 9999;

        uint16_t candidate = startMin;
        while (candidate <= nowMin && candidate <= endMin) {
            candidate += intervalMinutes;
        }
        return (candidate <= endMin) ? candidate : 9999;
    }

    String durationStr() {
        if (duration == 0) return "Full";
        if (duration < 60) return String(duration) + "s";
        if (duration % 60 == 0) return String(duration / 60) + "m";
        return String(duration / 60) + "m" + String(duration % 60) + "s";
    }

    String intervalStr() {
        if (isOneTime()) {                                     // ← NEW
            uint16_t y; uint8_t m, d;                         // ← NEW
            getOneTimeDate(y, m, d);                           // ← NEW
            char buf[12];                                      // ← NEW
            snprintf(buf, sizeof(buf), "%02d/%02d/%04d",      // ← NEW
                     d, m, y);                                 // ← NEW
            return String(buf);                                // ← NEW
        }                                                      // ← NEW
        if (eventType != EVENT_TYPE_INTERVAL) return "Fixed";
        if (intervalMinutes == 0) return "?";
        if (intervalMinutes < 60) return "Every " + String(intervalMinutes) + "min";
        if (intervalMinutes % 60 == 0) return "Every " + String(intervalMinutes / 60) + "h";
        return "Every " + String(intervalMinutes / 60) + "h" +
               String(intervalMinutes % 60) + "m";
    }

    String endTimeStr() {
        if (isOneTime()) return "Once";                        // ← NEW
        if (endMinute == 0) return "None";
        uint8_t h = endMinute / 60;
        uint8_t m = endMinute % 60;
        char buf[6];
        snprintf(buf, sizeof(buf), "%02d:%02d", h, m);
        return String(buf);
    }

    // One-time display helper                                 // ← NEW
    String onTimeDateStr() const {                             // ← NEW
        if (!isOneTime()) return "";                           // ← NEW
        uint16_t y; uint8_t m, d;                             // ← NEW
        getOneTimeDate(y, m, d);                               // ← NEW
        char buf[12];                                          // ← NEW
        snprintf(buf, sizeof(buf), "%04d-%02d-%02d", y, m, d);// ← NEW
        return String(buf);                                    // ← NEW
    }                                                          // ← NEW
};

#endif