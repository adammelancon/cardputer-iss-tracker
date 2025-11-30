#pragma once
#include <M5GFX.h>
#include <WiFi.h>

void drawHomeScreen(M5Canvas &d);
void drawLiveScreen(M5Canvas &d, int year, int mon, int day, int hr, int min);
void drawRadarScreen(M5Canvas &d, unsigned long currentUnix);
void drawPassScreen(M5Canvas &d, unsigned long currentUnix, int minEl);

void drawMainMenu(M5Canvas &d);
void drawWifiMenu(M5Canvas &d, String storedSsid);
void drawSatMenu(M5Canvas &d, int minEl);
void drawLocationMenu(M5Canvas &d, double lat, double lon);