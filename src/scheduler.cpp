#include "Scheduler.h"

Scheduler::Scheduler(EventManager &em, AudioManager &am)
    : eventManager(em), audio(am)
{
    lastCheck = 0;
    paused = false;
    storagePtr = nullptr;
    logPtr = nullptr;
    _holidayToday = false;
    _lastHolidayCheckDay = 255;
    _durationActive = false;
    _durationStartMs = 0;
    _durationLimitMs = 0;
    _durationEventId = 0;
}

void Scheduler::setStorage(StorageManager *storage) {
    storagePtr = storage;
}

void Scheduler::setLog(SystemLog *log) {
    logPtr = log;
}

void Scheduler::update(uint8_t hour,
                       uint8_t minute,
                       uint8_t weekday,
                       uint16_t year,
                       uint8_t month,
                       uint8_t day)
{
    if (paused) return;

    // ── Duration auto-stop ────────────────────────────────
    if (_durationActive) {
        if (millis() - _durationStartMs >= _durationLimitMs) {
            audio.stopDfPlayer();
            Serial.printf("[Scheduler] Duration expired for event #%d (%dms)\n",
                          _durationEventId, _durationLimitMs);
            if (logPtr) logPtr->logf(LOG_CAT_BELL,
                "Bell auto-stopped after %ds (event #%d)",
                _durationLimitMs / 1000, _durationEventId);
            _durationActive = false;
        }
    }

    if (millis() - lastCheck < SCHEDULER_CHECK_MS) return;
    lastCheck = millis();

    // ── Holiday check ─────────────────────────────────────
    if (storagePtr && day != _lastHolidayCheckDay && year > 0) {
        _holidayToday = storagePtr->isHoliday(year, month, day);
        _lastHolidayCheckDay = day;
        if (_holidayToday) {
            Serial.printf("[Scheduler] Today %04d-%02d-%02d is a HOLIDAY\n",
                          year, month, day);
            if (logPtr) logPtr->logf(LOG_CAT_BELL,
                "Holiday today %04d-%02d-%02d - all bells skipped",
                year, month, day);
        }
    }

    if (_holidayToday) return;

    Event* events = eventManager.getEvents();
    uint8_t count = eventManager.getCount();

    for (int i = 0; i < count; i++) {
        Event &e = events[i];

        if (!e.enabled) continue;

        bool shouldTrigger = false;

        // ── One-time event check ──────────────────────── // ← NEW
        if (e.isOneTime()) {                                // ← NEW
            // Only fire on the exact calendar date         // ← NEW
            if (!e.isOnTimeDate(year, month, day)) continue;// ← NEW
            // Fire at exact time                           // ← NEW
            shouldTrigger = (e.hour == hour &&             // ← NEW
                             e.minute == minute);          // ← NEW
        }                                                   // ← NEW
        // ── Regular event check ──────────────────────────
        else {
            if (!e.isActiveToday(weekday)) continue;

            if (e.isInterval()) {
                shouldTrigger = e.isIntervalTrigger(hour, minute);
            } else {
                shouldTrigger = (e.hour == hour && e.minute == minute);
            }
        }

        if (shouldTrigger) {
            if (e.lastTriggeredMinute != minute) {
                audio.playTrack(e.track);
                e.lastTriggeredMinute = minute;

                // ── Auto-disable one-time after firing ── // ← NEW
                if (e.isOneTime()) {                        // ← NEW
                    e.enabled = false;                      // ← NEW
                    // Save immediately so it won't fire    // ← NEW
                    // again even after reboot              // ← NEW
                    if (storagePtr) {                       // ← NEW
                        storagePtr->saveEvents(             // ← NEW
                            eventManager.getNextId(),       // ← NEW
                            eventManager.getCount(),        // ← NEW
                            eventManager.getEvents());      // ← NEW
                    }                                       // ← NEW
                    Serial.printf("[Scheduler] One-time event #%d fired"
                                  " and disabled.\n", e.id);// ← NEW
                    if (logPtr) logPtr->logf(LOG_CAT_BELL, // ← NEW
                        "One-time: %s at %02d:%02d (track %d)",
                        e.name, hour, minute, e.track);    // ← NEW
                }
                // ── Duration tracking ─────────────────────
                else if (e.duration > 0) {
                    _durationActive  = true;
                    _durationStartMs = millis();
                    _durationLimitMs = (uint32_t)e.duration * 1000;
                    _durationEventId = e.id;

                    if (e.isInterval()) {
                        Serial.printf("[Scheduler] Interval event #%d:"
                                      " track=%d duration=%ds (%s)\n",
                                      e.id, e.track, e.duration,
                                      e.intervalStr().c_str());
                        if (logPtr) logPtr->logf(LOG_CAT_BELL,
                            "Interval: %s at %02d:%02d (track %d, %ds)",
                            e.name, hour, minute, e.track, e.duration);
                    } else {
                        Serial.printf("[Scheduler] Triggered event #%d:"
                                      " track=%d duration=%ds\n",
                                      e.id, e.track, e.duration);
                        if (logPtr) logPtr->logf(LOG_CAT_BELL,
                            "Scheduled: %s at %02d:%02d (track %d, %ds)",
                            e.name, hour, minute, e.track, e.duration);
                    }
                } else {
                    _durationActive = false;

                    if (e.isInterval()) {
                        Serial.printf("[Scheduler] Interval event #%d:"
                                      " track=%d (full)\n",
                                      e.id, e.track);
                        if (logPtr) logPtr->logf(LOG_CAT_BELL,
                            "Interval: %s at %02d:%02d (track %d, full)",
                            e.name, hour, minute, e.track);
                    } else {
                        Serial.printf("[Scheduler] Triggered event #%d:"
                                      " track=%d (full)\n",
                                      e.id, e.track);
                        if (logPtr) logPtr->logf(LOG_CAT_BELL,
                            "Scheduled: %s at %02d:%02d (track %d, full)",
                            e.name, hour, minute, e.track);
                    }
                }
            }
        } else {
            if (e.lastTriggeredMinute != -1 &&
                minute != e.lastTriggeredMinute) {
                e.lastTriggeredMinute = -1;
            }
        }
    }
}

void Scheduler::pause() {
    paused = true;
    if (_durationActive) {
        _durationActive = false;
        Serial.println("[Scheduler] Duration timer cancelled (paused)");
    }
    Serial.println("[Scheduler] Paused.");
}

void Scheduler::resume() {
    paused = false;
    Serial.println("[Scheduler] Resumed.");
}

bool Scheduler::isPaused() {
    return paused;
}

bool Scheduler::isHolidayToday() {
    return _holidayToday;
}