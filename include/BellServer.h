#ifndef BELL_SERVER_H
#define BELL_SERVER_H

#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>

#include "config.h"
#include "RTCManager.h"
#include "AudioManager.h"
#include "EventManager.h"
#include "StorageManager.h"
#include "Scheduler.h"
#include "DisplayManager.h"
#include "SystemLog.h"

// System state (shared with main.cpp)
enum SystemState {
    STATE_IDLE,
    STATE_MANUAL_BELL,
    STATE_LIVE
};

// WiFi auto-reconnect states
enum ReconnectState {
    RECONNECT_IDLE = 0,
    RECONNECT_WAIT,
    RECONNECT_SCANNING,
    RECONNECT_SCAN_WAIT,
    RECONNECT_CONNECTING,
    RECONNECT_BACKOFF
};

// Callback function types for main.cpp actions
typedef void (*ActionCallback)();

class BellServer {
public:
    BellServer();

    void begin(RTCManager *rtc,
               AudioManager *audio,
               EventManager *events,
               StorageManager *storage,
               Scheduler *scheduler,
               DisplayManager *display,
               SystemState *state,
               SystemLog *log);

    void loop();
    String getIPAddress();
    void setStationMode(bool sta);
    bool isStationMode();
    bool applyStaticIP();

    void onManualBellStart(ActionCallback cb);
    void onManualBellStop(ActionCallback cb);
    void onLiveStart(ActionCallback cb);
    void onLiveStop(ActionCallback cb);
    typedef std::function<void()> FactoryResetCallback;
    void onFactoryReset(FactoryResetCallback cb);

private:
    AsyncWebServer server;

    String _restoreBody;
    String _soundsBody;

    RTCManager     *rtcPtr;
    AudioManager   *audioPtr;
    EventManager   *eventsPtr;
    StorageManager *storagePtr;
    Scheduler      *schedulerPtr;
    DisplayManager *displayPtr;
    SystemState    *statePtr;
    SystemLog      *logPtr;
    bool _isStationMode;
    unsigned long _lastNTPCheck;

    uint8_t _wifiConnState;
    unsigned long _wifiConnStart;

    // Auto-reconnect state machine
    ReconnectState _reconState;
    unsigned long  _reconTimer;
    unsigned long  _reconBackoff;
    unsigned long  _lastConnCheck;
    String         _reconRealSSID;
    bool           _reconEnabled;
    uint8_t        _reconAttempts;

    // Callbacks
    ActionCallback cbManualStart;
    ActionCallback cbManualStop;
    ActionCallback cbLiveStart;
    ActionCallback cbLiveStop;

    // Setup methods
    void setupWiFiAP();
    void setupRoutes();

    // Auto-reconnect
    void loopReconnect();
    void reconTransition(ReconnectState newState, const char* reason);

    // Route handlers
    void handleRoot(AsyncWebServerRequest *request);
    void handleGetStatus(AsyncWebServerRequest *request);

    // Time API
    void handleGetTime(AsyncWebServerRequest *request);
    void handleSetTime(AsyncWebServerRequest *request, uint8_t *data, size_t len);

    // Events API
    void handleGetEvents(AsyncWebServerRequest *request);
    void handleAddEvent(AsyncWebServerRequest *request, uint8_t *data, size_t len);
    void handleDeleteEvent(AsyncWebServerRequest *request);
    void handleToggleEvent(AsyncWebServerRequest *request);
    void handleClearEvents(AsyncWebServerRequest *request);

    // Control API
    void handleManualBell(AsyncWebServerRequest *request);
    void handleStop(AsyncWebServerRequest *request);
    void handleLiveStart(AsyncWebServerRequest *request);
    void handleLiveStop(AsyncWebServerRequest *request);

    // Sounds API
    void handleGetSounds(AsyncWebServerRequest *request);
    void handleSetSounds(AsyncWebServerRequest *request, uint8_t *data, size_t len);

    // Helpers
    void saveEventsToStorage();
    String buildEventsJSON();

    // School name API
    void handleGetSchoolName(AsyncWebServerRequest *request);
    void handleSetSchoolName(AsyncWebServerRequest *request, uint8_t *data, size_t len);

    // Theme API
    void handleGetTheme(AsyncWebServerRequest *request);
    void handleSetTheme(AsyncWebServerRequest *request, uint8_t *data, size_t len);

    // Colors API
    void handleGetColors(AsyncWebServerRequest *request);
    void handleSetColors(AsyncWebServerRequest *request, uint8_t *data, size_t len);

    // Custom background upload
    void handleUploadCustomBg(AsyncWebServerRequest *request,
                              uint8_t *data, size_t len,
                              size_t index, size_t total);

    // Holidays API
    void handleGetHolidays(AsyncWebServerRequest *request);
    void handleAddHoliday(AsyncWebServerRequest *request, uint8_t *data, size_t len);
    void handleDeleteHoliday(AsyncWebServerRequest *request);
    void handleClearHolidays(AsyncWebServerRequest *request);

    // Auth API
    void handleAuth(AsyncWebServerRequest *request, uint8_t *data, size_t len);
    void handleChangePassword(AsyncWebServerRequest *request, uint8_t *data, size_t len);

    // Templates API
    void handleGetTemplates(AsyncWebServerRequest *request);
    void handleSaveTemplate(AsyncWebServerRequest *request, uint8_t *data, size_t len);
    void handleLoadTemplate(AsyncWebServerRequest *request);
    void handleDeleteTemplate(AsyncWebServerRequest *request);

    // Backup & Restore
    void handleBackupDownload(AsyncWebServerRequest *request);
    void handleBackupRestore(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);

    // System Logs
    void handleGetLogs(AsyncWebServerRequest *request);
    void handleClearLogs(AsyncWebServerRequest *request);

    // WiFi API
    void handleGetWiFiStatus(AsyncWebServerRequest *request);
    void handleWiFiScan(AsyncWebServerRequest *request);
    void handleWiFiConnect(AsyncWebServerRequest *request, uint8_t *data, size_t len);
    void handleWiFiDisconnect(AsyncWebServerRequest *request);
    void handleWiFiAP(AsyncWebServerRequest *request);
    void handleGetStaticIP(AsyncWebServerRequest *request);
    void handleSetStaticIP(AsyncWebServerRequest *request, uint8_t *data, size_t len);
    void handleNTPStatus(AsyncWebServerRequest *request);
    void handleNTPSync(AsyncWebServerRequest *request);
    void handleGetSetupStatus(AsyncWebServerRequest *request);
    void handleSetupComplete(AsyncWebServerRequest *request);
    void handleSetupReset(AsyncWebServerRequest *request);
    FactoryResetCallback _factoryResetCb = nullptr;
    bool _factoryResetPending = false;

    // Profile API
    void handleGetProfile(AsyncWebServerRequest *request);
    void handleSetProfile(AsyncWebServerRequest *request, uint8_t *data, size_t len);

    // ── ICS Calendar Export ─────────────────────────────────────────────────
    void handleExportICS(AsyncWebServerRequest *request);

    String generateICS(int advanceMinutes);
    String maskToBYDAY(uint8_t weekdayMask);
    String buildExDates(uint8_t hour, uint8_t minute);
    String nextOccurrence(uint8_t weekdayMask, uint8_t evHour, uint8_t evMinute,
                        uint16_t curYear, uint8_t curMonth,
                        uint8_t curDay, uint8_t curDOW);
    void   appendFixedEvent(String &out, const Event &ev,
                            uint16_t curYear, uint8_t curMonth,
                            uint8_t curDay, uint8_t curDOW,
                            const char *dtStamp, int advanceMinutes);
    void   appendIntervalEvents(String &out, const Event &ev,
                                uint16_t curYear, uint8_t curMonth,
                                uint8_t curDay, uint8_t curDOW,
                                const char *dtStamp, int advanceMinutes);

    void   appendOneTimeEvent(String &out, const Event &ev,     // ← ADD
                              uint16_t curYear, uint8_t curMonth,
                              uint8_t  curDay,
                              const char *dtStamp,
                              int advanceMinutes);
};

#endif