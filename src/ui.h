#pragma once
#include <M5GFX.h>

void drawHomeScreen(M5Canvas &d);
void drawLiveScreen(M5Canvas &d, int year, int mon, int day, int hr, int min);
void drawRadarScreen(M5Canvas &d, unsigned long currentUnix);
void drawPassScreen(M5Canvas &d, unsigned long currentUnix, int minEl);

// Updated to accept Timezone
void drawOptionsScreen(M5Canvas &d, int minEl, int tzOffset);

void drawWifiMenuScreen(M5Canvas &d, String ssid, String pass);
void drawLocationMenuScreen(M5Canvas &d, double lat, double lon);