#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "config.h"
#include "buttons.h"
#include "RTCManager.h"
#include "AudioManager.h"
#include "EventManager.h"
#include "StorageManager.h"
#include "Scheduler.h"
#include "DisplayManager.h"
#include "BellServer.h"
#include "SystemLog.h"
#include <LittleFS.h>
#include <ESPmDNS.h>

// ========================================================
//  GLOBAL OBJECTS
// ========================================================

ButtonManager   btnMgr;
RTCManager      rtcManager;
AudioManager&   audio = AudioManager::instance();
EventManager    eventManager;
StorageManager  storage;
Scheduler       scheduler(eventManager, audio);
DisplayManager  display;
BellServer      bellServer;
SystemLog       sysLog;

static const char* DAY_NAMES[] = {
    "Sunday", "Monday", "Tuesday", "Wednesday",
    "Thursday", "Friday", "Saturday"
};

// ========================================================
//  SYSTEM STATE
// ========================================================

SystemState systemState = STATE_IDLE;
unsigned long lastWiFiIconCheck = 0;
unsigned long lastColonBlink = 0;

// ── Factory reset combo state ──
static unsigned long playPressedTime = 0;
static bool resetCountdownActive = false;
static unsigned long resetCountdownStart = 0;
static int resetCountdownLastSec = -1;

// ── PIN reset combo state ──
static unsigned long pinNextPressedTime = 0;
static unsigned long pinOkPressedTime   = 0;
static bool pinResetCountdownActive = false;
static unsigned long pinResetCountdownStart = 0;
static int pinResetCountdownLastSec = -1;

// ── Boot guard cleared flag ──
static bool bootGuardCleared = false;

// ========================================================
//  LOG TIME CALLBACK
// ========================================================

void logTimeCallback(char* buf, size_t len) {
    uint16_t year;
    uint8_t month, day, hour, minute, weekday;
    rtcManager.getDateTime(year, month, day, hour, minute, weekday);
    snprintf(buf, len, "%02d/%02d %02d:%02d:%02d",
             day, month, hour, minute, 0);
}

// ========================================================
//  FORWARD DECLARATIONS
// ========================================================

void handleButtons();
void handleHomeButtons();
void handleMenuButtons();
void handleSchedulerButtons();
void handleDateTimeButtons();
void handleManualButtons();
void handleLiveButtons();

void startManualBell();
void stopManualBell();
void startLive();
void stopLive();

void processSerial();

// ========================================================
//  WiFi SSID MATCHER
// ========================================================

static String scanForRealSSID(const char* storedSSID) {
    String stored = String(storedSSID);
    String storedTrimmed = stored;
    storedTrimmed.trim();

    Serial.printf("[WiFi] Scanning to match SSID \"%s\"...\n", storedSSID);

    int n = WiFi.scanNetworks(false, false, false, 500);
    Serial.printf("[WiFi] Scan found %d networks\n", n);

    for (int i = 0; i < n; i++) {
        String scanned = WiFi.SSID(i);

        if (scanned == stored) {
            WiFi.scanDelete();
            return scanned;
        }

        String scannedTrimmed = scanned;
        scannedTrimmed.trim();
        if (scannedTrimmed == storedTrimmed) {
            Serial.printf("[WiFi] SSID matched: stored=\"%s\"(%d) → broadcast=\"%s\"(%d)\n",
                storedSSID, stored.length(), scanned.c_str(), scanned.length());
            WiFi.scanDelete();
            return scanned;
        }
    }

    WiFi.scanDelete();
    Serial.println("[WiFi] No broadcast match, using stored SSID as-is");
    return stored;
}

// ========================================================
//  FACTORY RESET
// ========================================================

void executeFactoryReset() {
    Serial.println("[Main] *** FACTORY RESET EXECUTING ***");
    display.drawFactoryReset(0, false, false);

    const char* filesToDelete[] = {
        "/events.bin",
        "/wifi.txt",
        "/wifimode.txt",
        "/wifi_static.txt",
        "/schoolname.txt",
        "/theme.txt",
        "/colors.txt",
        "/password.txt",
        "/holidays.txt",
        "/setup_done.txt",
        "/profile.txt",
        "/location.txt",
        "/ambient.txt",
        // /sounds.txt PRESERVED — tied to SD card, not user settings
        "/templates.txt",
        "/tpl_0.bin",
        "/tpl_1.bin",
        "/tpl_2.bin",
        "/tpl_3.bin",
        "/tpl_4.bin",
        "/tpl_5.bin",
        "/tpl_6.bin",
        "/tpl_7.bin",
        "/tpl_8.bin",
        "/tpl_9.bin"
    };

    int fileCount = sizeof(filesToDelete) / sizeof(filesToDelete[0]);
    int deleted = 0;
    for (int i = 0; i < fileCount; i++) {
        if (LittleFS.exists(filesToDelete[i])) {
            LittleFS.remove(filesToDelete[i]);
            Serial.printf("[Reset] Deleted: %s\n", filesToDelete[i]);
            deleted++;
        }
    }

    Serial.printf("[Reset] %d files deleted. Backgrounds + sounds preserved.\n", deleted);
    sysLog.log(LOG_CAT_SYSTEM, "Factory reset executed!");

    display.drawFactoryReset(0, true, false);
    delay(3000);

    Serial.println("[Main] Rebooting...");
    ESP.restart();
}

// ========================================================
//  PIN RESET
// ========================================================

void executePinReset() {
    Serial.println("[Main] *** PIN RESET EXECUTING ***");
    storage.savePassword(DEFAULT_PASSWORD);
    Serial.printf("[Main] PIN reset to: %s\n", DEFAULT_PASSWORD);
    sysLog.log(LOG_CAT_SETTINGS, "PIN reset to default (buttons)");

    display.drawPinReset(0, true, false);
    delay(3000);
    display.goHome();
}

// ========================================================
//  SETUP
// ========================================================

void setup() {
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
    Serial.begin(115200);
    delay(100);
    Serial.println();
    Serial.println("================================");
    Serial.println("  KAALANADHAM System Starting...");
    Serial.println("================================");

    // ── Power-on stabilization delay ──────────────
    // Gives 5V rail, TFT, and SD card time to settle
    // before any SPI/I2C/UART communication begins
    delay(300);

    btnMgr.begin();
    Serial.println("[Main] Buttons initialized.");

    display.begin();
    display.setRTCManager(&rtcManager);
    display.setStorageManager(&storage);
    display.setScheduler(&scheduler);
    Serial.println("[Main] Display initialized.");

    if (rtcManager.begin()) {
        Serial.println("[Main] RTC initialized.");
    } else {
        Serial.println("[Main] RTC FAILED!");
    }

    sysLog.setTimeCallback(logTimeCallback);
    sysLog.log(LOG_CAT_SYSTEM, "KAALANADHAM booting...");

    if (storage.begin()) {
        Serial.println("[Main] Storage initialized.");
    } else {
        Serial.println("[Main] Storage FAILED!");
        sysLog.log(LOG_CAT_ERROR, "Storage init FAILED");
    }

    // ── LittleFS space check ──────────────────────────
    {
        size_t total = LittleFS.totalBytes();
        size_t used  = LittleFS.usedBytes();
        size_t free  = total - used;
        Serial.println("[LittleFS] ─────────────────────────────");
        Serial.println("[LittleFS] ─────────────────────────────");
        Serial.printf("[LittleFS] Total : %7u bytes (%u KB)\n", total, total/1024);
        Serial.printf("[LittleFS] Used  : %7u bytes (%u KB)\n", used,  used/1024);
        Serial.printf("[LittleFS] Free  : %7u bytes (%u KB)\n", free,  free/1024);
        Serial.printf("[LittleFS] Usage : %d%%\n", (int)((used * 100) / total));
        Serial.println("[LittleFS] ─────────────────────────────");
    }

    // ── Load sounds cache ─────────────────────────────
    storage.loadSoundsCache();
    uint16_t soundCount = storage.getSoundCount();
    if (soundCount > 0) {
        Serial.printf("[Main] Sound names loaded: %d tracks\n", soundCount);
    } else {
        Serial.println("[Main] No sound names found (sync from desktop app)");
    }

    // ── Load school name ──────────────────────────────
    char schoolName[MAX_SCHOOL_NAME_LEN + 1];
    if (storage.loadSchoolName(schoolName, sizeof(schoolName))) {
        display.setSchoolName(schoolName);
        Serial.printf("[Main] School name: %s\n", schoolName);
    } else {
        display.setSchoolName(DEFAULT_SCHOOL_NAME);
        Serial.println("[Main] Using default school name.");
    }

    // ── Audio ─────────────────────────────────────────
    audio.begin();
    Serial.println("[Main] Audio initialized.");

    // ── Load environment profile ──────────────────────
    uint8_t savedProfile = storage.loadProfile();
    display.setProfile(savedProfile);
    Serial.printf("[Main] Environment profile: %d\n", savedProfile);

    // ── Load background theme ─────────────────────────
    uint8_t savedTheme = storage.loadTheme();
    display.setBgTheme(savedTheme);
    Serial.printf("[Main] Background theme: %d\n", savedTheme);

    // ── Load custom colors ────────────────────────────
    uint8_t nR, nG, nB, cR, cG, cB;
    if (storage.loadColors(nR, nG, nB, cR, cG, cB)) {
        display.setNameColor(nR, nG, nB);
        display.setClockColor(cR, cG, cB);
        Serial.println("[Main] Custom colors loaded");
    } else {
        Serial.println("[Main] Using default colors (yellow)");
    }

    // ── Load events ───────────────────────────────────
    uint16_t nextId;
    uint8_t count;
    Event loaded[MAX_EVENTS];

    if (storage.loadEvents(nextId, count, loaded)) {
        eventManager.setState(nextId, count, loaded);
        Serial.printf("[Main] Loaded %d events from storage.\n", count);
    } else {
        Serial.println("[Main] No saved events found.");
    }

    scheduler.setStorage(&storage);
    scheduler.setLog(&sysLog);

    // ── WiFi Station Mode (BEFORE web server) ─────────
    bool staConnected = false;
    uint8_t wifiMode = storage.loadWiFiMode();
    if (wifiMode == 1 && storage.hasWiFiCredentials()) {
        char staSSID[WIFI_STA_MAX_SSID_LEN + 1];
        char staPass[WIFI_STA_MAX_PASS_LEN + 1];
        if (storage.loadWiFiCredentials(staSSID, sizeof(staSSID), staPass, sizeof(staPass))) {
            Serial.printf("[Main] Attempting WiFi STA: %s\n", staSSID);
            sysLog.logf(LOG_CAT_WIFI, "Connecting to %s...", staSSID);

            WiFi.mode(WIFI_STA);

            String realSSID = scanForRealSSID(staSSID);
            bellServer.applyStaticIP();
            WiFi.begin(realSSID.c_str(), staPass);

            unsigned long staStart = millis();
            while (WiFi.status() != WL_CONNECTED &&
                   millis() - staStart < WIFI_STA_CONNECT_TIMEOUT) {
                delay(250);
                Serial.print(".");
            }

            if (WiFi.status() == WL_CONNECTED) {
                staConnected = true;
                Serial.printf("\n[Main] WiFi STA connected! IP: %s  Channel: %d\n",
                              WiFi.localIP().toString().c_str(), WiFi.channel());
                sysLog.logf(LOG_CAT_WIFI, "Connected! IP: %s",
                            WiFi.localIP().toString().c_str());

                WiFi.mode(WIFI_AP_STA);
                WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD, WiFi.channel(), 0, WIFI_AP_MAX_CONN);
                delay(200);

                WiFi.setTxPower(WIFI_AP_TX_POWER);
                Serial.println("[Main] AP added on STA channel, TX power reduced");

                if (rtcManager.syncFromNTP()) {
                    sysLog.log(LOG_CAT_WIFI, "NTP boot sync OK");
                    display.requestRedraw();
                } else {
                    sysLog.log(LOG_CAT_ERROR, "NTP boot sync failed");
                }
            } else {
                Serial.printf("\n[Main] WiFi STA failed! Status: %d\n", WiFi.status());
                sysLog.logf(LOG_CAT_ERROR, "WiFi connect failed: %s (status %d)",
                            staSSID, WiFi.status());
                WiFi.disconnect(true);
            }
        }
    }

    // ── Start AP if not connected ─────────────────────
    if (!staConnected) {
        WiFi.mode(WIFI_AP);
        WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD, WIFI_AP_CHANNEL, 0, WIFI_AP_MAX_CONN);
        delay(200);
        Serial.printf("[Main] AP started: %s  IP: %s\n",
                      WIFI_AP_SSID, WiFi.softAPIP().toString().c_str());
    }

    // ── Web server ────────────────────────────────────
    bellServer.begin(&rtcManager, &audio, &eventManager,
                     &storage, &scheduler, &display, &systemState,
                     &sysLog);

    // ── Register callbacks ────────────────────────────
    bellServer.onManualBellStart(startManualBell);
    bellServer.onManualBellStop(stopManualBell);
    bellServer.onLiveStart(startLive);
    bellServer.onLiveStop(stopLive);
    bellServer.onFactoryReset(executeFactoryReset);

    // ── TX power ──────────────────────────────────────
    WiFi.setTxPower(WIFI_AP_TX_POWER);

    if (staConnected) {
        bellServer.setStationMode(true);
    }

    // ── mDNS ─────────────────────────────────────────
    if (MDNS.begin(MDNS_HOSTNAME)) {
        MDNS.addService("http", "tcp", 80);
        Serial.println("[mDNS] Started — http://kaalanadham.local");
    } else {
        Serial.println("[mDNS] Failed to start");
    }

    // ════════════════════════════════════════════════════════
    //  FIRST-BOOT CONNECTION GUIDE
    //  Runs AFTER WiFi AP + web server + mDNS are all live.
    //  User can now actually connect while reading the guide.
    // ════════════════════════════════════════════════════════
    if (!LittleFS.exists("/setup_done.txt")) {

        Serial.println("[Guide] Starting connection guide.");

        pinMode(BTN_OK_PIN,   INPUT_PULLUP);
        pinMode(BTN_PLAY_PIN, INPUT_PULLUP);

        Serial.printf("[Guide] OK   pin %d = %s\n", BTN_OK_PIN,
                      digitalRead(BTN_OK_PIN)   == HIGH ? "HIGH (good)" : "LOW (held?)");
        Serial.printf("[Guide] PLAY pin %d = %s\n", BTN_PLAY_PIN,
                      digitalRead(BTN_PLAY_PIN) == HIGH ? "HIGH (good)" : "LOW (held?)");

        auto guideWaitPress = [&]() -> bool {

            Serial.println("[Guide] Hard delay...");
            delay(1500);
            Serial.println("[Guide] Delay done. Polling for press...");

            unsigned long lastPrint = millis();

            while (true) {

                if (millis() - lastPrint >= 3000) {
                    lastPrint = millis();
                    Serial.printf("[Guide] Waiting... OK=%s PLAY=%s\n",
                        digitalRead(BTN_OK_PIN)   == LOW ? "LOW" : "HIGH",
                        digitalRead(BTN_PLAY_PIN) == LOW ? "LOW" : "HIGH");
                }

                if (digitalRead(BTN_OK_PIN) == LOW) {
                    Serial.println("[Guide] OK LOW detected...");
                    delay(50);
                    if (digitalRead(BTN_OK_PIN) == LOW) {
                        while (digitalRead(BTN_OK_PIN) == LOW) delay(5);
                        delay(100);
                        Serial.println("[Guide] OK confirmed.");
                        return true;
                    }
                    Serial.println("[Guide] OK noise — ignored.");
                }

                if (digitalRead(BTN_PLAY_PIN) == LOW) {
                    Serial.println("[Guide] PLAY LOW detected...");
                    delay(50);
                    if (digitalRead(BTN_PLAY_PIN) == LOW) {
                        while (digitalRead(BTN_PLAY_PIN) == LOW) delay(5);
                        delay(100);
                        Serial.println("[Guide] PLAY confirmed — skipping.");
                        return false;
                    }
                    Serial.println("[Guide] PLAY noise — ignored.");
                }

                delay(20);
            }
        };

        bool skipped = false;

        for (uint8_t page = 0; page < 4; page++) {
            display.showGuidePage(page);
            Serial.printf("[Guide] Page %d/4 drawn.\n", page + 1);

            bool ok = guideWaitPress();

            if (!ok) {
                Serial.println("[Guide] Guide skipped.");
                skipped = true;
                break;
            }

            Serial.printf("[Guide] Page %d/4 confirmed.\n", page + 1);
        }

        Serial.println(skipped ? "[Guide] Skipped." : "[Guide] All pages done.");

        delay(200);
        for (int i = 0; i < 5; i++) {
            btnMgr.update();
            btnMgr.flushAll();
            delay(20);
        }

        Serial.println("[Guide] Booting...");
        delay(300);
    }
    // ════════════════════════════════════════════════════════

    Serial.println("[Main] Setup complete.");
    btnMgr.flushAll();
    Serial.println("[Main] Setup complete.");
}

// ========================================================
//  MAIN LOOP
// ========================================================

void loop() {
    btnMgr.update();

    // ── TEMPORARY DIAGNOSTIC — remove after debugging ──
    static bool diagDone = false;
    if (!diagDone && millis() > 12000) {
        diagDone = true;
        Serial.println("[DIAG] Boot guard expired");
        Serial.printf("[DIAG] pinNextPressedTime = %lu\n", pinNextPressedTime);
        Serial.printf("[DIAG] pinOkPressedTime   = %lu\n", pinOkPressedTime);
        Serial.printf("[DIAG] playPressedTime     = %lu\n", playPressedTime);
        Serial.printf("[DIAG] millis()            = %lu\n", millis());
    }

    processSerial();
    audio.loop();
    bellServer.loop();

    // ── TFT watchdog — detect blank screen and restart ──
    static unsigned long lastTFTCheck = 0;
    if (millis() - lastTFTCheck >= 30000) {
        lastTFTCheck = millis();
        if (!display.isTFTResponding()) {
            Serial.println("[Main] TFT not responding — restarting...");
            delay(500);
            ESP.restart();
        }
    }

    // ── Auto-return when manual bell track finishes naturally ──
    if (audio.consumePlayFinished() && systemState == STATE_MANUAL_BELL) {
        Serial.println("[Main] Manual bell track ended — auto-returning to home.");
        sysLog.log(LOG_CAT_BELL, "Manual bell auto-stopped (track ended)");
        systemState = STATE_IDLE;
        scheduler.resume();
        display.setPlayingTrack(0);
        display.setManualActive(false);
        display.setCurrentScreen(SCREEN_HOME);
        display.requestRedraw();
    }

    if (systemState == STATE_IDLE) {
        uint16_t year;
        uint8_t month, day, hour, minute, weekday;
        rtcManager.getDateTime(year, month, day, hour, minute, weekday);
        scheduler.update(hour, minute, weekday, year, month, day);
    }

    handleButtons();

    // Skip display updates during any countdown
    if (!resetCountdownActive && !pinResetCountdownActive) {
        display.update(rtcManager, eventManager);

        if (millis() - lastWiFiIconCheck >= 3000) {
            lastWiFiIconCheck = millis();
            display.updateWiFiIcon(WiFi.status() == WL_CONNECTED);
        }

        if (millis() - lastColonBlink >= 500) {
            lastColonBlink = millis();
            display.updateColonBlink();
        }
    }
}

// ========================================================
//  BUTTON HANDLING — DISPATCHER
// ========================================================

void handleButtons() {
    switch (display.getCurrentScreen()) {
        case SCREEN_HOME:       handleHomeButtons();      break;
        case SCREEN_MENU:       handleMenuButtons();      break;
        case SCREEN_SCHEDULER:  handleSchedulerButtons(); break;
        case SCREEN_DATETIME:   handleDateTimeButtons();  break;
        case SCREEN_MANUAL:     handleManualButtons();    break;
        case SCREEN_LIVE:       handleLiveButtons();      break;
    }
}

// ========================================================
//  HOME SCREEN BUTTONS
// ========================================================

void handleHomeButtons() {
    bool playPress = btnMgr.pressed(BTN_ID_PLAY);
    bool menuPress = btnMgr.pressed(BTN_ID_MENU);
    bool nextPress = btnMgr.pressed(BTN_ID_NEXT);
    bool okPress   = btnMgr.pressed(BTN_ID_OK);

    // ══════════════════════════════════════════════════
    //  PIN RESET COUNTDOWN ACTIVE
    // ══════════════════════════════════════════════════
    if (pinResetCountdownActive) {
        if (playPress) {
            pinResetCountdownActive  = false;
            pinResetCountdownLastSec = -1;
            pinNextPressedTime = 0;
            pinOkPressedTime   = 0;
            Serial.println("[Main] PIN reset CANCELLED");
            display.drawPinReset(0, false, true);
            delay(1500);
            display.goHome();
            return;
        }

        unsigned long elapsed = millis() - pinResetCountdownStart;
        int remaining = 10 - (int)(elapsed / 1000);

        if (remaining != pinResetCountdownLastSec && remaining >= 0) {
            pinResetCountdownLastSec = remaining;
            display.drawPinReset(remaining, false, false);
            Serial.printf("[Main] PIN reset in %d...\n", remaining);
        }

        if (elapsed >= 10000) {
            pinResetCountdownActive = false;
            executePinReset();
        }
        return;
    }

    // ══════════════════════════════════════════════════
    //  FACTORY RESET COUNTDOWN ACTIVE
    // ══════════════════════════════════════════════════
    if (resetCountdownActive) {
        if (nextPress || okPress) {
            resetCountdownActive  = false;
            resetCountdownLastSec = -1;
            playPressedTime = 0;
            Serial.println("[Main] Factory reset CANCELLED");
            display.drawFactoryReset(0, false, true);
            delay(1500);
            display.goHome();
            return;
        }

        unsigned long elapsed = millis() - resetCountdownStart;
        int remaining = 10 - (int)(elapsed / 1000);

        if (remaining != resetCountdownLastSec && remaining >= 0) {
            resetCountdownLastSec = remaining;
            display.drawFactoryReset(remaining, false, false);
            Serial.printf("[Main] Factory reset in %d...\n", remaining);
        }

        if (elapsed >= 10000) {
            resetCountdownActive = false;
            executeFactoryReset();
        }
        return;
    }

    // ══════════════════════════════════════════════════
    //  BOOT GUARD — discard ALL input for first 15 seconds
    // ══════════════════════════════════════════════════
    if (millis() < 15000) {
        return;
    }

    // ══════════════════════════════════════════════════
    //  DRAIN PHASE — runs for 200ms after boot guard
    //  Flushes any hardware-glitch pressedFlags that
    //  accumulated during setup() before update() ran
    // ══════════════════════════════════════════════════
    static unsigned long drainUntil = 0;

    if (!bootGuardCleared) {
        bootGuardCleared = true;
        drainUntil = millis() + 200;
        playPressedTime    = 0;
        pinNextPressedTime = 0;
        pinOkPressedTime   = 0;
        Serial.println("[Main] Boot guard cleared — draining button flags...");
        return;
    }

    if (millis() < drainUntil) {
        return;
    }

    // ══════════════════════════════════════════════════
    //  COMBO TRACKING — only runs after drain complete
    // ══════════════════════════════════════════════════

    if (nextPress) {
        pinNextPressedTime = millis();
        Serial.println("[Main] NEXT pressed (PIN combo step 1)");
    }

    if (okPress) {
        if (pinNextPressedTime > 0 && (millis() - pinNextPressedTime) < 500) {
            pinOkPressedTime = millis();
            Serial.println("[Main] NEXT->OK detected (waiting for MENU)");
        } else {
            pinNextPressedTime = 0;
            pinOkPressedTime   = 0;
        }
    }

    if (playPress) {
        playPressedTime = millis();
        Serial.println("[Main] PLAY pressed (factory reset combo step 1)");
    }

    // ══════════════════════════════════════════════════
    //  MENU PRESSED — resolve combos or open menu
    // ══════════════════════════════════════════════════
    if (menuPress) {

        // Priority 1: PIN reset — NEXT → OK → MENU
        if (pinOkPressedTime > 0 && (millis() - pinOkPressedTime) < 500) {
            pinResetCountdownActive  = true;
            pinResetCountdownStart   = millis();
            pinResetCountdownLastSec = -1;
            pinNextPressedTime = 0;
            pinOkPressedTime   = 0;
            playPressedTime    = 0;
            Serial.println("[Main] *** PIN reset combo detected! 10s countdown ***");
            Serial.println("[Main] Press PLAY to cancel");
            display.drawPinReset(10, false, false);
            return;
        }

        // Priority 2: Factory reset — PLAY → MENU
        if (playPressedTime > 0 && (millis() - playPressedTime) < 500) {
            resetCountdownActive  = true;
            resetCountdownStart   = millis();
            resetCountdownLastSec = -1;
            playPressedTime    = 0;
            pinNextPressedTime = 0;
            pinOkPressedTime   = 0;
            Serial.println("[Main] *** Factory reset combo detected! 10s countdown ***");
            Serial.println("[Main] Press NEXT or OK to cancel");
            display.drawFactoryReset(10, false, false);
            return;
        }

        // Normal MENU press
        playPressedTime    = 0;
        pinNextPressedTime = 0;
        pinOkPressedTime   = 0;
        display.openMenu();
        Serial.println("[Nav] Home -> Menu");
        return;
    }

    // ══════════════════════════════════════════════════
    //  EXPIRE STALE COMBO TIMESTAMPS
    // ══════════════════════════════════════════════════
    if (playPressedTime > 0 && millis() - playPressedTime > 1000) {
        playPressedTime = 0;
    }
    if (pinNextPressedTime > 0 && millis() - pinNextPressedTime > 1000) {
        pinNextPressedTime = 0;
        pinOkPressedTime   = 0;
    }
    if (pinOkPressedTime > 0 && millis() - pinOkPressedTime > 1000) {
        pinOkPressedTime = 0;
    }
}

// ========================================================
//  MENU SCREEN BUTTONS
// ========================================================

void handleMenuButtons() {
    if (btnMgr.pressed(BTN_ID_MENU)) {
        display.goHome();
        Serial.println("[Nav] Menu -> Home");
    }
    if (btnMgr.pressed(BTN_ID_NEXT)) {
        display.menuNext();
        Serial.println("[Nav] Menu: Next item");
    }
    if (btnMgr.pressed(BTN_ID_OK)) {
        display.menuSelect();
        Serial.println("[Nav] Menu: Selected");
    }
}

// ========================================================
//  SCHEDULER SCREEN BUTTONS
// ========================================================

void handleSchedulerButtons() {
    if (btnMgr.pressed(BTN_ID_MENU)) {
        display.openMenu();
        Serial.println("[Nav] Scheduler -> Menu");
    }
    if (btnMgr.pressed(BTN_ID_NEXT)) {
        Serial.printf("[Nav] Scheduler: Scroll next (events=%d)\n",
                      eventManager.getCount());
        display.schedulerScrollNext(eventManager.getCount());
    }
}

// ========================================================
//  DATE/TIME SCREEN BUTTONS
// ========================================================

void handleDateTimeButtons() {
    if (btnMgr.pressed(BTN_ID_MENU)) {
        display.openMenu();
        Serial.println("[Nav] DateTime -> Menu");
    }
}

// ========================================================
//  MANUAL BELL SCREEN BUTTONS
// ========================================================

void handleManualButtons() {
    if (btnMgr.pressed(BTN_ID_MENU)) {
        if (systemState == STATE_MANUAL_BELL) stopManualBell();
        display.openMenu();
        Serial.println("[Nav] Manual -> Menu");
        return;
    }

    // Ignore button presses for 500ms after entering screen
    if (millis() - display.getScreenEntryTime() < 500) {
        btnMgr.pressed(BTN_ID_PLAY);
        btnMgr.pressed(BTN_ID_NEXT);
        btnMgr.pressed(BTN_ID_OK);
        return;
    }

    if (btnMgr.pressed(BTN_ID_NEXT)) {
        if (systemState != STATE_MANUAL_BELL) {
            display.ringtoneNext();
            Serial.println("[Nav] Manual: Next ringtone");
        }
    }

    if (btnMgr.pressed(BTN_ID_PLAY) || btnMgr.pressed(BTN_ID_OK)) {
        if (systemState == STATE_MANUAL_BELL) {
            stopManualBell();
        } else {
            startManualBell();
        }
    }
}

// ========================================================
//  LIVE ANNOUNCEMENT SCREEN BUTTONS
// ========================================================

void handleLiveButtons() {
    if (btnMgr.pressed(BTN_ID_MENU)) {
        if (systemState == STATE_LIVE) stopLive();
        display.openMenu();
        Serial.println("[Nav] Live -> Menu");
        return;
    }

    // Ignore button presses for 500ms after entering screen
    if (millis() - display.getScreenEntryTime() < 500) {
        btnMgr.pressed(BTN_ID_PLAY);
        btnMgr.pressed(BTN_ID_NEXT);
        btnMgr.pressed(BTN_ID_OK);
        return;
    }

    if (btnMgr.pressed(BTN_ID_PLAY) || btnMgr.pressed(BTN_ID_OK)) {
        if (systemState == STATE_LIVE) stopLive();
        else startLive();
    }
}

// ========================================================
//  MANUAL BELL — START / STOP
// ========================================================

void startManualBell() {
    uint16_t track = display.getSelectedRingtoneTrack();
    Serial.printf("[Manual] Bell started! Ringtone track: %d\n", track);
    systemState = STATE_MANUAL_BELL;
    scheduler.pause();
    audio.playManualBell(track);
    display.setPlayingTrack(track);
    display.setManualActive(true);
    display.setCurrentScreen(SCREEN_MANUAL);
    display.requestRedraw();
    sysLog.logf(LOG_CAT_BELL, "Manual bell: track %d (button)", track);
}

void stopManualBell() {
    Serial.println("[Manual] Bell stopped.");
    systemState = STATE_IDLE;
    audio.stopDfPlayer();
    display.setPlayingTrack(0);
    scheduler.resume();
    display.setManualActive(false);
    display.setCurrentScreen(SCREEN_HOME);
    display.requestRedraw();
    sysLog.log(LOG_CAT_BELL, "Manual bell stopped (button)");
}

// ========================================================
//  LIVE ANNOUNCEMENT — START / STOP
// ========================================================

void startLive() {
    Serial.println("[Live] Announcement started!");
    systemState = STATE_LIVE;
    scheduler.pause();
    audio.startLive();
    display.setLiveActive(true);
    display.setCurrentScreen(SCREEN_LIVE);
    display.requestRedraw();
    sysLog.log(LOG_CAT_AUDIO, "Live started (button)");
}

void stopLive() {
    Serial.println("[Live] Announcement stopped.");
    systemState = STATE_IDLE;
    audio.stopLive();
    scheduler.resume();
    display.setLiveActive(false);
    display.setCurrentScreen(SCREEN_HOME);
    display.requestRedraw();
    sysLog.log(LOG_CAT_AUDIO, "Live stopped (button)");
}

// ========================================================
//  SERIAL DEBUG COMMANDS
// ========================================================

void processSerial() {
    if (!Serial.available()) return;

    String cmd = Serial.readStringUntil('\n');
    cmd.trim();

    if (cmd == "time") {
        uint8_t h, m, w;
        rtcManager.getTime(h, m, w);
        Serial.printf("Time: %02d:%02d  Day: %s\n", h, m, DAY_NAMES[w]);
    }

    else if (cmd == "date") {
        uint16_t year;
        uint8_t month, day, hour, minute, weekday;
        rtcManager.getDateTime(year, month, day, hour, minute, weekday);
        Serial.printf("Date: %04d-%02d-%02d %02d:%02d  %s\n",
                      year, month, day, hour, minute, DAY_NAMES[weekday]);
    }

    else if (cmd.startsWith("settime")) {
        int year, month, day, hour, minute, second;
        int parsed = sscanf(cmd.c_str(),
                            "settime %d %d %d %d %d %d",
                            &year, &month, &day,
                            &hour, &minute, &second);
        if (parsed == 6) {
            rtcManager.setTime(year, month, day, hour, minute, second);
            sysLog.logf(LOG_CAT_SETTINGS,
                "Time set via serial: %04d-%02d-%02d %02d:%02d",
                year, month, day, hour, minute);
        } else {
            Serial.println("Usage: settime YYYY MM DD HH MM SS");
        }
    }

    else if (cmd == "events") {
        uint8_t count = eventManager.getCount();
        if (count == 0) {
            Serial.println("No events scheduled.");
            return;
        }
        Serial.printf("Events (%d):\n", count);
        Event* events = eventManager.getEvents();
        for (int i = 0; i < count; i++) {
            if (events[i].isInterval()) {
                Serial.printf(
                    "  #%d [INTV] start %02d:%02d %s end %s T%d Days=0x%02X Dur=%s %s %s\n",
                    events[i].id,
                    events[i].hour,
                    events[i].minute,
                    events[i].intervalStr().c_str(),
                    events[i].endTimeStr().c_str(),
                    events[i].track,
                    events[i].weekdayMask,
                    events[i].durationStr().c_str(),
                    events[i].enabled ? "ON" : "OFF",
                    events[i].hasName() ? events[i].name : "");
            } else {
                Serial.printf(
                    "  #%d %02d:%02d T%d Days=0x%02X Dur=%s %s %s\n",
                    events[i].id,
                    events[i].hour,
                    events[i].minute,
                    events[i].track,
                    events[i].weekdayMask,
                    events[i].durationStr().c_str(),
                    events[i].enabled ? "ON" : "OFF",
                    events[i].hasName() ? events[i].name : "");
            }
        }
    }

    else if (cmd == "dfreset") {
        audio.resetDfPlayer();
        Serial.println("DFPlayer reset done.");
    }

    else if (cmd == "bell") {
        startManualBell();
    }

    else if (cmd == "stop") {
        if (systemState == STATE_MANUAL_BELL)      stopManualBell();
        else if (systemState == STATE_LIVE)         stopLive();
        else                                        audio.stopDfPlayer();
        Serial.println("Stopped.");
    }

    else if (cmd == "live") {
        if (systemState == STATE_LIVE) stopLive();
        else startLive();
    }

    else if (cmd == "status") {
        Serial.printf("State: %s\n",
            systemState == STATE_IDLE        ? "IDLE" :
            systemState == STATE_MANUAL_BELL ? "MANUAL_BELL" : "LIVE");
        Serial.printf("Screen: %d\n",        display.getCurrentScreen());
        Serial.printf("Events: %d\n",        eventManager.getCount());
        Serial.printf("Scheduler: %s\n",     scheduler.isPaused() ? "PAUSED" : "RUNNING");
        Serial.printf("DFPlayer: %s\n",      audio.isDfPlayerReady() ? "READY" : "NOT READY");
        Serial.printf("DFPlayer playing: %s\n", audio.isDfPlayerPlaying() ? "YES" : "NO");
        Serial.printf("Live: %s\n",          audio.isLive() ? "YES" : "NO");
        Serial.printf("Sounds: %d tracks\n", storage.getSoundCount());
        Serial.printf("Profile: %d (%s)\n",  display.getProfile(), display.getProfileIcon());
        Serial.printf("Log entries: %d\n",   sysLog.getCount());
        Serial.printf("Boot guard: %s\n",    bootGuardCleared ? "CLEARED" : "ACTIVE");
    }

    else if (cmd == "clear") {
        eventManager.clear();
        storage.saveEvents(eventManager.getNextId(),
                           eventManager.getCount(),
                           eventManager.getEvents());
        Serial.println("All events cleared and saved.");
        sysLog.log(LOG_CAT_SCHEDULE, "All events cleared (serial)");
    }

    else if (cmd == "dftest") {
        Serial.println("DFPlayer test: playing track 1...");
        audio.playTrack(1);
    }

    else if (cmd == "logs") {
        uint8_t count = sysLog.getCount();
        Serial.printf("=== System Log (%d entries) ===\n", count);
        for (uint8_t i = 0; i < count; i++) {
            LogEntry* e = sysLog.getEntry(i);
            if (e) {
                Serial.printf("  [%s] [%-5s] %s\n",
                              e->timeStr, e->category, e->message);
            }
        }
        Serial.println("=== End of Log ===");
    }

    else if (cmd == "wifi") {
        Serial.printf("WiFi Mode: %s\n",
                      bellServer.isStationMode() ? "Station" : "AP");
        if (bellServer.isStationMode() && WiFi.status() == WL_CONNECTED) {
            Serial.printf("SSID: %s\n",  WiFi.SSID().c_str());
            Serial.printf("IP: %s\n",    WiFi.localIP().toString().c_str());
            Serial.printf("RSSI: %d dBm\n", WiFi.RSSI());
        }
        Serial.printf("AP IP: %s\n",     WiFi.softAPIP().toString().c_str());
        Serial.printf("NTP synced: %s\n",rtcManager.isNTPSynced() ? "YES" : "NO");
    }

    else if (cmd == "wifitest") {
        char ssid[33], pass[65];
        if (storage.loadWiFiCredentials(ssid, sizeof(ssid), pass, sizeof(pass))) {
            Serial.printf("SSID string: [%s] len=%d\n", ssid, strlen(ssid));
            Serial.print("SSID hex: ");
            for (int i = 0; i < (int)strlen(ssid); i++) {
                Serial.printf("%02X ", (uint8_t)ssid[i]);
            }
            Serial.println();
            Serial.printf("Pass len: %d\n", strlen(pass));

            int n = WiFi.scanNetworks(false, false, false, 300);
            String storedTrimmed = String(ssid);
            storedTrimmed.trim();
            for (int i = 0; i < n; i++) {
                String scanned = WiFi.SSID(i);
                String scannedTrimmed = scanned;
                scannedTrimmed.trim();
                if (scannedTrimmed == storedTrimmed || scanned == String(ssid)) {
                    Serial.printf("Found \"%s\": RSSI=%d, Auth=%d, len=%d\n",
                        scanned.c_str(), WiFi.RSSI(i),
                        WiFi.encryptionType(i), scanned.length());
                    Serial.print("  Broadcast hex: ");
                    for (unsigned int c = 0; c < scanned.length(); c++) {
                        Serial.printf("%02X ", (uint8_t)scanned[c]);
                    }
                    Serial.println();
                }
            }
            WiFi.scanDelete();

            Serial.println("Attempting connect with scan-matched SSID...");
            WiFi.disconnect(true);
            delay(500);
            WiFi.mode(WIFI_STA);
            delay(500);

            String realSSID = scanForRealSSID(ssid);
            WiFi.begin(realSSID.c_str(), pass);

            for (int i = 0; i < 40; i++) {
                delay(500);
                Serial.printf("Status: %d\n", WiFi.status());
                if (WiFi.status() == WL_CONNECTED) {
                    Serial.printf("CONNECTED! IP: %s\n",
                                  WiFi.localIP().toString().c_str());
                    WiFi.mode(WIFI_AP_STA);
                    WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD,
                                WiFi.channel(), 0, WIFI_AP_MAX_CONN);
                    WiFi.setTxPower(WIFI_AP_TX_POWER);
                    bellServer.setStationMode(true);
                    return;
                }
            }
            Serial.printf("FAILED. Final status: %d\n", WiFi.status());
            WiFi.mode(WIFI_AP);
            WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD,
                        WIFI_AP_CHANNEL, 0, WIFI_AP_MAX_CONN);
            WiFi.setTxPower(WIFI_AP_TX_POWER);
        } else {
            Serial.println("No saved credentials");
        }
    }

    else if (cmd == "ntp") {
        if (bellServer.isStationMode() && WiFi.status() == WL_CONNECTED) {
            Serial.println("Forcing NTP sync...");
            if (rtcManager.syncFromNTP()) {
                Serial.println("NTP sync OK!");
                sysLog.log(LOG_CAT_WIFI, "NTP manual sync (serial)");
            } else {
                Serial.println("NTP sync FAILED!");
            }
        } else {
            Serial.println("NTP requires station mode with WiFi connected.");
        }
    }

    else if (cmd == "resetpassword") {
        storage.savePassword(DEFAULT_PASSWORD);
        Serial.printf("PIN reset to: %s\n", DEFAULT_PASSWORD);
        sysLog.log(LOG_CAT_SETTINGS, "PIN reset via serial");
    }

    else if (cmd == "showpassword") {
        char pin[MAX_PASSWORD_LEN + 1];
        if (storage.loadPassword(pin, sizeof(pin))) {
            Serial.printf("Current PIN: %s\n", pin);
        } else {
            Serial.printf("Default PIN: %s (no custom set)\n", DEFAULT_PASSWORD);
        }
    }

    else if (cmd == "factoryreset YES") {
        executeFactoryReset();
    }
    else if (cmd == "factoryreset") {
        Serial.println("*** To confirm, type exactly: factoryreset YES ***");
    }

    else if (cmd == "help") {
        Serial.println("Commands:");
        Serial.println("  time, date, settime YYYY MM DD HH MM SS");
        Serial.println("  events, bell, stop, live, dftest");
        Serial.println("  status, clear, logs, wifi, wifitest, ntp");
        Serial.println("  resetpassword, showpassword");
        Serial.println("  factoryreset YES, help");
        Serial.println("  dfreset — reset DFPlayer after SD card change");
    }

    else {
        Serial.println("Unknown command. Type 'help' for list.");
    }
}