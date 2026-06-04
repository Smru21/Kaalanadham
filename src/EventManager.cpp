#include "EventManager.h"

EventManager::EventManager() {
    _events    = new Event[MAX_EVENTS];
    eventCount = 0;
    nextId     = 1;
    Serial.printf("[Events] Heap allocated %d event slots (%d bytes)\n",
                  MAX_EVENTS, (int)(MAX_EVENTS * sizeof(Event)));
}

uint16_t EventManager::addEvent(uint8_t hour,
                                uint8_t minute,
                                uint8_t weekdayMask,
                                uint8_t folder,
                                uint8_t track,
                                const char* name,
                                uint16_t duration,
                                uint8_t eventType,
                                uint16_t intervalMinutes,
                                uint16_t endMinute)
{
    if (eventCount >= MAX_EVENTS) {
        Serial.println("[Events] Max events reached!");
        return 0;
    }

    _events[eventCount] = Event(nextId, hour, minute, weekdayMask,
                                folder, track, name, duration,
                                eventType, intervalMinutes, endMinute);
    eventCount++;

    if (eventType == EVENT_TYPE_INTERVAL) {
        Serial.printf("[Events] Added interval #%d: start %02d:%02d every %dmin"
                      " end %d F%d/T%d days=0x%02X dur=%d name=%s\n",
                      nextId, hour, minute, intervalMinutes, endMinute,
                      folder, track, weekdayMask, duration,
                      name[0] ? name : "(none)");
    } else {
        Serial.printf("[Events] Added event #%d: %02d:%02d F%d/T%d"
                      " days=0x%02X dur=%d name=%s\n",
                      nextId, hour, minute, folder, track,
                      weekdayMask, duration,
                      name[0] ? name : "(none)");
    }

    return nextId++;
}

uint16_t EventManager::addOneTimeEvent(uint8_t hour,
                                        uint8_t minute,
                                        uint16_t year,
                                        uint8_t month,
                                        uint8_t day,
                                        uint8_t track,
                                        const char* name,
                                        uint16_t duration)
{
    if (eventCount >= MAX_EVENTS) {
        Serial.println("[Events] Max events reached!");
        return 0;
    }

    Event ev;
    ev.id       = nextId;
    ev.hour     = hour;
    ev.minute   = minute;
    ev.track    = track;
    ev.duration = duration;
    ev.enabled  = true;
    ev.endMinute = 0;
    ev.eventType = EVENT_TYPE_FIXED;
    ev.lastTriggeredMinute = -1;
    strncpy(ev.name, name, MAX_EVENT_NAME_LEN);
    ev.name[MAX_EVENT_NAME_LEN] = '\0';

    ev.setOneTimeDate(year, month, day);

    _events[eventCount] = ev;
    eventCount++;

    Serial.printf("[Events] Added one-time #%d: %04d-%02d-%02d"
                  " %02d:%02d T%d dur=%d name=%s\n",
                  nextId, year, month, day,
                  hour, minute, track, duration,
                  name[0] ? name : "(none)");

    return nextId++;
}

bool EventManager::deleteEvent(uint16_t id) {
    for (int i = 0; i < eventCount; i++) {
        if (_events[i].id == id) {
            for (int j = i; j < eventCount - 1; j++) {
                _events[j] = _events[j + 1];
            }
            eventCount--;
            Serial.printf("[Events] Deleted event #%d\n", id);
            return true;
        }
    }
    Serial.printf("[Events] Event #%d not found\n", id);
    return false;
}

bool EventManager::toggleEvent(uint16_t id) {
    Event* ev = getEventById(id);
    if (!ev) {
        Serial.printf("[Events] Event #%d not found for toggle\n", id);
        return false;
    }
    ev->enabled = !ev->enabled;
    Serial.printf("[Events] Toggled event #%d: %s\n",
                  id, ev->enabled ? "ON" : "OFF");
    return true;
}

Event* EventManager::getEventById(uint16_t id) {
    for (int i = 0; i < eventCount; i++) {
        if (_events[i].id == id) return &_events[i];
    }
    return nullptr;
}

Event* EventManager::getEvents() {
    return _events;
}

uint8_t EventManager::getCount() {
    return eventCount;
}

uint16_t EventManager::getNextId() {
    return nextId;
}

void EventManager::setState(uint16_t nextIdValue,
                            uint8_t count,
                            Event* loadedEvents)
{
    nextId     = nextIdValue;
    eventCount = count;
    for (int i = 0; i < count; i++) {
        _events[i] = loadedEvents[i];
    }
    Serial.printf("[Events] Loaded %d events. Next ID: %d\n",
                  count, nextId);
}

void EventManager::clear() {
    eventCount = 0;
    nextId     = 1;
    Serial.println("[Events] All events cleared.");
}