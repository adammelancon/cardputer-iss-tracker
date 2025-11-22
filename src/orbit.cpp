#include "orbit.h"
#include "config.h"
#include <Sgp4.h>

// The SGP4 object
Sgp4 sat;
bool sgp4Ready = false;

char tleLine1Buf[130];
char tleLine2Buf[130];

// Orbital elements for display
float tleIncDeg = 0;
float tleRAANDeg = 0;
float tleEcc = 0;
float tleArgPerDeg = 0;

void initOrbitSystem() {
    // Placeholder if needed
}

bool isOrbitReady() {
    return sgp4Ready && tleParsedOK;
}

void updateSatellitePos(unsigned long unixtime) {
    if (isOrbitReady()) {
        sat.findsat(unixtime);
    }
}

void setupOrbitLocation(double lat, double lon) {
    sat.site(lat, lon, OBS_ALT_M);
}

void parseTLEData(const String &rawTLE) {
    // Reset flags
    sgp4Ready = false;
    tleParsedOK = false;

    int firstNL = rawTLE.indexOf('\n');
    if (firstNL < 0) return;
    int secondNL = rawTLE.indexOf('\n', firstNL + 1);
    if (secondNL < 0) return;

    satName = rawTLE.substring(0, firstNL);
    satName.trim();

    String t1 = rawTLE.substring(firstNL + 1, secondNL);
    String t2 = rawTLE.substring(secondNL + 1);
    t1.trim(); 
    t2.trim();

    if (t1.length() < 69 || t2.length() < 69) return;

    // Extract visual data
    tleIncDeg    = t2.substring(8, 16).toFloat();
    tleRAANDeg   = t2.substring(17, 25).toFloat();
    tleEcc       = t2.substring(26, 33).toFloat() / 10000000.0f; // Correct decimal
    tleArgPerDeg = t2.substring(34, 42).toFloat();

    // Prepare buffers for SGP4
    t1.toCharArray(tleLine1Buf, sizeof(tleLine1Buf));
    t2.toCharArray(tleLine2Buf, sizeof(tleLine2Buf));

    // Init SGP4
    sat.init(satName.c_str(), tleLine1Buf, tleLine2Buf);
    tleParsedOK = true;
    sgp4Ready = true;
    
    // Apply current location
    setupOrbitLocation(obsLatDeg, obsLonDeg);
}

// --- PREDICTION ENGINE ---
// Looks ahead up to 24 hours to find the next AOS > minElThreshold
bool predictNextPass(unsigned long startUnix, PassDetails &pass, int minElThreshold) {
    if (!isOrbitReady()) return false;

    unsigned long t = startUnix;
    unsigned long step = 30; // check every 30 seconds for speed
    unsigned long maxSearch = startUnix + (24 * 3600);

    bool inPass = false;
    double maxEl = -999;
    unsigned long aosTime = 0;

    // Initial check to fast forward if we are currently IN a pass
    sat.findsat(t);
    if (sat.satEl > 0) {
        while(t < maxSearch) {
            sat.findsat(t);
            if (sat.satEl < 0) break;
            t += step;
        }
    }

    // Search loop
    while (t < maxSearch) {
        sat.findsat(t);
        
        if (!inPass && sat.satEl > 0) {
            // Pass started
            inPass = true;
            aosTime = t;
            maxEl = sat.satEl;
        } else if (inPass && sat.satEl > 0) {
            // Tracking max elevation
            if (sat.satEl > maxEl) maxEl = sat.satEl;
        } else if (inPass && sat.satEl < 0) {
            // Pass ended. CHECK THRESHOLD.
            if (maxEl >= minElThreshold) {
                // Good pass found!
                pass.aosUnix = aosTime;
                pass.losUnix = t;
                pass.maxElevation = maxEl;
                pass.durationMins = (t - aosTime) / 60.0;
                
                updateSatellitePos(startUnix); 
                return true;
            } else {
                // Pass was too low. Reset and keep searching.
                inPass = false;
                maxEl = -999;
            }
        }
        t += step;
    }
    
    updateSatellitePos(startUnix);
    return false;
}