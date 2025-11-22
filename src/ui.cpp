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
    d.drawLine(TEXT_LEFT, TEXT_TOP + 10, lineEnd, TEXT_TOP+10, COL_ACCENT);
    
    d.setTextColor(COL_TEXT);
}

void drawHomeScreen(M5Canvas &d) {
    drawFrame(d, "ISS Tracker " APP_VERSION);
    
    // UPDATED: Scooted icon left (was -40, now -55)
    d.pushImage(d.width() - 55, 20, 32, 32, ISS_ICON_32x32);

    int y = TEXT_TOP + 20;
    d.setCursor(TEXT_LEFT, y);
    d.println("Btn G0: Cycle Screens");
    y += LINE_SPACING * 2;

    const char* menu[] = {
        "LIVE    - Position Data",
        "RADAR   - Skyplot View",
        "PASS    - Next Prediction",
        "OPTIONS - Config/WiFi"
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
        d.printf("Tracking: %s", satName.c_str());
    } else {
        d.setTextColor(COL_SAT_NOW);
        d.setCursor(TEXT_LEFT, y);
        d.println("NO TLE DATA LOADED");
    }
}

void drawLiveScreen(M5Canvas &d, int year, int mon, int day, int hr, int min) {
    drawFrame(d, "Live Telemetry");
    int y = TEXT_TOP + 20;

    if (!isOrbitReady()) {
        d.setCursor(TEXT_LEFT, y); d.println("No Data."); return;
    }

    d.setCursor(TEXT_LEFT, y);
    d.printf("Local: %02d:%02d:%02d\n", hr, min, 0); 
    y += LINE_SPACING;
    d.setCursor(TEXT_LEFT, y);
    d.printf("Lat: %.2f  Lon: %.2f\n", sat.satLat, sat.satLon);
    y += LINE_SPACING;
    d.setCursor(TEXT_LEFT, y);
    d.printf("Alt: %.1f km\n", sat.satAlt);
    y += LINE_SPACING;
    d.setCursor(TEXT_LEFT, y);
    d.printf("Az:  %.1f deg\n", sat.satAz);
    y += LINE_SPACING;
    d.setCursor(TEXT_LEFT, y);
    d.printf("El:  %.1f deg\n", sat.satEl);
    
    y += LINE_SPACING * 2;
    d.setCursor(TEXT_LEFT, y);
    if (sat.satEl > 0) {
        d.setTextColor(COL_SAT_PATH);
        d.println("VISIBLE (AOS)");
    } else {
        d.setTextColor(COL_ACCENT);
        d.println("BELOW HORIZON (LOS)");
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

void drawPassScreen(M5Canvas &d, unsigned long currentUnix, int minEl) {
    drawFrame(d, "Next Pass Prediction");
    int y = TEXT_TOP + 20;

    if (!isOrbitReady()) {
        d.setCursor(TEXT_LEFT, y); d.println("No TLE."); return;
    }

    static PassDetails nextPass;
    static unsigned long lastCalc = 0;
    static int lastMinEl = -1;

    if (currentUnix - lastCalc > 10000 || nextPass.aosUnix < currentUnix || lastMinEl != minEl) {
        d.setCursor(TEXT_LEFT, y);
        d.println("Calculating...");
        d.pushSprite(0,0);
        
        if (predictNextPass(currentUnix, nextPass, minEl)) {
            lastCalc = currentUnix;
            lastMinEl = minEl;
            drawFrame(d, "Next Pass Prediction");
        } else {
            drawFrame(d, "Next Pass Prediction");
            d.setCursor(TEXT_LEFT, y);
            d.printf("No pass > %d deg\n", minEl);
            y+= LINE_SPACING;
            d.setCursor(TEXT_LEFT, y);
            d.println("in next 24h.");
            return;
        }
    }

    time_t rawAos = nextPass.aosUnix;
    struct tm * taos = localtime(&rawAos);
    
    d.setCursor(TEXT_LEFT, y);
    d.setTextColor(COL_HEADER);
    d.println("NEXT AOS:");
    d.setTextColor(COL_TEXT);
    
    char timeBuf[30];
    strftime(timeBuf, 30, "%Y-%m-%d %H:%M", taos);
    y += LINE_SPACING;
    d.setCursor(TEXT_LEFT, y);
    d.println(timeBuf);

    y += LINE_SPACING * 2;
    d.setCursor(TEXT_LEFT, y);
    d.printf("Duration: %.1f min\n", nextPass.durationMins);
    
    y += LINE_SPACING;
    d.setCursor(TEXT_LEFT, y);
    d.printf("Max El:   %.0f deg\n", nextPass.maxElevation);
    
    y += LINE_SPACING * 2;
    d.setCursor(TEXT_LEFT, y);
    d.setTextColor(COL_ACCENT);
    d.printf("Filter: > %d deg", minEl);
}

void drawOptionsScreen(M5Canvas &d, int minEl, int tzOffset) {
    drawFrame(d, "Options");
    int y = TEXT_TOP + 20;
    
    d.setCursor(TEXT_LEFT, y); d.println("1) Wi-Fi Setup");
    y += LINE_SPACING;
    d.setCursor(TEXT_LEFT, y); d.println("2) Location Setup");
    y += LINE_SPACING;
    d.setCursor(TEXT_LEFT, y); d.println("3) Force TLE Update");
    y += LINE_SPACING;
    d.setCursor(TEXT_LEFT, y); d.printf("4) Min Elevation: %d", minEl);
    y += LINE_SPACING;
    d.setCursor(TEXT_LEFT, y); d.printf("5) Timezone: UTC%+d", tzOffset);
    
    y += LINE_SPACING * 2;
    d.setCursor(TEXT_LEFT, y);
    d.printf("Orbit: %.3f deg inc", tleIncDeg);
}