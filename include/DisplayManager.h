#pragma once
#include "Event.h"
#include <TFT_eSPI.h>
#include "config.h"
#include "RTCManager.h"
#include "EventManager.h"
#include "StorageManager.h"

// Forward declaration to avoid circular includes
class Scheduler;

enum Screen {
    SCREEN_HOME,
    SCREEN_MENU,
    SCREEN_SCHEDULER,
    SCREEN_DATETIME,
    SCREEN_LIVE,
    SCREEN_MANUAL
};

class DisplayManager {
public:
    DisplayManager();
    void begin();
    bool isTFTResponding();
    void setRTCManager(RTCManager *rtc);
    void setStorageManager(StorageManager *storage);
    void setScheduler(Scheduler *sched);
    void update(RTCManager &rtc, EventManager &events);

    void goHome();
    void openMenu();
    void menuNext();
    void menuSelect();

    void setCurrentScreen(Screen screen);
    Screen getCurrentScreen();
    void requestRedraw();

    void setLiveActive(bool active);
    void setManualActive(bool active);
    void setPlayingTrack(uint16_t track);

    // Ringtone selection
    void ringtoneNext();
    uint16_t getSelectedRingtoneTrack();
    void setSelectedRingtoneTrack(uint16_t track);

    void schedulerScrollNext(uint8_t totalEvents);
    void schedulerResetScroll();

    // Environment profile
    void setProfile(uint8_t profile);
    uint8_t getProfile();
    const char* getProfileLabel();       // "School Name", "Hospital Name", etc.
    const char* getProfileIcon();        // emoji string for web UI


    void setSchoolName(const char* name);
    uint32_t getScreenEntryTime() const;
    void resetActivityTimer();

    void setBgTheme(uint8_t theme);
    uint8_t getBgTheme();

    void setNameColor(uint8_t r, uint8_t g, uint8_t b);
    void setClockColor(uint8_t r, uint8_t g, uint8_t b);
    void getNameColor(uint8_t &r, uint8_t &g, uint8_t &b);
    void getClockColor(uint8_t &r, uint8_t &g, uint8_t &b);
    
    // ── WiFi icon (ADD these 2 lines) ──
    void drawWiFiIcon(bool connected);
    void updateWiFiIcon(bool connected);
    // ── Blinking colon (ADD this line) ──
    void updateColonBlink();
    
    void showGuidePage(uint8_t page);

    // ── Factory reset screen ──
    void drawFactoryReset(int secondsLeft, bool done, bool cancelled);    

    // ── PIN reset screen ──                                          // +PIN_RESET
    void drawPinReset(int secondsLeft, bool done, bool cancelled);     // +PIN_RESET

private:
    TFT_eSPI tft = TFT_eSPI();
    RTCManager *rtcPtr = nullptr;
    StorageManager *storagePtr = nullptr;
    Scheduler *schedulerPtr = nullptr;
    uint16_t currentPlayingTrack = 0;
    Event* findNextEventToday(EventManager &events,
                           uint8_t hour,
                           uint8_t minute,
                           uint8_t weekday,
                           bool &isNow);
    Screen currentScreen = SCREEN_HOME;
    int menuIndex = 0;
    bool needsRedraw = true;
    bool firstUpdateDone = false;

    uint32_t lastUpdate = 0;
    uint32_t refreshInterval = DISPLAY_REFRESH_MS;

    bool liveActive = false;
    bool manualActive = false;

    // Ringtone selection
    int selectedRingtone = 0;
    uint32_t screenEntryTime = 0;
    uint32_t lastActivityTime = 0;

    // Scheduler scroll state
    int schedulerScrollOffset = 0;
    int schedulerCursorIndex = 0;

    char schoolName[MAX_SCHOOL_NAME_LEN + 1] = DEFAULT_SCHOOL_NAME;

    String getTrackName(uint16_t track, int maxChars);

    void drawHome(RTCManager &rtc, EventManager &events);
    void drawHeader();
    void drawFooter(RTCManager &rtc, EventManager &events);
    void drawClock(uint8_t hour, uint8_t minute);
    void drawDate(uint8_t day, uint8_t month, uint16_t year, uint8_t weekday);  // ★ NEW
    void drawSchoolGraphic();
    void drawEventCard(RTCManager &rtc, EventManager &events);

    void drawMenu();
    void drawMenuItem(int index, int x, int y, int w, int h,
                      String label, int iconType, uint16_t tileColor);
    void drawIcon(int type, int cx, int cy, uint16_t color);

    void drawScheduler(EventManager &events);
    void drawDateTime(RTCManager &rtc);
    void drawLive();
    void drawManual();
    void drawManualRingtoneRow(int index, bool isSelected);

    uint8_t currentProfile = DEFAULT_PROFILE;    

    uint8_t bgTheme = DEFAULT_BG_THEME;
    uint8_t nameColorR = 255, nameColorG = 255, nameColorB = 0;
    uint8_t clockColorR = 255, clockColorG = 255, clockColorB = 0;

    // ── WiFi icon state (ADD these 2 lines) ──
    bool lastWiFiState = false;
    bool wifiIconDrawn = false;

    // ── Blinking colon state (ADD these 3 lines) ──
    bool colonVisible = true;
    unsigned long lastColonToggle = 0;
    uint8_t lastClockHour = 255;
    uint8_t lastClockMinute = 255;
    void drawBgCustom();
  
    
    void drawBackground();
    void drawImageBg(const char* filename);
    void drawBgSchool();
    void drawBgHospital();
    void drawBgHostel();
    void drawBgOffice();
    void drawBgVenue();
    void drawBgTemple();
    void drawBgSmartHome();
};