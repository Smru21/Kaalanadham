#include "SystemLog.h"
#include <cstdarg>

SystemLog::SystemLog()
    : _head(0), _count(0), _timeCb(nullptr)
{
    _mutex = xSemaphoreCreateMutex();
    memset(_entries, 0, sizeof(_entries));
}

void SystemLog::setTimeCallback(TimeCallback cb) {
    _timeCb = cb;
}

void SystemLog::log(const char* category, const char* message) {
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return;

    LogEntry &entry = _entries[_head];
    entry.timestamp = millis();

    // Get RTC time string if callback set
    if (_timeCb) {
        _timeCb(entry.timeStr, sizeof(entry.timeStr));
    } else {
        unsigned long s = millis() / 1000;
        snprintf(entry.timeStr, sizeof(entry.timeStr), "%02lu:%02lu:%02lu",
                 (s / 3600) % 24, (s / 60) % 60, s % 60);
    }

    strncpy(entry.category, category, sizeof(entry.category) - 1);
    entry.category[sizeof(entry.category) - 1] = '\0';

    strncpy(entry.message, message, MAX_LOG_MSG_LEN);
    entry.message[MAX_LOG_MSG_LEN] = '\0';

    _head = (_head + 1) % MAX_LOG_ENTRIES;
    if (_count < MAX_LOG_ENTRIES) _count++;

    xSemaphoreGive(_mutex);

    // Also print to Serial for debugging
    Serial.printf("[LOG][%s][%s] %s\n", entry.timeStr, category, message);
}

void SystemLog::logf(const char* category, const char* fmt, ...) {
    char buf[MAX_LOG_MSG_LEN + 1];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    log(category, buf);
}

uint8_t SystemLog::getCount() const {
    return _count;
}

LogEntry* SystemLog::getEntry(uint8_t index) const {
    // index 0 = newest entry
    if (index >= _count) return nullptr;
    int pos = ((int)_head - 1 - index + MAX_LOG_ENTRIES) % MAX_LOG_ENTRIES;
    return (LogEntry*)&_entries[pos];
}

void SystemLog::clear() {
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return;
    _head = 0;
    _count = 0;
    memset(_entries, 0, sizeof(_entries));
    xSemaphoreGive(_mutex);
}

String SystemLog::toJSON() const {
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(200)) != pdTRUE) {
        return "{\"count\":0,\"logs\":[]}";
    }

    String json = "{\"count\":";
    json += _count;
    json += ",\"logs\":[";

    for (uint8_t i = 0; i < _count; i++) {
        int pos = ((int)_head - 1 - i + MAX_LOG_ENTRIES) % MAX_LOG_ENTRIES;
        const LogEntry &e = _entries[pos];

        if (i > 0) json += ",";
        json += "{\"time\":\"";
        json += e.timeStr;
        json += "\",\"cat\":\"";
        json += e.category;
        json += "\",\"msg\":\"";

        // Escape quotes and backslashes in message
        for (int j = 0; e.message[j] != '\0'; j++) {
            char c = e.message[j];
            if (c == '"') json += "\\\"";
            else if (c == '\\') json += "\\\\";
            else if (c == '\n') json += "\\n";
            else json += c;
        }

        json += "\"}";
    }

    json += "]}";
    xSemaphoreGive(_mutex);
    return json;
}