#pragma once
#include <Arduino.h>
#include <Sgp4.h>

struct PassDetails {
    unsigned long aosUnix;
    unsigned long losUnix;
    double maxElevation;
    double durationMins;
};

extern Sgp4 sat;
extern float tleIncDeg;
extern float tleRAANDeg;
extern float tleEcc;
extern float tleArgPerDeg;

void initOrbitSystem();
bool isOrbitReady();
void setupOrbitLocation(double lat, double lon);
void parseTLEData(const String &rawTLE);
void updateSatellitePos(unsigned long unixtime);
bool predictNextPass(unsigned long startUnix, PassDetails &pass, int minElThreshold);