#include "AudioManager.h"

// ============================================================
//  Singleton
// ============================================================
AudioManager& AudioManager::instance() {
    static AudioManager inst;
    return inst;
}

AudioManager::AudioManager()
    : _mp3(nullptr)
    , _dfReady(false)
    , _dfPlaying(false)
    , _playFinishedFlag(false)
    , _sdReinserted(false)        // ← NEW
    , _playStartedMs(0)
    , _liveActive(false)
    , _liveTaskHandle(NULL)
{
    memset(_micBuf, 0, sizeof(_micBuf));
    memset(_dacBuf, 0, sizeof(_dacBuf));
}

// ============================================================
//  Initialisation
// ============================================================
void AudioManager::begin() {
    pinMode(RELAY_PIN, OUTPUT);
    setRelay(false);

    pinMode(AMP_POWER_PIN, OUTPUT);
    _ampOff();

    Serial.println("[Audio] Waiting for DFPlayer boot (2 s)...");
    delay(2000);

    Serial2.begin(9600, SERIAL_8N1, DFPLAYER_RX, DFPLAYER_TX);
    _mp3 = new DfMp3(Serial2);
    _mp3->begin();
    delay(300);

    _mp3->reset();
    Serial.println("[Audio] DFPlayer reset sent — waiting 2 s...");
    delay(2000);

    _mp3->setVolume(DFPLAYER_VOLUME);
    delay(100);

    uint16_t totalFiles = _mp3->getTotalTrackCount(DfMp3_PlaySource_Sd);
    Serial.printf("[Audio] DFPlayer ready — %u files on SD\n", totalFiles);
    _dfReady = (totalFiles > 0);

    if (!_dfReady) {
        Serial.println("[Audio] WARNING: DFPlayer reported 0 files — check SD card!");
    }

    Serial.println("[Audio] AudioManager initialised.");
}

// ============================================================
//  Main loop
// ============================================================
void AudioManager::loop() {
    if (_mp3 && !_liveActive) {
        static uint32_t lastPoll = 0;
        if (millis() - lastPoll >= 200) {
            lastPoll = millis();
            _mp3->loop();        // ← callback may set _sdReinserted here
        }
    }

    // ── Auto-recover on SD card reinsertion ──────────────────────────
    // Only runs when flag is set AND live mode is not active.
    // If live is active, flag is kept set and reset happens after live ends.
    if (_sdReinserted && !_liveActive) {
        _sdReinserted = false;
        Serial.println("[Audio] SD reinsertion detected — resetting DFPlayer...");
        delay(500);              // let SD card fully settle
        resetDfPlayer();
    }
}

// ============================================================
//  SD card insertion / removal callbacks
// ============================================================
void AudioManager::onSdRemoved() {
    Serial.println("[Audio] SD removed — marking not ready");
    _dfReady   = false;
    _dfPlaying = false;
    _ampOff();
}

void AudioManager::onSdInserted() {
    Serial.println("[Audio] SD inserted — reset scheduled");
    _sdReinserted = true;        // handled safely in loop(), not here
}
// ============================================================
//  Amp power control
// ============================================================
void AudioManager::_ampOn() {
    digitalWrite(AMP_POWER_PIN, HIGH);
    delay(AMP_WARMUP_MS);
    Serial.println("[Audio] Amp ON");
}

void AudioManager::_ampOff() {
    digitalWrite(AMP_POWER_PIN, LOW);
    Serial.println("[Audio] Amp OFF");
}

void AudioManager::resetDfPlayer() {
    Serial.println("[Audio] DFPlayer soft reset...");
    _dfPlaying = false;
    _dfReady   = false;

    if (_mp3) {
        _mp3->reset();
        delay(2000);
        _mp3->setVolume(DFPLAYER_VOLUME);
        delay(100);
        uint16_t total = _mp3->getTotalTrackCount(DfMp3_PlaySource_Sd);
        _dfReady = (total > 0);
        Serial.printf("[Audio] DFPlayer reset done — %d files\n", total);
    }
}

// ============================================================
//  DFPlayer playback
// ============================================================
void AudioManager::playTrack(uint16_t track) {
    if (!_dfReady || !_mp3) {
        Serial.println("[Audio] DFPlayer not ready — ignoring playTrack()");
        return;
    }
    if (_liveActive) {
        Serial.println("[Audio] Live active — ignoring playTrack()");
        return;
    }

    setRelay(false);
    _ampOn();
    Serial.printf("[Audio] Playing /mp3/%04u.mp3\n", track);
    _mp3->playMp3FolderTrack(track);
    _dfPlaying = true;
    _playFinishedFlag = false;    // ← NEW: clear stale flag on new play
    _playStartedMs = millis();
}

void AudioManager::playFolderTrack(uint8_t folder, uint8_t track) {
    if (!_dfReady) {
        Serial.println("[Audio] DFPlayer not ready");
        return;
    }
    _ampOn();
    _mp3->playFolderTrack(folder, track);
    _dfPlaying = true;
    _playFinishedFlag = false;    // ← NEW: clear stale flag on new play
    _playStartedMs = millis();
    Serial.printf("[Audio] Playing folder %d track %d\n", folder, track);
}

uint8_t AudioManager::getVolume() const {
    return DFPLAYER_VOLUME;
}

void AudioManager::playManualBell(uint16_t track) {
    playTrack(track);
}

void AudioManager::stopDfPlayer() {
    if (!_mp3) return;
    _mp3->stop();
    _dfPlaying = false;
    _playFinishedFlag = false;    // ← NEW: manual stop clears flag too
    _ampOff();
    Serial.println("[Audio] DFPlayer stopped.");
}

void AudioManager::setVolume(uint8_t vol) {
    if (!_mp3) return;
    if (vol > 30) vol = 30;
    _mp3->setVolume(vol);
    Serial.printf("[Audio] Volume set to %u\n", vol);
}

bool AudioManager::isDfPlayerPlaying() const {
    return _dfPlaying;
}

bool AudioManager::isDfPlayerReady() const {
    return _dfReady;
}

// ============================================================
//  Play-finished flag — consumed by main loop              // ← NEW
// ============================================================
bool AudioManager::consumePlayFinished() {
    if (_playFinishedFlag) {
        // Guard: ignore finish signals within 2s of starting
        // This blocks stale UART callbacks from previous track  // ← NEW
        if (millis() - _playStartedMs < 2000) {                 // ← NEW
            _playFinishedFlag = false;                           // ← NEW
            Serial.println("[Audio] Ignoring early finish signal (stale UART)"); // ← NEW
            return false;                                        // ← NEW
        }                                                        // ← NEW
        _playFinishedFlag = false;
        return true;
    }
    return false;
}

// ============================================================
//  Callbacks
// ============================================================
void AudioManager::onPlayFinished(uint16_t track) {
    _dfPlaying = false;
    _playFinishedFlag = true;    // ← CHANGED: set flag instead of doing nothing
    _ampOff();
    Serial.printf("[Audio] Track %u finished.\n", track);
}

void AudioManager::onError(uint16_t errorCode) {
    Serial.printf("[Audio] DFPlayer error %u\n", errorCode);
    _dfPlaying = false;
    _playFinishedFlag = true;    // ← NEW: errors also trigger auto-return
    _ampOff();
}

// ============================================================
//  Relay control
// ============================================================
void AudioManager::setRelay(bool on) {
    digitalWrite(RELAY_PIN, on ? HIGH : LOW);
    Serial.printf("[Audio] Relay %s (%s path)\n",
                  on ? "ON" : "OFF",
                  on ? "PCM5100A / Live" : "DFPlayer");
}

// ============================================================
//  Live announcement
// ============================================================
void AudioManager::startLive() {
    if (_liveActive) {
        Serial.println("[Audio] Already live.");
        return;
    }

    if (_dfPlaying) stopDfPlayer();

    Serial.println("[Audio] Starting live announcement...");

    _initPcmI2S();
    _ampOn();
    delay(50);
    setRelay(true);
    _initMicI2S();

    int32_t flushBuf[BUF_LEN];
    for (int i = 0; i < 20; i++) {
        size_t br = 0;
        i2s_read(I2S_NUM_1, flushBuf, BUF_LEN * sizeof(int32_t),
                 &br, pdMS_TO_TICKS(50));
    }

    _liveActive = true;

    xTaskCreatePinnedToCore(
        _liveTaskFunc,
        "liveAudio",
        4096,
        this,
        5,
        &_liveTaskHandle,
        1
    );

    Serial.println("[Audio] LIVE — mic → PCM5100A task started.");
}

void AudioManager::stopLive() {
    if (!_liveActive) return;

    Serial.println("[Audio] Stopping live announcement...");

    _liveActive = false;

    if (_liveTaskHandle) {
        vTaskDelay(pdMS_TO_TICKS(200));
        _liveTaskHandle = NULL;
    }

    _deinitMicI2S();
    _deinitPcmI2S();
    setRelay(false);
    _ampOff();

    Serial.println("[Audio] Live stopped.");
}

bool AudioManager::isLive() const {
    return _liveActive;
}

// ============================================================
//  FreeRTOS task
// ============================================================
void AudioManager::_liveTaskFunc(void* param) {
    AudioManager* self = (AudioManager*)param;
    Serial.println("[Audio] Live task running.");

    while (self->_liveActive) {
        self->_livePassthroughLoop();
    }

    Serial.println("[Audio] Live task exiting.");
    vTaskDelete(NULL);
}

// ============================================================
//  I2S Mic (I2S_NUM_1 — RX)
// ============================================================
void AudioManager::_initMicI2S() {
    i2s_config_t cfg = {};
    cfg.mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
    cfg.sample_rate          = MIC_SAMPLE_RATE;
    cfg.bits_per_sample      = (i2s_bits_per_sample_t)MIC_SAMPLE_BITS;
    cfg.channel_format       = MIC_CHANNEL_FORMAT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count        = 8;
    cfg.dma_buf_len          = BUF_LEN;
    cfg.use_apll             = false;
    cfg.tx_desc_auto_clear   = false;
    cfg.fixed_mclk           = 0;

    esp_err_t err = i2s_driver_install(I2S_NUM_1, &cfg, 0, NULL);
    if (err != ESP_OK) {
        Serial.printf("[Audio] Mic I2S install FAILED: %s\n", esp_err_to_name(err));
        return;
    }

    i2s_pin_config_t pins = {};
    pins.bck_io_num   = I2S_MIC_BCLK;
    pins.ws_io_num    = I2S_MIC_WS;
    pins.data_in_num  = I2S_MIC_DOUT;
    pins.data_out_num = I2S_PIN_NO_CHANGE;

    err = i2s_set_pin(I2S_NUM_1, &pins);
    if (err != ESP_OK) {
        Serial.printf("[Audio] Mic I2S set pin FAILED: %s\n", esp_err_to_name(err));
    }

    i2s_zero_dma_buffer(I2S_NUM_1);
    Serial.println("[Audio] I2S mic initialised on I2S_NUM_1.");
}

void AudioManager::_deinitMicI2S() {
    i2s_driver_uninstall(I2S_NUM_1);
    Serial.println("[Audio] I2S mic driver uninstalled.");
}

// ============================================================
//  PCM5100A DAC (I2S_NUM_0 — TX)
// ============================================================
void AudioManager::_initPcmI2S() {
    i2s_config_t cfg = {};
    cfg.mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    cfg.sample_rate          = PCM_SAMPLE_RATE;
    cfg.bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count        = 8;
    cfg.dma_buf_len          = BUF_LEN;
    cfg.use_apll             = false;
    cfg.tx_desc_auto_clear   = true;
    cfg.fixed_mclk           = 0;

    esp_err_t err = i2s_driver_install(I2S_NUM_0, &cfg, 0, NULL);
    if (err != ESP_OK) {
        Serial.printf("[Audio] PCM I2S install FAILED: %s\n", esp_err_to_name(err));
        return;
    }

    i2s_pin_config_t pins = {};
    pins.bck_io_num   = PCM_BCK;
    pins.ws_io_num    = PCM_WS;
    pins.data_out_num = PCM_DIN;
    pins.data_in_num  = I2S_PIN_NO_CHANGE;

    err = i2s_set_pin(I2S_NUM_0, &pins);
    if (err != ESP_OK) {
        Serial.printf("[Audio] PCM I2S set pin FAILED: %s\n", esp_err_to_name(err));
    }

    i2s_zero_dma_buffer(I2S_NUM_0);
    Serial.println("[Audio] PCM5100A initialised on I2S_NUM_0.");
}

void AudioManager::_deinitPcmI2S() {
    i2s_driver_uninstall(I2S_NUM_0);
    Serial.println("[Audio] PCM5100A driver uninstalled.");
}

// ============================================================
//  Live passthrough
// ============================================================
void AudioManager::_livePassthroughLoop() {
    size_t bytesRead = 0;

    esp_err_t err = i2s_read(I2S_NUM_1,
                             _micBuf,
                             BUF_LEN * sizeof(int32_t),
                             &bytesRead,
                             pdMS_TO_TICKS(100));

    if (err != ESP_OK || bytesRead == 0) return;

    size_t samples = bytesRead / sizeof(int32_t);

    for (size_t i = 0; i < samples; i++) {
        int32_t raw = _micBuf[i] >> 14;
        if (raw >  32767) raw =  32767;
        if (raw < -32768) raw = -32768;
        int16_t sample = (int16_t)raw;
        _dacBuf[i * 2]     = sample;
        _dacBuf[i * 2 + 1] = sample;
    }

    size_t bw = 0;
    i2s_write(I2S_NUM_0, _dacBuf, samples * 2 * sizeof(int16_t),
              &bw, pdMS_TO_TICKS(100));
}