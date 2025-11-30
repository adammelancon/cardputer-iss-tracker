#pragma once
#include <M5GFX.h>
#include <WiFi.h>
#include <TinyGPS++.h>

void drawHomeScreen(M5Canvas &d);
void drawLiveScreen(M5Canvas &d, int year, int mon, int day, int hr, int min);
void drawRadarScreen(M5Canvas &d, unsigned long currentUnix);
void drawPassScreen(M5Canvas &d, unsigned long currentUnix, int minEl, double lat, double lon);

void drawMainMenu(M5Canvas &d);
void drawWifiMenu(M5Canvas &d, String storedSsid);
void drawSatMenu(M5Canvas &d, int minEl);
void drawLocationMenu(M5Canvas &d, double lat, double lon, bool useGps, bool gpsFix, int sats);
void drawGpsInfoScreen(M5Canvas &d, TinyGPSPlus &gps);