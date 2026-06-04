#include "StorageManager.h"

bool StorageManager::begin() {
    if (!LittleFS.begin(true)) {
        Serial.println("[Storage] LittleFS mount failed!");
        return false;
    }
    Serial.println("[Storage] LittleFS mounted OK.");
    return true;
}

void StorageManager::saveEvents(uint16_t nextId,
                                 uint8_t count,
                                 Event* events)
{
    File file = LittleFS.open(EVENTS_FILE, "w");
    if (!file) {
        Serial.println("[Storage] Failed to open file for writing!");
        return;
    }

    uint8_t magic = EVENT_FORMAT_V3;
    file.write(&magic, 1);
    file.write((uint8_t*)&nextId, sizeof(nextId));
    file.write((uint8_t*)&count, sizeof(count));
    file.write((uint8_t*)events, sizeof(Event) * count);
    file.close();

    Serial.printf("[Storage] Saved %d events (v3 format).\n", count);
}

bool StorageManager::loadEvents(uint16_t &nextId,
                                 uint8_t &count,
                                 Event* events)
{
    if (!LittleFS.exists(EVENTS_FILE)) {
        Serial.println("[Storage] No events file found.");
        return false;
    }

    File file = LittleFS.open(EVENTS_FILE, "r");
    if (!file) {
        Serial.println("[Storage] Failed to open file for reading!");
        return false;
    }

    uint8_t firstByte;
    file.read(&firstByte, 1);

    if (firstByte == EVENT_FORMAT_V3) {
        // ── V3 format (current): magic(0xA3) + nextId(2) + count(1) + events ──
        file.read((uint8_t*)&nextId, sizeof(nextId));
        file.read((uint8_t*)&count, sizeof(count));

        if (count > MAX_EVENTS) {
            Serial.println("[Storage] Corrupt v3 data!");
            count = 0;
            file.close();
            return false;
        }

        file.read((uint8_t*)events, sizeof(Event) * count);
        file.close();

        for (int i = 0; i < count; i++) {
            events[i].lastTriggeredMinute = -1;
        }

        Serial.printf("[Storage] Loaded %d events (v3 format).\n", count);
        return true;

    } else if (firstByte == 0xA4) {
        // ── V4 format (Phase C) — struct size changed, cannot read safely ──
        Serial.println("[Storage] V4 format detected — events cleared (downgrade)");
        count = 0;
        nextId = 1;
        file.close();
        saveEvents(nextId, count, events);
        return false;

    } else if (firstByte == EVENT_FORMAT_V2) {
        // ── V2 format: magic(0xA2) + nextId(2) + count(1) + v2 events ──
        file.read((uint8_t*)&nextId, sizeof(nextId));
        file.read((uint8_t*)&count, sizeof(count));

        if (count > MAX_EVENTS) {
            count = 0;
            file.close();
            return false;
        }

        EventV2* v2Events = new EventV2[MAX_EVENTS];
        file.read((uint8_t*)v2Events, sizeof(EventV2) * count);
        file.close();

        for (int i = 0; i < count; i++) {
            events[i].id = v2Events[i].id;
            events[i].hour = v2Events[i].hour;
            events[i].minute = v2Events[i].minute;
            events[i].weekdayMask = v2Events[i].weekdayMask;
            events[i].folder = v2Events[i].folder;
            events[i].track = v2Events[i].track;
            events[i].enabled = v2Events[i].enabled;
            strncpy(events[i].name, v2Events[i].name, MAX_EVENT_NAME_LEN);
            events[i].name[MAX_EVENT_NAME_LEN] = '\0';
            events[i].duration = v2Events[i].duration;
            events[i].eventType = EVENT_TYPE_FIXED;
            events[i].intervalMinutes = 0;
            events[i].endMinute = 0;
            events[i].lastTriggeredMinute = -1;
        }

        delete[] v2Events;
        Serial.printf("[Storage] Migrated %d events from v2 → v3.\n", count);
        saveEvents(nextId, count, events);
        return true;

    } else {
        // ── V1 format (legacy): no magic byte ──
        file.seek(0);
        file.read((uint8_t*)&nextId, sizeof(nextId));
        file.read((uint8_t*)&count, sizeof(count));

        if (count > MAX_EVENTS) {
            count = 0;
            file.close();
            return false;
        }

        EventV1* v1Events = new EventV1[MAX_EVENTS];
        size_t v1Size = sizeof(EventV1) * count;
        size_t bytesRead = file.read((uint8_t*)v1Events, v1Size);
        file.close();

        if (bytesRead < v1Size) {
            count = bytesRead / sizeof(EventV1);
        }

        for (int i = 0; i < count; i++) {
            events[i].id = v1Events[i].id;
            events[i].hour = v1Events[i].hour;
            events[i].minute = v1Events[i].minute;
            events[i].weekdayMask = v1Events[i].weekdayMask;
            events[i].folder = v1Events[i].folder;
            events[i].track = v1Events[i].track;
            events[i].enabled = v1Events[i].enabled;
            strncpy(events[i].name, v1Events[i].name, MAX_EVENT_NAME_LEN);
            events[i].name[MAX_EVENT_NAME_LEN] = '\0';
            events[i].duration = 0;
            events[i].eventType = EVENT_TYPE_FIXED;
            events[i].intervalMinutes = 0;
            events[i].endMinute = 0;
            events[i].lastTriggeredMinute = -1;
        }

        delete[] v1Events;
        Serial.printf("[Storage] Migrated %d events from v1 → v3.\n", count);
        saveEvents(nextId, count, events);
        return true;
    }
}

void StorageManager::clearEvents() {
    if (LittleFS.exists(EVENTS_FILE)) {
        LittleFS.remove(EVENTS_FILE);
        Serial.println("[Storage] Events file deleted.");
    }
}

void StorageManager::saveSchoolName(const char* name) {
    File file = LittleFS.open(SCHOOL_NAME_FILE, "w");
    if (!file) {
        Serial.println("[Storage] Failed to save school name!");
        return;
    }
    file.print(name);
    file.close();
    Serial.printf("[Storage] School name saved: %s\n", name);
}

bool StorageManager::loadSchoolName(char* name, size_t maxLen) {
    if (!LittleFS.exists(SCHOOL_NAME_FILE)) {
        Serial.println("[Storage] No school name file found.");
        return false;
    }
    File file = LittleFS.open(SCHOOL_NAME_FILE, "r");
    if (!file) {
        Serial.println("[Storage] Failed to read school name!");
        return false;
    }
    String s = file.readStringUntil('\n');
    s.trim();
    file.close();

    if (s.length() == 0) return false;

    strncpy(name, s.c_str(), maxLen - 1);
    name[maxLen - 1] = '\0';
    Serial.printf("[Storage] School name loaded: %s\n", name);
    return true;
}

void StorageManager::saveSounds(const String &data) {
    File file = LittleFS.open(SOUNDS_FILE, "w");
    if (!file) {
        Serial.println("[Storage] Failed to save sounds!");
        return;
    }
    file.print(data);
    file.close();
    Serial.println("[Storage] Sounds saved to flash.");

    _soundsCacheValid = false;
    loadSoundsCache();
    Serial.printf("[Storage] Sounds cache refreshed: %d names\n", _soundsCount);
}

String StorageManager::loadSoundsJSON() {
    if (!_soundsCacheValid) loadSoundsCache();
    if (_soundsCache.length() == 0) return "[]";

    String json = "[";
    bool first = true;
    int idx = 0;

    while (idx < (int)_soundsCache.length()) {
        int nl = _soundsCache.indexOf('\n', idx);
        String line;
        if (nl >= 0) {
            line = _soundsCache.substring(idx, nl);
            idx = nl + 1;
        } else {
            line = _soundsCache.substring(idx);
            idx = _soundsCache.length();
        }
        line.trim();
        if (line.length() == 0) continue;

        int commaIdx = line.indexOf(',');
        if (commaIdx < 0) continue;

        String trackStr = line.substring(0, commaIdx);
        trackStr.trim();
        String nameStr = line.substring(commaIdx + 1);
        nameStr.trim();

        if (trackStr.length() == 0 || nameStr.length() == 0) continue;

        if (!first) json += ",";
        json += "{\"track\":" + trackStr + ",\"name\":\"" + nameStr + "\"}";
        first = false;
    }

    json += "]";
    return json;
}

String StorageManager::getSoundName(uint16_t track) {
    if (!_soundsCacheValid) loadSoundsCache();
    if (_soundsCache.length() == 0) return "";

    char target[6];
    sprintf(target, "%04d,", track);

    int idx = _soundsCache.indexOf(target);
    while (idx >= 0) {
        if (idx == 0 || _soundsCache.charAt(idx - 1) == '\n') {
            int nameStart = idx + 5;
            int nameEnd = _soundsCache.indexOf('\n', nameStart);
            if (nameEnd < 0) nameEnd = _soundsCache.length();

            String name = _soundsCache.substring(nameStart, nameEnd);
            name.trim();
            return name;
        }
        idx = _soundsCache.indexOf(target, idx + 1);
    }
    return "";
}

void StorageManager::loadSoundsCache() {
    _soundsCache = "";
    _soundsCount = 0;
    _soundsCacheValid = true;

    if (!LittleFS.exists(SOUNDS_FILE)) {
        Serial.println("[Storage] No sounds file — cache empty");
        return;
    }

    File f = LittleFS.open(SOUNDS_FILE, "r");
    if (!f) {
        Serial.println("[Storage] Failed to open sounds file for cache");
        return;
    }

    _soundsCache = f.readString();
    f.close();

    int idx = 0;
    while (idx < (int)_soundsCache.length()) {
        int nl = _soundsCache.indexOf('\n', idx);
        String line;
        if (nl >= 0) {
            line = _soundsCache.substring(idx, nl);
            idx = nl + 1;
        } else {
            line = _soundsCache.substring(idx);
            idx = _soundsCache.length();
        }
        line.trim();
        if (line.length() > 0 && line.indexOf(',') >= 0) {
            _soundsCount++;
        }
    }

    Serial.printf("[Storage] Sounds cache loaded: %d names (%d bytes)\n",
                  _soundsCount, _soundsCache.length());
}

uint16_t StorageManager::getSoundCount() {
    if (!_soundsCacheValid) loadSoundsCache();
    return _soundsCount;
}

void StorageManager::saveTheme(uint8_t theme) {
    File file = LittleFS.open(THEME_FILE, "w");
    if (!file) {
        Serial.println("[Storage] Failed to save theme!");
        return;
    }
    file.print(theme);
    file.close();
    Serial.printf("[Storage] Theme saved: %d\n", theme);
}

uint8_t StorageManager::loadTheme() {
    if (!LittleFS.exists(THEME_FILE)) {
        Serial.println("[Storage] No theme file found, using default.");
        return DEFAULT_BG_THEME;
    }
    File file = LittleFS.open(THEME_FILE, "r");
    if (!file) {
        return DEFAULT_BG_THEME;
    }
    String s = file.readStringUntil('\n');
    s.trim();
    file.close();

    uint8_t theme = s.toInt();
    if (theme >= MAX_BG_THEMES) theme = DEFAULT_BG_THEME;
    Serial.printf("[Storage] Theme loaded: %d\n", theme);
    return theme;
}

bool StorageManager::saveCustomBg(uint8_t *data, size_t len, size_t offset, size_t total) {
    const char* path = "/custom.bin";

    if (offset == 0) {
        File f = LittleFS.open(path, "w");
        if (!f) {
            Serial.println("[Storage] Failed to create custom.bin");
            return false;
        }
        f.write(data, len);
        f.close();
        Serial.printf("[Storage] Custom BG: first chunk %d/%d bytes\n", len, total);
    } else {
        File f = LittleFS.open(path, "a");
        if (!f) {
            Serial.println("[Storage] Failed to append custom.bin");
            return false;
        }
        f.write(data, len);
        f.close();
        Serial.printf("[Storage] Custom BG: chunk at offset %d, %d bytes\n", offset, len);
    }

    if (offset + len >= total) {
        Serial.printf("[Storage] Custom BG complete: %d bytes\n", total);
        return true;
    }
    return true;
}

bool StorageManager::hasCustomBg() {
    return LittleFS.exists("/custom.bin");
}

void StorageManager::saveColors(uint8_t nR, uint8_t nG, uint8_t nB,
                                 uint8_t cR, uint8_t cG, uint8_t cB) {
    File f = LittleFS.open(COLORS_FILE, "w");
    if (!f) return;
    f.printf("%d,%d,%d\n%d,%d,%d\n", nR, nG, nB, cR, cG, cB);
    f.close();
    Serial.printf("[Storage] Colors saved: name=#%02X%02X%02X clock=#%02X%02X%02X\n",
                  nR, nG, nB, cR, cG, cB);
}

bool StorageManager::loadColors(uint8_t &nR, uint8_t &nG, uint8_t &nB,
                                 uint8_t &cR, uint8_t &cG, uint8_t &cB) {
    File f = LittleFS.open(COLORS_FILE, "r");
    if (!f) return false;

    String line1 = f.readStringUntil('\n');
    String line2 = f.readStringUntil('\n');
    f.close();

    int r, g, b;
    if (sscanf(line1.c_str(), "%d,%d,%d", &r, &g, &b) == 3) {
        nR = r; nG = g; nB = b;
    } else {
        return false;
    }
    if (sscanf(line2.c_str(), "%d,%d,%d", &r, &g, &b) == 3) {
        cR = r; cG = g; cB = b;
    } else {
        return false;
    }

    Serial.printf("[Storage] Colors loaded: name=#%02X%02X%02X clock=#%02X%02X%02X\n",
                  nR, nG, nB, cR, cG, cB);
    return true;
}

// ========================================================
//  HOLIDAYS
// ========================================================

bool StorageManager::addHoliday(uint16_t year, uint8_t month, uint8_t day, const char* name) {
    if (getHolidayCount() >= MAX_HOLIDAYS) {
        Serial.println("[Storage] Max holidays reached!");
        return false;
    }

    char dateStr[12];
    snprintf(dateStr, sizeof(dateStr), "%04d-%02d-%02d", year, month, day);

    if (LittleFS.exists(HOLIDAYS_FILE)) {
        File f = LittleFS.open(HOLIDAYS_FILE, "r");
        if (f) {
            while (f.available()) {
                String line = f.readStringUntil('\n');
                line.trim();
                if (line.startsWith(dateStr)) {
                    f.close();
                    Serial.printf("[Storage] Holiday %s already exists\n", dateStr);
                    return false;
                }
            }
            f.close();
        }
    }

    File f = LittleFS.open(HOLIDAYS_FILE, "a");
    if (!f) {
        Serial.println("[Storage] Failed to open holidays file for append!");
        return false;
    }

    if (name && name[0]) {
        f.printf("%s,%s\n", dateStr, name);
    } else {
        f.printf("%s,\n", dateStr);
    }
    f.close();

    _holCacheValid = false;

    Serial.printf("[Storage] Holiday added: %s %s\n", dateStr,
                  (name && name[0]) ? name : "(no name)");
    return true;
}

bool StorageManager::removeHoliday(const char* dateStr) {
    if (!LittleFS.exists(HOLIDAYS_FILE)) return false;

    File f = LittleFS.open(HOLIDAYS_FILE, "r");
    if (!f) return false;

    String newContent = "";
    bool found = false;

    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;

        if (line.startsWith(dateStr)) {
            found = true;
        } else {
            newContent += line + "\n";
        }
    }
    f.close();

    if (!found) {
        Serial.printf("[Storage] Holiday %s not found\n", dateStr);
        return false;
    }

    f = LittleFS.open(HOLIDAYS_FILE, "w");
    if (!f) return false;
    f.print(newContent);
    f.close();

    _holCacheValid = false;

    Serial.printf("[Storage] Holiday removed: %s\n", dateStr);
    return true;
}

bool StorageManager::isHoliday(uint16_t year, uint8_t month, uint8_t day) {
    if (_holCacheValid &&
        _holCacheYear == year &&
        _holCacheMonth == month &&
        _holCacheDay == day) {
        return _holCacheResult;
    }

    _holCacheYear  = year;
    _holCacheMonth = month;
    _holCacheDay   = day;
    _holCacheResult = false;
    _holCacheValid  = true;

    if (!LittleFS.exists(HOLIDAYS_FILE)) return false;

    char dateStr[12];
    snprintf(dateStr, sizeof(dateStr), "%04d-%02d-%02d", year, month, day);

    File f = LittleFS.open(HOLIDAYS_FILE, "r");
    if (!f) return false;

    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.startsWith(dateStr)) {
            f.close();
            _holCacheResult = true;
            return true;
        }
    }
    f.close();
    return false;
}

String StorageManager::loadHolidaysJSON() {
    if (!LittleFS.exists(HOLIDAYS_FILE)) {
        return "[]";
    }

    File f = LittleFS.open(HOLIDAYS_FILE, "r");
    if (!f) return "[]";

    String json = "[";
    bool first = true;

    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() < 10) continue;

        String dateStr = line.substring(0, 10);
        String name = "";
        if (line.length() > 11) {
            name = line.substring(11);
            name.trim();
        }

        if (!first) json += ",";
        json += "{\"date\":\"" + dateStr + "\",\"name\":\"" + name + "\"}";
        first = false;
    }
    f.close();

    json += "]";
    return json;
}

uint8_t StorageManager::getHolidayCount() {
    if (!LittleFS.exists(HOLIDAYS_FILE)) return 0;

    File f = LittleFS.open(HOLIDAYS_FILE, "r");
    if (!f) return 0;

    uint8_t count = 0;
    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() >= 10) count++;
    }
    f.close();
    return count;
}

void StorageManager::clearHolidays() {
    if (LittleFS.exists(HOLIDAYS_FILE)) {
        LittleFS.remove(HOLIDAYS_FILE);
        _holCacheValid = false;
        Serial.println("[Storage] Holidays cleared.");
    }
}

// ========================================================
//  PASSWORD
// ========================================================

void StorageManager::savePassword(const char* password) {
    File f = LittleFS.open(PASSWORD_FILE, "w");
    if (!f) {
        Serial.println("[Storage] Failed to save password!");
        return;
    }
    f.print(password);
    f.close();
    Serial.println("[Storage] Password saved.");
}

bool StorageManager::loadPassword(char* password, size_t maxLen) {
    if (!LittleFS.exists(PASSWORD_FILE)) {
        Serial.println("[Storage] No password file, using default.");
        return false;
    }
    File f = LittleFS.open(PASSWORD_FILE, "r");
    if (!f) return false;

    String s = f.readStringUntil('\n');
    s.trim();
    f.close();

    if (s.length() == 0) return false;

    strncpy(password, s.c_str(), maxLen - 1);
    password[maxLen - 1] = '\0';
    Serial.println("[Storage] Password loaded.");
    return true;
}

// ========================================================
//  TEMPLATES
// ========================================================

static String templatePath(uint8_t id) {
    char buf[16];
    snprintf(buf, sizeof(buf), "/tpl_%d.bin", id);
    return String(buf);
}

bool StorageManager::saveTemplate(uint8_t id, const char* name,
                                   uint16_t nextId, uint8_t count, Event* events)
{
    if (id >= MAX_TEMPLATES) return false;

    String path = templatePath(id);
    File f = LittleFS.open(path, "w");
    if (!f) {
        Serial.println("[Storage] Failed to save template binary!");
        return false;
    }

    uint8_t magic = EVENT_FORMAT_V3;
    f.write(&magic, 1);
    f.write((uint8_t*)&nextId, sizeof(nextId));
    f.write((uint8_t*)&count, sizeof(count));
    f.write((uint8_t*)events, sizeof(Event) * count);
    f.close();

    String newIndex = "";
    if (LittleFS.exists(TEMPLATES_INDEX)) {
        File idx = LittleFS.open(TEMPLATES_INDEX, "r");
        if (idx) {
            while (idx.available()) {
                String line = idx.readStringUntil('\n');
                line.trim();
                if (line.length() == 0) continue;
                int comma = line.indexOf(',');
                if (comma < 0) continue;
                int lineId = line.substring(0, comma).toInt();
                if (lineId != id) {
                    newIndex += line + "\n";
                }
            }
            idx.close();
        }
    }

    char entry[64];
    snprintf(entry, sizeof(entry), "%d,%s\n", id, name);
    newIndex += String(entry);

    File idx = LittleFS.open(TEMPLATES_INDEX, "w");
    if (!idx) return false;
    idx.print(newIndex);
    idx.close();

    Serial.printf("[Storage] Template #%d saved: %s (%d events, v3)\n", id, name, count);
    return true;
}

bool StorageManager::loadTemplate(uint8_t id, uint16_t &nextId,
                                   uint8_t &count, Event* events)
{
    String path = templatePath(id);
    if (!LittleFS.exists(path)) {
        Serial.printf("[Storage] Template #%d not found\n", id);
        return false;
    }

    File f = LittleFS.open(path, "r");
    if (!f) return false;

    uint8_t firstByte;
    f.read(&firstByte, 1);

    if (firstByte == EVENT_FORMAT_V3) {
        // ── V3 format (current) ──
        f.read((uint8_t*)&nextId, sizeof(nextId));
        f.read((uint8_t*)&count, sizeof(count));
        if (count > MAX_EVENTS) { count = 0; f.close(); return false; }

        f.read((uint8_t*)events, sizeof(Event) * count);
        f.close();
        for (int i = 0; i < count; i++) {
            events[i].lastTriggeredMinute = -1;
        }
        Serial.printf("[Storage] Template #%d loaded: %d events (v3)\n", id, count);
        return true;

    } else if (firstByte == 0xA4) {
        // ── V4 template (Phase C) — can't read, struct size changed ──
        Serial.printf("[Storage] Template #%d is V4 — skipped (downgrade)\n", id);
        count = 0;
        f.close();
        return false;

    } else if (firstByte == EVENT_FORMAT_V2) {
        f.read((uint8_t*)&nextId, sizeof(nextId));
        f.read((uint8_t*)&count, sizeof(count));
        if (count > MAX_EVENTS) { count = 0; f.close(); return false; }

        EventV2* v2Events = new EventV2[MAX_EVENTS];
        f.read((uint8_t*)v2Events, sizeof(EventV2) * count);
        f.close();

        for (int i = 0; i < count; i++) {
            events[i].id = v2Events[i].id;
            events[i].hour = v2Events[i].hour;
            events[i].minute = v2Events[i].minute;
            events[i].weekdayMask = v2Events[i].weekdayMask;
            events[i].folder = v2Events[i].folder;
            events[i].track = v2Events[i].track;
            events[i].enabled = v2Events[i].enabled;
            strncpy(events[i].name, v2Events[i].name, MAX_EVENT_NAME_LEN);
            events[i].name[MAX_EVENT_NAME_LEN] = '\0';
            events[i].duration = v2Events[i].duration;
            events[i].eventType = EVENT_TYPE_FIXED;
            events[i].intervalMinutes = 0;
            events[i].endMinute = 0;
            events[i].lastTriggeredMinute = -1;
        }
        delete[] v2Events;
        Serial.printf("[Storage] Template #%d migrated v2→v3: %d events\n", id, count);
        return true;

    } else {
        // ── V1 format (legacy) ──
        f.seek(0);
        f.read((uint8_t*)&nextId, sizeof(nextId));
        f.read((uint8_t*)&count, sizeof(count));
        if (count > MAX_EVENTS) { count = 0; f.close(); return false; }

        EventV1* v1Events = new EventV1[MAX_EVENTS];
        f.read((uint8_t*)v1Events, sizeof(EventV1) * count);
        f.close();

        for (int i = 0; i < count; i++) {
            events[i].id = v1Events[i].id;
            events[i].hour = v1Events[i].hour;
            events[i].minute = v1Events[i].minute;
            events[i].weekdayMask = v1Events[i].weekdayMask;
            events[i].folder = v1Events[i].folder;
            events[i].track = v1Events[i].track;
            events[i].enabled = v1Events[i].enabled;
            strncpy(events[i].name, v1Events[i].name, MAX_EVENT_NAME_LEN);
            events[i].name[MAX_EVENT_NAME_LEN] = '\0';
            events[i].duration = 0;
            events[i].eventType = EVENT_TYPE_FIXED;
            events[i].intervalMinutes = 0;
            events[i].endMinute = 0;
            events[i].lastTriggeredMinute = -1;
        }
        delete[] v1Events;
        Serial.printf("[Storage] Template #%d migrated v1→v3: %d events\n", id, count);
        return true;
    }
}

bool StorageManager::deleteTemplate(uint8_t id) {
    String path = templatePath(id);
    bool deleted = false;

    if (LittleFS.exists(path)) {
        LittleFS.remove(path);
        deleted = true;
    }

    if (LittleFS.exists(TEMPLATES_INDEX)) {
        String newIndex = "";
        File idx = LittleFS.open(TEMPLATES_INDEX, "r");
        if (idx) {
            while (idx.available()) {
                String line = idx.readStringUntil('\n');
                line.trim();
                if (line.length() == 0) continue;
                int comma = line.indexOf(',');
                if (comma < 0) continue;
                int lineId = line.substring(0, comma).toInt();
                if (lineId != id) {
                    newIndex += line + "\n";
                }
            }
            idx.close();
        }

        File w = LittleFS.open(TEMPLATES_INDEX, "w");
        if (w) {
            w.print(newIndex);
            w.close();
        }
    }

    Serial.printf("[Storage] Template #%d deleted: %s\n", id, deleted ? "OK" : "not found");
    return deleted;
}

String StorageManager::loadTemplatesJSON() {
    if (!LittleFS.exists(TEMPLATES_INDEX)) return "[]";

    File idx = LittleFS.open(TEMPLATES_INDEX, "r");
    if (!idx) return "[]";

    String json = "[";
    bool first = true;

    while (idx.available()) {
        String line = idx.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;

        int comma = line.indexOf(',');
        if (comma < 0) continue;

        int id = line.substring(0, comma).toInt();
        String name = line.substring(comma + 1);
        name.trim();

        String path = templatePath(id);
        uint8_t evCount = 0;
        if (LittleFS.exists(path)) {
            File f = LittleFS.open(path, "r");
            if (f) {
                uint8_t peek;
                f.read(&peek, 1);
                if (peek == EVENT_FORMAT_V3 || peek == 0xA4 || peek == EVENT_FORMAT_V2) {
                    uint16_t skip;
                    f.read((uint8_t*)&skip, sizeof(skip));
                    f.read((uint8_t*)&evCount, sizeof(evCount));
                } else {
                    uint8_t secondByte;
                    f.read(&secondByte, 1);
                    f.read((uint8_t*)&evCount, sizeof(evCount));
                }
                f.close();
            }
        }
        if (!first) json += ",";
        json += "{\"id\":" + String(id);
        json += ",\"name\":\"" + name + "\"";
        json += ",\"events\":" + String(evCount) + "}";
        first = false;
    }
    idx.close();

    json += "]";
    return json;
}

uint8_t StorageManager::getTemplateCount() {
    if (!LittleFS.exists(TEMPLATES_INDEX)) return 0;

    File idx = LittleFS.open(TEMPLATES_INDEX, "r");
    if (!idx) return 0;

    uint8_t count = 0;
    while (idx.available()) {
        String line = idx.readStringUntil('\n');
        line.trim();
        if (line.length() > 0 && line.indexOf(',') >= 0) count++;
    }
    idx.close();
    return count;
}

uint8_t StorageManager::getNextTemplateId() {
    bool used[MAX_TEMPLATES] = {false};

    if (LittleFS.exists(TEMPLATES_INDEX)) {
        File idx = LittleFS.open(TEMPLATES_INDEX, "r");
        if (idx) {
            while (idx.available()) {
                String line = idx.readStringUntil('\n');
                line.trim();
                int comma = line.indexOf(',');
                if (comma >= 0) {
                    int id = line.substring(0, comma).toInt();
                    if (id >= 0 && id < MAX_TEMPLATES) used[id] = true;
                }
            }
            idx.close();
        }
    }

    for (int i = 0; i < MAX_TEMPLATES; i++) {
        if (!used[i]) return i;
    }
    return 255;
}

// ========================================================
//  WIFI CREDENTIALS
// ========================================================

void StorageManager::saveWiFiCredentials(const char* ssid, const char* password) {
    File f = LittleFS.open(WIFI_CRED_FILE, "w");
    if (!f) {
        Serial.println("[Storage] Failed to save WiFi credentials!");
        return;
    }
    f.println(ssid);
    f.println(password);
    f.close();
    Serial.printf("[Storage] WiFi credentials saved: SSID=%s\n", ssid);
}

bool StorageManager::loadWiFiCredentials(char* ssid, size_t ssidLen, char* password, size_t passLen) {
    if (!LittleFS.exists(WIFI_CRED_FILE)) return false;

    File f = LittleFS.open(WIFI_CRED_FILE, "r");
    if (!f) return false;

    String s = f.readStringUntil('\n');
    s.trim();
    String p = f.readStringUntil('\n');
    p.trim();
    f.close();

    if (s.length() == 0) return false;

    strncpy(ssid, s.c_str(), ssidLen - 1);
    ssid[ssidLen - 1] = '\0';
    strncpy(password, p.c_str(), passLen - 1);
    password[passLen - 1] = '\0';

    Serial.printf("[Storage] WiFi credentials loaded: SSID=%s\n", ssid);
    return true;
}

bool StorageManager::hasWiFiCredentials() {
    return LittleFS.exists(WIFI_CRED_FILE);
}

void StorageManager::clearWiFiCredentials() {
    if (LittleFS.exists(WIFI_CRED_FILE)) {
        LittleFS.remove(WIFI_CRED_FILE);
        Serial.println("[Storage] WiFi credentials cleared.");
    }
}

void StorageManager::saveWiFiMode(uint8_t mode) {
    File f = LittleFS.open(WIFI_MODE_FILE, "w");
    if (!f) return;
    f.print(mode);
    f.close();
    Serial.printf("[Storage] WiFi mode saved: %d\n", mode);
}

uint8_t StorageManager::loadWiFiMode() {
    if (!LittleFS.exists(WIFI_MODE_FILE)) return 0;
    File f = LittleFS.open(WIFI_MODE_FILE, "r");
    if (!f) return 0;
    String s = f.readStringUntil('\n');
    s.trim();
    f.close();
    return s.toInt();
}

// ========================================================
//  SETUP WIZARD
// ========================================================

bool StorageManager::isFirstBoot() {
    return !LittleFS.exists(SETUP_DONE_FILE);
}

void StorageManager::markSetupDone() {
    File f = LittleFS.open(SETUP_DONE_FILE, "w");
    if (f) {
        f.print("1");
        f.close();
        Serial.println("[Storage] Setup wizard completed");
    }
}

void StorageManager::resetSetup() {
    if (LittleFS.exists(SETUP_DONE_FILE)) {
        LittleFS.remove(SETUP_DONE_FILE);
        Serial.println("[Storage] Setup wizard reset");
    }
}

// ========================================================
//  ENVIRONMENT PROFILE
// ========================================================

void StorageManager::saveProfile(uint8_t profile) {
    File f = LittleFS.open(PROFILE_FILE, "w");
    if (!f) {
        Serial.println("[Storage] Failed to save profile!");
        return;
    }
    f.print(profile);
    f.close();
    Serial.printf("[Storage] Profile saved: %d\n", profile);
}

uint8_t StorageManager::loadProfile() {
    if (!LittleFS.exists(PROFILE_FILE)) {
        Serial.println("[Storage] No profile file, using default.");
        return DEFAULT_PROFILE;
    }
    File f = LittleFS.open(PROFILE_FILE, "r");
    if (!f) return DEFAULT_PROFILE;
    String s = f.readStringUntil('\n');
    s.trim();
    f.close();
    uint8_t profile = s.toInt();
    if (profile >= MAX_PROFILES) profile = DEFAULT_PROFILE;
    Serial.printf("[Storage] Profile loaded: %d\n", profile);
    return profile;
}