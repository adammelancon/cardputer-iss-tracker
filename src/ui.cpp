#include "ui.h"
#include "config.h"
#include "orbit.h"
#include "iss_icon.h"

void drawFrame(M5Canvas &d, String title) {
    d.fillScreen(COL_BG);
    d.drawRect(FRAME_MARGIN, FRAME_MARGIN,
               d.width() - FRAME_MARGIN*2,
               d.height() - FRAME_MARGIN*2,
               COL_ACCENT);
    
    d.setTextColor(COL_HEADER);
    d.setCursor(TEXT_LEFT, TEXT_TOP);
    d.println(title);
    
    int lineEnd = d.width() * 0.75;
    d.drawLine(TEXT_LEFT, TEXT_TOP + 20, lineEnd, TEXT_TOP+20, COL_ACCENT);
    
    d.setTextColor(COL_TEXT);
}

void drawHomeScreen(M5Canvas &d) {
    drawFrame(d, "ISS Tracker " APP_VERSION);
    
    // UPDATED: Scooted icon left (was -40, now -55)
    d.pushImage(d.width() - 55, 20, 32, 32, ISS_ICON_32x32);

    int y = TEXT_TOP + 20;
    d.setCursor(TEXT_LEFT, y);
    d.println("Change Menu - G0 Btn");
    y += LINE_SPACING;

    // Inside drawHomeScreen(M5Canvas &d)

    const char* menu[] = {
        "LIVE     - Position Data",
        "RADAR   - Skyplot View",
        "PASS     - Next Prediction",
        "CONFIG  - Press 'c' key"   // <--- CHANGED THIS LINE
    };

    for (const char* item : menu) {
        d.setCursor(TEXT_LEFT, y);
        d.println(item);
        y += LINE_SPACING;
    }

    y += LINE_SPACING;
    if (isOrbitReady()) {
        d.setTextColor(COL_SAT_PATH);
        d.setCursor(TEXT_LEFT, y);
        d.printf("Tracking - %s", satName.c_str());
    } else {
        d.setTextColor(COL_SAT_NOW);
        d.setCursor(TEXT_LEFT, y);
        d.println("NO TLE DATA LOADED");
    }
}

void drawLiveScreen(M5Canvas &d, int year, int mon, int day, int hr, int min) {
    drawFrame(d, "Live Telemetry");
    int y = TEXT_TOP + 22;

    if (!isOrbitReady()) {
        d.setCursor(TEXT_LEFT, y); d.println("No Data."); return;
    }

    d.setCursor(TEXT_LEFT, y);
    d.printf("Local Time - %02d : %02d : %02d\n", hr, min, 0); 
    y += LINE_SPACING;
    d.setCursor(TEXT_LEFT, y);
    d.printf("Lat : %.2f  Lon : %.2f\n", sat.satLat, sat.satLon);
    y += LINE_SPACING;
    d.setCursor(TEXT_LEFT, y);
    d.printf("Alt : %.1f km\n", sat.satAlt);
    y += LINE_SPACING;
    d.setCursor(TEXT_LEFT, y);
    d.printf("Az : %.0f  El : %.0f\n", sat.satAz, sat.satEl);
    y += LINE_SPACING;
    
    d.setCursor(TEXT_LEFT, y);
    if (sat.satEl > 0) {
        d.setTextColor(COL_SAT_PATH);
        d.println("VISIBLE (Acqired)");
    } else {
        d.setTextColor(COL_ACCENT);
        d.println("BELOW HORIZON (Loss)");
    }
}

void drawRadarScreen(M5Canvas &d, unsigned long currentUnix) {
    d.fillScreen(COL_BG);
    
    int cx = d.width() / 2;
    int cy = d.height() / 2 + 5;
    int r  = 60;

    d.drawCircle(cx, cy, r, COL_ACCENT);      
    d.drawCircle(cx, cy, r*0.66, 0x2124);     
    d.drawCircle(cx, cy, r*0.33, 0x2124);     
    d.drawLine(cx-r, cy, cx+r, cy, 0x2124);   
    d.drawLine(cx, cy-r, cx, cy+r, 0x2124);   

    d.setTextColor(COL_HEADER);
    d.setCursor(cx - 3, cy - r - 10); d.print("N");
    d.setCursor(cx - 3, cy + r + 2);  d.print("S");
    d.setCursor(cx - r - 8, cy - 4);  d.print("W");
    d.setCursor(cx + r + 2, cy - 4);  d.print("E");

    if (!isOrbitReady()) return;

    unsigned long startT = currentUnix - (5 * 60);
    unsigned long endT   = currentUnix + (15 * 60);
    
    for (unsigned long t = startT; t < endT; t+=60) {
        sat.findsat(t);
        if (sat.satEl > 0) {
            float theta = (sat.satAz - 90) * DEG_TO_RAD;
            float rad = map(sat.satEl, 0, 90, r, 0);
            
            int px = cx + rad * cos(theta);
            int py = cy + rad * sin(theta);
            
            d.drawPixel(px, py, COL_SAT_PATH);
        }
    }

    sat.findsat(currentUnix);
    if (sat.satEl > 0) {
        float theta = (sat.satAz - 90) * DEG_TO_RAD;
        float rad = map(sat.satEl, 0, 90, r, 0);
        int px = cx + rad * cos(theta);
        int py = cy + rad * sin(theta);
        
        d.fillCircle(px, py, 4, COL_SAT_NOW);
        d.drawCircle(px, py, 5, COL_TEXT);
    } else {
        d.setCursor(5, d.height() - 15);
        d.setTextColor(COL_ACCENT);
        d.print("Sat below horizon");
    }
}

void drawPassScreen(M5Canvas &d, unsigned long currentUnix, int minEl, double lat, double lon) {
    drawFrame(d, "Pass Prediction");
    int y = TEXT_TOP + 25;

    if (!isOrbitReady()) {
        d.setCursor(TEXT_LEFT, y); d.println("No TLE."); return;
    }

    static PassDetails nextPass;
    static unsigned long lastCalc = 0;
    static int lastMinEl = -1;
    // ADD CACHING VARIABLES FOR LOCATION
    static double lastLat = -999;
    static double lastLon = -999;

    // UPDATE THE CHECK CONDITION:
    // We now recalc if: Time expired OR Pass passed OR MinEl changed OR Location changed
    if (currentUnix - lastCalc > 10000 || 
        nextPass.aosUnix < currentUnix || 
        lastMinEl != minEl || 
        lastLat != lat || 
        lastLon != lon) {
            
        d.setCursor(TEXT_LEFT, y);
        d.println("Calculating...");
        d.pushSprite(0,0);
        
        if (predictNextPass(currentUnix, nextPass, minEl)) {
            lastCalc = currentUnix;
            lastMinEl = minEl;
            lastLat = lat; // Update Cache
            lastLon = lon; // Update Cache
            drawFrame(d, "Pass Prediction");
        } else {
            drawFrame(d, "Pass Prediction");
            d.setCursor(TEXT_LEFT, y);
            d.printf("No pass > %d deg\n", minEl);
            y+= LINE_SPACING;
            d.setCursor(TEXT_LEFT, y);
            d.println("in next 24h.");
            return;
        }
    }

    // ... (The rest of the drawing code remains exactly the same) ...
    time_t rawAos = nextPass.aosUnix;
    struct tm * taos = localtime(&rawAos);
    
    d.setCursor(TEXT_LEFT, y);
    d.setTextColor(COL_HEADER);
    d.println("Next Acquisition of Signal");
    d.setTextColor(COL_TEXT);
    
    char timeBuf[30];
    strftime(timeBuf, 30, "%Y-%m-%d %H:%M", taos);
    y += LINE_SPACING;
    d.setCursor(TEXT_LEFT, y);
    d.println(timeBuf);

    y += LINE_SPACING;
    d.setCursor(TEXT_LEFT, y);
    d.printf("Duration - %.1f min\n", nextPass.durationMins);
    
    y += LINE_SPACING;
    d.setCursor(TEXT_LEFT, y);
    d.printf("Max Elev - %.0f deg\n", nextPass.maxElevation);
    
    y += LINE_SPACING;
    d.setCursor(TEXT_LEFT, y);
    d.setTextColor(COL_ACCENT);
    d.printf("Filter > %d deg", minEl);
}


// New Helper for consistent menu look
void drawMenu(M5Canvas &d, String title, const char* items[], int count) {
    drawFrame(d, title);
    int y = TEXT_TOP + 20;
    
    for (int i = 0; i < count; i++) {
        d.setCursor(TEXT_LEFT, y);
        d.println(items[i]);
        y += LINE_SPACING;
    }
    
    // Footer instruction
    d.setTextColor(COL_ACCENT);
    d.setCursor(TEXT_LEFT, d.height() - 22);
    d.print("Use Keypad #s");
    d.setTextColor(COL_TEXT);
}

// 1. The Main Configure Menu
void drawMainMenu(M5Canvas &d) {
    const char* items[] = {
        "1) WiFi Settings >",
        "2) Satellite / TLE >",
        "3) Location Setup >",
        "4) Timezone Setup"
    };
    drawMenu(d, "Configuration", items, 4);
}

// 2. The WiFi Sub-Menu
void drawWifiMenu(M5Canvas &d, String storedSsid) {
    const char* items[] = {
        "1) Scan Networks (Coming Soon)",
        "2) Manual Entry"
    };
    drawMenu(d, "WiFi Setup", items, 2);
    
    // Show current
    d.setCursor(TEXT_LEFT, TEXT_TOP + 70);
    d.setTextColor(COL_HEADER);
    d.print("Current: ");
    d.println(storedSsid);
}

// 3. The Satellite Sub-Menu
void drawSatMenu(M5Canvas &d, int minEl) {
    drawFrame(d, "Satellite Config");
    int y = TEXT_TOP + 20;
    
    d.setCursor(TEXT_LEFT, y); 
    d.printf("1) Min Elevation: %d deg\n", minEl);
    y += LINE_SPACING;
    
    d.setCursor(TEXT_LEFT, y); 
    d.println("2) Force TLE Update");
    y += LINE_SPACING;
    
    // Show Orbit info at bottom
    y += 10;
    d.setCursor(TEXT_LEFT, y);
    d.setTextColor(COL_ACCENT);
    d.printf("Inc: %.3f  Ecc: %.4f", tleIncDeg, tleEcc);
}

void drawLocationMenu(M5Canvas &d, double lat, double lon, bool useGps, bool gpsFix, int sats) {
    drawFrame(d, "Location Setup");
    int y = TEXT_TOP + 20;

    // Item 1: Source
    d.setCursor(TEXT_LEFT, y);
    d.setTextColor(useGps ? COL_SAT_PATH : COL_TEXT);
    d.printf("1) Source: [%s]\n", useGps ? "GPS Module" : "MANUAL");
    d.setTextColor(COL_TEXT);
    y += LINE_SPACING;

    // Item 2 & 3: Lat/Lon
    d.setCursor(TEXT_LEFT, y);
    if (useGps) d.setTextColor(COL_ACCENT); // Dim if GPS active
    d.printf("2) Set Lat: %.4f\n", lat);
    y += LINE_SPACING;
    
    d.setCursor(TEXT_LEFT, y);
    d.printf("3) Set Lon: %.4f\n", lon);
    d.setTextColor(COL_TEXT);
    y += LINE_SPACING;

    // Item 4: Details Link
    d.setCursor(TEXT_LEFT, y);
    if (!useGps) d.setTextColor(COL_ACCENT);
    d.println("4) GPS Status Info >");
    d.setTextColor(COL_TEXT);

    // Footer Status
    d.setCursor(TEXT_LEFT, d.height() - 25);
    if (useGps) {
        if (gpsFix) {
            d.setTextColor(COL_SAT_PATH); // Green
            d.printf("GPS Acquired (%d Sats)", sats);
        } else {
            d.setTextColor(COL_SAT_NOW); // Red/Orange
            d.print("GPS: Searching...");
        }
    } else {
        d.setTextColor(COL_ACCENT);
        d.print("Mode: Fixed Location");
    }
}

void drawGpsInfoScreen(M5Canvas &d, TinyGPSPlus &gps) {
    drawFrame(d, "GPS Details");
    int y = TEXT_TOP + 20;
    
    d.setCursor(TEXT_LEFT, y);
    d.printf("Sats: %d  HDOP: %.1f\n", gps.satellites.value(), gps.hdop.hdop());
    y += LINE_SPACING;

    d.setCursor(TEXT_LEFT, y);
    d.printf("Lat: %.5f\n", gps.location.lat());
    y += LINE_SPACING;

    d.setCursor(TEXT_LEFT, y);
    d.printf("Lon: %.5f\n", gps.location.lng());
    y += LINE_SPACING;

    d.setCursor(TEXT_LEFT, y);
    d.printf("Alt: %.1f m\n", gps.altitude.meters());
    y += LINE_SPACING;

    d.setCursor(TEXT_LEFT, y);
    d.printf("Time: %02d:%02d:%02d UTC\n", gps.time.hour(), gps.time.minute(), gps.time.second());
    
    y += LINE_SPACING;
    d.setCursor(TEXT_LEFT, y);
    if (gps.location.isValid()) {
        d.setTextColor(COL_SAT_PATH);
        d.print("STATUS: 3D FIX");
    } else {
        d.setTextColor(COL_SAT_NOW);
        d.print("STATUS: NO FIX");
    }
}
