#pragma once
#include <Arduino.h>
#include <M5Cardputer.h>
#include <M5GFX.h>

// App Version
#define APP_VERSION "v1.4.21"

// ---------- Colors ----------
#define COL_BG        0x0000  // Black
#define COL_ACCENT    0x0018  // Deep NASA blue
#define COL_TEXT      0xDFFF  // Ice-blue white
#define COL_HEADER    0xFDB4  // NASA gold/orange
#define COL_SAT_PATH  0x07E0  // Green for radar path
#define COL_SAT_NOW   0xF800  // Red for current pos

// ---------- Geometry ----------
#define FRAME_MARGIN  5
#define TEXT_LEFT     22
#define TEXT_TOP      15
#define LINE_SPACING  12

// ---------- Pins (Cardputer ADV) ----------
#define SD_SPI_SCK_PIN  40
#define SD_SPI_MISO_PIN 39
#define SD_SPI_MOSI_PIN 14
#define SD_SPI_CS_PIN   12

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