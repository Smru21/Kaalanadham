#include "BellServer.h"
#include "webpage.h"
#include <Update.h>
#include <LittleFS.h>

// WiFi SSID matcher — handles hotspots with trailing spaces
static String scanForRealSSID_blocking(const char* storedSSID) {
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

static String matchSSIDFromScanResults(const char* storedSSID) {
    String stored = String(storedSSID);
    String storedTrimmed = stored;
    storedTrimmed.trim();

    int n = WiFi.scanComplete();
    if (n <= 0) return stored;

    for (int i = 0; i < n; i++) {
        String scanned = WiFi.SSID(i);

        if (scanned == stored) {
            return scanned;
        }

        String scannedTrimmed = scanned;
        scannedTrimmed.trim();
        if (scannedTrimmed == storedTrimmed) {
            Serial.printf("[WiFi-Recon] SSID matched: stored=\"%s\"(%d) → broadcast=\"%s\"(%d)\n",
                storedSSID, stored.length(), scanned.c_str(), scanned.length());
            return scanned;
        }
    }

    Serial.println("[WiFi-Recon] No broadcast match, using stored SSID");
    return stored;
}

BellServer::BellServer() : server(WEB_SERVER_PORT) {
    rtcPtr       = nullptr;
    audioPtr     = nullptr;
    eventsPtr    = nullptr;
    storagePtr   = nullptr;
    schedulerPtr = nullptr;
    displayPtr   = nullptr;
    statePtr     = nullptr;
    logPtr       = nullptr;
    _isStationMode = false;
    _wifiConnState = 0;
    _wifiConnStart = 0;
    _lastNTPCheck  = 0;

    _reconState    = RECONNECT_IDLE;
    _reconTimer    = 0;
    _reconBackoff  = WIFI_RECONNECT_BACKOFF_INITIAL;
    _lastConnCheck = 0;
    _reconRealSSID = "";
    _reconEnabled  = true;
    _reconAttempts = 0;

    cbManualStart = nullptr;
    cbManualStop  = nullptr;
    cbLiveStart   = nullptr;
    cbLiveStop    = nullptr;
}

void BellServer::begin(RTCManager *rtc,
                        AudioManager *audio,
                        EventManager *events,
                        StorageManager *storage,
                        Scheduler *scheduler,
                        DisplayManager *display,
                        SystemState *state,
                        SystemLog *log)
{
    rtcPtr       = rtc;
    audioPtr     = audio;
    eventsPtr    = events;
    storagePtr   = storage;
    schedulerPtr = scheduler;
    displayPtr   = display;
    statePtr     = state;
    logPtr       = log;

    setupWiFiAP();
    setupRoutes();

    server.begin();
    Serial.println("[Web] Server started on port 80");
}

// ========================================================
//  AUTO-RECONNECT STATE MACHINE
// ========================================================

void BellServer::reconTransition(ReconnectState newState, const char* reason) {
    Serial.printf("[WiFi-Recon] %d → %d (%s)\n", _reconState, newState, reason);
    _reconState = newState;
    _reconTimer = millis();
}

void BellServer::loopReconnect() {
    if (!_isStationMode || !_reconEnabled) return;
    if (_wifiConnState != 0) return;
    if (!storagePtr->hasWiFiCredentials()) return;

    switch (_reconState) {

    case RECONNECT_IDLE: {
        if (millis() - _lastConnCheck < WIFI_RECONNECT_CHECK_INTERVAL) return;
        _lastConnCheck = millis();

        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("[WiFi-Recon] Connection lost detected!");
            if (logPtr) logPtr->log(LOG_CAT_WIFI, "WiFi connection lost — auto-reconnect starting");
            _reconAttempts = 0;
            _reconBackoff = WIFI_RECONNECT_BACKOFF_INITIAL;
            reconTransition(RECONNECT_WAIT, "disconnect detected");
        }
        break;
    }

    case RECONNECT_WAIT: {
        if (millis() - _reconTimer < WIFI_RECONNECT_COOLDOWN) return;

        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("[WiFi-Recon] Connection recovered during cooldown");
            if (logPtr) logPtr->log(LOG_CAT_WIFI, "WiFi recovered on its own");
            reconTransition(RECONNECT_IDLE, "self-recovered");
            return;
        }

        WiFi.setTxPower(WIFI_TX_POWER_BOOST);
        _reconAttempts++;
        Serial.printf("[WiFi-Recon] Attempt #%d — starting scan\n", _reconAttempts);

        WiFi.scanDelete();
        WiFi.scanNetworks(true, false);
        reconTransition(RECONNECT_SCANNING, "scan started");
        break;
    }

    case RECONNECT_SCANNING:
    case RECONNECT_SCAN_WAIT: {
        int scanResult = WiFi.scanComplete();

        if (scanResult == WIFI_SCAN_RUNNING) {
            if (millis() - _reconTimer > WIFI_RECONNECT_SCAN_TIMEOUT) {
                Serial.println("[WiFi-Recon] Scan timeout");
                WiFi.scanDelete();
                WiFi.setTxPower(WIFI_AP_TX_POWER);
                reconTransition(RECONNECT_BACKOFF, "scan timeout");
            }
            return;
        }

        if (scanResult == WIFI_SCAN_FAILED || scanResult == 0) {
            Serial.printf("[WiFi-Recon] Scan failed or empty (%d)\n", scanResult);
            WiFi.scanDelete();
            WiFi.setTxPower(WIFI_AP_TX_POWER);
            reconTransition(RECONNECT_BACKOFF, "scan failed/empty");
            return;
        }

        char ssid[WIFI_STA_MAX_SSID_LEN + 1];
        char pass[WIFI_STA_MAX_PASS_LEN + 1];
        if (!storagePtr->loadWiFiCredentials(ssid, sizeof(ssid), pass, sizeof(pass))) {
            Serial.println("[WiFi-Recon] No credentials!");
            WiFi.scanDelete();
            WiFi.setTxPower(WIFI_AP_TX_POWER);
            reconTransition(RECONNECT_IDLE, "no credentials");
            _reconEnabled = false;
            return;
        }

        _reconRealSSID = matchSSIDFromScanResults(ssid);
        WiFi.scanDelete();

        Serial.printf("[WiFi-Recon] Connecting to \"%s\"\n", _reconRealSSID.c_str());

        WiFi.disconnect(false);
        delay(50);

        WiFi.mode(WIFI_AP_STA);
        WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD, WiFi.channel(), 0, WIFI_AP_MAX_CONN);
        applyStaticIP();
        WiFi.begin(_reconRealSSID.c_str(), pass);
        reconTransition(RECONNECT_CONNECTING, "WiFi.begin called");
        break;
    }

    case RECONNECT_CONNECTING: {
        wl_status_t status = WiFi.status();

        if (status == WL_CONNECTED) {
            uint8_t newChannel = WiFi.channel();
            Serial.printf("[WiFi-Recon] Connected! IP: %s  Ch: %d  (attempt #%d)\n",
                WiFi.localIP().toString().c_str(), newChannel, _reconAttempts);

            WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD, newChannel, 0, WIFI_AP_MAX_CONN);
            WiFi.setTxPower(WIFI_AP_TX_POWER);

            if (logPtr) logPtr->logf(LOG_CAT_WIFI,
                "WiFi reconnected (attempt #%d, ch %d)", _reconAttempts, newChannel);

            _lastNTPCheck = 0;
            _reconBackoff = WIFI_RECONNECT_BACKOFF_INITIAL;
            _reconAttempts = 0;

            reconTransition(RECONNECT_IDLE, "connected");
            return;
        }

        if (millis() - _reconTimer > WIFI_RECONNECT_CONNECT_TIMEOUT) {
            Serial.printf("[WiFi-Recon] Connect timeout (status=%d)\n", status);
            WiFi.disconnect(false);
            WiFi.setTxPower(WIFI_AP_TX_POWER);

            if (logPtr) logPtr->logf(LOG_CAT_WIFI,
                "Reconnect attempt #%d failed (status %d)", _reconAttempts, status);

            reconTransition(RECONNECT_BACKOFF, "connect timeout");
        }
        break;
    }

    case RECONNECT_BACKOFF: {
        if (millis() - _reconTimer < _reconBackoff) return;

        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("[WiFi-Recon] Connected during backoff!");
            WiFi.setTxPower(WIFI_AP_TX_POWER);
            if (logPtr) logPtr->log(LOG_CAT_WIFI, "WiFi reconnected during backoff");
            _reconBackoff = WIFI_RECONNECT_BACKOFF_INITIAL;
            _reconAttempts = 0;
            _lastNTPCheck = 0;
            reconTransition(RECONNECT_IDLE, "connected in backoff");
            return;
        }

        unsigned long nextBackoff = _reconBackoff * WIFI_RECONNECT_BACKOFF_MULT;
        if (nextBackoff > WIFI_RECONNECT_BACKOFF_MAX) {
            nextBackoff = WIFI_RECONNECT_BACKOFF_MAX;
        }
        _reconBackoff = nextBackoff;

        Serial.printf("[WiFi-Recon] Backoff done. Next attempt, backoff=%lus\n",
            _reconBackoff / 1000);

        reconTransition(RECONNECT_WAIT, "backoff expired");
        break;
    }

    } // switch
}

void BellServer::onFactoryReset(FactoryResetCallback cb) {
    _factoryResetCb = cb;
}

// ========================================================
//  MAIN LOOP
// ========================================================

void BellServer::loop() {
    if (_wifiConnState == 1 && millis() - _wifiConnStart > 500) {
        char ssid[WIFI_STA_MAX_SSID_LEN + 1];
        char pass[WIFI_STA_MAX_PASS_LEN + 1];
        if (storagePtr->loadWiFiCredentials(ssid, sizeof(ssid), pass, sizeof(pass))) {
            Serial.printf("[WiFi] loop() connecting to: %s\n", ssid);

            WiFi.disconnect();
            delay(100);
            WiFi.mode(WIFI_AP_STA);

            String realSSID = scanForRealSSID_blocking(ssid);
            applyStaticIP();
            WiFi.begin(realSSID.c_str(), pass);

            _wifiConnState = 2;
            _wifiConnStart = millis();
        } else {
            Serial.println("[WiFi] No credentials found");
            _wifiConnState = 0;
        }
    }

    if (_wifiConnState == 2) {
        if (WiFi.status() == WL_CONNECTED) {
            _isStationMode = true;
            _wifiConnState = 0;
            _lastNTPCheck = 0;
            _reconEnabled = true;
            _reconState = RECONNECT_IDLE;
            _reconBackoff = WIFI_RECONNECT_BACKOFF_INITIAL;
            _reconAttempts = 0;
            WiFi.setTxPower(WIFI_AP_TX_POWER);

            WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD, WiFi.channel(), 0, WIFI_AP_MAX_CONN);

            Serial.printf("[WiFi] Connected! IP: %s  Channel: %d\n",
                WiFi.localIP().toString().c_str(), WiFi.channel());
            if (logPtr) logPtr->logf(LOG_CAT_WIFI, "Connected to %s (IP: %s)",
                WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
        } else if (millis() - _wifiConnStart > WIFI_STA_CONNECT_TIMEOUT) {
            _wifiConnState = 0;
            WiFi.setTxPower(WIFI_AP_TX_POWER);
            Serial.printf("[WiFi] Connection timeout. Status: %d\n", WiFi.status());
            if (logPtr) logPtr->log(LOG_CAT_ERROR, "WiFi connect timeout");
        }
    }

    if (_factoryResetPending) {
        _factoryResetPending = false;
        delay(300);
        if (_factoryResetCb) _factoryResetCb();
        return;
    }

    loopReconnect();

    if (_isStationMode && WiFi.status() == WL_CONNECTED) {
        if (_lastNTPCheck == 0 || (millis() - _lastNTPCheck > NTP_SYNC_INTERVAL)) {
            _lastNTPCheck = millis();
            rtcPtr->startNTPSync();
        }
        if (rtcPtr->updateNTPSync()) {
            if (logPtr) logPtr->log(LOG_CAT_WIFI, "NTP sync successful");
            displayPtr->requestRedraw();
        }
    }
}

void BellServer::setStationMode(bool sta) {
    _isStationMode = sta;
    if (sta) {
        _reconEnabled = true;
    }
}

bool BellServer::isStationMode() {
    return _isStationMode;
}

String BellServer::getIPAddress() {
    return WiFi.softAPIP().toString();
}

// ========================================================
//  CALLBACKS
// ========================================================

void BellServer::onManualBellStart(ActionCallback cb) { cbManualStart = cb; }
void BellServer::onManualBellStop(ActionCallback cb)  { cbManualStop = cb; }
void BellServer::onLiveStart(ActionCallback cb)       { cbLiveStart = cb; }
void BellServer::onLiveStop(ActionCallback cb)        { cbLiveStop = cb; }

// ========================================================
//  WiFi ACCESS POINT
// ========================================================

void BellServer::setupWiFiAP() {
    if (WiFi.softAPIP() != IPAddress(0, 0, 0, 0)) {
        Serial.println("[WiFi] AP already running, skipping setup");
        Serial.print("[WiFi] AP IP: ");
        Serial.println(WiFi.softAPIP());
        return;
    }

    Serial.println("[WiFi] Starting Access Point...");

    WiFi.mode(WIFI_AP);
    WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD, WIFI_AP_CHANNEL, 0, WIFI_AP_MAX_CONN);

    delay(500);

    Serial.print("[WiFi] AP SSID: ");
    Serial.println(WIFI_AP_SSID);
    Serial.print("[WiFi] AP Password: ");
    Serial.println(WIFI_AP_PASSWORD);
    Serial.print("[WiFi] AP IP: ");
    Serial.println(WiFi.softAPIP());
    Serial.printf("[WiFi] Max connections: %d\n", WIFI_AP_MAX_CONN);
}

// ========================================================
//  STATIC IP CONFIGURATION
// ========================================================

bool BellServer::applyStaticIP() {
    if (!LittleFS.exists(WIFI_STATIC_IP_FILE)) return false;

    File f = LittleFS.open(WIFI_STATIC_IP_FILE, "r");
    if (!f) return false;

    String line = f.readStringUntil('\n');
    f.close();
    line.trim();

    if (line.length() == 0) return false;

    int c1 = line.indexOf(',');
    int c2 = line.indexOf(',', c1 + 1);
    int c3 = line.indexOf(',', c2 + 1);

    if (c1 < 0 || c2 < 0 || c3 < 0) {
        Serial.println("[WiFi] Static IP file malformed");
        return false;
    }

    IPAddress ip, gw, sn, dns;
    if (!ip.fromString(line.substring(0, c1)) ||
        !gw.fromString(line.substring(c1 + 1, c2)) ||
        !sn.fromString(line.substring(c2 + 1, c3)) ||
        !dns.fromString(line.substring(c3 + 1))) {
        Serial.println("[WiFi] Static IP parse failed");
        return false;
    }

    WiFi.config(ip, gw, sn, dns);
    Serial.printf("[WiFi] Static IP applied: %s (gw: %s)\n",
        ip.toString().c_str(), gw.toString().c_str());
    return true;
}

// ========================================================
//  ROUTE SETUP
// ========================================================

void BellServer::setupRoutes() {

    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type");

    // ── Root — serve index.html from LittleFS ──────────────────────────────
    // ⚠️ Changed from PROGMEM (webpage.h) to LittleFS (/index.html)
    // Run: pio run --target uploadfs   to upload data/ folder first
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (LittleFS.exists("/index.html")) {
            AsyncWebServerResponse *response =
                request->beginResponse(LittleFS, "/index.html", "text/html");
            response->addHeader("Cache-Control", "no-cache");
            request->send(response);
        } else {
            request->send(503, "text/plain",
                "Web UI not found. Run: pio run --target uploadfs");
        }
    });

    server.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest *request) {
        handleGetStatus(request);
    });

    server.on("/api/time", HTTP_GET, [this](AsyncWebServerRequest *request) {
        handleGetTime(request);
    });

    server.on("/api/time", HTTP_POST,
        [](AsyncWebServerRequest *request) {},
        NULL,
        [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            handleSetTime(request, data, len);
        }
    );

    server.on("/api/events/clear", HTTP_POST, [this](AsyncWebServerRequest *request) {
        handleClearEvents(request);
    });

    server.on("/api/events/toggle", HTTP_POST, [this](AsyncWebServerRequest *request) {
        handleToggleEvent(request);
    });

    server.on("/api/theme", HTTP_GET, [this](AsyncWebServerRequest *request) {
        handleGetTheme(request);
    });

    server.on("/api/theme", HTTP_POST,
        [](AsyncWebServerRequest *request) {},
        NULL,
        [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            handleSetTheme(request, data, len);
        }
    );

    server.on("/api/events", HTTP_GET, [this](AsyncWebServerRequest *request) {
        handleGetEvents(request);
    });

    server.on("/api/events", HTTP_POST,
        [](AsyncWebServerRequest *request) {},
        NULL,
        [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            handleAddEvent(request, data, len);
        }
    );

    server.on("/api/events", HTTP_DELETE, [this](AsyncWebServerRequest *request) {
        handleDeleteEvent(request);
    });

    server.on("/api/manual-bell", HTTP_POST, [this](AsyncWebServerRequest *request) {
        handleManualBell(request);
    });

    server.on("/api/stop", HTTP_POST, [this](AsyncWebServerRequest *request) {
        handleStop(request);
    });

    server.on("/api/live/start", HTTP_POST, [this](AsyncWebServerRequest *request) {
        handleLiveStart(request);
    });

    server.on("/api/live/stop", HTTP_POST, [this](AsyncWebServerRequest *request) {
        handleLiveStop(request);
    });

    server.on("/api/schoolname", HTTP_GET, [this](AsyncWebServerRequest *request) {
        handleGetSchoolName(request);
    });

    server.on("/api/schoolname", HTTP_POST,
        [](AsyncWebServerRequest *request) {},
        NULL,
        [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            handleSetSchoolName(request, data, len);
        }
    );

    server.on("/api/sounds", HTTP_GET, [this](AsyncWebServerRequest *request) {
        handleGetSounds(request);
    });

    server.on("/api/sounds", HTTP_POST,
        [this](AsyncWebServerRequest *request) {
            Serial.printf("[Web] Sounds POST complete: %d bytes\n", _soundsBody.length());
            if (_soundsBody.length() > 0) {
                handleSetSounds(request, (uint8_t*)_soundsBody.c_str(), _soundsBody.length());
            } else {
                request->send(400, "application/json", "{\"error\":\"Empty body\"}");
            }
            _soundsBody = "";
        },
        NULL,
        [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            if (index == 0) {
                _soundsBody = "";
                _soundsBody.reserve(total + 1);
                Serial.printf("[Web] Sounds POST starting: %d bytes expected\n", total);
            }
            char *temp = (char*)malloc(len + 1);
            if (temp) {
                memcpy(temp, data, len);
                temp[len] = '\0';
                _soundsBody += String(temp);
                free(temp);
            }
        }
    );

    server.on("/api/custom-bg", HTTP_POST,
        [this](AsyncWebServerRequest *request) {
            if (storagePtr->hasCustomBg()) {
                displayPtr->setBgTheme(CUSTOM_THEME_INDEX);
                storagePtr->saveTheme(CUSTOM_THEME_INDEX);
                displayPtr->requestRedraw();
                request->send(200, "application/json",
                    "{\"success\":true,\"message\":\"Custom background set\"}");
                Serial.println("[Web] Custom background upload complete & applied");
            } else {
                request->send(500, "application/json",
                    "{\"error\":\"Upload failed\"}");
            }
        },
        NULL,
        [this](AsyncWebServerRequest *request, uint8_t *data, size_t len,
               size_t index, size_t total) {
            handleUploadCustomBg(request, data, len, index, total);
        }
    );

    server.on("/api/colors", HTTP_GET, [this](AsyncWebServerRequest *request) {
        handleGetColors(request);
    });

    server.on("/api/colors", HTTP_POST,
        [](AsyncWebServerRequest *request) {},
        NULL,
        [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            handleSetColors(request, data, len);
        }
    );

    server.on("/api/holidays/clear", HTTP_POST, [this](AsyncWebServerRequest *request) {
        handleClearHolidays(request);
    });

    server.on("/api/holidays", HTTP_GET, [this](AsyncWebServerRequest *request) {
        handleGetHolidays(request);
    });

    server.on("/api/holidays", HTTP_POST,
        [](AsyncWebServerRequest *request) {},
        NULL,
        [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            handleAddHoliday(request, data, len);
        }
    );

    server.on("/api/holidays", HTTP_DELETE, [this](AsyncWebServerRequest *request) {
        handleDeleteHoliday(request);
    });

    server.on("/api/auth", HTTP_POST,
        [](AsyncWebServerRequest *request) {},
        NULL,
        [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            handleAuth(request, data, len);
        }
    );

    server.on("/api/password", HTTP_POST,
        [](AsyncWebServerRequest *request) {},
        NULL,
        [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            handleChangePassword(request, data, len);
        }
    );

    server.on("/api/templates", HTTP_GET, [this](AsyncWebServerRequest *request) {
        handleGetTemplates(request);
    });

    server.on("/api/templates/save", HTTP_POST,
        [](AsyncWebServerRequest *request) {},
        NULL,
        [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            handleSaveTemplate(request, data, len);
        }
    );

    server.on("/api/templates/load", HTTP_POST, [this](AsyncWebServerRequest *request) {
        handleLoadTemplate(request);
    });

    server.on("/api/templates", HTTP_DELETE, [this](AsyncWebServerRequest *request) {
        handleDeleteTemplate(request);
    });

    server.on("/api/backup", HTTP_GET, [this](AsyncWebServerRequest *r) {
        handleBackupDownload(r);
    });

    server.on("/api/restore", HTTP_POST,
        [this](AsyncWebServerRequest *r) {
        },
        NULL,
        [this](AsyncWebServerRequest *r, uint8_t *data, size_t len, size_t index, size_t total) {
            handleBackupRestore(r, data, len, index, total);
        }
    );

    server.on("/api/logs", HTTP_GET, [this](AsyncWebServerRequest *request) {
        handleGetLogs(request);
    });

    server.on("/api/logs/clear", HTTP_POST, [this](AsyncWebServerRequest *request) {
        handleClearLogs(request);
    });

    server.on("/api/wifi/status", HTTP_GET, [this](AsyncWebServerRequest *request) {
        handleGetWiFiStatus(request);
    });

    server.on("/api/wifi/scan", HTTP_GET, [this](AsyncWebServerRequest *request) {
        handleWiFiScan(request);
    });

    server.on("/api/wifi/connect", HTTP_POST,
        [](AsyncWebServerRequest *request) {},
        NULL,
        [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            handleWiFiConnect(request, data, len);
        }
    );

    server.on("/api/wifi/disconnect", HTTP_POST, [this](AsyncWebServerRequest *request) {
        handleWiFiDisconnect(request);
    });

    server.on("/api/wifi/ap", HTTP_POST, [this](AsyncWebServerRequest *request) {
        handleWiFiAP(request);
    });

    server.on("/api/wifi/staticip", HTTP_GET, [this](AsyncWebServerRequest *request) {
        handleGetStaticIP(request);
    });

    server.on("/api/wifi/staticip", HTTP_POST,
        [](AsyncWebServerRequest *request) {},
        NULL,
        [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            handleSetStaticIP(request, data, len);
        }
    );

    server.on("/api/ntp/status", HTTP_GET, [this](AsyncWebServerRequest *request) {
        handleNTPStatus(request);
    });

    server.on("/api/ntp/sync", HTTP_POST, [this](AsyncWebServerRequest *request) {
        handleNTPSync(request);
    });

    server.on("/api/factoryreset", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (_factoryResetCb) {
            request->send(200, "application/json", "{\"status\":\"resetting\"}");
            _factoryResetPending = true;
            Serial.println("[Web] Factory reset requested via API");
        } else {
            request->send(500, "application/json", "{\"error\":\"no handler\"}");
        }
    });

    server.on("/api/profile", HTTP_GET, [this](AsyncWebServerRequest *request) {
        handleGetProfile(request);
    });

    server.on("/api/profile", HTTP_POST,
        [](AsyncWebServerRequest *request) {},
        NULL,
        [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            handleSetProfile(request, data, len);
        }
    );

    server.on("/api/setup/status", HTTP_GET, [this](AsyncWebServerRequest *request) {
        handleGetSetupStatus(request);
    });

    server.on("/api/setup/complete", HTTP_POST, [this](AsyncWebServerRequest *request) {
        handleSetupComplete(request);
    });

    server.on("/api/setup/reset", HTTP_POST, [this](AsyncWebServerRequest *request) {
        handleSetupReset(request);
    });

    // ── ICS Calendar Export ────────────────────────────────────────────────
    // GET /api/export-ics?advance=5
    // Returns smartbell_schedule.ics as a downloadable text/calendar file
    server.on("/api/export-ics", HTTP_GET, [this](AsyncWebServerRequest *request) {
        handleExportICS(request);
    });
    // ──────────────────────────────────────────────────────────────────────

    server.on("/manifest.json", HTTP_GET, [](AsyncWebServerRequest *request) {
        const char* manifest =
            "{\"name\":\"K\u0100lan\u0101dam\","
            "\"short_name\":\"K\u0100lan\u0101dam\","
            "\"description\":\"Automatic Bell System\","
            "\"start_url\":\"/\","
            "\"display\":\"standalone\","
            "\"background_color\":\"#0f0f1e\","
            "\"theme_color\":\"#1a1a3e\","
            "\"orientation\":\"portrait\","
            "\"icons\":[{\"src\":\"/icon.svg\",\"sizes\":\"any\",\"type\":\"image/svg+xml\",\"purpose\":\"any maskable\"}]}";
        AsyncWebServerResponse *response = request->beginResponse(200, "application/manifest+json", manifest);
        response->addHeader("Cache-Control", "public, max-age=86400");
        request->send(response);
    });

    server.on("/icon.svg", HTTP_GET, [](AsyncWebServerRequest *request) {
        String svg = "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 512 512'>";
        svg += "<rect width='512' height='512' rx='96' fill='#1a1a3e'/>";
        svg += "<path d='M256 96c-11 0-20 9-20 20v14c-50 12-88 56-88 110v68l-24 24v12h264v-12";
        svg += "l-24-24v-68c0-54-38-98-88-110v-14c0-11-9-20-20-20z' fill='#ffd700'/>";
        svg += "<path d='M224 368c0 18 14 32 32 32s32-14 32-32z' fill='#e6c200'/>";
        svg += "</svg>";
        AsyncWebServerResponse *response = request->beginResponse(200, "image/svg+xml", svg);
        response->addHeader("Cache-Control", "public, max-age=86400");
        request->send(response);
    });

    // ── Audio reset — SD card reinsertion recovery ──────────────────
    server.on("/api/audio/reset", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (*statePtr == STATE_LIVE) {
            request->send(409, "application/json",
                "{\"error\":\"Cannot reset audio during live announcement\"}");
            return;
        }
        // Stop any currently playing audio cleanly
        if (*statePtr == STATE_MANUAL_BELL && cbManualStop) cbManualStop();
        else audioPtr->stopDfPlayer();
        // Soft reset DFPlayer and re-scan SD
        audioPtr->resetDfPlayer();
        if (logPtr) logPtr->log(LOG_CAT_AUDIO, "DFPlayer reset via web UI");
        Serial.println("[Web] DFPlayer soft reset via /api/audio/reset");
        request->send(200, "application/json",
            "{\"success\":true,\"message\":\"DFPlayer reset complete. SD card re-scanned.\"}");
    });
    // ────────────────────────────────────────────────────────────────

    server.onNotFound([](AsyncWebServerRequest *request) {  // ← already exists
        if (request->method() == HTTP_OPTIONS) {
            request->send(200);
        } else {
            request->send(404, "application/json", "{\"error\":\"Not found\"}");
        }
    });

    Serial.println("[Web] Routes configured.");
}

// ========================================================
//  HELPERS
// ========================================================

void BellServer::saveEventsToStorage() {
    storagePtr->saveEvents(
        eventsPtr->getNextId(),
        eventsPtr->getCount(),
        eventsPtr->getEvents()
    );
}

String BellServer::buildEventsJSON() {
    String json = "[";

    Event* events = eventsPtr->getEvents();
    uint8_t count = eventsPtr->getCount();

    for (int i = 0; i < count; i++) {
        if (i > 0) json += ",";

        json += "{";
        json += "\"id\":"             + String(events[i].id);
        json += ",\"hour\":"          + String(events[i].hour);
        json += ",\"minute\":"        + String(events[i].minute);
        json += ",\"weekdayMask\":"   + String(events[i].weekdayMask);
        json += ",\"folder\":"        + String(events[i].folder);
        json += ",\"track\":"         + String(events[i].track);
        json += ",\"enabled\":"       + String(events[i].enabled ? "true" : "false");
        json += ",\"name\":\""        + String(events[i].name) + "\"";
        json += ",\"duration\":"      + String(events[i].duration);
        json += ",\"eventType\":"     + String(events[i].eventType);
        json += ",\"intervalMinutes\":"+ String(events[i].intervalMinutes);
        json += ",\"endMinute\":"     + String(events[i].endMinute);

        // ── One-time fields ───────────────────────────────────────────────
        bool oneTime = events[i].isOneTime();
        json += ",\"isOneTime\":"     + String(oneTime ? "true" : "false");
        if (oneTime) {
            json += ",\"oneTimeDate\":\"" + events[i].onTimeDateStr() + "\"";
        }

        // ── Days array (strip bit 7 for one-time events) ──────────────────
        json += ",\"days\":[";
        bool firstDay = true;
        uint8_t effectiveMask = events[i].weekdayMask & 0x7F;
        for (int d = 0; d < 7; d++) {
            if (effectiveMask & (1 << d)) {
                if (!firstDay) json += ",";
                json += String(d);
                firstDay = false;
            }
        }
        json += "]";

        json += "}";
    }

    json += "]";
    return json;
}
// ========================================================
//  ROOT PAGE
// ========================================================

void BellServer::handleRoot(AsyncWebServerRequest *request) {
    if (LittleFS.exists("/index.html")) {
        request->send(LittleFS, "/index.html", "text/html");
    } else {
        request->send(503, "text/plain",
            "Web UI not found. Run: pio run --target uploadfs");
    }
}

// ========================================================
//  STATUS API
// ========================================================

void BellServer::handleGetStatus(AsyncWebServerRequest *request) {

    uint16_t year;
    uint8_t month, day, hour, minute, weekday;
    rtcPtr->getDateTime(year, month, day, hour, minute, weekday);

    char timeStr[6];
    sprintf(timeStr, "%02d:%02d", hour, minute);
    char dateStr[12];
    sprintf(dateStr, "%04d-%02d-%02d", year, month, day);

    String json = "{";
    json += "\"state\":\"";
    if (*statePtr == STATE_IDLE)             json += "idle";
    else if (*statePtr == STATE_MANUAL_BELL) json += "manual_bell";
    else if (*statePtr == STATE_LIVE)        json += "live";
    json += "\"";
    json += ",\"time\":\"" + String(timeStr) + "\"";
    json += ",\"date\":\"" + String(dateStr) + "\"";
    json += ",\"weekday\":" + String(weekday);
    json += ",\"events\":" + String(eventsPtr->getCount());
    json += ",\"scheduler\":\"" + String(schedulerPtr->isPaused() ? "paused" : "running") + "\"";
    json += ",\"holiday\":" + String(schedulerPtr->isHolidayToday() ? "true" : "false");
    json += ",\"profile\":" + String(displayPtr->getProfile());
    json += ",\"dfPlayerReady\":" + String(audioPtr->isDfPlayerReady() ? "true" : "false");
    json += ",\"playing\":" + String(audioPtr->isDfPlayerPlaying() ? "true" : "false");
    json += ",\"live\":" + String(audioPtr->isLive() ? "true" : "false");

    json += "}";

    request->send(200, "application/json", json);
}

// ========================================================
//  TIME API
// ========================================================

void BellServer::handleGetTime(AsyncWebServerRequest *request) {

    uint16_t year;
    uint8_t month, day, hour, minute, weekday;
    rtcPtr->getDateTime(year, month, day, hour, minute, weekday);

    char buf[30];
    sprintf(buf, "%04d-%02d-%02d %02d:%02d", year, month, day, hour, minute);

    String json = "{";
    json += "\"year\":" + String(year);
    json += ",\"month\":" + String(month);
    json += ",\"day\":" + String(day);
    json += ",\"hour\":" + String(hour);
    json += ",\"minute\":" + String(minute);
    json += ",\"weekday\":" + String(weekday);
    json += ",\"formatted\":\"" + String(buf) + "\"";
    json += "}";

    request->send(200, "application/json", json);
}

void BellServer::handleSetTime(AsyncWebServerRequest *request, uint8_t *data, size_t len) {

    String body = String((char*)data).substring(0, len);

    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    int idx;

    idx = body.indexOf("\"year\":");
    if (idx >= 0) year = body.substring(idx + 7).toInt();

    idx = body.indexOf("\"month\":");
    if (idx >= 0) month = body.substring(idx + 8).toInt();

    idx = body.indexOf("\"day\":");
    if (idx >= 0) day = body.substring(idx + 6).toInt();

    idx = body.indexOf("\"hour\":");
    if (idx >= 0) hour = body.substring(idx + 7).toInt();

    idx = body.indexOf("\"minute\":");
    if (idx >= 0) minute = body.substring(idx + 9).toInt();

    idx = body.indexOf("\"second\":");
    if (idx >= 0) second = body.substring(idx + 9).toInt();

    if (year < 2020 || year > 2099 || month < 1 || month > 12 ||
        day < 1 || day > 31 || hour > 23 || minute > 59 || second > 59) {
        request->send(400, "application/json",
            "{\"error\":\"Invalid date/time values\"}");
        return;
    }

    rtcPtr->setTime(year, month, day, hour, minute, second);
    displayPtr->requestRedraw();

    request->send(200, "application/json",
        "{\"success\":true,\"message\":\"Time updated\"}");

    Serial.printf("[Web] Time set to: %04d-%02d-%02d %02d:%02d:%02d\n",
                  year, month, day, hour, minute, second);
    if (logPtr) logPtr->logf(LOG_CAT_SETTINGS,
        "Time set: %04d-%02d-%02d %02d:%02d", year, month, day, hour, minute);
}

// ========================================================
//  EVENTS API
// ========================================================

void BellServer::handleGetEvents(AsyncWebServerRequest *request) {

    String json = "{";
    json += "\"count\":" + String(eventsPtr->getCount());
    json += ",\"events\":" + buildEventsJSON();
    json += "}";

    request->send(200, "application/json", json);
}

void BellServer::handleAddEvent(AsyncWebServerRequest *request,
                                 uint8_t *data, size_t len) {

    String body = String((char*)data).substring(0, len);
    int idx;

    // ── Parse eventType first — determines code path ──────────────────────
    int eventType = EVENT_TYPE_FIXED;
    idx = body.indexOf("\"eventType\":");
    if (idx >= 0) eventType = body.substring(idx + 12).toInt();
    if (eventType < 0 || eventType > 2) eventType = EVENT_TYPE_FIXED;

    // ── Fields common to all event types ─────────────────────────────────
    int hour = -1, minute = -1, track = -1;
    String eventName = "";

    idx = body.indexOf("\"hour\":");
    if (idx >= 0) hour = body.substring(idx + 7).toInt();

    idx = body.indexOf("\"minute\":");
    if (idx >= 0) minute = body.substring(idx + 9).toInt();

    idx = body.indexOf("\"track\":");
    if (idx >= 0) track = body.substring(idx + 8).toInt();

    int duration = 0;
    idx = body.indexOf("\"duration\":");
    if (idx >= 0) duration = body.substring(idx + 11).toInt();
    if (duration < 0) duration = 0;
    if (duration > MAX_BELL_DURATION) duration = MAX_BELL_DURATION;

    idx = body.indexOf("\"name\":\"");
    if (idx >= 0) {
        int start = idx + 8;
        int end = body.indexOf("\"", start);
        if (end > start) {
            eventName = body.substring(start, end);
            eventName.trim();
            if ((int)eventName.length() > MAX_EVENT_NAME_LEN)
                eventName = eventName.substring(0, MAX_EVENT_NAME_LEN);
        }
    }

    // ── Validate common fields ────────────────────────────────────────────
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59 ||
        track < 1 || track > 255) {
        request->send(400, "application/json",
            "{\"error\":\"Invalid hour/minute/track values\"}");
        return;
    }

    if (eventsPtr->getCount() >= MAX_EVENTS) {
        request->send(400, "application/json",
            "{\"error\":\"Maximum events reached\"}");
        return;
    }

    // ══════════════════════════════════════════════════════════════════════
    //  ONE-TIME EVENT PATH  (eventType == 2)
    // ══════════════════════════════════════════════════════════════════════
    if (eventType == 2) {

        String dateStr = "";
        idx = body.indexOf("\"date\":\"");
        if (idx >= 0) {
            int start = idx + 8;
            int end   = body.indexOf("\"", start);
            if (end > start) dateStr = body.substring(start, end);
        }

        if (dateStr.length() != 10 ||
            dateStr[4] != '-' || dateStr[7] != '-') {
            request->send(400, "application/json",
                "{\"error\":\"Invalid date format. Use YYYY-MM-DD\"}");
            return;
        }

        uint16_t year  = (uint16_t)dateStr.substring(0, 4).toInt();
        uint8_t  month = (uint8_t) dateStr.substring(5, 7).toInt();
        uint8_t  day   = (uint8_t) dateStr.substring(8, 10).toInt();

        if (year < 2024 || year > 2058 ||
            month < 1 || month > 12 ||
            day < 1   || day > 31) {
            request->send(400, "application/json",
                "{\"error\":\"Invalid date (year must be 2024-2058)\"}");
            return;
        }

        uint16_t newId = eventsPtr->addOneTimeEvent(
            (uint8_t)hour, (uint8_t)minute,
            year, month, day,
            (uint8_t)track,
            eventName.c_str(),
            (uint16_t)duration);

        if (newId == 0) {
            request->send(500, "application/json",
                "{\"error\":\"Failed to add one-time event\"}");
            return;
        }

        saveEventsToStorage();
        displayPtr->requestRedraw();

        String json = "{\"success\":true,\"id\":"  + String(newId)
                    + ",\"oneTime\":true,\"date\":\"" + dateStr + "\"}";
        request->send(200, "application/json", json);

        Serial.printf("[Web] One-time #%d: %04d-%02d-%02d %02d:%02d"
                      " T%d dur=%d name=%s\n",
                      newId, year, month, day, hour, minute, track,
                      duration,
                      eventName.length() > 0 ? eventName.c_str() : "(none)");
        if (logPtr) logPtr->logf(LOG_CAT_SCHEDULE,
            "One-time added: %s on %04d-%02d-%02d at %02d:%02d",
            eventName.length() > 0 ? eventName.c_str() : "unnamed",
            year, month, day, hour, minute);
        return;
    }

    // ══════════════════════════════════════════════════════════════════════
    //  FIXED / INTERVAL EVENT PATH  (eventType == 0 or 1)
    // ══════════════════════════════════════════════════════════════════════
    int weekdayMask = -1;
    int folder = -1;

    idx = body.indexOf("\"weekdayMask\":");
    if (idx >= 0) weekdayMask = body.substring(idx + 14).toInt();

    idx = body.indexOf("\"folder\":");
    if (idx >= 0) folder = body.substring(idx + 9).toInt();

    if (eventType != EVENT_TYPE_FIXED && eventType != EVENT_TYPE_INTERVAL)
        eventType = EVENT_TYPE_FIXED;

    int intervalMinutes = 0;
    idx = body.indexOf("\"intervalMinutes\":");
    if (idx >= 0) intervalMinutes = body.substring(idx + 18).toInt();
    if (intervalMinutes < 0) intervalMinutes = 0;
    if (intervalMinutes > MAX_INTERVAL_MINUTES) intervalMinutes = MAX_INTERVAL_MINUTES;
    if (eventType == EVENT_TYPE_INTERVAL &&
        intervalMinutes < MIN_INTERVAL_MINUTES)
        intervalMinutes = MIN_INTERVAL_MINUTES;

    int endMinute = 0;
    idx = body.indexOf("\"endMinute\":");
    if (idx >= 0) endMinute = body.substring(idx + 12).toInt();
    if (endMinute < 0) endMinute = 0;
    if (endMinute > 1439) endMinute = 1439;

    if (weekdayMask < 0 || weekdayMask > 127 ||
        folder < 1    || folder > 99) {
        request->send(400, "application/json",
            "{\"error\":\"Invalid event values\"}");
        return;
    }

    uint16_t newId = eventsPtr->addEvent(
        (uint8_t)hour, (uint8_t)minute,
        (uint8_t)weekdayMask,
        (uint8_t)folder,
        (uint8_t)track,
        eventName.c_str(),
        (uint16_t)duration,
        (uint8_t)eventType,
        (uint16_t)intervalMinutes,
        (uint16_t)endMinute);

    if (newId == 0) {
        request->send(500, "application/json",
            "{\"error\":\"Failed to add event\"}");
        return;
    }

    saveEventsToStorage();
    displayPtr->requestRedraw();

    String json = "{\"success\":true,\"id\":" + String(newId) + "}";
    request->send(200, "application/json", json);

    if (eventType == EVENT_TYPE_INTERVAL) {
        Serial.printf("[Web] Added interval #%d: start %02d:%02d"
                      " every %dmin end %d F%d/T%d days=0x%02X"
                      " dur=%d name=%s\n",
                      newId, hour, minute, intervalMinutes, endMinute,
                      folder, track, weekdayMask, duration,
                      eventName.length() > 0 ? eventName.c_str() : "(none)");
    } else {
        Serial.printf("[Web] Added event #%d: %02d:%02d F%d/T%d"
                      " days=0x%02X dur=%d name=%s\n",
                      newId, hour, minute, folder, track,
                      weekdayMask, duration,
                      eventName.length() > 0 ? eventName.c_str() : "(none)");
    }
    if (logPtr) logPtr->logf(LOG_CAT_SCHEDULE,
        "Event added: %s at %02d:%02d (track %d)",
        eventName.length() > 0 ? eventName.c_str() : "unnamed",
        hour, minute, track);
}

void BellServer::handleDeleteEvent(AsyncWebServerRequest *request) {

    if (!request->hasParam("id")) {
        request->send(400, "application/json",
            "{\"error\":\"Missing 'id' parameter\"}");
        return;
    }

    uint16_t id = request->getParam("id")->value().toInt();

    if (eventsPtr->deleteEvent(id)) {
        saveEventsToStorage();
        displayPtr->requestRedraw();

        request->send(200, "application/json",
            "{\"success\":true,\"message\":\"Event deleted\"}");

        Serial.printf("[Web] Deleted event #%d\n", id);
        if (logPtr) logPtr->logf(LOG_CAT_SCHEDULE, "Event deleted: ID %d", id);
    } else {
        request->send(404, "application/json",
            "{\"error\":\"Event not found\"}");
    }
}

void BellServer::handleToggleEvent(AsyncWebServerRequest *request) {

    if (!request->hasParam("id")) {
        request->send(400, "application/json",
            "{\"error\":\"Missing 'id' parameter\"}");
        return;
    }

    uint16_t id = request->getParam("id")->value().toInt();

    if (eventsPtr->toggleEvent(id)) {
        saveEventsToStorage();
        displayPtr->requestRedraw();

        Event* ev = eventsPtr->getEventById(id);
        bool enabled = ev ? ev->enabled : false;

        String json = "{\"success\":true,\"id\":" + String(id) +
                      ",\"enabled\":" + String(enabled ? "true" : "false") + "}";
        request->send(200, "application/json", json);

        Serial.printf("[Web] Toggled event #%d -> %s\n", id, enabled ? "ON" : "OFF");
        if (logPtr) logPtr->logf(LOG_CAT_SCHEDULE,
            "Event %d %s", id, enabled ? "enabled" : "disabled");
    } else {
        request->send(404, "application/json",
            "{\"error\":\"Event not found\"}");
    }
}

void BellServer::handleClearEvents(AsyncWebServerRequest *request) {

    eventsPtr->clear();
    saveEventsToStorage();
    displayPtr->requestRedraw();

    request->send(200, "application/json",
        "{\"success\":true,\"message\":\"All events cleared\"}");

    Serial.println("[Web] All events cleared.");
    if (logPtr) logPtr->log(LOG_CAT_SCHEDULE, "All events cleared");
}

// ========================================================
//  CONTROL API
// ========================================================

void BellServer::handleManualBell(AsyncWebServerRequest *request) {

    if (*statePtr == STATE_LIVE) {
        request->send(409, "application/json",
            "{\"error\":\"Cannot ring bell during live announcement\"}");
        return;
    }

    if (*statePtr == STATE_MANUAL_BELL) {
        if (cbManualStop) cbManualStop();
        request->send(200, "application/json",
            "{\"success\":true,\"action\":\"stopped\"}");
        if (logPtr) logPtr->log(LOG_CAT_BELL, "Manual bell stopped (web)");
    } else {
        uint16_t track = RINGTONE_START_TRACK;
        if (request->hasParam("track")) {
            track = request->getParam("track")->value().toInt();
            if (track < RINGTONE_START_TRACK ||
                track >= RINGTONE_START_TRACK + RINGTONE_COUNT) {
                track = RINGTONE_START_TRACK;
            }
        }

        displayPtr->setSelectedRingtoneTrack(track);

        if (cbManualStart) cbManualStart();

        String json = "{\"success\":true,\"action\":\"started\",\"track\":"
                      + String(track) + "}";
        request->send(200, "application/json", json);

        Serial.printf("[Web] Manual bell started with track %d\n", track);
        if (logPtr) logPtr->logf(LOG_CAT_BELL,
            "Manual bell: track %d (web)", track);
    }
}

void BellServer::handleStop(AsyncWebServerRequest *request) {

    if (*statePtr == STATE_MANUAL_BELL && cbManualStop) {
        cbManualStop();
    }
    else if (*statePtr == STATE_LIVE && cbLiveStop) {
        cbLiveStop();
    }
    else {
        audioPtr->stopDfPlayer();
    }

    request->send(200, "application/json",
        "{\"success\":true,\"message\":\"Stopped\"}");
    if (logPtr) logPtr->log(LOG_CAT_AUDIO, "Stop all (web)");
}

void BellServer::handleLiveStart(AsyncWebServerRequest *request) {

    if (*statePtr == STATE_LIVE) {
        request->send(200, "application/json",
            "{\"success\":true,\"message\":\"Already live\"}");
        return;
    }

    if (cbLiveStart) cbLiveStart();

    request->send(200, "application/json",
        "{\"success\":true,\"message\":\"Live started\"}");
    if (logPtr) logPtr->log(LOG_CAT_AUDIO, "Live announcement started (web)");
}

void BellServer::handleLiveStop(AsyncWebServerRequest *request) {

    if (*statePtr != STATE_LIVE) {
        request->send(200, "application/json",
            "{\"success\":true,\"message\":\"Not in live mode\"}");
        return;
    }

    if (cbLiveStop) cbLiveStop();

    request->send(200, "application/json",
        "{\"success\":true,\"message\":\"Live stopped\"}");
    if (logPtr) logPtr->log(LOG_CAT_AUDIO, "Live announcement stopped (web)");
}

// ========================================================
//  SCHOOL NAME API
// ========================================================

void BellServer::handleGetSchoolName(AsyncWebServerRequest *request) {
    char name[MAX_SCHOOL_NAME_LEN + 1];
    if (!storagePtr->loadSchoolName(name, sizeof(name))) {
        strncpy(name, DEFAULT_SCHOOL_NAME, sizeof(name));
    }

    String json = "{\"name\":\"" + String(name) + "\"}";
    request->send(200, "application/json", json);
}

void BellServer::handleSetSchoolName(AsyncWebServerRequest *request, uint8_t *data, size_t len) {
    String body = String((char*)data).substring(0, len);

    int idx = body.indexOf("\"name\":\"");
    if (idx < 0) {
        request->send(400, "application/json", "{\"error\":\"Missing name field\"}");
        return;
    }

    int start = idx + 8;
    int end = body.indexOf("\"", start);
    if (end < 0 || end == start) {
        request->send(400, "application/json", "{\"error\":\"Invalid name\"}");
        return;
    }

    String name = body.substring(start, end);
    name.trim();

    if (name.length() == 0 || name.length() > MAX_SCHOOL_NAME_LEN) {
        request->send(400, "application/json",
            "{\"error\":\"Name must be 1-" + String(MAX_SCHOOL_NAME_LEN) + " characters\"}");
        return;
    }

    name.toUpperCase();

    storagePtr->saveSchoolName(name.c_str());
    displayPtr->setSchoolName(name.c_str());
    displayPtr->requestRedraw();

    String json = "{\"success\":true,\"name\":\"" + name + "\"}";
    request->send(200, "application/json", json);

    Serial.printf("[Web] School name changed to: %s\n", name.c_str());
    if (logPtr) logPtr->logf(LOG_CAT_SETTINGS,
        "School name: %s", name.c_str());
}

// ========================================================
//  SOUNDS API
// ========================================================

void BellServer::handleGetSounds(AsyncWebServerRequest *request) {
    String json = "{\"sounds\":" + storagePtr->loadSoundsJSON() + "}";
    request->send(200, "application/json", json);
}

void BellServer::handleSetSounds(AsyncWebServerRequest *request, uint8_t *data, size_t len) {
    String body;
    body.reserve(len + 1);
    for (size_t i = 0; i < len; i++) {
        body += (char)data[i];
    }

    Serial.printf("[Web] Sounds body (%d bytes): %s\n", body.length(), body.c_str());

    String csvData = "";
    int count = 0;
    int idx = 0;

    while (true) {
        int trackIdx = body.indexOf("\"track\":", idx);
        if (trackIdx < 0) break;

        int nameIdx = body.indexOf("\"name\":", trackIdx);
        if (nameIdx < 0) break;

        int trackVal = body.substring(trackIdx + 8).toInt();

        int quoteStart = body.indexOf("\"", nameIdx + 7);
        if (quoteStart < 0) break;
        int nameStart = quoteStart + 1;
        int nameEnd = body.indexOf("\"", nameStart);
        if (nameEnd < 0) break;

        String name = body.substring(nameStart, nameEnd);

        if (trackVal > 0 && name.length() > 0) {
            char line[128];
            snprintf(line, sizeof(line), "%04d,%s\n", trackVal, name.c_str());
            csvData += line;
            count++;
            Serial.printf("[Web] Sound #%d: %04d = %s\n", count, trackVal, name.c_str());
        }

        idx = nameEnd + 1;
    }

    if (count == 0) {
        Serial.println("[Web] ERROR: No valid sound entries found in body!");
        request->send(400, "application/json", "{\"error\":\"No valid sound entries\"}");
        return;
    }

    storagePtr->saveSounds(csvData);

    String json = "{\"success\":true,\"count\":" + String(count) + "}";
    request->send(200, "application/json", json);

    Serial.printf("[Web] Saved %d sound names from desktop app.\n", count);
    if (logPtr) logPtr->logf(LOG_CAT_SETTINGS,
        "Sound names synced: %d tracks", count);
}

// ========================================================
// THEME API
// ========================================================

void BellServer::handleGetTheme(AsyncWebServerRequest *request) {
    uint8_t theme = displayPtr->getBgTheme();
    String json = "{\"theme\":" + String(theme) + ",\"max\":" + String(MAX_BG_THEMES) + "}";
    request->send(200, "application/json", json);
}

void BellServer::handleSetTheme(AsyncWebServerRequest *request, uint8_t *data, size_t len) {
    String body = String((char*)data).substring(0, len);

    int idx = body.indexOf("\"theme\":");
    if (idx < 0) {
        request->send(400, "application/json", "{\"error\":\"Missing theme field\"}");
        return;
    }

    int theme = body.substring(idx + 8).toInt();
    if (theme < 0 || theme >= MAX_BG_THEMES) {
        request->send(400, "application/json",
            "{\"error\":\"Theme must be 0-" + String(MAX_BG_THEMES - 1) + "\"}");
        return;
    }

    displayPtr->setBgTheme(theme);
    storagePtr->saveTheme(theme);
    displayPtr->requestRedraw();

    String json = "{\"success\":true,\"theme\":" + String(theme) + "}";
    request->send(200, "application/json", json);

    Serial.printf("[Web] Theme changed to: %d\n", theme);
    if (logPtr) logPtr->logf(LOG_CAT_SETTINGS, "Theme changed to %d", theme);
}

// ========================================================
//  CUSTOM BACKGROUND UPLOAD
// ========================================================

void BellServer::handleUploadCustomBg(AsyncWebServerRequest *request,
                                       uint8_t *data, size_t len,
                                       size_t index, size_t total)
{
    if (index == 0) {
        Serial.printf("[Web] Custom BG upload starting: %d bytes\n", total);
        if (total != 153600) {
            Serial.printf("[Web] WARNING: Expected 153600 bytes, got %d\n", total);
        }
    }

    storagePtr->saveCustomBg(data, len, index, total);

    if (index + len >= total) {
        Serial.println("[Web] Custom BG upload received completely");
        if (logPtr) logPtr->log(LOG_CAT_SETTINGS, "Custom background uploaded");
    }
}

// ========================================================
//  COLORS API
// ========================================================

void BellServer::handleGetColors(AsyncWebServerRequest *request) {
    uint8_t nR, nG, nB, cR, cG, cB;
    displayPtr->getNameColor(nR, nG, nB);
    displayPtr->getClockColor(cR, cG, cB);

    char json[120];
    snprintf(json, sizeof(json),
        "{\"nameColor\":\"#%02X%02X%02X\",\"clockColor\":\"#%02X%02X%02X\"}",
        nR, nG, nB, cR, cG, cB);
    request->send(200, "application/json", json);
}

void BellServer::handleSetColors(AsyncWebServerRequest *request, uint8_t *data, size_t len) {
    String body = String((char*)data).substring(0, len);

    uint8_t nR = 255, nG = 255, nB = 0;
    uint8_t cR = 255, cG = 255, cB = 0;

    int idx = body.indexOf("\"nameColor\":\"#");
    if (idx >= 0) {
        String hex = body.substring(idx + 14, idx + 20);
        long val = strtol(hex.c_str(), NULL, 16);
        nR = (val >> 16) & 0xFF;
        nG = (val >> 8) & 0xFF;
        nB = val & 0xFF;
    }

    idx = body.indexOf("\"clockColor\":\"#");
    if (idx >= 0) {
        String hex = body.substring(idx + 15, idx + 21);
        long val = strtol(hex.c_str(), NULL, 16);
        cR = (val >> 16) & 0xFF;
        cG = (val >> 8) & 0xFF;
        cB = val & 0xFF;
    }

    displayPtr->setNameColor(nR, nG, nB);
    displayPtr->setClockColor(cR, cG, cB);
    storagePtr->saveColors(nR, nG, nB, cR, cG, cB);
    displayPtr->requestRedraw();

    char json[120];
    snprintf(json, sizeof(json),
        "{\"success\":true,\"nameColor\":\"#%02X%02X%02X\",\"clockColor\":\"#%02X%02X%02X\"}",
        nR, nG, nB, cR, cG, cB);
    request->send(200, "application/json", json);

    Serial.printf("[Web] Colors set: name=#%02X%02X%02X clock=#%02X%02X%02X\n",
                  nR, nG, nB, cR, cG, cB);
    if (logPtr) logPtr->log(LOG_CAT_SETTINGS, "Display colors updated");
}

// ========================================================
//  HOLIDAYS API
// ========================================================

void BellServer::handleGetHolidays(AsyncWebServerRequest *request) {
    String json = "{";
    json += "\"count\":" + String(storagePtr->getHolidayCount());
    json += ",\"max\":" + String(MAX_HOLIDAYS);
    json += ",\"holidays\":" + storagePtr->loadHolidaysJSON();
    json += "}";
    request->send(200, "application/json", json);
}

void BellServer::handleAddHoliday(AsyncWebServerRequest *request, uint8_t *data, size_t len) {
    String body = String((char*)data).substring(0, len);

    String dateStr = "";
    String holName = "";

    int idx = body.indexOf("\"date\":\"");
    if (idx >= 0) {
        int start = idx + 8;
        int end = body.indexOf("\"", start);
        if (end > start) {
            dateStr = body.substring(start, end);
            dateStr.trim();
        }
    }

    idx = body.indexOf("\"name\":\"");
    if (idx >= 0) {
        int start = idx + 8;
        int end = body.indexOf("\"", start);
        if (end > start) {
            holName = body.substring(start, end);
            holName.trim();
            if (holName.length() > MAX_HOLIDAY_NAME_LEN) {
                holName = holName.substring(0, MAX_HOLIDAY_NAME_LEN);
            }
        }
    }

    if (dateStr.length() != 10 || dateStr[4] != '-' || dateStr[7] != '-') {
        request->send(400, "application/json",
            "{\"error\":\"Invalid date format. Use YYYY-MM-DD\"}");
        return;
    }

    int year  = dateStr.substring(0, 4).toInt();
    int month = dateStr.substring(5, 7).toInt();
    int day   = dateStr.substring(8, 10).toInt();

    if (year < 2020 || year > 2099 || month < 1 || month > 12 || day < 1 || day > 31) {
        request->send(400, "application/json",
            "{\"error\":\"Invalid date values\"}");
        return;
    }

    if (storagePtr->getHolidayCount() >= MAX_HOLIDAYS) {
        request->send(400, "application/json",
            "{\"error\":\"Maximum holidays reached (" + String(MAX_HOLIDAYS) + ")\"}");
        return;
    }

    if (storagePtr->addHoliday(year, month, day, holName.c_str())) {
        String json = "{\"success\":true,\"date\":\"" + dateStr +
                      "\",\"name\":\"" + holName + "\"}";
        request->send(200, "application/json", json);
        Serial.printf("[Web] Holiday added: %s %s\n", dateStr.c_str(), holName.c_str());
        if (logPtr) logPtr->logf(LOG_CAT_SCHEDULE,
            "Holiday added: %s %s", dateStr.c_str(), holName.c_str());
    } else {
        request->send(409, "application/json",
            "{\"error\":\"Holiday already exists or save failed\"}");
    }
}

void BellServer::handleDeleteHoliday(AsyncWebServerRequest *request) {
    if (!request->hasParam("date")) {
        request->send(400, "application/json",
            "{\"error\":\"Missing 'date' parameter\"}");
        return;
    }

    String dateStr = request->getParam("date")->value();

    if (storagePtr->removeHoliday(dateStr.c_str())) {
        request->send(200, "application/json",
            "{\"success\":true,\"message\":\"Holiday removed\"}");
        Serial.printf("[Web] Holiday removed: %s\n", dateStr.c_str());
        if (logPtr) logPtr->logf(LOG_CAT_SCHEDULE,
            "Holiday removed: %s", dateStr.c_str());
    } else {
        request->send(404, "application/json",
            "{\"error\":\"Holiday not found\"}");
    }
}

void BellServer::handleClearHolidays(AsyncWebServerRequest *request) {
    storagePtr->clearHolidays();
    request->send(200, "application/json",
        "{\"success\":true,\"message\":\"All holidays cleared\"}");
    Serial.println("[Web] All holidays cleared.");
    if (logPtr) logPtr->log(LOG_CAT_SCHEDULE, "All holidays cleared");
}

// ========================================================
//  AUTH API
// ========================================================

void BellServer::handleAuth(AsyncWebServerRequest *request, uint8_t *data, size_t len) {
    String body = String((char*)data).substring(0, len);

    String pin = "";
    int idx = body.indexOf("\"pin\":\"");
    if (idx >= 0) {
        int start = idx + 7;
        int end = body.indexOf("\"", start);
        if (end > start) {
            pin = body.substring(start, end);
        }
    }

    char storedPin[MAX_PASSWORD_LEN + 1];
    if (!storagePtr->loadPassword(storedPin, sizeof(storedPin))) {
        strncpy(storedPin, DEFAULT_PASSWORD, sizeof(storedPin));
    }

    if (pin == String(storedPin)) {
        request->send(200, "application/json", "{\"success\":true}");
        Serial.println("[Web] Auth: correct PIN");
    } else {
        request->send(401, "application/json",
            "{\"success\":false,\"error\":\"Wrong PIN\"}");
        Serial.println("[Web] Auth: WRONG PIN attempt");
        if (logPtr) logPtr->log(LOG_CAT_ERROR, "Auth failed - wrong PIN");
    }
}

void BellServer::handleChangePassword(AsyncWebServerRequest *request, uint8_t *data, size_t len) {
    String body = String((char*)data).substring(0, len);

    String current = "";
    String newPass = "";

    int idx = body.indexOf("\"current\":\"");
    if (idx >= 0) {
        int start = idx + 11;
        int end = body.indexOf("\"", start);
        if (end > start) current = body.substring(start, end);
    }

    idx = body.indexOf("\"password\":\"");
    if (idx >= 0) {
        int start = idx + 12;
        int end = body.indexOf("\"", start);
        if (end > start) newPass = body.substring(start, end);
    }

    char storedPin[MAX_PASSWORD_LEN + 1];
    if (!storagePtr->loadPassword(storedPin, sizeof(storedPin))) {
        strncpy(storedPin, DEFAULT_PASSWORD, sizeof(storedPin));
    }

    if (current != String(storedPin)) {
        request->send(401, "application/json",
            "{\"error\":\"Current PIN is wrong\"}");
        Serial.println("[Web] Password change: wrong current PIN");
        if (logPtr) logPtr->log(LOG_CAT_ERROR, "Password change failed - wrong current PIN");
        return;
    }

    if (newPass.length() < MIN_PASSWORD_LEN || newPass.length() > MAX_PASSWORD_LEN) {
        String err = "{\"error\":\"PIN must be " + String(MIN_PASSWORD_LEN) +
                     "-" + String(MAX_PASSWORD_LEN) + " characters\"}";
        request->send(400, "application/json", err);
        return;
    }

    storagePtr->savePassword(newPass.c_str());

    request->send(200, "application/json",
        "{\"success\":true,\"message\":\"PIN changed\"}");
    Serial.println("[Web] Password changed successfully");
    if (logPtr) logPtr->log(LOG_CAT_SETTINGS, "Password changed");
}

// ========================================================
//  TEMPLATES API
// ========================================================

void BellServer::handleGetTemplates(AsyncWebServerRequest *request) {
    String json = "{";
    json += "\"count\":" + String(storagePtr->getTemplateCount());
    json += ",\"max\":" + String(MAX_TEMPLATES);
    json += ",\"templates\":" + storagePtr->loadTemplatesJSON();
    json += "}";
    request->send(200, "application/json", json);
}

void BellServer::handleSaveTemplate(AsyncWebServerRequest *request, uint8_t *data, size_t len) {
    String body = String((char*)data).substring(0, len);

    String tplName = "";
    int idx = body.indexOf("\"name\":\"");
    if (idx >= 0) {
        int start = idx + 8;
        int end = body.indexOf("\"", start);
        if (end > start) {
            tplName = body.substring(start, end);
            tplName.trim();
            if (tplName.length() > MAX_TEMPLATE_NAME_LEN) {
                tplName = tplName.substring(0, MAX_TEMPLATE_NAME_LEN);
            }
        }
    }

    if (tplName.length() == 0) {
        request->send(400, "application/json",
            "{\"error\":\"Template name required\"}");
        return;
    }

    if (eventsPtr->getCount() == 0) {
        request->send(400, "application/json",
            "{\"error\":\"No events to save\"}");
        return;
    }

    if (storagePtr->getTemplateCount() >= MAX_TEMPLATES) {
        request->send(400, "application/json",
            "{\"error\":\"Maximum templates reached (" + String(MAX_TEMPLATES) + ")\"}");
        return;
    }

    uint8_t newId = storagePtr->getNextTemplateId();
    if (newId >= MAX_TEMPLATES) {
        request->send(400, "application/json",
            "{\"error\":\"No template slots available\"}");
        return;
    }

    bool ok = storagePtr->saveTemplate(newId, tplName.c_str(),
                                        eventsPtr->getNextId(),
                                        eventsPtr->getCount(),
                                        eventsPtr->getEvents());
    if (ok) {
        String json = "{\"success\":true,\"id\":" + String(newId) +
                      ",\"name\":\"" + tplName + "\"}";
        request->send(200, "application/json", json);
        Serial.printf("[Web] Template saved: #%d '%s' (%d events)\n",
                      newId, tplName.c_str(), eventsPtr->getCount());
        if (logPtr) logPtr->logf(LOG_CAT_SCHEDULE,
            "Template saved: %s (%d events)", tplName.c_str(), eventsPtr->getCount());
    } else {
        request->send(500, "application/json",
            "{\"error\":\"Failed to save template\"}");
    }
}

void BellServer::handleLoadTemplate(AsyncWebServerRequest *request) {
    if (!request->hasParam("id")) {
        request->send(400, "application/json",
            "{\"error\":\"Missing 'id' parameter\"}");
        return;
    }

    int id = request->getParam("id")->value().toInt();
    if (id < 0 || id >= MAX_TEMPLATES) {
        request->send(400, "application/json",
            "{\"error\":\"Invalid template ID\"}");
        return;
    }

    uint16_t nextId;
    uint8_t count;
    Event loaded[MAX_EVENTS];

    if (!storagePtr->loadTemplate(id, nextId, count, loaded)) {
        request->send(404, "application/json",
            "{\"error\":\"Template not found\"}");
        return;
    }

    eventsPtr->clear();
    eventsPtr->setState(nextId, count, loaded);
    saveEventsToStorage();
    displayPtr->requestRedraw();

    String json = "{\"success\":true,\"id\":" + String(id) +
                  ",\"events\":" + String(count) + "}";
    request->send(200, "application/json", json);

    Serial.printf("[Web] Template #%d loaded: %d events\n", id, count);
    if (logPtr) logPtr->logf(LOG_CAT_SCHEDULE,
        "Template loaded: ID %d (%d events)", id, count);
}

void BellServer::handleDeleteTemplate(AsyncWebServerRequest *request) {
    if (!request->hasParam("id")) {
        request->send(400, "application/json",
            "{\"error\":\"Missing 'id' parameter\"}");
        return;
    }

    int id = request->getParam("id")->value().toInt();

    if (storagePtr->deleteTemplate(id)) {
        request->send(200, "application/json",
            "{\"success\":true,\"message\":\"Template deleted\"}");
        Serial.printf("[Web] Template #%d deleted\n", id);
        if (logPtr) logPtr->logf(LOG_CAT_SCHEDULE,
            "Template deleted: ID %d", id);
    } else {
        request->send(404, "application/json",
            "{\"error\":\"Template not found\"}");
    }
}

// ========================================================
//  BACKUP & RESTORE
// ========================================================

void BellServer::handleBackupDownload(AsyncWebServerRequest *request) {
    Serial.println("[Server] Backup download requested");

    String json = "{\n";
    json += "  \"version\": 1,\n";

    char schoolName[MAX_SCHOOL_NAME_LEN + 1];
    if (storagePtr->loadSchoolName(schoolName, sizeof(schoolName))) {
        json += "  \"schoolName\": \"" + String(schoolName) + "\",\n";
    } else {
        json += "  \"schoolName\": \"" + String(DEFAULT_SCHOOL_NAME) + "\",\n";
    }

    uint8_t profile = storagePtr->loadProfile();
    json += "  \"profile\": " + String(profile) + ",\n";

    uint8_t theme = storagePtr->loadTheme();
    json += "  \"theme\": " + String(theme) + ",\n";

    uint8_t nR, nG, nB, cR, cG, cB;
    if (storagePtr->loadColors(nR, nG, nB, cR, cG, cB)) {
        char nameBuf[8], clockBuf[8];
        snprintf(nameBuf, sizeof(nameBuf), "#%02X%02X%02X", nR, nG, nB);
        snprintf(clockBuf, sizeof(clockBuf), "#%02X%02X%02X", cR, cG, cB);
        json += "  \"nameColor\": \"" + String(nameBuf) + "\",\n";
        json += "  \"clockColor\": \"" + String(clockBuf) + "\",\n";
    } else {
        json += "  \"nameColor\": \"#FFFF00\",\n";
        json += "  \"clockColor\": \"#FFFF00\",\n";
    }

    char password[MAX_PASSWORD_LEN + 1];
    if (storagePtr->loadPassword(password, sizeof(password))) {
        json += "  \"password\": \"" + String(password) + "\",\n";
    } else {
        json += "  \"password\": \"" + String(DEFAULT_PASSWORD) + "\",\n";
    }

    json += "  \"events\": [\n";
    uint8_t evCount = eventsPtr->getCount();
    Event* events = eventsPtr->getEvents();
    for (int i = 0; i < evCount; i++) {
        Event &e = events[i];
        json += "    {";
        json += "\"id\":" + String(e.id);
        json += ",\"name\":\"";
        for (int c = 0; c < MAX_EVENT_NAME_LEN && e.name[c]; c++) {
            if (e.name[c] == '"') json += "\\\"";
            else if (e.name[c] == '\\') json += "\\\\";
            else json += e.name[c];
        }
        json += "\"";
        json += ",\"hour\":" + String(e.hour);
        json += ",\"minute\":" + String(e.minute);
        json += ",\"weekdayMask\":" + String(e.weekdayMask);
        json += ",\"folder\":" + String(e.folder);
        json += ",\"track\":" + String(e.track);
        json += ",\"enabled\":" + String(e.enabled ? "true" : "false");
        json += ",\"duration\":" + String(e.duration);
        json += ",\"eventType\":" + String(e.eventType);
        json += ",\"intervalMinutes\":" + String(e.intervalMinutes);
        json += ",\"endMinute\":" + String(e.endMinute);
        json += "}";
        if (i < evCount - 1) json += ",";
        json += "\n";
    }
    json += "  ],\n";

    json += "  \"holidays\": ";
    json += storagePtr->loadHolidaysJSON();
    json += ",\n";

    json += "  \"sounds\": [\n";
    bool firstSound = true;
    for (int t = 1; t <= MAX_MUSIC_TRACKS; t++) {
        String sName = storagePtr->getSoundName(t);
        if (sName.length() > 0) {
            if (!firstSound) json += ",\n";
            json += "    {\"track\":" + String(t) + ",\"name\":\"" + sName + "\"}";
            firstSound = false;
        }
    }
    for (int t = RINGTONE_START_TRACK; t < RINGTONE_START_TRACK + RINGTONE_COUNT; t++) {
        String sName = storagePtr->getSoundName(t);
        if (sName.length() > 0) {
            if (!firstSound) json += ",\n";
            json += "    {\"track\":" + String(t) + ",\"name\":\"" + sName + "\"}";
            firstSound = false;
        }
    }
    json += "\n  ]\n";
    json += "}";

    AsyncWebServerResponse *response =
        request->beginResponse(200, "application/json", json);
    response->addHeader("Content-Disposition",
        "attachment; filename=\"smartbell_backup.json\"");
    response->addHeader("Access-Control-Allow-Origin", "*");
    request->send(response);

    Serial.printf("[Server] Backup sent: %d bytes, %d events\n",
                  json.length(), evCount);
    if (logPtr) logPtr->logf(LOG_CAT_SYSTEM,
        "Backup downloaded (%d events)", evCount);
}

void BellServer::handleBackupRestore(AsyncWebServerRequest *request,
                                      uint8_t *data, size_t len,
                                      size_t index, size_t total) {
    if (index == 0) {
        _restoreBody = "";
        _restoreBody.reserve(total);
        Serial.printf("[Server] Restore upload started: %d bytes\n", total);
    }

    _restoreBody += String((char*)data).substring(0, len);

    if (index + len < total) return;

    Serial.printf("[Server] Restore data received: %d bytes\n", _restoreBody.length());

    String &j = _restoreBody;
    int restoredCount = 0;

    int vIdx = j.indexOf("\"version\"");
    if (vIdx < 0) {
        request->send(400, "application/json",
            "{\"error\":\"Invalid backup file - no version field\"}");
        _restoreBody = "";
        return;
    }

    // --- School Name ---
    int snIdx = j.indexOf("\"schoolName\"");
    if (snIdx >= 0) {
        int colonIdx = j.indexOf(':', snIdx);
        int quoteStart = j.indexOf('"', colonIdx + 1);
        int quoteEnd = j.indexOf('"', quoteStart + 1);
        if (quoteStart >= 0 && quoteEnd > quoteStart) {
            String name = j.substring(quoteStart + 1, quoteEnd);
            char nameBuf[MAX_SCHOOL_NAME_LEN + 1];
            name.toCharArray(nameBuf, sizeof(nameBuf));
            storagePtr->saveSchoolName(nameBuf);
            displayPtr->setSchoolName(nameBuf);
            restoredCount++;
            Serial.printf("[Restore] School name: %s\n", nameBuf);
        }
    }

    // --- Profile ---
    int prIdx = j.indexOf("\"profile\"");
    if (prIdx >= 0) {
        int colonIdx = j.indexOf(':', prIdx);
        int valStart = colonIdx + 1;
        while (valStart < (int)j.length() && j[valStart] == ' ') valStart++;
        uint8_t profile = j.substring(valStart).toInt();
        if (profile < MAX_PROFILES) {
            storagePtr->saveProfile(profile);
            displayPtr->setProfile(profile);
            restoredCount++;
            Serial.printf("[Restore] Profile: %d\n", profile);
        }
    }

    // --- Theme ---
    int thIdx = j.indexOf("\"theme\"");
    if (thIdx >= 0) {
        int colonIdx = j.indexOf(':', thIdx);
        int valStart = colonIdx + 1;
        while (valStart < (int)j.length() && j[valStart] == ' ') valStart++;
        uint8_t theme = j.substring(valStart).toInt();
        storagePtr->saveTheme(theme);
        displayPtr->setBgTheme(theme);
        restoredCount++;
        Serial.printf("[Restore] Theme: %d\n", theme);
    }

    // --- Name Color ---
    int ncIdx = j.indexOf("\"nameColor\"");
    uint8_t nR = 255, nG = 255, nB = 0;
    if (ncIdx >= 0) {
        int q1 = j.indexOf('"', j.indexOf(':', ncIdx) + 1);
        int q2 = j.indexOf('"', q1 + 1);
        if (q1 >= 0 && q2 > q1) {
            String hex = j.substring(q1 + 1, q2);
            if (hex.length() == 7 && hex[0] == '#') {
                nR = strtol(hex.substring(1, 3).c_str(), NULL, 16);
                nG = strtol(hex.substring(3, 5).c_str(), NULL, 16);
                nB = strtol(hex.substring(5, 7).c_str(), NULL, 16);
            }
        }
    }

    // --- Clock Color ---
    int ccIdx = j.indexOf("\"clockColor\"");
    uint8_t cR = 255, cG = 255, cB = 0;
    if (ccIdx >= 0) {
        int q1 = j.indexOf('"', j.indexOf(':', ccIdx) + 1);
        int q2 = j.indexOf('"', q1 + 1);
        if (q1 >= 0 && q2 > q1) {
            String hex = j.substring(q1 + 1, q2);
            if (hex.length() == 7 && hex[0] == '#') {
                cR = strtol(hex.substring(1, 3).c_str(), NULL, 16);
                cG = strtol(hex.substring(3, 5).c_str(), NULL, 16);
                cB = strtol(hex.substring(5, 7).c_str(), NULL, 16);
            }
        }
    }

    storagePtr->saveColors(nR, nG, nB, cR, cG, cB);
    displayPtr->setNameColor(nR, nG, nB);
    displayPtr->setClockColor(cR, cG, cB);
    restoredCount++;
    Serial.printf("[Restore] Colors: name=#%02X%02X%02X clock=#%02X%02X%02X\n",
                  nR, nG, nB, cR, cG, cB);

    // --- Password ---
    int pwIdx = j.indexOf("\"password\"");
    if (pwIdx >= 0) {
        int q1 = j.indexOf('"', j.indexOf(':', pwIdx) + 1);
        int q2 = j.indexOf('"', q1 + 1);
        if (q1 >= 0 && q2 > q1) {
            String pw = j.substring(q1 + 1, q2);
            char pwBuf[MAX_PASSWORD_LEN + 1];
            pw.toCharArray(pwBuf, sizeof(pwBuf));
            storagePtr->savePassword(pwBuf);
            restoredCount++;
            Serial.printf("[Restore] Password updated\n");
        }
    }

    // --- Events ---
    int evArrayStart = j.indexOf("\"events\"");
    if (evArrayStart >= 0) {
        int bracketStart = j.indexOf('[', evArrayStart);
        int bracketEnd   = j.indexOf(']', bracketStart);

        if (bracketStart >= 0 && bracketEnd > bracketStart) {
            eventsPtr->clear();

            String evBlock = j.substring(bracketStart + 1, bracketEnd);
            int searchFrom = 0;
            int eventsParsed = 0;

            while (searchFrom < (int)evBlock.length() && eventsParsed < MAX_EVENTS) {
                int objStart = evBlock.indexOf('{', searchFrom);
                int objEnd   = evBlock.indexOf('}', objStart);
                if (objStart < 0 || objEnd < 0) break;

                String obj = evBlock.substring(objStart, objEnd + 1);
                searchFrom = objEnd + 1;

                char eName[MAX_EVENT_NAME_LEN + 1] = "";
                uint8_t eHour = 0, eMinute = 0, eMask = 0x7E,
                        eFolder = 1, eTrack = 1;
                bool eEnabled = true;

                int nIdx2 = obj.indexOf("\"name\"");
                if (nIdx2 >= 0) {
                    int q1 = obj.indexOf('"', obj.indexOf(':', nIdx2) + 1);
                    int q2 = obj.indexOf('"', q1 + 1);
                    if (q1 >= 0 && q2 > q1)
                        obj.substring(q1 + 1, q2).toCharArray(eName, sizeof(eName));
                }

                int hIdx = obj.indexOf("\"hour\"");
                if (hIdx >= 0) eHour = obj.substring(obj.indexOf(':', hIdx) + 1).toInt();

                int mIdx = obj.indexOf("\"minute\"");
                if (mIdx >= 0) eMinute = obj.substring(obj.indexOf(':', mIdx) + 1).toInt();

                int wIdx = obj.indexOf("\"weekdayMask\"");
                if (wIdx >= 0) eMask = obj.substring(obj.indexOf(':', wIdx) + 1).toInt();

                int fIdx = obj.indexOf("\"folder\"");
                if (fIdx >= 0) eFolder = obj.substring(obj.indexOf(':', fIdx) + 1).toInt();

                int tIdx = obj.indexOf("\"track\"");
                if (tIdx >= 0) {
                    int c = obj.indexOf(':', tIdx);
                    String tStr = obj.substring(c + 1);
                    int endPos = 0;
                    while (endPos < (int)tStr.length() &&
                           (isDigit(tStr[endPos]) || tStr[endPos] == ' ')) endPos++;
                    eTrack = tStr.substring(0, endPos).toInt();
                }

                int enIdx = obj.indexOf("\"enabled\"");
                if (enIdx >= 0) {
                    int c = obj.indexOf(':', enIdx);
                    String val = obj.substring(c + 1);
                    val.trim();
                    eEnabled = val.startsWith("true");
                }

                uint16_t eDuration = 0;
                int durIdx = obj.indexOf("\"duration\"");
                if (durIdx >= 0) {
                    eDuration = obj.substring(obj.indexOf(':', durIdx) + 1).toInt();
                    if (eDuration > MAX_BELL_DURATION) eDuration = MAX_BELL_DURATION;
                }

                uint8_t eEventType = EVENT_TYPE_FIXED;
                int etIdx = obj.indexOf("\"eventType\"");
                if (etIdx >= 0) {
                    eEventType = (uint8_t)obj.substring(
                        obj.indexOf(':', etIdx) + 1).toInt();
                    if (eEventType != EVENT_TYPE_FIXED &&
                        eEventType != EVENT_TYPE_INTERVAL)
                        eEventType = EVENT_TYPE_FIXED;
                }

                uint16_t eIntervalMin = 0;
                int imIdx = obj.indexOf("\"intervalMinutes\"");
                if (imIdx >= 0)
                    eIntervalMin = (uint16_t)obj.substring(
                        obj.indexOf(':', imIdx) + 1).toInt();

                uint16_t eEndMin = 0;
                int emIdx = obj.indexOf("\"endMinute\"");
                if (emIdx >= 0)
                    eEndMin = (uint16_t)obj.substring(
                        obj.indexOf(':', emIdx) + 1).toInt();

                if (eHour <= 23 && eMinute <= 59) {
                    int16_t newId = eventsPtr->addEvent(
                        eHour, eMinute, eMask, eFolder, eTrack, eName,
                        eDuration, eEventType, eIntervalMin, eEndMin);
                    if (newId >= 0 && !eEnabled) {
                        Event* evts = eventsPtr->getEvents();
                        for (int i = 0; i < eventsPtr->getCount(); i++) {
                            if (evts[i].id == newId) {
                                evts[i].enabled = false;
                                break;
                            }
                        }
                    }
                    eventsParsed++;
                }
            }

            storagePtr->saveEvents(eventsPtr->getNextId(),
                                   eventsPtr->getCount(),
                                   eventsPtr->getEvents());
            restoredCount++;
            Serial.printf("[Restore] Events: %d restored\n", eventsParsed);
        }
    }

    // --- Holidays ---
    int holArrayStart = j.indexOf("\"holidays\"");
    if (holArrayStart >= 0) {
        int bracketStart = j.indexOf('[', holArrayStart);
        int bracketEnd   = j.indexOf(']', bracketStart);

        if (bracketStart >= 0 && bracketEnd > bracketStart) {
            storagePtr->clearHolidays();

            String holBlock = j.substring(bracketStart + 1, bracketEnd);
            int searchFrom = 0;
            int holParsed = 0;

            while (searchFrom < (int)holBlock.length() && holParsed < MAX_HOLIDAYS) {
                int objStart = holBlock.indexOf('{', searchFrom);
                int objEnd   = holBlock.indexOf('}', objStart);
                if (objStart < 0 || objEnd < 0) break;

                String obj = holBlock.substring(objStart, objEnd + 1);
                searchFrom = objEnd + 1;

                char hDate[12] = "";
                char hName[MAX_HOLIDAY_NAME_LEN + 1] = "";

                int dIdx = obj.indexOf("\"date\"");
                if (dIdx >= 0) {
                    int q1 = obj.indexOf('"', obj.indexOf(':', dIdx) + 1);
                    int q2 = obj.indexOf('"', q1 + 1);
                    if (q1 >= 0 && q2 > q1)
                        obj.substring(q1 + 1, q2).toCharArray(hDate, sizeof(hDate));
                }

                int hnIdx = obj.indexOf("\"name\"");
                if (hnIdx >= 0) {
                    int q1 = obj.indexOf('"', obj.indexOf(':', hnIdx) + 1);
                    int q2 = obj.indexOf('"', q1 + 1);
                    if (q1 >= 0 && q2 > q1)
                        obj.substring(q1 + 1, q2).toCharArray(hName, sizeof(hName));
                }

                if (strlen(hDate) == 10 && hDate[4] == '-' && hDate[7] == '-') {
                    uint16_t hYear  = atoi(hDate);
                    uint8_t  hMonth = atoi(hDate + 5);
                    uint8_t  hDay   = atoi(hDate + 8);

                    if (hYear >= 2020 && hYear <= 2099 &&
                        hMonth >= 1 && hMonth <= 12 &&
                        hDay >= 1 && hDay <= 31) {
                        storagePtr->addHoliday(hYear, hMonth, hDay, hName);
                        holParsed++;
                    }
                }
            }
            restoredCount++;
            Serial.printf("[Restore] Holidays: %d restored\n", holParsed);
        }
    }

    // --- Sounds ---
    int sndArrayStart = j.indexOf("\"sounds\"");
    if (sndArrayStart >= 0) {
        int bracketStart = j.indexOf('[', sndArrayStart);
        int bracketEnd   = j.indexOf(']', bracketStart);

        if (bracketStart >= 0 && bracketEnd > bracketStart) {
            String soundsData = "";
            String sndBlock = j.substring(bracketStart + 1, bracketEnd);
            int searchFrom = 0;
            int sndParsed = 0;

            while (searchFrom < (int)sndBlock.length()) {
                int objStart = sndBlock.indexOf('{', searchFrom);
                int objEnd   = sndBlock.indexOf('}', objStart);
                if (objStart < 0 || objEnd < 0) break;

                String obj = sndBlock.substring(objStart, objEnd + 1);
                searchFrom = objEnd + 1;

                int sTrack = 0;
                String sName = "";

                int stIdx = obj.indexOf("\"track\"");
                if (stIdx >= 0)
                    sTrack = obj.substring(obj.indexOf(':', stIdx) + 1).toInt();

                int snIdx2 = obj.indexOf("\"name\"");
                if (snIdx2 >= 0) {
                    int q1 = obj.indexOf('"', obj.indexOf(':', snIdx2) + 1);
                    int q2 = obj.indexOf('"', q1 + 1);
                    if (q1 >= 0 && q2 > q1)
                        sName = obj.substring(q1 + 1, q2);
                }

                if (sTrack > 0 && sName.length() > 0) {
                    char trackStr[6];
                    snprintf(trackStr, sizeof(trackStr), "%04d", sTrack);
                    soundsData += String(trackStr) + "," + sName + "\n";
                    sndParsed++;
                }
            }

            if (soundsData.length() > 0) {
                storagePtr->saveSounds(soundsData);
                restoredCount++;
                Serial.printf("[Restore] Sounds: %d names restored\n", sndParsed);
            }
        }
    }

    displayPtr->requestRedraw();

    String response = "{\"success\":true,\"restored\":" +
                      String(restoredCount) + "}";
    request->send(200, "application/json", response);
    Serial.printf("[Server] Restore complete: %d categories restored\n", restoredCount);
    if (logPtr) logPtr->logf(LOG_CAT_SYSTEM,
        "Settings restored from backup (%d categories)", restoredCount);

    _restoreBody = "";
}

// ========================================================
//  SYSTEM LOGS API
// ========================================================

void BellServer::handleGetLogs(AsyncWebServerRequest *request) {
    String json = logPtr->toJSON();
    request->send(200, "application/json", json);
}

void BellServer::handleClearLogs(AsyncWebServerRequest *request) {
    logPtr->clear();
    logPtr->log(LOG_CAT_SYSTEM, "Logs cleared by user");
    request->send(200, "application/json", "{\"ok\":true}");
}

// ========================================================
//  WIFI API
// ========================================================

void BellServer::handleGetWiFiStatus(AsyncWebServerRequest *request) {
    String json = "{";
    json += "\"mode\":\"" + String(_isStationMode ? "station" : "ap") + "\"";

    if (_isStationMode && WiFi.status() == WL_CONNECTED) {
        json += ",\"connected\":true";
        json += ",\"ssid\":\"" + WiFi.SSID() + "\"";
        json += ",\"ip\":\"" + WiFi.localIP().toString() + "\"";
        json += ",\"rssi\":" + String(WiFi.RSSI());
        json += ",\"gateway\":\"" + WiFi.gatewayIP().toString() + "\"";

        int rssi = WiFi.RSSI();
        String quality = "Excellent";
        if (rssi < -80) quality = "Poor";
        else if (rssi < -70) quality = "Fair";
        else if (rssi < -60) quality = "Good";
        json += ",\"quality\":\"" + quality + "\"";
    } else if (_isStationMode) {
        json += ",\"connected\":false";
        json += ",\"ssid\":\"\"";
        json += ",\"ip\":\"\"";
    } else {
        json += ",\"connected\":true";
        json += ",\"ssid\":\"" + String(WIFI_AP_SSID) + "\"";
        json += ",\"ip\":\"" + WiFi.softAPIP().toString() + "\"";
        json += ",\"clients\":" + String(WiFi.softAPgetStationNum());
    }

    json += ",\"apIP\":\"" + WiFi.softAPIP().toString() + "\"";
    json += ",\"apSSID\":\"" + String(WIFI_AP_SSID) + "\"";
    json += ",\"ntpSynced\":" + String(rtcPtr->isNTPSynced() ? "true" : "false");

    unsigned long lastSync = rtcPtr->lastNTPSync();
    if (lastSync > 0) {
        unsigned long ago = (millis() - lastSync) / 1000;
        json += ",\"ntpLastSync\":" + String(ago);
    } else {
        json += ",\"ntpLastSync\":-1";
    }

    json += ",\"hasCreds\":" +
            String(storagePtr->hasWiFiCredentials() ? "true" : "false");
    json += ",\"reconnecting\":" +
            String(_reconState != RECONNECT_IDLE ? "true" : "false");

    if (_reconState != RECONNECT_IDLE) {
        json += ",\"reconAttempts\":" + String(_reconAttempts);
        json += ",\"reconState\":\"";
        switch (_reconState) {
            case RECONNECT_WAIT:       json += "cooldown";   break;
            case RECONNECT_SCANNING:   json += "scanning";   break;
            case RECONNECT_SCAN_WAIT:  json += "scanning";   break;
            case RECONNECT_CONNECTING: json += "connecting"; break;
            case RECONNECT_BACKOFF:    json += "backoff";    break;
            default:                   json += "unknown";    break;
        }
        json += "\"";
    }

    json += "}";
    request->send(200, "application/json", json);
}

void BellServer::handleWiFiScan(AsyncWebServerRequest *request) {
    Serial.println("[WiFi] Starting async network scan...");

    int n = WiFi.scanComplete();

    if (n == WIFI_SCAN_FAILED) {
        WiFi.scanNetworks(true, false);
        request->send(200, "application/json",
            "{\"count\":0,\"networks\":[],\"scanning\":true,"
            "\"message\":\"Scan started. Tap scan again in 3 seconds.\"}");
        Serial.println("[WiFi] Async scan started");
        return;
    }

    if (n == WIFI_SCAN_RUNNING) {
        request->send(200, "application/json",
            "{\"count\":0,\"networks\":[],\"scanning\":true,"
            "\"message\":\"Still scanning... tap again.\"}");
        Serial.println("[WiFi] Scan still running...");
        return;
    }

    String json = "{\"count\":" + String(n) +
                  ",\"scanning\":false,\"networks\":[";

    for (int i = 0; i < n; i++) {
        if (i > 0) json += ",";
        json += "{\"ssid\":\"";

        String ssid = WiFi.SSID(i);
        for (unsigned int c = 0; c < ssid.length(); c++) {
            if      (ssid[c] == '"')  json += "\\\"";
            else if (ssid[c] == '\\') json += "\\\\";
            else                       json += ssid[c];
        }

        json += "\",\"rssi\":" + String(WiFi.RSSI(i));
        json += ",\"encrypted\":" +
                String(WiFi.encryptionType(i) != WIFI_AUTH_OPEN ? "true" : "false");

        int rssi = WiFi.RSSI(i);
        String quality = "Excellent";
        if (rssi < -80) quality = "Poor";
        else if (rssi < -70) quality = "Fair";
        else if (rssi < -60) quality = "Good";
        json += ",\"quality\":\"" + quality + "\"";
        json += "}";
    }
    json += "]}";

    WiFi.scanDelete();
    request->send(200, "application/json", json);
    Serial.printf("[WiFi] Scan complete: %d networks found\n", n);
}

void BellServer::handleWiFiConnect(AsyncWebServerRequest *request,
                                    uint8_t *data, size_t len) {
    String body = String((char*)data).substring(0, len);

    String ssid = "";
    String pass = "";

    int idx = body.indexOf("\"ssid\":\"");
    if (idx >= 0) {
        int start = idx + 8;
        int end = body.indexOf("\"", start);
        if (end > start) ssid = body.substring(start, end);
    }

    idx = body.indexOf("\"password\":\"");
    if (idx >= 0) {
        int start = idx + 12;
        int end = body.indexOf("\"", start);
        if (end > start) pass = body.substring(start, end);
    }

    pass.trim();

    if (ssid.length() == 0 || ssid.length() > WIFI_STA_MAX_SSID_LEN) {
        request->send(400, "application/json", "{\"error\":\"Invalid SSID\"}");
        return;
    }

    storagePtr->saveWiFiCredentials(ssid.c_str(), pass.c_str());
    storagePtr->saveWiFiMode(1);

    _reconEnabled = true;
    _reconState   = RECONNECT_IDLE;

    if (logPtr) logPtr->logf(LOG_CAT_WIFI,
        "WiFi credentials saved: %s", ssid.c_str());
    Serial.printf("[WiFi] Credentials saved for: %s — connecting from loop()\n",
                  ssid.c_str());

    String json = "{\"success\":true,\"message\":\"Credentials saved. Connecting to "
                  + ssid + "... Check status in a few seconds.\"}";
    request->send(200, "application/json", json);

    _wifiConnState = 1;
    _wifiConnStart = millis();
}

void BellServer::handleWiFiDisconnect(AsyncWebServerRequest *request) {
    storagePtr->clearWiFiCredentials();
    storagePtr->saveWiFiMode(0);

    _reconEnabled  = false;
    _reconState    = RECONNECT_IDLE;
    _reconAttempts = 0;

    WiFi.disconnect(true);
    WiFi.mode(WIFI_AP);
    WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD, WIFI_AP_CHANNEL, 0, WIFI_AP_MAX_CONN);

    _isStationMode = false;

    if (logPtr) logPtr->log(LOG_CAT_WIFI,
        "WiFi disconnected, auto-reconnect disabled");

    request->send(200, "application/json",
        "{\"success\":true,\"message\":\"Disconnected. Reverted to AP mode."
        " Reconnect to SmartBell WiFi.\"}");

    Serial.println("[WiFi] Disconnected, back to AP mode, auto-reconnect OFF");
}

void BellServer::handleWiFiAP(AsyncWebServerRequest *request) {
    storagePtr->saveWiFiMode(0);

    _reconEnabled  = false;
    _reconState    = RECONNECT_IDLE;
    _reconAttempts = 0;

    WiFi.disconnect(true);
    WiFi.mode(WIFI_AP);
    WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD, WIFI_AP_CHANNEL, 0, WIFI_AP_MAX_CONN);

    _isStationMode = false;

    if (logPtr) logPtr->log(LOG_CAT_WIFI,
        "Forced AP mode, auto-reconnect disabled");

    request->send(200, "application/json",
        "{\"success\":true,\"message\":\"Switched to AP mode."
        " Connect to SmartBell WiFi.\"}");

    Serial.println("[WiFi] Forced AP mode, auto-reconnect OFF");
}

void BellServer::handleGetStaticIP(AsyncWebServerRequest *request) {
    String json = "{\"enabled\":false}";

    if (LittleFS.exists(WIFI_STATIC_IP_FILE)) {
        File f = LittleFS.open(WIFI_STATIC_IP_FILE, "r");
        if (f) {
            String line = f.readStringUntil('\n');
            f.close();
            line.trim();

            if (line.length() > 0) {
                int c1 = line.indexOf(',');
                int c2 = line.indexOf(',', c1 + 1);
                int c3 = line.indexOf(',', c2 + 1);

                if (c1 > 0 && c2 > 0 && c3 > 0) {
                    json  = "{\"enabled\":true";
                    json += ",\"ip\":\""      + line.substring(0, c1)      + "\"";
                    json += ",\"gateway\":\"" + line.substring(c1 + 1, c2) + "\"";
                    json += ",\"subnet\":\""  + line.substring(c2 + 1, c3) + "\"";
                    json += ",\"dns\":\""     + line.substring(c3 + 1)     + "\"";
                    json += "}";
                }
            }
        }
    }

    request->send(200, "application/json", json);
}

void BellServer::handleSetStaticIP(AsyncWebServerRequest *request,
                                    uint8_t *data, size_t len) {
    String body = String((char*)data).substring(0, len);

    int enIdx = body.indexOf("\"enabled\"");
    if (enIdx >= 0) {
        String val = body.substring(body.indexOf(':', enIdx) + 1);
        val.trim();
        if (val.startsWith("false")) {
            LittleFS.remove(WIFI_STATIC_IP_FILE);
            WiFi.config(IPAddress(0U), IPAddress(0U),
                        IPAddress(0U), IPAddress(0U));
            Serial.println("[WiFi] Static IP cleared → DHCP mode");
            if (logPtr) logPtr->log(LOG_CAT_WIFI, "Static IP cleared, using DHCP");
            request->send(200, "application/json",
                "{\"success\":true,\"message\":\"Switched to DHCP."
                " Reconnect for changes to take effect.\"}");
            return;
        }
    }

    String ip = "", gw = "", sn = "", dns = "";

    int idx = body.indexOf("\"ip\":\"");
    if (idx >= 0) {
        int s = idx + 6, e = body.indexOf('"', s);
        if (e > s) ip = body.substring(s, e);
    }

    idx = body.indexOf("\"gateway\":\"");
    if (idx >= 0) {
        int s = idx + 11, e = body.indexOf('"', s);
        if (e > s) gw = body.substring(s, e);
    }

    idx = body.indexOf("\"subnet\":\"");
    if (idx >= 0) {
        int s = idx + 10, e = body.indexOf('"', s);
        if (e > s) sn = body.substring(s, e);
    }

    idx = body.indexOf("\"dns\":\"");
    if (idx >= 0) {
        int s = idx + 7, e = body.indexOf('"', s);
        if (e > s) dns = body.substring(s, e);
    }

    IPAddress ipAddr, gwAddr, snAddr, dnsAddr;
    if (!ipAddr.fromString(ip)  || !gwAddr.fromString(gw) ||
        !snAddr.fromString(sn)  || !dnsAddr.fromString(dns)) {
        request->send(400, "application/json",
            "{\"error\":\"Invalid IP address format. Use x.x.x.x for all fields.\"}");
        return;
    }

    String line = ip + "," + gw + "," + sn + "," + dns;
    File f = LittleFS.open(WIFI_STATIC_IP_FILE, "w");
    if (!f) {
        request->send(500, "application/json", "{\"error\":\"Failed to save\"}");
        return;
    }
    f.println(line);
    f.close();

    Serial.printf("[WiFi] Static IP saved: %s\n", line.c_str());
    if (logPtr) logPtr->logf(LOG_CAT_WIFI, "Static IP set: %s", ip.c_str());

    String json = "{\"success\":true,\"ip\":\"" + ip +
                  "\",\"gateway\":\"" + gw +
                  "\",\"subnet\":\""  + sn +
                  "\",\"dns\":\""     + dns +
                  "\",\"message\":\"Static IP saved."
                  " Reconnect for changes to take effect.\"}";
    request->send(200, "application/json", json);
}

void BellServer::handleNTPStatus(AsyncWebServerRequest *request) {
    String json = "{";
    json += "\"synced\":"       + String(rtcPtr->isNTPSynced() ? "true" : "false");
    json += ",\"stationMode\":" + String(_isStationMode ? "true" : "false");
    json += ",\"wifiConnected\":"
            + String(WiFi.status() == WL_CONNECTED ? "true" : "false");

    unsigned long lastSync = rtcPtr->lastNTPSync();
    if (lastSync > 0) {
        unsigned long agoSec = (millis() - lastSync) / 1000;
        json += ",\"lastSyncAgo\":" + String(agoSec);

        unsigned long nextIn = 0;
        if (millis() - _lastNTPCheck < NTP_SYNC_INTERVAL)
            nextIn = (NTP_SYNC_INTERVAL - (millis() - _lastNTPCheck)) / 1000;
        json += ",\"nextSyncIn\":" + String(nextIn);
    } else {
        json += ",\"lastSyncAgo\":-1";
        json += ",\"nextSyncIn\":-1";
    }

    json += ",\"server\":\"" + String(NTP_SERVER_1) + "\"";
    json += ",\"gmtOffset\":" + String(NTP_GMT_OFFSET);
    json += "}";

    request->send(200, "application/json", json);
}

void BellServer::handleNTPSync(AsyncWebServerRequest *request) {
    if (!_isStationMode || WiFi.status() != WL_CONNECTED) {
        request->send(400, "application/json",
            "{\"error\":\"NTP requires station mode with active WiFi connection\"}");
        return;
    }

    rtcPtr->startNTPSync();
    _lastNTPCheck = millis();
    if (logPtr) logPtr->log(LOG_CAT_WIFI, "NTP manual sync started");

    request->send(200, "application/json",
        "{\"success\":true,\"message\":\"NTP sync started."
        " Time will update shortly.\"}");
}

// ========================================================
//  SETUP WIZARD API
// ========================================================

void BellServer::handleGetSetupStatus(AsyncWebServerRequest *request) {
    bool firstBoot = storagePtr->isFirstBoot();
    String json = "{\"firstBoot\":" +
                  String(firstBoot ? "true" : "false") + "}";
    request->send(200, "application/json", json);
}

void BellServer::handleSetupComplete(AsyncWebServerRequest *request) {
    storagePtr->markSetupDone();
    request->send(200, "application/json",
        "{\"success\":true,\"message\":\"Setup complete\"}");
    Serial.println("[Web] Setup wizard completed");
    if (logPtr) logPtr->log(LOG_CAT_SYSTEM, "Setup wizard completed");
}

void BellServer::handleSetupReset(AsyncWebServerRequest *request) {
    storagePtr->resetSetup();
    request->send(200, "application/json",
        "{\"success\":true,\"message\":\"Setup wizard will show on next login\"}");
    Serial.println("[Web] Setup wizard reset");
    if (logPtr) logPtr->log(LOG_CAT_SYSTEM, "Setup wizard reset");
}

// ========================================================
//  PROFILE API
// ========================================================

void BellServer::handleGetProfile(AsyncWebServerRequest *request) {
    uint8_t profile = displayPtr->getProfile();
    String json = "{\"profile\":" + String(profile) +
                  ",\"max\":" + String(MAX_PROFILES) + "}";
    request->send(200, "application/json", json);
}

void BellServer::handleSetProfile(AsyncWebServerRequest *request,
                                   uint8_t *data, size_t len) {
    String body = String((char*)data).substring(0, len);

    int idx = body.indexOf("\"profile\":");
    if (idx < 0) {
        request->send(400, "application/json",
            "{\"error\":\"Missing profile field\"}");
        return;
    }

    int profile = body.substring(idx + 10).toInt();
    if (profile < 0 || profile >= MAX_PROFILES) {
        request->send(400, "application/json",
            "{\"error\":\"Profile must be 0-" +
            String(MAX_PROFILES - 1) + "\"}");
        return;
    }

    storagePtr->saveProfile(profile);
    displayPtr->setProfile(profile);

    String json = "{\"success\":true,\"profile\":" + String(profile) + "}";
    request->send(200, "application/json", json);

    Serial.printf("[Web] Profile changed to: %d\n", profile);
    if (logPtr) logPtr->logf(LOG_CAT_SETTINGS,
        "Profile changed to %d", profile);
}

// ════════════════════════════════════════════════════════════════════════════
//  ICS CALENDAR EXPORT
//  GET /api/export-ics?advance=5
// ════════════════════════════════════════════════════════════════════════════

// ICS weekday tokens — index matches weekdayMask bit (0=Sun ... 6=Sat)
static const char* ICS_WDAY[] = {
    "SU","MO","TU","WE","TH","FR","SA"
};

// Days-per-month table (index 1–12). Leap year handled separately.
static const uint8_t DAYS_IN_MONTH[] = {
    0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

static bool isLeapYear(uint16_t y) {
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

static uint8_t daysInMonth(uint8_t m, uint16_t y) {
    if (m == 2 && isLeapYear(y)) return 29;
    return DAYS_IN_MONTH[m];
}

// ───────────────────────────────────────────────────────────────────────────
//  handleExportICS
// ───────────────────────────────────────────────────────────────────────────
void BellServer::handleExportICS(AsyncWebServerRequest *request) {
    int advance = 5;
    if (request->hasParam("advance")) {
        advance = request->getParam("advance")->value().toInt();
        if (advance < 0)  advance = 0;
        if (advance > 60) advance = 60;
    }

    String ics = generateICS(advance);

    AsyncWebServerResponse *response =
        request->beginResponse(200, "text/calendar; charset=utf-8", ics);

    response->addHeader("Content-Disposition",
                        "attachment; filename=\"smartbell_schedule.ics\"");
    response->addHeader("Cache-Control", "no-cache");
    request->send(response);

    Serial.printf("[ICS] Exported %d bytes (advance=%d min, %d events)\n",
                  ics.length(), advance, eventsPtr->getCount());
    if (logPtr) logPtr->logf(LOG_CAT_SYSTEM,
        "Calendar exported (%d events)", eventsPtr->getCount());
}

// ───────────────────────────────────────────────────────────────────────────
//  generateICS  —  builds complete .ics file
// ───────────────────────────────────────────────────────────────────────────
String BellServer::generateICS(int advanceMinutes) {

    // Get current date/time from RTC
    uint16_t year;
    uint8_t  month, day, hour, minute, weekday;
    rtcPtr->getDateTime(year, month, day, hour, minute, weekday);

    String out;
    out.reserve(4096);

    // ── VCALENDAR header ──────────────────────────────────────────────────
    out += "BEGIN:VCALENDAR\r\n";
    out += "VERSION:2.0\r\n";
    out += "PRODID:-//Smart Bell//SmartBell ESP32//EN\r\n";
    out += "CALSCALE:GREGORIAN\r\n";
    out += "METHOD:PUBLISH\r\n";
    out += "X-WR-CALNAME:SmartBell Schedule\r\n";
    out += "X-WR-CALDESC:Auto-generated bell schedule\r\n";
    out += "X-WR-TIMEZONE:Asia/Kolkata\r\n";
    out += "\r\n";

    // ── VTIMEZONE block (IST = UTC+5:30, no DST) ─────────────────────────
    out += "BEGIN:VTIMEZONE\r\n";
    out += "TZID:Asia/Kolkata\r\n";
    out += "BEGIN:STANDARD\r\n";
    out += "TZOFFSETFROM:+0530\r\n";
    out += "TZOFFSETTO:+0530\r\n";
    out += "TZNAME:IST\r\n";
    out += "DTSTART:19700101T000000\r\n";
    out += "END:STANDARD\r\n";
    out += "END:VTIMEZONE\r\n";
    out += "\r\n";

    // ── Timestamp string for DTSTAMP (UTC) ───────────────────────────────
    char dtStamp[18];
    snprintf(dtStamp, sizeof(dtStamp),
             "%04u%02u%02uT%02u%02u%02uZ",
             year, month, day, hour, minute, (uint8_t)0);

    // ── Walk all events ───────────────────────────────────────────────────
    Event*  events = eventsPtr->getEvents();
    uint8_t count  = eventsPtr->getCount();

    for (int i = 0; i < count; i++) {
        const Event &ev = events[i];

        if (!ev.enabled) continue;

        // ── One-time event ────────────────────────────────────────────────
        if (ev.isOneTime()) {
            appendOneTimeEvent(out, ev, year, month, day,
                               dtStamp, advanceMinutes);
            continue;
        }

        // ── Regular events — must have at least one weekday ───────────────
        if ((ev.weekdayMask & 0x7F) == 0) continue;

        if (ev.eventType == EVENT_TYPE_FIXED) {
            appendFixedEvent(out, ev, year, month, day, weekday,
                             dtStamp, advanceMinutes);
        } else {
            appendIntervalEvents(out, ev, year, month, day, weekday,
                                 dtStamp, advanceMinutes);
        }
    }

    out += "END:VCALENDAR\r\n";
    return out;
}

// ───────────────────────────────────────────────────────────────────────────
//  appendFixedEvent  —  one VEVENT for a fixed-time event
// ───────────────────────────────────────────────────────────────────────────
void BellServer::appendFixedEvent(String &out,
                                   const Event &ev,
                                   uint16_t curYear, uint8_t curMonth,
                                   uint8_t curDay,  uint8_t curDOW,
                                   const char *dtStamp,
                                   int advanceMinutes)
{
    // DTSTART — next matching weekday from today
    String dtStart = nextOccurrence(ev.weekdayMask, ev.hour, ev.minute,
                                    curYear, curMonth, curDay, curDOW);

    // UID — stable, unique per event
    char uid[52];
    snprintf(uid, sizeof(uid), "smartbell-ev%u@smartbell.local", ev.id);

    // SUMMARY — 🔔 + event name (UTF-8 bell emoji = \xF0\x9F\x94\x94)
    char summary[40];
    snprintf(summary, sizeof(summary),
             "\xF0\x9F\x94\x94 %s", ev.name);

    // DESCRIPTION
    char description[48];
    snprintf(description, sizeof(description),
             "Bell at %02u:%02u", ev.hour, ev.minute);

    out += "BEGIN:VEVENT\r\n";
    out += "UID:";         out += uid;         out += "\r\n";
    out += "DTSTAMP:";     out += dtStamp;     out += "\r\n";
    out += "DTSTART;TZID=Asia/Kolkata:";
    out += dtStart;
    out += "\r\n";
    out += "SUMMARY:";     out += summary;     out += "\r\n";
    out += "DESCRIPTION:"; out += description; out += "\r\n";
    out += "RRULE:FREQ=WEEKLY;BYDAY=";
    out += maskToBYDAY(ev.weekdayMask);
    out += "\r\n";

    // EXDATE — one entry per holiday at this event's time
    String exdates = buildExDates(ev.hour, ev.minute);
    if (exdates.length() > 0) out += exdates;

    // VALARM — advance notification
    if (advanceMinutes > 0) {
        char trigger[12];
        snprintf(trigger, sizeof(trigger), "-PT%dM", advanceMinutes);

        char alarmDesc[48];
        snprintf(alarmDesc, sizeof(alarmDesc),
                 "Bell in %d minute%s",
                 advanceMinutes, advanceMinutes == 1 ? "" : "s");

        out += "BEGIN:VALARM\r\n";
        out += "ACTION:DISPLAY\r\n";
        out += "TRIGGER:";     out += trigger;   out += "\r\n";
        out += "DESCRIPTION:"; out += alarmDesc; out += "\r\n";
        out += "END:VALARM\r\n";
    }

    out += "END:VEVENT\r\n";
    out += "\r\n";
}

// ───────────────────────────────────────────────────────────────────────────
//  appendIntervalEvents  —  expands one interval event into multiple VEVENTs
//  e.g. every 45 min from 09:00 → 17:00 generates one VEVENT per slot
// ───────────────────────────────────────────────────────────────────────────
void BellServer::appendIntervalEvents(String &out,
                                       const Event &ev,
                                       uint16_t curYear, uint8_t curMonth,
                                       uint8_t curDay,  uint8_t curDOW,
                                       const char *dtStamp,
                                       int advanceMinutes)
{
    if (ev.intervalMinutes == 0) return;    // guard: infinite loop risk

    uint16_t startTotal = (uint16_t)ev.hour * 60 + ev.minute;
    uint16_t endTotal   = (ev.endMinute > startTotal) ? ev.endMinute
                                                       : startTotal;

    uint16_t slot      = startTotal;
    uint16_t slotIndex = 0;

    while (slot <= endTotal && slotIndex < 100) {   // 100-slot safety cap

        uint8_t slotHour   = slot / 60;
        uint8_t slotMinute = slot % 60;

        if (slotHour > 23) break;   // overflow guard

        char uid[60];
        snprintf(uid, sizeof(uid),
                 "smartbell-ev%u-s%u@smartbell.local",
                 ev.id, slotIndex);

        char summary[40];
        snprintf(summary, sizeof(summary),
                 "\xF0\x9F\x94\x94 %s", ev.name);

        // Count total slots for description
        uint16_t totalSlots = (endTotal - startTotal) / ev.intervalMinutes + 1;

        char description[64];
        snprintf(description, sizeof(description),
                 "Bell at %02u:%02u (repeat %u/%u)",
                 slotHour, slotMinute,
                 slotIndex + 1, totalSlots);

        String dtStart = nextOccurrence(ev.weekdayMask,
                                        slotHour, slotMinute,
                                        curYear, curMonth,
                                        curDay,  curDOW);

        out += "BEGIN:VEVENT\r\n";
        out += "UID:";         out += uid;         out += "\r\n";
        out += "DTSTAMP:";     out += dtStamp;     out += "\r\n";
        out += "DTSTART;TZID=Asia/Kolkata:";
        out += dtStart;
        out += "\r\n";
        out += "SUMMARY:";     out += summary;     out += "\r\n";
        out += "DESCRIPTION:"; out += description; out += "\r\n";
        out += "RRULE:FREQ=WEEKLY;BYDAY=";
        out += maskToBYDAY(ev.weekdayMask);
        out += "\r\n";

        // EXDATE for this slot's time
        String exdates = buildExDates(slotHour, slotMinute);
        if (exdates.length() > 0) out += exdates;

        // VALARM
        if (advanceMinutes > 0) {
            char trigger[12];
            snprintf(trigger, sizeof(trigger), "-PT%dM", advanceMinutes);

            char alarmDesc[48];
            snprintf(alarmDesc, sizeof(alarmDesc),
                     "Bell in %d minute%s",
                     advanceMinutes, advanceMinutes == 1 ? "" : "s");

            out += "BEGIN:VALARM\r\n";
            out += "ACTION:DISPLAY\r\n";
            out += "TRIGGER:";     out += trigger;   out += "\r\n";
            out += "DESCRIPTION:"; out += alarmDesc; out += "\r\n";
            out += "END:VALARM\r\n";
        }

        out += "END:VEVENT\r\n";
        out += "\r\n";

        slot += ev.intervalMinutes;
        slotIndex++;
    }
}
// ───────────────────────────────────────────────────────────────────────────
//  appendOneTimeEvent  —  single VEVENT, no RRULE, specific calendar date
// ───────────────────────────────────────────────────────────────────────────
void BellServer::appendOneTimeEvent(String &out,
                                     const Event &ev,
                                     uint16_t curYear, uint8_t curMonth,
                                     uint8_t  curDay,
                                     const char *dtStamp,
                                     int advanceMinutes)
{
    uint16_t evYear;
    uint8_t  evMonth, evDay;
    ev.getOneTimeDate(evYear, evMonth, evDay);

    // Skip if already in the past
    if (evYear < curYear) return;
    if (evYear == curYear && evMonth < curMonth) return;
    if (evYear == curYear && evMonth == curMonth && evDay < curDay) return;

    char uid[52];
    snprintf(uid, sizeof(uid),
             "smartbell-ot%u@smartbell.local", ev.id);

    char summary[40];
    snprintf(summary, sizeof(summary),
             "\xF0\x9F\x94\x94 %s", ev.name);

    char description[64];
    snprintf(description, sizeof(description),
             "One-time bell at %02u:%02u on %04u-%02u-%02u",
             ev.hour, ev.minute, evYear, evMonth, evDay);

    char dtStart[16];
    snprintf(dtStart, sizeof(dtStart),
             "%04u%02u%02uT%02u%02u00",
             evYear, evMonth, evDay, ev.hour, ev.minute);

    out += "BEGIN:VEVENT\r\n";
    out += "UID:";         out += uid;         out += "\r\n";
    out += "DTSTAMP:";     out += dtStamp;     out += "\r\n";
    out += "DTSTART;TZID=Asia/Kolkata:";
    out += dtStart;        out += "\r\n";
    out += "SUMMARY:";     out += summary;     out += "\r\n";
    out += "DESCRIPTION:"; out += description; out += "\r\n";
    // Intentionally no RRULE — fires exactly once

    if (advanceMinutes > 0) {
        char trigger[12];
        snprintf(trigger, sizeof(trigger), "-PT%dM", advanceMinutes);

        char alarmDesc[48];
        snprintf(alarmDesc, sizeof(alarmDesc),
                 "Bell in %d minute%s",
                 advanceMinutes, advanceMinutes == 1 ? "" : "s");

        out += "BEGIN:VALARM\r\n";
        out += "ACTION:DISPLAY\r\n";
        out += "TRIGGER:";     out += trigger;   out += "\r\n";
        out += "DESCRIPTION:"; out += alarmDesc; out += "\r\n";
        out += "END:VALARM\r\n";
    }

    out += "END:VEVENT\r\n";
    out += "\r\n";
}
// ───────────────────────────────────────────────────────────────────────────
//  maskToBYDAY  —  converts weekdayMask to ICS BYDAY string
//  bit 0=Sun, 1=Mon, 2=Tue, 3=Wed, 4=Thu, 5=Fri, 6=Sat
//  e.g. 0x3E (Mon–Fri) → "MO,TU,WE,TH,FR"
// ───────────────────────────────────────────────────────────────────────────
String BellServer::maskToBYDAY(uint8_t mask) {
    String result;
    mask &= 0x7F;   // strip one-time flag before conversion
    for (int bit = 0; bit < 7; bit++) {
        if (mask & (1 << bit)) {
            if (result.length() > 0) result += ",";
            result += ICS_WDAY[bit];
        }
    }
    return result;
}

// ───────────────────────────────────────────────────────────────────────────
//  buildExDates  —  generates EXDATE line from holidays.txt
//  Format stored in LittleFS: one "YYYY-MM-DD,Name" per line
//  Output:  EXDATE;TZID=Asia/Kolkata:20251225T080000,20260101T080000\r\n
//  Returns empty string if no holidays exist.
// ───────────────────────────────────────────────────────────────────────────
String BellServer::buildExDates(uint8_t hour, uint8_t minute) {

    // Use StorageManager's JSON output and parse dates from it
    // loadHolidaysJSON() returns:  [{"date":"2025-12-25","name":"Christmas"},...]
    String json = storagePtr->loadHolidaysJSON();

    if (json.length() < 3) return "";   // empty array "[]"

    String dates;
    dates.reserve(128);

    int searchFrom = 0;

    while (true) {
        int dateIdx = json.indexOf("\"date\":\"", searchFrom);
        if (dateIdx < 0) break;

        int start = dateIdx + 8;            // skip past  "date":"
        int end   = json.indexOf('"', start);
        if (end < 0) break;

        String dateStr = json.substring(start, end);   // "YYYY-MM-DD"
        searchFrom = end + 1;

        if (dateStr.length() != 10) continue;
        if (dateStr[4] != '-' || dateStr[7] != '-') continue;

        // Build ICS datetime:  YYYYMMDDTHHMMSS
        char buf[16];
        snprintf(buf, sizeof(buf),
                 "%c%c%c%c%c%c%c%cT%02u%02u00",
                 dateStr[0], dateStr[1], dateStr[2], dateStr[3],   // YYYY
                 dateStr[5], dateStr[6],                            // MM
                 dateStr[8], dateStr[9],                            // DD
                 hour, minute);

        if (dates.length() > 0) dates += ",";
        dates += buf;
    }

    if (dates.length() == 0) return "";

    String line = "EXDATE;TZID=Asia/Kolkata:";
    line += dates;
    line += "\r\n";
    return line;
}

// ───────────────────────────────────────────────────────────────────────────
//  nextOccurrence  —  finds the next calendar date matching weekdayMask
//  Searches forward up to 7 days from today.
//  Returns ICS local datetime string:  "20250901T083000"
//  curDOW: 0=Sunday … 6=Saturday  (matches RTClib dayOfTheWeek)
// ───────────────────────────────────────────────────────────────────────────
String BellServer::nextOccurrence(uint8_t weekdayMask,
                                   uint8_t evHour, uint8_t evMinute,
                                   uint16_t curYear, uint8_t curMonth,
                                   uint8_t curDay,   uint8_t curDOW)
{
    uint16_t y = curYear;
    uint8_t  m = curMonth;
    uint8_t  d = curDay;
    uint8_t  dow = curDOW;   // 0=Sun

    for (int offset = 0; offset < 7; offset++) {

        if (weekdayMask & (1 << dow)) {
            // Found a matching weekday — format as ICS datetime
            char buf[16];
            snprintf(buf, sizeof(buf),
                     "%04u%02u%02uT%02u%02u00",
                     y, m, d, evHour, evMinute);
            return String(buf);
        }

        // Advance one day
        d++;
        dow = (dow + 1) % 7;

        if (d > daysInMonth(m, y)) {
            d = 1;
            m++;
            if (m > 12) {
                m = 1;
                y++;
            }
        }
    }

    // Fallback — should never reach here when mask != 0
    char buf[16];
    snprintf(buf, sizeof(buf),
             "%04u%02u%02uT%02u%02u00",
             curYear, curMonth, curDay, evHour, evMinute);
    return String(buf);
}