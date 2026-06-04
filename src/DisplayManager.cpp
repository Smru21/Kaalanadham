#include "DisplayManager.h"
#include "Scheduler.h"
#include <LittleFS.h>
#include <FS.h>
#include <WiFi.h>

// ===== Custom UI Colors =====
#define UI_BG_DARK        0x1082
#define UI_BG_LIGHT       0x3186
#define UI_TILE_BLUE      0x5D9B
#define UI_TILE_GREEN     0x2D6B
#define UI_TILE_ORANGE    0xFD20
#define UI_TILE_PURPLE    0xA1B0
#define UI_SHADOW         0x0841
#define UI_TEXT_WHITE     TFT_WHITE
#define UI_TEXT_BLACK     TFT_BLACK

// Day name lookup tables
static const char* DAY_NAMES[] = {
    "Sunday", "Monday", "Tuesday", "Wednesday",
    "Thursday", "Friday", "Saturday"
};

static const char* DAY_SHORT[] = {
    "Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"
};

static const char* PROFILE_LABELS[] = {
    "School Name",
    "Hospital Name",
    "Hostel Name",
    "Company Name",
    "Venue Name",
    "Temple Name",
    "Factory Name",
    "Name"
};

static const char* PROFILE_NAMES[] = {
    "School",
    "Hospital",
    "Hostel",
    "Office",
    "Venue",
    "Temple",
    "Factory",
    "Custom"
};

// ========================================================
//  CONSTRUCTOR & SETUP
// ========================================================

DisplayManager::DisplayManager() {
    menuIndex = 0;
}

void DisplayManager::begin() {
#if TFT_BL_PIN >= 0
    pinMode(TFT_BL_PIN, OUTPUT);
    digitalWrite(TFT_BL_PIN, HIGH);
#endif

    // ── Power rail stabilization ─────────────────────────
    // ESP32 boots faster than 5V rail stabilizes.
    // ILI9341 needs VCC stable before accepting SPI init.
    // 1000ms covers slow supplies and inrush current delay.
    delay(1000);

    tft.init();
    tft.setRotation(3);

    // ── TFT health check ──────────────────────────────────
    // 0x00 = dead SPI / wrong pins
    // 0xFF = bus floating / TFT not connected
    // Anything else = controller responded = OK
    // Note: ILI9341 returns various values (0x9C, 0x08 etc)
    // depending on sleep/booster state — all are valid.
    uint8_t pwr = tft.readcommand8(0x0A);
    Serial.printf("[Display] TFT power mode: 0x%02X %s\n",
        pwr, (pwr == 0x00 || pwr == 0xFF) ? "NO RESPONSE" : "OK");

    if (pwr == 0x00 || pwr == 0xFF) {
        // TFT truly not responding — retry once before restarting
        Serial.println("[Display] TFT no response — retrying init...");
        delay(500);
        tft.init();
        tft.setRotation(3);
        delay(200);
        pwr = tft.readcommand8(0x0A);
        Serial.printf("[Display] TFT retry: 0x%02X\n", pwr);

        if (pwr == 0x00 || pwr == 0xFF) {
            Serial.println("[Display] TFT still dead — restarting in 2s...");
            delay(2000);
            ESP.restart();
        }
    }

    Serial.println("[Display] TFT initialized OK.");
}

bool DisplayManager::isTFTResponding() {
    uint8_t pwr = tft.readcommand8(0x0A);
    return (pwr != 0x00 && pwr != 0xFF);
}

// ========================================================
//  SETTERS
// ========================================================

void DisplayManager::setRTCManager(RTCManager *rtc) {
    rtcPtr = rtc;
}

void DisplayManager::setStorageManager(StorageManager *storage) {
    storagePtr = storage;
}

void DisplayManager::setScheduler(Scheduler *sched) {
    schedulerPtr = sched;
}

void DisplayManager::setProfile(uint8_t profile) {
    if (profile >= MAX_PROFILES) profile = DEFAULT_PROFILE;
    currentProfile = profile;
    Serial.printf("[Display] Profile set to: %d (%s)\n",
                  profile, PROFILE_NAMES[profile]);
    if (currentScreen == SCREEN_HOME) needsRedraw = true;
}

uint8_t DisplayManager::getProfile() {
    return currentProfile;
}

const char* DisplayManager::getProfileLabel() {
    if (currentProfile >= MAX_PROFILES) return PROFILE_LABELS[0];
    return PROFILE_LABELS[currentProfile];
}

const char* DisplayManager::getProfileIcon() {
    if (currentProfile >= MAX_PROFILES) return "School";
    return PROFILE_NAMES[currentProfile];
}

void DisplayManager::setPlayingTrack(uint16_t track) {
    currentPlayingTrack = track;
    if (currentScreen == SCREEN_MANUAL) needsRedraw = true;
}

void DisplayManager::setBgTheme(uint8_t theme) {
    if (theme >= MAX_BG_THEMES) theme = 0;
    bgTheme = theme;
    Serial.printf("[Display] Theme set to: %d\n", theme);
    if (currentScreen == SCREEN_HOME) needsRedraw = true;
}

uint8_t DisplayManager::getBgTheme() {
    return bgTheme;
}

void DisplayManager::setNameColor(uint8_t r, uint8_t g, uint8_t b) {
    nameColorR = r; nameColorG = g; nameColorB = b;
    Serial.printf("[Display] Name color: #%02X%02X%02X\n", r, g, b);
    if (currentScreen == SCREEN_HOME) needsRedraw = true;
}

void DisplayManager::setClockColor(uint8_t r, uint8_t g, uint8_t b) {
    clockColorR = r; clockColorG = g; clockColorB = b;
    Serial.printf("[Display] Clock color: #%02X%02X%02X\n", r, g, b);
    if (currentScreen == SCREEN_HOME) needsRedraw = true;
}

void DisplayManager::getNameColor(uint8_t &r, uint8_t &g, uint8_t &b) {
    r = nameColorR; g = nameColorG; b = nameColorB;
}

void DisplayManager::getClockColor(uint8_t &r, uint8_t &g, uint8_t &b) {
    r = clockColorR; g = clockColorG; b = clockColorB;
}

void DisplayManager::setSchoolName(const char* name) {
    strncpy(schoolName, name, MAX_SCHOOL_NAME_LEN);
    schoolName[MAX_SCHOOL_NAME_LEN] = '\0';
    Serial.printf("[Display] School name set: %s\n", schoolName);
    if (currentScreen == SCREEN_HOME) needsRedraw = true;
}

void DisplayManager::resetActivityTimer() {
    lastActivityTime = millis();
}

uint32_t DisplayManager::getScreenEntryTime() const {
    return screenEntryTime;
}

// ========================================================
//  SCREEN MANAGEMENT
// ========================================================

void DisplayManager::setCurrentScreen(Screen screen) {
    currentScreen = screen;
    screenEntryTime = millis();
    lastActivityTime = millis();
    needsRedraw = true;
}

Screen DisplayManager::getCurrentScreen() {
    return currentScreen;
}

void DisplayManager::requestRedraw() {
    needsRedraw = true;
}

void DisplayManager::setLiveActive(bool active) {
    liveActive = active;
    if (currentScreen == SCREEN_LIVE) needsRedraw = true;
}

void DisplayManager::setManualActive(bool active) {
    manualActive = active;
    if (currentScreen == SCREEN_MANUAL) needsRedraw = true;
}

// ========================================================
//  NAVIGATION
// ========================================================

void DisplayManager::goHome() {
    currentScreen = SCREEN_HOME;
    lastActivityTime = millis();
    needsRedraw = true;
}

void DisplayManager::openMenu() {
    currentScreen = SCREEN_MENU;
    menuIndex = 0;
    lastActivityTime = millis();
    drawMenu();
}

void DisplayManager::menuNext() {
    if (currentScreen != SCREEN_MENU) return;
    menuIndex = (menuIndex + 1) % 4;
    lastActivityTime = millis();
    drawMenu();
}

void DisplayManager::menuSelect() {
    if (currentScreen != SCREEN_MENU) return;
    switch (menuIndex) {
        case 0: currentScreen = SCREEN_SCHEDULER; schedulerResetScroll(); break;
        case 1: currentScreen = SCREEN_DATETIME;  break;
        case 2: currentScreen = SCREEN_LIVE;      break;
        case 3: currentScreen = SCREEN_MANUAL;    break;
    }
    screenEntryTime = millis();
    lastActivityTime = millis();
    needsRedraw = true;
}

void DisplayManager::schedulerResetScroll() {
    schedulerScrollOffset = 0;
    schedulerCursorIndex = 0;
}

void DisplayManager::schedulerScrollNext(uint8_t totalEvents) {
    if (currentScreen != SCREEN_SCHEDULER) return;
    if (totalEvents == 0) return;
    lastActivityTime = millis();

    const int maxVisible = 5;

    schedulerCursorIndex++;

    if (schedulerCursorIndex >= totalEvents) {
        schedulerCursorIndex = 0;
        schedulerScrollOffset = 0;
    } else if (schedulerCursorIndex >= schedulerScrollOffset + maxVisible) {
        schedulerScrollOffset = schedulerCursorIndex - maxVisible + 1;
    } else if (schedulerCursorIndex < schedulerScrollOffset) {
        schedulerScrollOffset = schedulerCursorIndex;
    }

    Serial.printf("[Display] Cursor: %d, Offset: %d (showing %d-%d of %d)\n",
                  schedulerCursorIndex, schedulerScrollOffset,
                  schedulerScrollOffset + 1,
                  min(schedulerScrollOffset + maxVisible, (int)totalEvents),
                  totalEvents);

    needsRedraw = true;
}

// ========================================================
//  RINGTONE SELECTION
// ========================================================

void DisplayManager::ringtoneNext() {
    if (currentScreen != SCREEN_MANUAL) return;
    if (manualActive) return;
    lastActivityTime = millis();

    int prevSelected = selectedRingtone;
    selectedRingtone = (selectedRingtone + 1) % RINGTONE_COUNT;

    Serial.printf("[Display] Ringtone selected: %d (track %d)\n",
                  selectedRingtone + 1,
                  RINGTONE_START_TRACK + selectedRingtone);

    drawManualRingtoneRow(prevSelected, false);
    drawManualRingtoneRow(selectedRingtone, true);
}

uint16_t DisplayManager::getSelectedRingtoneTrack() {
    return RINGTONE_START_TRACK + selectedRingtone;
}

void DisplayManager::setSelectedRingtoneTrack(uint16_t track) {
    if (track >= RINGTONE_START_TRACK &&
        track < RINGTONE_START_TRACK + RINGTONE_COUNT) {
        selectedRingtone = track - RINGTONE_START_TRACK;
    }
    needsRedraw = true;
}

// ========================================================
//  TRACK NAME HELPER
// ========================================================

String DisplayManager::getTrackName(uint16_t track, int maxChars) {
    String name = "";
    if (storagePtr) {
        name = storagePtr->getSoundName(track);
    }
    if (name.length() == 0) {
        char buf[12];
        sprintf(buf, "Track %d", track);
        name = buf;
    }
    if (maxChars > 0 && (int)name.length() > maxChars) {
        name = name.substring(0, maxChars - 2) + "..";
    }
    return name;
}

// ========================================================
//  MAIN UPDATE LOOP
// ========================================================

void DisplayManager::update(RTCManager &rtc, EventManager &events) {

    // ── Screen timeout ────────────────────────────────────
    if (currentScreen != SCREEN_HOME && !manualActive && !liveActive) {
        if (millis() - lastActivityTime > SCREEN_TIMEOUT_MS) {
            currentScreen = SCREEN_HOME;
            needsRedraw = true;
            Serial.println("[Display] Screen timeout - returning home");
        }
    }

    // ── Periodic TFT re-init ──────────────────────────── // ← NEW
    // If TFT lost state after power glitch, re-init it    // ← NEW
    // and force full redraw every 60s as safety net       // ← NEW
    static unsigned long lastTFTCheck = 0;                 // ← NEW
    if (millis() - lastTFTCheck >= 60000) {                // ← NEW
        lastTFTCheck = millis();                           // ← NEW
        uint8_t pwr = tft.readcommand8(0x0A);              // ← NEW
        if (pwr == 0x00 || pwr == 0xFF) {                  // ← NEW
            Serial.println("[Display] TFT lost mid-run — reinit..."); // ← NEW
            delay(100);                                    // ← NEW
            tft.init();                                    // ← NEW
            tft.setRotation(3);                            // ← NEW
            delay(100);                                    // ← NEW
            Serial.println("[Display] TFT reinit done."); // ← NEW
        }                                                  // ← NEW
        needsRedraw = true;  // force full redraw either way // ← NEW
    }                                                      // ← NEW

    if (!firstUpdateDone) {
        drawHome(rtc, events);
        firstUpdateDone = true;
        return;
    }

    switch (currentScreen) {

        case SCREEN_HOME:
            if (needsRedraw || (millis() - lastUpdate >= refreshInterval)) {
                lastUpdate = millis();
                drawHome(rtc, events);
                needsRedraw = false;
            }
            break;

        case SCREEN_MENU:
            break;

        case SCREEN_SCHEDULER:
            if (needsRedraw) {
                drawScheduler(events);
                needsRedraw = false;
            }
            break;

        case SCREEN_DATETIME:
            if (needsRedraw) {
                drawDateTime(rtc);
                needsRedraw = false;
            }
            break;

        case SCREEN_LIVE:
            if (needsRedraw) {
                drawLive();
                needsRedraw = false;
            }
            break;

        case SCREEN_MANUAL:
            if (needsRedraw) {
                drawManual();
                needsRedraw = false;
            }
            break;
    }
}

// ========================================================
//  HOME SCREEN
// ========================================================

void DisplayManager::drawHome(RTCManager &rtc, EventManager &events) {
    uint16_t year;
    uint8_t month, day, hour, minute, weekday;
    rtc.getDateTime(year, month, day, hour, minute, weekday);

    drawBackground();
    drawClock(hour, minute);
    drawDate(day, month, year, weekday);
    drawEventCard(rtc, events);
    drawFooter(rtc, events);
    drawWiFiIcon(WiFi.status() == WL_CONNECTED);
}

// ========================================================
//  BACKGROUND
// ========================================================

void DisplayManager::drawBackground() {
    switch (bgTheme) {
        case 0:  drawBgSchool();     break;
        case 1:  drawBgHospital();   break;
        case 2:  drawBgHostel();     break;
        case 3:  drawBgOffice();     break;
        case 4:  drawBgVenue();      break;
        case 5:  drawBgTemple();     break;
        case 6:  drawBgSmartHome();  break;
        case 7:  drawBgCustom();     break;
        default: drawBgSchool();     break;
    }
}

void DisplayManager::drawImageBg(const char* filename) {
    int w = 320, h = 240;

    fs::File imgFile = LittleFS.open(filename, "r");

    if (!imgFile) {
        Serial.printf("[Display] %s not found — using fallback\n", filename);
        tft.fillScreen(tft.color565(30, 30, 50));

        tft.setTextDatum(TC_DATUM);
        int nameLen = strlen(schoolName);
        if (nameLen <= 10)       tft.setTextSize(3);
        else if (nameLen <= 16)  tft.setTextSize(2);
        else                     tft.setTextSize(1);

        tft.setTextColor(TFT_YELLOW, tft.color565(30, 30, 50));
        tft.drawString(schoolName, w / 2, 5);

        tft.setTextColor(TFT_DARKGREY, tft.color565(30, 30, 50));
        tft.setTextDatum(MC_DATUM);
        tft.setTextSize(1);
        char msg[50];
        snprintf(msg, sizeof(msg), "Upload %s to LittleFS", filename);
        tft.drawString(msg, 160, 120);
        tft.setTextDatum(TL_DATUM);
        return;
    }

    bool oldSwap = tft.getSwapBytes();
    tft.setSwapBytes(true);

    uint16_t lineBuf[320];
    for (int y = 0; y < 240; y++) {
        if (imgFile.read((uint8_t*)lineBuf, 640) != 640) break;
        tft.pushImage(0, y, 320, 1, lineBuf);
    }

    imgFile.close();
    tft.setSwapBytes(oldSwap);

    // School name with shadow
    tft.setTextDatum(TC_DATUM);
    int nameLen = strlen(schoolName);
    if (nameLen <= 10)       tft.setTextSize(3);
    else if (nameLen <= 16)  tft.setTextSize(2);
    else                     tft.setTextSize(1);

    tft.setTextColor(TFT_BLACK);
    tft.drawString(schoolName, w / 2 + 1, 6);
    tft.drawString(schoolName, w / 2 - 1, 4);
    tft.drawString(schoolName, w / 2 + 1, 4);
    tft.drawString(schoolName, w / 2 - 1, 6);

    tft.setTextColor(tft.color565(nameColorR, nameColorG, nameColorB));
    tft.drawString(schoolName, w / 2, 5);
    tft.setTextDatum(TL_DATUM);
}

void DisplayManager::drawBgSchool()    { drawImageBg("/school.bin");    }
void DisplayManager::drawBgHospital()  { drawImageBg("/hospital.bin");  }
void DisplayManager::drawBgHostel()    { drawImageBg("/hostel.bin");    }
void DisplayManager::drawBgOffice()    { drawImageBg("/office.bin");    }
void DisplayManager::drawBgVenue()     { drawImageBg("/venue.bin");     }
void DisplayManager::drawBgTemple()    { drawImageBg("/temple.bin");    }
void DisplayManager::drawBgSmartHome() { drawImageBg("/smarthome.bin"); }
void DisplayManager::drawBgCustom()    { drawImageBg("/custom.bin");    }

// ========================================================
//  HEADER
// ========================================================

void DisplayManager::drawHeader() {
    tft.fillRect(0, 0, 320, 40, TFT_NAVY);
    tft.setTextColor(TFT_WHITE, TFT_NAVY);
    tft.setTextSize(2);
    tft.setTextDatum(TC_DATUM);
    tft.drawString(schoolName, 160, 12);
    tft.setTextDatum(TL_DATUM);
}

// ========================================================
//  FOOTER
// ========================================================

void DisplayManager::drawFooter(RTCManager &rtc, EventManager &events) {
    uint8_t hour, minute, weekday;
    rtc.getTime(hour, minute, weekday);

    tft.fillRect(0, 220, 320, 20, TFT_NAVY);
    tft.setTextColor(TFT_WHITE, TFT_NAVY);
    tft.setTextSize(1);
    tft.setCursor(5, 225);

    if (events.getCount() == 0) {
        tft.print("No Music");
    } else {
        bool isNow = false;
        Event* nextEvent = findNextEventToday(events, hour, minute, weekday, isNow);

        if (nextEvent != nullptr) {
            tft.print("\x0E ");
            String name = getTrackName(nextEvent->track, 32);
            tft.print(name);
        } else {
            tft.print("No more events today");
        }
    }
}

// ========================================================
//  CLOCK
// ========================================================

void DisplayManager::drawClock(uint8_t hour, uint8_t minute) {
    char timeStr[6];
    sprintf(timeStr, "%02d:%02d", hour, minute);

    int cx = 160, cy = 60;

    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(3);

    tft.setTextColor(TFT_BLACK);
    tft.drawString(timeStr, cx + 1, cy + 1);
    tft.drawString(timeStr, cx - 1, cy - 1);
    tft.drawString(timeStr, cx + 1, cy - 1);
    tft.drawString(timeStr, cx - 1, cy + 1);

    tft.setTextColor(tft.color565(clockColorR, clockColorG, clockColorB));
    tft.drawString(timeStr, cx, cy);

    lastClockHour   = hour;
    lastClockMinute = minute;
    colonVisible    = true;
    lastColonToggle = millis();
}

// ========================================================
//  COLON BLINK
// ========================================================

void DisplayManager::updateColonBlink() {
    if (currentScreen != SCREEN_HOME) return;
    if (lastClockHour == 255) return;

    if (millis() - lastColonToggle < 1000) return;
    lastColonToggle = millis();
    colonVisible = !colonVisible;

    int cx = 160, cy = 60;
    int colonX = cx - 5;
    int colonY = cy - 11;
    int colonW = 11;
    int colonH = 22;

    if (colonVisible) {
        tft.setTextDatum(MC_DATUM);
        tft.setTextSize(3);

        tft.setTextColor(TFT_BLACK);
        tft.drawString(":", cx + 1, cy + 1);
        tft.drawString(":", cx - 1, cy - 1);
        tft.drawString(":", cx + 1, cy - 1);
        tft.drawString(":", cx - 1, cy + 1);

        tft.setTextColor(tft.color565(clockColorR, clockColorG, clockColorB));
        tft.drawString(":", cx, cy);

    } else {
        // Re-read background image to erase colon area
        const char* bgFiles[] = {
            "/school.bin", "/hospital.bin", "/hostel.bin",
            "/office.bin", "/venue.bin",    "/temple.bin",
            "/smarthome.bin"
        };
        const char* bgFile = (bgTheme == 7) ? "/custom.bin" :
                             (bgTheme < 7)  ? bgFiles[bgTheme] :
                                              "/school.bin";

        fs::File imgFile = LittleFS.open(bgFile, "r");
        if (imgFile) {
            bool oldSwap = tft.getSwapBytes();
            tft.setSwapBytes(true);
            uint16_t lineBuf[320];
            for (int y = colonY; y < colonY + colonH; y++) {
                if (y < 0 || y >= 240) continue;
                imgFile.seek(y * 640);
                if (imgFile.read((uint8_t*)lineBuf, 640) == 640) {
                    tft.pushImage(colonX, y, colonW, 1, &lineBuf[colonX]);
                }
            }
            imgFile.close();
            tft.setSwapBytes(oldSwap);
        } else {
            tft.fillRect(colonX, colonY, colonW, colonH,
                         tft.color565(30, 30, 50));
        }
    }
}

// ========================================================
//  WiFi ICON
// ========================================================

void DisplayManager::drawWiFiIcon(bool connected) {
    const int badgeX = 293;
    const int badgeY = 1;
    const int badgeW = 26;
    const int badgeH = 22;
    const int cx = 306;
    const int cy = 19;
    const uint16_t badgeBg = 0x18E3;

    tft.fillRoundRect(badgeX, badgeY, badgeW, badgeH, 3, badgeBg);

    uint16_t color = connected ? TFT_GREEN : 0x6B4D;

    tft.fillCircle(cx, cy, 2, color);

    const int radii[] = {6, 10, 14};
    const float startA = -2.356f;
    const float endA   = -0.785f;

    for (int a = 0; a < 3; a++) {
        int r = radii[a];
        for (float ang = startA; ang <= endA; ang += 0.025f) {
            float cosA = cosf(ang);
            float sinA = sinf(ang);
            for (int t = 0; t < 2; t++) {
                int px = cx + (int)((r + t) * cosA);
                int py = cy + (int)((r + t) * sinA);
                if (px >= badgeX && px < badgeX + badgeW &&
                    py >= badgeY && py < badgeY + badgeH) {
                    tft.drawPixel(px, py, color);
                }
            }
        }
    }

    if (!connected) {
        for (int t = -1; t <= 1; t++) {
            tft.drawLine(cx + 8 + t, cy - 15, cx - 6 + t, cy + 1, TFT_RED);
        }
    }

    lastWiFiState = connected;
    wifiIconDrawn = true;
}

void DisplayManager::updateWiFiIcon(bool connected) {
    if (currentScreen != SCREEN_HOME) {
        wifiIconDrawn = false;
        return;
    }
    if (connected == lastWiFiState && wifiIconDrawn) return;
    drawWiFiIcon(connected);
}

// ========================================================
//  DATE
// ========================================================

void DisplayManager::drawDate(uint8_t day, uint8_t month,
                               uint16_t year, uint8_t weekday) {
    static const char* SHORT_DAYS[] = {
        "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
    };

    char dateStr[20];
    snprintf(dateStr, sizeof(dateStr), "%s, %02d/%02d/%04d",
             SHORT_DAYS[weekday % 7], day, month, year);

    int cx = 160;
    int cy = DATE_Y;

    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(DATE_TEXT_SIZE);

    tft.setTextColor(TFT_BLACK);
    tft.drawString(dateStr, cx + 1, cy + 1);
    tft.drawString(dateStr, cx - 1, cy - 1);
    tft.drawString(dateStr, cx + 1, cy - 1);
    tft.drawString(dateStr, cx - 1, cy + 1);

    tft.setTextColor(tft.color565(clockColorR, clockColorG, clockColorB));
    tft.drawString(dateStr, cx, cy);
}

// ========================================================
//  MANUAL RINGTONE ROW (partial redraw)
// ========================================================

void DisplayManager::drawManualRingtoneRow(int index, bool isSelected) {
    int startY = 40;
    int rowH   = 18;
    int y      = startY + (index * rowH);
    uint16_t track = RINGTONE_START_TRACK + index;

    uint16_t rowBg = isSelected ? 0x2A4A :
                     (index % 2 == 0) ? 0x18E3 : 0x2104;

    tft.fillRect(5, y, 310, rowH - 1, rowBg);

    if (isSelected) {
        tft.drawRect(5, y, 310, rowH - 1, TFT_YELLOW);
    }

    tft.setTextDatum(ML_DATUM);
    tft.setTextSize(1);

    if (isSelected) {
        tft.setTextColor(TFT_YELLOW, rowBg);
        tft.drawString(">", 8, y + rowH / 2);
    }

    char numStr[5];
    sprintf(numStr, "%2d.", index + 1);
    tft.setTextColor(isSelected ? TFT_YELLOW : TFT_LIGHTGREY, rowBg);
    tft.drawString(numStr, 18, y + rowH / 2);

    String name = getTrackName(track, 38);
    tft.setTextColor(isSelected ? TFT_WHITE : TFT_LIGHTGREY, rowBg);
    tft.drawString(name, 50, y + rowH / 2);
}

// ========================================================
//  HELPER: Find next event for today
// ========================================================

Event* DisplayManager::findNextEventToday(EventManager &events,
                                           uint8_t hour,
                                           uint8_t minute,
                                           uint8_t weekday,
                                           bool &isNow)
{
    isNow = false;
    Event* bestNow      = nullptr;
    Event* bestUpcoming = nullptr;
    int    bestUpcomingMins = 9999;

    int currentMins = hour * 60 + minute;

    for (int i = 0; i < events.getCount(); i++) {
        Event* ev = &events.getEvents()[i];

        if (!ev->enabled)           continue;
        if (!ev->isActiveToday(weekday)) continue;

        int evMins;

        if (ev->isInterval()) {
            uint16_t nextTrig = ev->nextTriggerAfter(currentMins);
            if (nextTrig >= 9999) continue;
            evMins = nextTrig;

            if (ev->isIntervalTrigger(hour, minute)) {
                bestNow = ev;
                continue;
            }
        } else {
            evMins = ev->hour * 60 + ev->minute;
        }

        if (evMins == currentMins) {
            bestNow = ev;
        } else if (evMins > currentMins) {
            int diff = evMins - currentMins;
            if (diff < bestUpcomingMins) {
                bestUpcomingMins = diff;
                bestUpcoming = ev;
            }
        }
    }

    if (bestNow) {
        isNow = true;
        return bestNow;
    }
    return bestUpcoming;
}

// ========================================================
//  EVENT CARD
// ========================================================

void DisplayManager::drawEventCard(RTCManager &rtc, EventManager &events) {
    uint8_t hour, minute, weekday;
    rtc.getTime(hour, minute, weekday);

    int cardY = 193;
    int cardH = 26;

    tft.fillRoundRect(10, cardY, 300, cardH, 6, TFT_WHITE);
    tft.setTextDatum(ML_DATUM);

    if (events.getCount() == 0) {
        tft.setTextColor(TFT_DARKGREY, TFT_WHITE);
        tft.setTextSize(2);
        tft.drawString("No Events", 20, cardY + cardH / 2);
        return;
    }

    bool isNow = false;
    Event* nextEvent = findNextEventToday(events, hour, minute, weekday, isNow);

    if (nextEvent == nullptr) {
        tft.setTextColor(TFT_DARKGREY, TFT_WHITE);
        tft.setTextSize(2);
        tft.drawString("Done for Today", 20, cardY + cardH / 2);
        return;
    }

    char timeStr[6];
    sprintf(timeStr, "%02d:%02d", nextEvent->hour, nextEvent->minute);
    int textY = cardY + cardH / 2;

    if (isNow) {
        tft.fillRoundRect(12, cardY + 2, 38, cardH - 4, 4, TFT_RED);
        tft.setTextColor(TFT_WHITE, TFT_RED);
        tft.setTextSize(1);
        tft.drawString("NOW", 17, textY);

        tft.setTextColor(TFT_BLACK, TFT_WHITE);
        tft.setTextSize(2);

        if (nextEvent->hasName()) {
            String disp = String(nextEvent->name) + " " + timeStr;
            if (disp.length() > 18) disp = disp.substring(0, 16) + "..";
            tft.drawString(disp, 56, textY);
        } else {
            tft.drawString(timeStr, 56, textY);
        }
    } else {
        tft.fillRoundRect(12, cardY + 2, 38, cardH - 4, 4, TFT_NAVY);
        tft.setTextColor(TFT_WHITE, TFT_NAVY);
        tft.setTextSize(1);
        tft.drawString("NEXT", 15, textY);

        tft.setTextColor(TFT_BLACK, TFT_WHITE);
        tft.setTextSize(2);

        if (nextEvent->hasName()) {
            String disp = String(nextEvent->name) + " " + timeStr;
            if (disp.length() > 18) disp = disp.substring(0, 16) + "..";
            tft.drawString(disp, 56, textY);
        } else {
            tft.drawString(String("Event ") + timeStr, 56, textY);
        }
    }

    tft.setTextDatum(TL_DATUM);
}

// ========================================================
//  MENU SCREEN
// ========================================================

void DisplayManager::drawMenu() {
    tft.fillScreen(UI_BG_DARK);

    int boxW = 140, boxH = 90;
    int x1 = 10,  x2 = 170;
    int y1 = 20,  y2 = 130;

    drawMenuItem(0, x1, y1, boxW, boxH, "Scheduler",    0, UI_TILE_BLUE);
    drawMenuItem(1, x2, y1, boxW, boxH, "Date & Time",  1, UI_TILE_GREEN);
    drawMenuItem(2, x1, y2, boxW, boxH, "Announcement", 2, UI_TILE_ORANGE);
    drawMenuItem(3, x2, y2, boxW, boxH, "Manual Bell",  3, UI_TILE_PURPLE);
}

void DisplayManager::drawMenuItem(int index, int x, int y, int w, int h,
                                   String label, int iconType,
                                   uint16_t tileColor) {
    bool selected = (menuIndex == index);

    tft.fillRoundRect(x + 4, y + 4, w, h, 12, UI_SHADOW);
    tft.fillRoundRect(x, y, w, h, 12, tileColor);

    if (selected) {
        tft.drawRoundRect(x - 2, y - 2, w + 4, h + 4, 14, TFT_DARKGREY);
        tft.drawRoundRect(x - 1, y - 1, w + 2, h + 2, 13, TFT_WHITE);
        tft.drawRoundRect(x,     y,     w,     h,     12, TFT_WHITE);
        tft.drawRoundRect(x + 1, y + 1, w - 2, h - 2, 11, TFT_WHITE);
        tft.drawRoundRect(x + 2, y + 2, w - 4, h - 4, 10, TFT_WHITE);
    } else {
        tft.drawRoundRect(x, y, w, h, 12, TFT_LIGHTGREY);
    }

    drawIcon(iconType, x + w / 2, y + 35, TFT_WHITE);

    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE, tileColor);
    tft.drawString(label, x + w / 2, y + h - 20);
}

void DisplayManager::drawIcon(int type, int cx, int cy, uint16_t color) {
    switch (type) {
        case 0: {
            tft.fillRect(cx - 18, cy - 14, 36, 28, TFT_WHITE);
            tft.drawRect(cx - 18, cy - 14, 36, 28, TFT_BLACK);
            tft.fillRect(cx - 18, cy - 14, 36, 7,  TFT_RED);
            int startX = cx - 12, startY = cy - 3;
            for (int r = 0; r < 2; r++)
                for (int c = 0; c < 3; c++)
                    tft.fillRect(startX + c * 8, startY + r * 8, 5, 5, TFT_BLUE);
        } break;
        case 1: {
            tft.fillCircle(cx, cy, 16, TFT_WHITE);
            tft.drawCircle(cx, cy, 16, TFT_BLACK);
            tft.drawLine(cx, cy, cx, cy - 10, TFT_BLACK);
            tft.drawLine(cx, cy, cx + 8, cy + 6, TFT_RED);
            tft.drawCircle(cx, cy, 2, TFT_BLACK);
        } break;
        case 2: {
            tft.fillRect(cx - 14, cy - 16, 28, 32, TFT_WHITE);
            tft.drawRect(cx - 14, cy - 16, 28, 32, TFT_BLACK);
            tft.drawCircle(cx, cy + 6, 9, TFT_BLACK);
            tft.drawCircle(cx, cy + 6, 5, TFT_BLACK);
            tft.fillCircle(cx, cy - 8, 4, TFT_BLACK);
        } break;
        case 3: {
            tft.fillCircle(cx, cy, 14, TFT_WHITE);
            tft.drawCircle(cx, cy, 14, TFT_BLACK);
            tft.fillCircle(cx, cy, 3,  TFT_BLACK);
            tft.drawLine(cx + 18, cy - 10, cx + 8, cy - 2, TFT_BLACK);
            tft.fillCircle(cx + 18, cy - 10, 4, TFT_BLACK);
        } break;
    }
}

// ========================================================
//  SCHEDULER SCREEN
// ========================================================

void DisplayManager::drawScheduler(EventManager &events) {
    tft.fillScreen(UI_BG_DARK);

    tft.fillRect(0, 0, 320, 35, TFT_NAVY);
    tft.setTextColor(TFT_WHITE, TFT_NAVY);
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(2);
    tft.drawString("Scheduled Events", 160, 18);

    uint8_t count = events.getCount();

    if (count == 0) {
        tft.setTextColor(TFT_LIGHTGREY, UI_BG_DARK);
        tft.setTextSize(2);
        tft.setTextDatum(MC_DATUM);
        tft.drawString("No events set", 160, 100);
        tft.setTextSize(1);
        tft.drawString("Use phone to add events", 160, 130);
        return;
    }

    Event* eventList = events.getEvents();

    const int maxVisible = 5;
    const int startY     = 40;
    const int rowHeight  = 33;

    if (schedulerCursorIndex >= count) {
        schedulerCursorIndex = 0;
        schedulerScrollOffset = 0;
    }
    if (count <= maxVisible) {
        schedulerScrollOffset = 0;
    } else if (schedulerScrollOffset > count - maxVisible) {
        schedulerScrollOffset = count - maxVisible;
    }

    int showCount = (count > maxVisible) ? maxVisible : count;

    for (int i = 0; i < showCount; i++) {
        int eventIndex = i + schedulerScrollOffset;
        if (eventIndex >= count) break;

        Event &e = eventList[eventIndex];
        int y = startY + (i * rowHeight);
        bool isSelected = (eventIndex == schedulerCursorIndex);

        uint16_t rowColor = (i % 2 == 0) ? 0x18E3 : 0x2104;
        tft.fillRect(5, y, 310, rowHeight - 2, rowColor);

        if (isSelected) {
            tft.drawRect(5, y, 310, rowHeight - 2, TFT_YELLOW);
            tft.drawRect(6, y + 1, 308, rowHeight - 4, TFT_YELLOW);
        }

        char numStr[5];
        sprintf(numStr, "%d.", eventIndex + 1);
        tft.setTextDatum(ML_DATUM);
        tft.setTextSize(1);
        tft.setTextColor(isSelected ? TFT_YELLOW : TFT_LIGHTGREY, rowColor);
        tft.drawString(numStr, 10, y + rowHeight / 2);

        char timeStr[6];
        sprintf(timeStr, "%02d:%02d", e.hour, e.minute);
        tft.setTextSize(2);
        tft.setTextColor(isSelected ? TFT_YELLOW : TFT_WHITE, rowColor);
        tft.drawString(timeStr, 30, y + rowHeight / 2);

        tft.setTextSize(1);
        tft.setTextColor(isSelected ? TFT_WHITE : TFT_LIGHTGREY, rowColor);
        if (e.hasName()) {
            char nameBuf[13];
            strncpy(nameBuf, e.name, 12);
            nameBuf[12] = '\0';
            tft.drawString(nameBuf, 110, y + rowHeight / 2);
        } else if (e.isInterval()) {
            tft.setTextColor(TFT_CYAN, rowColor);
            tft.drawString("Interval", 110, y + rowHeight / 2);
        } else {
            tft.drawString("Event", 110, y + rowHeight / 2);
        }

        const char dayLetters[] = "SMTWTFS";
        char days[8];
        for (int d = 0; d < 7; d++)
            days[d] = (e.weekdayMask & (1 << d)) ? dayLetters[d] : '-';
        days[7] = '\0';
        tft.setTextColor(TFT_CYAN, rowColor);
        tft.drawString(days, 220, y + rowHeight / 2);

        tft.fillCircle(305, y + rowHeight / 2, 4,
                       e.enabled ? TFT_GREEN : TFT_RED);
    }

    int detailY = startY + (showCount * rowHeight) + 2;
    tft.fillRect(5, detailY, 310, 16, 0x1082);
    Event &selected = eventList[schedulerCursorIndex];
    String trackName = getTrackName(selected.track, 45);
    tft.setTextDatum(ML_DATUM);
    tft.setTextSize(1);
    tft.setTextColor(TFT_CYAN, 0x1082);
    tft.drawString("~ " + trackName, 10, detailY + 8);

    if (count > maxVisible) {
        int barX = 317;
        int barAreaY = startY;
        int barAreaH = showCount * rowHeight;
        tft.fillRect(barX, barAreaY, 3, barAreaH, 0x2104);
        int thumbH = max(10, barAreaH * maxVisible / count);
        int thumbY = barAreaY + (barAreaH - thumbH) *
                     schedulerScrollOffset / (count - maxVisible);
        tft.fillRect(barX, thumbY, 3, thumbH, TFT_CYAN);
    }

    tft.fillRect(0, 225, 320, 15, TFT_NAVY);
    tft.setTextColor(TFT_LIGHTGREY, TFT_NAVY);
    tft.setTextSize(1);
    tft.setTextDatum(MC_DATUM);
    char footerStr[50];
    sprintf(footerStr, "MENU:Back | NEXT:Scroll  [%d/%d]",
            schedulerCursorIndex + 1, count);
    tft.drawString(footerStr, 160, 232);
}

// ========================================================
//  DATE & TIME SCREEN
// ========================================================

void DisplayManager::drawDateTime(RTCManager &rtc) {
    uint16_t year;
    uint8_t month, day, hour, minute, weekday;
    rtc.getDateTime(year, month, day, hour, minute, weekday);

    tft.fillScreen(UI_BG_DARK);

    tft.fillRect(0, 0, 320, 35, TFT_NAVY);
    tft.setTextColor(TFT_WHITE, TFT_NAVY);
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(2);
    tft.drawString("Date & Time", 160, 18);

    tft.fillRoundRect(30, 55, 260, 55, 10, 0x2104);
    tft.drawRoundRect(30, 55, 260, 55, 10, TFT_LIGHTGREY);
    tft.setTextColor(TFT_LIGHTGREY, 0x2104);
    tft.setTextSize(1);
    tft.setTextDatum(ML_DATUM);
    tft.drawString("DATE", 45, 65);

    char dateStr[20];
    sprintf(dateStr, "%02d / %02d / %04d", day, month, year);
    tft.setTextColor(TFT_WHITE, 0x2104);
    tft.setTextSize(3);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(dateStr, 160, 95);

    tft.setTextColor(TFT_CYAN, UI_BG_DARK);
    tft.setTextSize(2);
    tft.drawString(DAY_NAMES[weekday], 160, 125);

    tft.fillRoundRect(30, 145, 260, 55, 10, 0x2104);
    tft.drawRoundRect(30, 145, 260, 55, 10, TFT_LIGHTGREY);
    tft.setTextColor(TFT_LIGHTGREY, 0x2104);
    tft.setTextSize(1);
    tft.setTextDatum(ML_DATUM);
    tft.drawString("TIME", 45, 155);

    char timeStr[6];
    sprintf(timeStr, "%02d:%02d", hour, minute);
    tft.setTextColor(TFT_YELLOW, 0x2104);
    tft.setTextSize(3);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(timeStr, 160, 185);

    tft.fillRect(0, 225, 320, 15, TFT_NAVY);
    tft.setTextColor(TFT_LIGHTGREY, TFT_NAVY);
    tft.setTextSize(1);
    tft.drawString("MENU: Back  |  Set via phone", 160, 232);
}

// ========================================================
//  LIVE ANNOUNCEMENT SCREEN
// ========================================================

void DisplayManager::drawLive() {
    tft.fillScreen(UI_BG_DARK);

    uint16_t titleColor = liveActive ? TFT_RED : TFT_NAVY;
    tft.fillRect(0, 0, 320, 35, titleColor);
    tft.setTextColor(TFT_WHITE, titleColor);
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(2);
    tft.drawString("Live Announcement", 160, 18);

    drawIcon(2, 160, 90, TFT_WHITE);

    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(2);

    if (liveActive) {
        tft.fillCircle(130, 140, 8, TFT_RED);
        tft.setTextColor(TFT_RED, UI_BG_DARK);
        tft.drawString("LIVE", 170, 140);
        tft.setTextColor(TFT_WHITE, UI_BG_DARK);
        tft.setTextSize(1);
        tft.drawString("Microphone is active", 160, 165);
        tft.drawString("All scheduled bells are paused", 160, 180);
    } else {
        tft.setTextColor(TFT_LIGHTGREY, UI_BG_DARK);
        tft.drawString("Ready", 160, 140);
        tft.setTextSize(1);
        tft.drawString("Press PLAY to start announcement", 160, 165);
        tft.drawString("Or trigger from phone", 160, 180);
    }

    tft.fillRect(0, 225, 320, 15, TFT_NAVY);
    tft.setTextColor(TFT_LIGHTGREY, TFT_NAVY);
    tft.setTextSize(1);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(liveActive ?
        "PLAY: Stop  |  MENU: Back (stops)" :
        "PLAY: Start  |  MENU: Back",
        160, 232);
}

// ========================================================
//  MANUAL BELL SCREEN
// ========================================================

void DisplayManager::drawManual() {
    tft.fillScreen(UI_BG_DARK);

    if (manualActive) {
        tft.fillRect(0, 0, 320, 35, UI_TILE_PURPLE);
        tft.setTextColor(TFT_WHITE, UI_TILE_PURPLE);
        tft.setTextDatum(MC_DATUM);
        tft.setTextSize(2);
        tft.drawString("Manual Bell", 160, 18);

        drawIcon(3, 160, 80, TFT_WHITE);

        tft.setTextDatum(MC_DATUM);
        tft.setTextSize(2);
        tft.setTextColor(TFT_YELLOW, UI_BG_DARK);
        tft.drawString("RINGING!", 160, 120);

        tft.setTextColor(TFT_WHITE, UI_BG_DARK);
        tft.setTextSize(1);
        if (currentPlayingTrack > 0) {
            tft.drawString(getTrackName(currentPlayingTrack, 40), 160, 150);
        } else {
            tft.drawString("Bell is playing now", 160, 150);
        }
        tft.drawString("Schedule is paused", 160, 170);

        tft.fillRect(0, 225, 320, 15, TFT_NAVY);
        tft.setTextColor(TFT_LIGHTGREY, TFT_NAVY);
        tft.setTextSize(1);
        tft.setTextDatum(MC_DATUM);
        tft.drawString("PLAY: Stop  |  MENU: Back (stops)", 160, 232);

    } else {
        tft.fillRect(0, 0, 320, 35, TFT_NAVY);
        tft.setTextColor(TFT_WHITE, TFT_NAVY);
        tft.setTextDatum(MC_DATUM);
        tft.setTextSize(2);
        tft.drawString("Select Ringtone", 160, 18);

        int startY = 40, rowH = 18;

        for (int i = 0; i < RINGTONE_COUNT; i++) {
            int y = startY + (i * rowH);
            uint16_t track = RINGTONE_START_TRACK + i;
            bool isSelected = (i == selectedRingtone);

            uint16_t rowBg = isSelected ? 0x2A4A :
                             (i % 2 == 0) ? 0x18E3 : 0x2104;

            tft.fillRect(5, y, 310, rowH - 1, rowBg);

            if (isSelected)
                tft.drawRect(5, y, 310, rowH - 1, TFT_YELLOW);

            tft.setTextDatum(ML_DATUM);
            tft.setTextSize(1);

            if (isSelected) {
                tft.setTextColor(TFT_YELLOW, rowBg);
                tft.drawString(">", 8, y + rowH / 2);
            }

            char numStr[5];
            sprintf(numStr, "%2d.", i + 1);
            tft.setTextColor(isSelected ? TFT_YELLOW : TFT_LIGHTGREY, rowBg);
            tft.drawString(numStr, 18, y + rowH / 2);

            tft.setTextColor(isSelected ? TFT_WHITE : TFT_LIGHTGREY, rowBg);
            tft.drawString(getTrackName(track, 38), 50, y + rowH / 2);
        }

        tft.fillRect(0, 225, 320, 15, TFT_NAVY);
        tft.setTextColor(TFT_LIGHTGREY, TFT_NAVY);
        tft.setTextSize(1);
        tft.setTextDatum(MC_DATUM);
        tft.drawString("NEXT: Scroll | PLAY: Ring | MENU: Back",
                       160, 232);
    }
}

// ========================================================
//  FACTORY RESET SCREEN
// ========================================================

void DisplayManager::drawFactoryReset(int secondsLeft, bool done,
                                       bool cancelled) {
    if (done) {
        tft.fillScreen(TFT_BLACK);
        tft.fillRoundRect(30, 60, 260, 120, 12, tft.color565(0, 80, 0));
        tft.drawRoundRect(30, 60, 260, 120, 12, TFT_GREEN);
        tft.setTextDatum(MC_DATUM);
        tft.setTextSize(2);
        tft.setTextColor(TFT_GREEN, tft.color565(0, 80, 0));
        tft.drawString("FACTORY RESET", 160, 90);
        tft.drawString("COMPLETE!", 160, 115);
        tft.setTextSize(1);
        tft.setTextColor(TFT_WHITE, tft.color565(0, 80, 0));
        tft.drawString("Rebooting in 3 seconds...", 160, 150);
        return;
    }

    if (cancelled) {
        tft.fillScreen(TFT_BLACK);
        tft.fillRoundRect(30, 80, 260, 80, 12, tft.color565(0, 0, 80));
        tft.drawRoundRect(30, 80, 260, 80, 12, TFT_CYAN);
        tft.setTextDatum(MC_DATUM);
        tft.setTextSize(2);
        tft.setTextColor(TFT_CYAN, tft.color565(0, 0, 80));
        tft.drawString("CANCELLED", 160, 110);
        tft.setTextSize(1);
        tft.setTextColor(TFT_WHITE, tft.color565(0, 0, 80));
        tft.drawString("Normal boot continuing...", 160, 140);
        return;
    }

    tft.fillScreen(TFT_BLACK);
    tft.fillRoundRect(20, 30, 280, 160, 12, tft.color565(80, 0, 0));
    tft.drawRoundRect(20, 30, 280, 160, 12, TFT_RED);
    tft.fillTriangle(160, 45, 140, 75, 180, 75, TFT_YELLOW);
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(2);
    tft.setTextColor(TFT_BLACK, TFT_YELLOW);
    tft.drawString("!", 160, 64);
    tft.setTextColor(TFT_RED, tft.color565(80, 0, 0));
    tft.drawString("FACTORY RESET", 160, 95);
    char countStr[4];
    sprintf(countStr, "%d", secondsLeft);
    tft.setTextSize(3);
    tft.setTextColor(TFT_YELLOW, tft.color565(80, 0, 0));
    tft.drawString(countStr, 160, 125);
    int barX = 40, barY = 150, barW = 240, barH = 14;
    int filled = barW * (10 - secondsLeft) / 10;
    tft.drawRoundRect(barX, barY, barW, barH, 4, TFT_WHITE);
    if (filled > 0)
        tft.fillRoundRect(barX + 2, barY + 2, filled - 4, barH - 4, 2, TFT_RED);
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE, tft.color565(80, 0, 0));
    tft.drawString("Press NEXT or OK to cancel", 160, 175);
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.drawString("All settings will be erased!", 160, 210);
}

// ========================================================
//  PIN RESET SCREEN
// ========================================================

void DisplayManager::drawPinReset(int secondsLeft, bool done,
                                   bool cancelled) {
    if (done) {
        tft.fillScreen(TFT_BLACK);
        tft.fillRoundRect(30, 50, 260, 140, 12, tft.color565(0, 80, 0));
        tft.drawRoundRect(30, 50, 260, 140, 12, TFT_GREEN);
        tft.setTextDatum(MC_DATUM);
        tft.setTextSize(2);
        tft.setTextColor(TFT_GREEN, tft.color565(0, 80, 0));
        tft.drawString("PIN RESET", 160, 80);
        tft.drawString("COMPLETE!", 160, 105);
        char pinStr[16];
        snprintf(pinStr, sizeof(pinStr), "PIN: %s", DEFAULT_PASSWORD);
        tft.setTextSize(2);
        tft.setTextColor(TFT_YELLOW, tft.color565(0, 80, 0));
        tft.drawString(pinStr, 160, 140);
        tft.setTextSize(1);
        tft.setTextColor(TFT_WHITE, tft.color565(0, 80, 0));
        tft.drawString("Returning to home...", 160, 170);
        return;
    }

    if (cancelled) {
        tft.fillScreen(TFT_BLACK);
        tft.fillRoundRect(30, 80, 260, 80, 12, tft.color565(0, 0, 80));
        tft.drawRoundRect(30, 80, 260, 80, 12, TFT_CYAN);
        tft.setTextDatum(MC_DATUM);
        tft.setTextSize(2);
        tft.setTextColor(TFT_CYAN, tft.color565(0, 0, 80));
        tft.drawString("CANCELLED", 160, 110);
        tft.setTextSize(1);
        tft.setTextColor(TFT_WHITE, tft.color565(0, 0, 80));
        tft.drawString("PIN unchanged", 160, 135);
        return;
    }

    // ── Countdown screen — amber/orange theme ──
    tft.fillScreen(TFT_BLACK);

    uint16_t amberBg     = tft.color565(80, 50, 0);
    uint16_t amberBorder = tft.color565(255, 180, 0);

    tft.fillRoundRect(20, 30, 280, 160, 12, amberBg);
    tft.drawRoundRect(20, 30, 280, 160, 12, amberBorder);

    // Lock icon
    int lx = 160, ly = 55;
    for (int t = 0; t < 3; t++)
        tft.drawCircle(lx, ly, 10 - t, TFT_YELLOW);
    tft.fillRect(lx - 12, ly, 24, 12, amberBg);
    tft.fillRoundRect(lx - 12, ly + 2, 24, 18, 3, TFT_YELLOW);
    tft.fillCircle(lx, ly + 9, 3, amberBg);
    tft.fillRect(lx - 1, ly + 9, 3, 6, amberBg);

    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(amberBorder, amberBg);
    tft.setTextSize(2);
    tft.drawString("PIN RESET", 160, 95);

    char countStr[4];
    sprintf(countStr, "%d", secondsLeft);
    tft.setTextSize(3);
    tft.setTextColor(TFT_YELLOW, amberBg);
    tft.drawString(countStr, 160, 125);

    int barX = 40, barY = 150, barW = 240, barH = 14;
    int filled = barW * (10 - secondsLeft) / 10;
    tft.drawRoundRect(barX, barY, barW, barH, 4, TFT_WHITE);
    if (filled > 0)
        tft.fillRoundRect(barX + 2, barY + 2, filled - 4, barH - 4, 2, amberBorder);

    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE, amberBg);
    tft.drawString("Press PLAY to cancel", 160, 175);

    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    char bottomStr[32];
    snprintf(bottomStr, sizeof(bottomStr), "PIN will reset to %s", DEFAULT_PASSWORD);
    tft.drawString(bottomStr, 160, 210);
}

// ════════════════════════════════════════════════════════════════
//  FIRST-BOOT CONNECTION GUIDE
//  Shown once on fresh flash or after factory reset.
//  Triggered from main.cpp when /setup_done.txt does not exist.
//  Advances page by page on OK button press.
//  PLAY skips the entire guide.
//  Screen: 320×240 ILI9341, Rotation 3
// ════════════════════════════════════════════════════════════════

// ── Guide internal colour palette ──────────────────────────────
#define G_PURPLE     tft.color565(60,  0, 120)
#define G_DKGREEN    tft.color565(0,  80,  30)
#define G_NAVY       tft.color565(0,  30,  90)
#define G_DARKORANGE tft.color565(100, 45,  0)
#define G_BOXBG      tft.color565(15,  15,  25)
#define G_GREY       tft.color565(100,100,100)

// ── Draw filled progress dots ───────────────────────────────────
// filled = how many dots are active (0..total)
static void _guideDots(TFT_eSPI &t, uint8_t filled, uint8_t total) {
    const int R       = 6;
    const int spacing = 26;
    const int cy      = 213;
    const int startX  = 160 - ((total - 1) * spacing / 2);
    for (uint8_t i = 0; i < total; i++) {
        uint16_t col = (i < filled) ? TFT_YELLOW : t.color565(50, 50, 50);
        t.fillCircle(startX + i * spacing, cy, R, col);
        t.drawCircle(startX + i * spacing, cy, R, TFT_WHITE);
    }
}

// ── Draw a labelled value box ────────────────────────────────────
// Draws a rounded box with a small label at top-left and big value centred.
static void _guideBox(TFT_eSPI &t,
                      int x, int y, int w, int h,
                      const char* label,
                      uint16_t labelCol,
                      const char* value,
                      uint16_t valueCol,
                      uint16_t borderCol) {
    t.fillRoundRect(x,     y,     w,     h,     8, t.color565(15, 15, 25));
    t.drawRoundRect(x,     y,     w,     h,     8, borderCol);
    t.drawRoundRect(x + 1, y + 1, w - 2, h - 2, 7, borderCol);

    // label (small, top-left inside box)
    t.setTextDatum(ML_DATUM);
    t.setTextSize(1);
    t.setTextColor(labelCol, t.color565(15, 15, 25));
    t.drawString(label, x + 10, y + 10);

    // value (large, centred)
    t.setTextDatum(MC_DATUM);
    t.setTextSize(3);
    t.setTextColor(valueCol, t.color565(15, 15, 25));
    t.drawString(value, x + w / 2, y + h / 2 + 6);
}

// ── Draw bottom footer bar ───────────────────────────────────────
static void _guideFooter(TFT_eSPI &t, const char* text) {
    t.fillRect(0, 226, 320, 14, t.color565(20, 20, 20));
    t.setTextDatum(MC_DATUM);
    t.setTextSize(1);
    t.setTextColor(t.color565(140, 140, 140), t.color565(20, 20, 20));
    t.drawString(text, 160, 233);
}

// ════════════════════════════════════════════════════════════════
void DisplayManager::showGuidePage(uint8_t page) {

    tft.fillScreen(TFT_BLACK);

    switch (page) {

    // ────────────────────────────────────────────────────────────
    //  PAGE 0 — WELCOME
    // ────────────────────────────────────────────────────────────
    case 0: {

        // ── Gradient-style header band ─────────────────────────
        tft.fillRect(0, 0, 320, 50, G_PURPLE);
        tft.drawFastHLine(0, 50, 320, TFT_MAGENTA);

        tft.setTextDatum(MC_DATUM);

        // Title
        tft.setTextSize(3);
        tft.setTextColor(TFT_BLACK, G_PURPLE);
        tft.drawString("KAALANADHAM", 161, 25 + 1);
        tft.setTextColor(TFT_CYAN, G_PURPLE);
        tft.drawString("KAALANADHAM", 160, 25);

        // Sub-title
        tft.setTextSize(1);
        tft.setTextColor(tft.color565(180, 180, 180), G_PURPLE);
        tft.drawString("Smart Bell System", 160, 42);

        // Welcome message
        tft.setTextSize(2);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.drawString("Welcome!", 160, 78);

        // Body text
        tft.setTextSize(1);
        tft.setTextColor(tft.color565(200, 200, 200), TFT_BLACK);
        tft.drawString("Let's get you connected to the", 160, 105);
        tft.drawString("web app in 3 simple steps.", 160, 120);

        // Divider
        tft.drawFastHLine(30, 137, 260, tft.color565(50, 50, 50));

        // Steps preview row
        const char* stepLabels[] = { "WiFi", "Browser", "PIN" };
        const uint16_t stepCols[] = { TFT_GREEN, TFT_CYAN, TFT_YELLOW };
        for (int i = 0; i < 3; i++) {
            int bx = 30 + i * 90;
            tft.fillRoundRect(bx, 145, 75, 38, 6,
                              tft.color565(20, 20, 20));
            tft.drawRoundRect(bx, 145, 75, 38, 6, stepCols[i]);
            tft.setTextSize(1);
            tft.setTextColor(stepCols[i], tft.color565(20, 20, 20));

            char stepNum[4];
            sprintf(stepNum, "0%d", i + 1);
            tft.drawString(stepNum, bx + 37, 153);
            tft.drawString(stepLabels[i], bx + 37, 166);
        }

        // OK prompt
        tft.setTextSize(2);
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
        tft.drawString("Press  OK  to Start", 160, 196);

        _guideFooter(tft, "PLAY = Skip guide    OK = Next");
        break;
    }

    // ────────────────────────────────────────────────────────────
    //  PAGE 1 — STEP 1: CONNECT TO WiFi
    // ────────────────────────────────────────────────────────────
    case 1: {

        // ── Header ──────────────────────────────────────────────
        tft.fillRect(0, 0, 320, 46, G_DKGREEN);
        tft.drawFastHLine(0, 46, 320, TFT_GREEN);

        tft.setTextDatum(MC_DATUM);

        tft.setTextSize(1);
        tft.setTextColor(TFT_GREEN, G_DKGREEN);
        tft.drawString("STEP 1 OF 3", 160, 10);

        tft.setTextSize(2);
        tft.setTextColor(TFT_WHITE, G_DKGREEN);
        tft.drawString("Connect to WiFi", 160, 30);

        // ── Instruction ─────────────────────────────────────────
        tft.setTextSize(1);
        tft.setTextColor(tft.color565(200, 200, 200), TFT_BLACK);
        tft.drawString("Open WiFi settings on your phone", 160, 60);
        tft.drawString("and connect to this network:", 160, 74);

        // ── Network name box ────────────────────────────────────
        _guideBox(tft,
                  8, 84, 304, 48,
                  "  Network Name",
                  tft.color565(120, 120, 120),
                  "KAALANADHAM",
                  TFT_CYAN,
                  TFT_CYAN);

        // ── Password box ────────────────────────────────────────
        _guideBox(tft,
                  8, 138, 304, 48,
                  "  Password",
                  tft.color565(120, 120, 120),
                  "1234",
                  TFT_MAGENTA,
                  TFT_MAGENTA);

        // ── Progress dots ────────────────────────────────────────
        _guideDots(tft, 1, 3);

        // ── Prompt ───────────────────────────────────────────────
        tft.setTextSize(1);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.drawString("Connected?   Press  OK  >>", 160, 195);

        _guideFooter(tft, "PLAY = Skip    OK = Next step");
        break;
    }

    // ────────────────────────────────────────────────────────────
    //  PAGE 2 — STEP 2: OPEN BROWSER
    // ────────────────────────────────────────────────────────────
    case 2: {

        // ── Header ──────────────────────────────────────────────
        tft.fillRect(0, 0, 320, 46, G_NAVY);
        tft.drawFastHLine(0, 46, 320, TFT_CYAN);

        tft.setTextDatum(MC_DATUM);

        tft.setTextSize(1);
        tft.setTextColor(TFT_CYAN, G_NAVY);
        tft.drawString("STEP 2 OF 3", 160, 10);

        tft.setTextSize(2);
        tft.setTextColor(TFT_WHITE, G_NAVY);
        tft.drawString("Open Browser", 160, 30);

        // ── Instruction ─────────────────────────────────────────
        tft.setTextSize(1);
        tft.setTextColor(tft.color565(200, 200, 200), TFT_BLACK);
        tft.drawString("Open Chrome, Safari or any browser.", 160, 60);
        tft.drawString("Type this address and press Go:", 160, 74);

        // ── URL box (big, prominent) ─────────────────────────────
        // Outer glow effect — draw larger box in accent colour first
        tft.fillRoundRect(6, 84, 308, 70, 10,
                          tft.color565(0, 60, 80));
        tft.drawRoundRect(6,     84,     308,    70, 10, TFT_YELLOW);
        tft.drawRoundRect(7,     85,     306,    68, 9,  TFT_YELLOW);

        // Label
        tft.setTextDatum(ML_DATUM);
        tft.setTextSize(1);
        tft.setTextColor(tft.color565(150, 150, 150),
                         tft.color565(0, 60, 80));
        tft.drawString("  Address Bar", 16, 94);

        // URL — primary (mDNS)
        tft.setTextDatum(MC_DATUM);
        tft.setTextSize(2);
        tft.setTextColor(TFT_BLACK, tft.color565(0, 60, 80));
        tft.drawString("kaalanadham.local", 161, 113);   // shadow
        tft.setTextColor(TFT_YELLOW, tft.color565(0, 60, 80));
        tft.drawString("kaalanadham.local", 160, 112);   // main

        // "or try" label
        tft.setTextSize(1);
        tft.setTextColor(tft.color565(140, 140, 140), tft.color565(0, 60, 80));
        tft.drawString("or try:", 160, 130);

        // Fallback IP — size 2 (bigger)
        tft.setTextSize(2);
        tft.setTextColor(TFT_BLACK, tft.color565(0, 60, 80));
        tft.drawString("192.168.4.1", 161, 148);   // shadow
        tft.setTextColor(tft.color565(180, 180, 180), tft.color565(0, 60, 80));
        tft.drawString("192.168.4.1", 160, 147);   // main

        // ── Hint text ────────────────────────────────────────────
        tft.setTextSize(1);
        tft.setTextColor(tft.color565(180, 180, 180), TFT_BLACK);
        tft.drawString("Then press  Enter  or  Go", 160, 164);

        // ── Progress dots ────────────────────────────────────────
        _guideDots(tft, 2, 3);

        // ── Prompt ───────────────────────────────────────────────
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.drawString("Page loaded?   Press  OK  >>", 160, 195);

        _guideFooter(tft, "PLAY = Skip    OK = Next step");
        break;
    }

    // ────────────────────────────────────────────────────────────
    //  PAGE 3 — STEP 3: ENTER PIN
    // ────────────────────────────────────────────────────────────
    case 3: {

        // ── Header ──────────────────────────────────────────────
        tft.fillRect(0, 0, 320, 46, G_DARKORANGE);
        tft.drawFastHLine(0, 46, 320, TFT_ORANGE);

        tft.setTextDatum(MC_DATUM);

        tft.setTextSize(1);
        tft.setTextColor(TFT_ORANGE, G_DARKORANGE);
        tft.drawString("STEP 3 OF 3", 160, 10);

        tft.setTextSize(2);
        tft.setTextColor(TFT_WHITE, G_DARKORANGE);
        tft.drawString("Enter the PIN", 160, 30);

        // ── Instruction ─────────────────────────────────────────
        tft.setTextSize(1);
        tft.setTextColor(tft.color565(200, 200, 200), TFT_BLACK);
        tft.drawString("The web app will ask for a PIN.", 160, 60);
        tft.drawString("Enter the default PIN:", 160, 74);

        // ── PIN box ─────────────────────────────────────────────
        // Prominent green box for PIN
        tft.fillRoundRect(60, 84, 200, 68, 10,
                          tft.color565(0, 50, 10));
        tft.drawRoundRect(60,     84,     200,    68, 10, TFT_GREEN);
        tft.drawRoundRect(61,     85,     198,    66,  9, TFT_GREEN);

        tft.setTextDatum(MC_DATUM);
        tft.setTextSize(1);
        tft.setTextColor(G_GREY, tft.color565(0, 50, 10));
        tft.drawString("Default PIN", 160, 94);

        // PIN digits — very large
        tft.setTextSize(4);
        tft.setTextColor(TFT_BLACK, tft.color565(0, 50, 10));
        tft.drawString("1234", 161, 132);     // shadow
        tft.setTextColor(TFT_GREEN, tft.color565(0, 50, 10));
        tft.drawString("1234", 160, 131);     // main

        // ── Tip ──────────────────────────────────────────────────
        tft.setTextSize(1);
        tft.setTextColor(tft.color565(180, 180, 180), TFT_BLACK);
        tft.drawString("You can change this PIN later", 160, 163);

        // ── Progress dots — all filled ────────────────────────────
        _guideDots(tft, 3, 3);

        // ── Done prompt ───────────────────────────────────────────
        tft.setTextSize(2);
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
        tft.drawString("Press  OK  to Finish!", 160, 195);

        _guideFooter(tft, "OK = Enter web app now");
        break;
    }

    default:
        break;
    }
}