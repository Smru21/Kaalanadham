#ifndef EVENT_MANAGER_H
#define EVENT_MANAGER_H

#include "Event.h"

class EventManager {
public:
    EventManager();

    uint16_t addEvent(uint8_t hour,
                      uint8_t minute,
                      uint8_t weekdayMask,
                      uint8_t folder,
                      uint8_t track,
                      const char* name = "",
                      uint16_t duration = 0,
                      uint8_t eventType = EVENT_TYPE_FIXED,
                      uint16_t intervalMinutes = 0,
                      uint16_t endMinute = 0);

    // ── One-time event add ──────────────────────────────── // ← NEW
    uint16_t addOneTimeEvent(uint8_t hour,                    // ← NEW
                             uint8_t minute,                  // ← NEW
                             uint16_t year,                   // ← NEW
                             uint8_t month,                   // ← NEW
                             uint8_t day,                     // ← NEW
                             uint8_t track,                   // ← NEW
                             const char* name = "",           // ← NEW
                             uint16_t duration = 0);          // ← NEW

    bool deleteEvent(uint16_t id);
    bool toggleEvent(uint16_t id);
    Event* getEventById(uint16_t id);
    Event* getEvents();
    uint8_t getCount();
    uint16_t getNextId();

    void setState(uint16_t nextIdValue,
                  uint8_t count,
                  Event* loadedEvents);

    void clear();

private:
    Event*   _events;        // ← heap allocated in constructor
    uint8_t  eventCount;
    uint16_t nextId;

};

#endif