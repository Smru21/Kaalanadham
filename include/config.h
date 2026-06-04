#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

#define COLORS_FILE "/colors.txt"
#define CUSTOM_THEME_INDEX   7
#define MAX_BG_THEMES        8
#define DEFAULT_BG_THEME    0

#define SCREEN_TIMEOUT_MS    60000   // 1 minute inactivity → return home

// Date display on home screen
#define DATE_Y              80      // Y center below clock (MC_DATUM)
#define DATE_TEXT_SIZE       1      // 12×16 per char — legible but compact

// TFT ILI9341 (SPI)
#define TFT_MISO_PIN    12
#define TFT_MOSI_PIN    13
#define TFT_SCLK_PIN    14
#define TFT_CS_PIN      15
#define TFT_DC_PIN       2
#define TFT_BL_PIN      -1      // Backlight wired directly to 3.3V

// DS1307 RTC (I2C)
#define RTC_SDA         32
#define RTC_SCL         33

// Amplifier Power Control
#define AMP_POWER_PIN        5
#define AMP_WARMUP_MS       100

// DFPlayer Mini (UART2)
#define DFPLAYER_TX     25
#define DFPLAYER_RX     35

// Buttons (Active LOW with INPUT_PULLUP)
#define BTN_PLAY_PIN    16
#define BTN_NEXT_PIN    17
#define BTN_MENU_PIN    18
#define BTN_OK_PIN      19

// I2S MEMS Microphone (I2S_NUM_1 RX)
#define I2S_MIC_BCLK    22
#define I2S_MIC_WS      23
#define I2S_MIC_DOUT    34

// PCM5100A I2S DAC (I2S_NUM_0 TX)
#define PCM_BCK         26
#define PCM_WS          27
#define PCM_DIN         21

// Relay
#define RELAY_PIN        4

// System Constants
#define MAX_EVENTS              100
#define MAX_HOLIDAYS            50
#define MAX_HOLIDAY_NAME_LEN    20
#define SCHEDULER_CHECK_MS    1000
#define DISPLAY_REFRESH_MS   30000
#define DEBOUNCE_MS             50
#define DEFAULT_PASSWORD    "1234"
#define MAX_PASSWORD_LEN        8
#define MIN_PASSWORD_LEN        4
#define MAX_TEMPLATES           10
#define MAX_TEMPLATE_NAME_LEN   20

// School Name
#define DEFAULT_SCHOOL_NAME    "ABC SCHOOL"
#define MAX_SCHOOL_NAME_LEN    20

// Audio Constants
#define DFPLAYER_VOLUME         30
#define MANUAL_BELL_TRACK        101
#define RINGTONE_START_TRACK     101
#define RINGTONE_COUNT           10
#define MAX_MUSIC_TRACKS         100

// I2S Configuration
#define MIC_SAMPLE_RATE     16000
#define MIC_SAMPLE_BITS        32
#define MIC_CHANNEL_FORMAT  I2S_CHANNEL_FMT_ONLY_RIGHT

// PCM5100A DAC Configuration
#define PCM_SAMPLE_RATE     16000

// WiFi Access Point
#define WIFI_AP_SSID       "KĀLANĀDAM"
#define WIFI_AP_PASSWORD   "1234"
#define WIFI_AP_CHANNEL    1
#define WIFI_AP_MAX_CONN   4
#define WIFI_AP_TX_POWER   WIFI_POWER_2dBm
#define WEB_SERVER_PORT    80

// mDNS
#define MDNS_HOSTNAME     "kaalanadham"
// Access via: http://kaalanadham.local

// System Log
#define MAX_LOG_ENTRIES         50
#define MAX_LOG_MSG_LEN        80

// Log categories
#define LOG_CAT_BELL       "BELL"
#define LOG_CAT_SCHEDULE   "SCHED"
#define LOG_CAT_SYSTEM     "SYS"
#define LOG_CAT_AUDIO      "AUDIO"
#define LOG_CAT_ERROR      "ERROR"
#define LOG_CAT_SETTINGS   "SET"

// WiFi Station Mode
#define WIFI_STA_CONNECT_TIMEOUT  10000    // 10s connection attempt
#define WIFI_STA_MAX_SSID_LEN    32
#define WIFI_STA_MAX_PASS_LEN    64
#define WIFI_CRED_FILE           "/wifi.txt"
#define WIFI_MODE_FILE           "/wifimode.txt"

// NTP Configuration
#define NTP_SERVER_1        "pool.ntp.org"
#define NTP_SERVER_2        "time.nist.gov"
#define NTP_SYNC_INTERVAL   21600000       // 6 hours in ms
#define NTP_GMT_OFFSET      19800          // IST = UTC+5:30 (change for your timezone)
#define NTP_DAYLIGHT_OFFSET 0
#define NTP_SYNC_TIMEOUT    10000          // 10s timeout for NTP fetch

// WiFi Log category
#define LOG_CAT_WIFI       "WIFI"

// ── WiFi Auto-Reconnect ──
#define WIFI_RECONNECT_CHECK_INTERVAL   5000     // Check connection every 5s
#define WIFI_RECONNECT_COOLDOWN         5000     // Wait 5s after detecting disconnect
#define WIFI_RECONNECT_BACKOFF_INITIAL  30000    // First retry backoff: 30s
#define WIFI_RECONNECT_BACKOFF_MAX      300000   // Max backoff: 5 minutes
#define WIFI_RECONNECT_BACKOFF_MULT     2        // Backoff multiplier
#define WIFI_RECONNECT_SCAN_TIMEOUT     10000    // Abort scan after 10s
#define WIFI_RECONNECT_CONNECT_TIMEOUT  15000    // Per-attempt connect timeout
#define WIFI_TX_POWER_BOOST             WIFI_POWER_19_5dBm  // Boost during reconnect
// Static IP configuration
#define WIFI_STATIC_IP_FILE  "/wifi_static.txt"

// Setup wizard
#define SETUP_DONE_FILE      "/setup_done.txt"

// Environment Profile
#define PROFILE_FILE         "/profile.txt"
#define MAX_PROFILES         8
#define DEFAULT_PROFILE      0

// Bell Duration
#define MAX_BELL_DURATION       300     // 5 minutes max
#define DEFAULT_BELL_DURATION   0       // 0 = full track

// Event Types
#define EVENT_TYPE_FIXED    0
#define EVENT_TYPE_INTERVAL 1

// Interval limits
#define MIN_INTERVAL_MINUTES  5     // Minimum 5 minutes between repeats
#define MAX_INTERVAL_MINUTES  1440  // Maximum 24 hours



#endif