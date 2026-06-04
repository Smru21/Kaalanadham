#ifndef SYSTEM_LOG_H
#define SYSTEM_LOG_H

#include <Arduino.h>
#include "config.h"

struct LogEntry {
    uint32_t timestamp;                    // millis() at log time
    char     timeStr[20];                  // "23/06 14:35:07" from RTC
    char     category[8];                  // "BELL", "SCHED", etc.
    char     message[MAX_LOG_MSG_LEN + 1]; // Human-readable message
};

class SystemLog {
public:
    SystemLog();

    // Add a log entry
    void log(const char* category, const char* message);
    void logf(const char* category, const char* fmt, ...);

    // Set RTC time string callback (so log doesn't depend on RTCManager)
    typedef void (*TimeCallback)(char* buf, size_t len);
    void setTimeCallback(TimeCallback cb);

    // Access
    uint8_t   getCount() const;
    LogEntry* getEntry(uint8_t index) const;  // 0 = newest
    void      clear();

    // JSON export (newest first)
    String toJSON() const;

private:
    LogEntry          _entries[MAX_LOG_ENTRIES];
    uint8_t           _head;       // Next write position
    uint8_t           _count;      // Current number of entries
    TimeCallback      _timeCb;
    SemaphoreHandle_t _mutex;      // Thread safety for FreeRTOS
};

#endif