#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <Arduino.h>
#include "config.h"
#include "EventManager.h"
#include "AudioManager.h"
#include "StorageManager.h"
#include "SystemLog.h"

class Scheduler {
public:
    Scheduler(EventManager &em, AudioManager &am);

    void update(uint8_t hour,
                uint8_t minute,
                uint8_t weekday,
                uint16_t year = 0,
                uint8_t month = 0,
                uint8_t day = 0);

    void setStorage(StorageManager *storage);
    void setLog(SystemLog *log);

    void pause();
    void resume();
    bool isPaused();
    bool isHolidayToday();

private:
    EventManager &eventManager;
    AudioManager &audio;
    StorageManager *storagePtr;
    SystemLog *logPtr;
    unsigned long lastCheck;
    bool paused;
    bool _holidayToday;
    uint8_t _lastHolidayCheckDay;

    // Duration auto-stop
    bool _durationActive;
    unsigned long _durationStartMs;
    uint16_t _durationLimitMs;
    uint16_t _durationEventId;
};

#endif