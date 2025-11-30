#pragma once
#include <Arduino.h>
#include <M5Cardputer.h>
#include <M5GFX.h>

// App Version
#define APP_VERSION "v1.6.00"

// ---------- Colors ----------
#define COL_BG        0x0000  // Black
#define COL_ACCENT    0x0018  // Deep NASA blue
#define COL_TEXT      0xDFFF  // Ice-blue white
#define COL_HEADER    0xFDB4  // NASA gold/orange
#define COL_SAT_PATH  0x07E0  // Green for radar path
#define COL_SAT_NOW   0xF800  // Red for current pos

// ---------- GPS Module (CAP LoRa868) ----------
#define GPS_RX_PIN      15  // ESP32 RX (Receives from GPS TX)
#define GPS_TX_PIN      13  // ESP32 TX (Sends to GPS RX)
#define GPS_BAUD        115200

// Shared Globals
extern bool useGpsModule; // New config flag

// ---------- Geometry ----------
#define FRAME_MARGIN  5
#define TEXT_LEFT     22
#define TEXT_TOP      10
#define LINE_SPACING  18 // Height of Font2 (16) + 2px gap

// ---------- Pins (Cardputer ADV) ----------
#define SD_SPI_SCK_PIN  40
#define SD_SPI_MISO_PIN 39
#define SD_SPI_MOSI_PIN 14
#define SD_SPI_CS_PIN   12

// ----------LEDs------------
#define LED_PIN         21  // M5StampS3 RGB LED
#define LED_COUNT       1
#define LED_BRIGHTNESS  200  // Keep it low (0-255) to save power/eyes

// ---------- Settings ----------
#define ISS_TLE_PATH "/apps/iss_tracker/iss.tle"
#define OBS_ALT_M    15.0
#define DEFAULT_MIN_EL 10  // Default to 10 degree passes

// Shared Globals (defined in main.cpp)
extern double obsLatDeg;
extern double obsLonDeg;
extern int tzOffsetHours;
extern int minElevation;   // New Global
extern bool tleParsedOK;
extern String satName;