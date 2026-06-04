#ifndef AUDIO_MANAGER_H
#define AUDIO_MANAGER_H

#include <Arduino.h>
#include <DFMiniMp3.h>
#include <driver/i2s.h>
#include "config.h"

// Forward declaration
class Mp3Notify;

// Typedef for readability
typedef DFMiniMp3<HardwareSerial, Mp3Notify> DfMp3;

class AudioManager {
public:
    // Singleton access
    static AudioManager& instance();

    void begin();
    void loop();

    // --- DFPlayer controls ---
    void playTrack(uint16_t track);
    void playFolderTrack(uint8_t folder, uint8_t track);
    void playManualBell(uint16_t track = RINGTONE_START_TRACK);
    void stopDfPlayer();
    void setVolume(uint8_t vol);
    uint8_t getVolume() const;
    bool isDfPlayerPlaying() const;
    bool isDfPlayerReady() const;

    void resetDfPlayer();       // ← already exists

    void onSdInserted();        // ← NEW: called by Mp3Notify on SD reinsertion
    void onSdRemoved();         // ← NEW: called by Mp3Notify on SD removal
    // --- Play-finished flag (checked by main loop) ---   // ← NEW
    bool consumePlayFinished();                            // ← NEW

    // --- Live announcement (I2S mic → PCM5100A DAC) ---
    void startLive();
    void stopLive();
    bool isLive() const;

    // --- Relay ---
    void setRelay(bool on);

    // --- Callbacks from Mp3Notify ---
    void onPlayFinished(uint16_t track);
    void onError(uint16_t errorCode);

private:
    AudioManager();
    AudioManager(const AudioManager&) = delete;
    AudioManager& operator=(const AudioManager&) = delete;

    // DFPlayer
    DfMp3*   _mp3;
    bool     _dfReady;
    bool     _dfPlaying;
    bool     _playFinishedFlag;    // ← set by onPlayFinished, read by main loop
    bool     _sdReinserted;        // ← set by callback, handled in loop()
    unsigned long _playStartedMs;  // ← was accidentally removed — restore this
    
    // Live audio
    volatile bool   _liveActive;
    TaskHandle_t    _liveTaskHandle;
    static void     _liveTaskFunc(void* param);
    void            _initMicI2S();
    void            _deinitMicI2S();
    void            _initPcmI2S();
    void            _deinitPcmI2S();
    void            _ampOn();
    void            _ampOff();
    void            _livePassthroughLoop();

    // Buffers
    static constexpr size_t BUF_LEN = 256;
    int32_t  _micBuf[BUF_LEN];
    int16_t  _dacBuf[BUF_LEN * 2];
};

// ---- DFPlayer notification handler ----
class Mp3Notify {
public:
    static void OnError(DfMp3&, uint16_t errorCode) {
        Serial.printf("[DFPlayer] Error: %u\n", errorCode);
        AudioManager::instance().onError(errorCode);
    }
    static void OnPlayFinished(DfMp3&, DfMp3_PlaySources, uint16_t track) {
        Serial.printf("[DFPlayer] Finished track: %u\n", track);
        AudioManager::instance().onPlayFinished(track);
    }
    static void OnPlaySourceOnline(DfMp3&, DfMp3_PlaySources) {}
    static void OnPlaySourceInserted(DfMp3&, DfMp3_PlaySources) {
        Serial.println("[DFPlayer] SD card inserted.");
        AudioManager::instance().onSdInserted();   // ← NEW
    }
    static void OnPlaySourceRemoved(DfMp3&, DfMp3_PlaySources) {
        Serial.println("[DFPlayer] SD card removed!");
        AudioManager::instance().onSdRemoved();    // ← NEW
    }
};

#endif