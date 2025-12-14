#include <M5Cardputer.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <SD.h>
#include <Preferences.h>
#include <time.h>
#include <Adafruit_NeoPixel.h>
#include <TinyGPS++.h>

#include "config.h"
#include "orbit.h"
#include "ui.h"
#include "credentials.h"
#include "iss_icon.h" 


// --- GLOBALS ---
M5Canvas canvas(&M5Cardputer.Display);
Preferences prefs;
Adafruit_NeoPixel pixels(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

String wifiSsid = WIFI_SSID;
String wifiPass = WIFI_PSK;
int wifiScanCount = 0;

double obsLatDeg = 30.22; 
double obsLonDeg = -92.02;
int minElevation = DEFAULT_MIN_EL;
int tzOffsetHours = -6; 
String satName = "";
int satCatNumber = 25544;
bool tleParsedOK = false;
bool isTimeSet = false; // New flag to track if clock is valid


TinyGPSPlus gps;
HardwareSerial gpsSerial(1); // Use UART 1
bool useGpsModule = false;

// --- LED State Variables ---
bool wasVisible = false;       // Tracks state from previous loop
bool losTimerActive = false;   // Are we currently showing the Red LOS light?
unsigned long losStartTime = 0; // When did LOS start?
const int LOS_DURATION_MS = 5000; // How long to stay red (5 seconds)

// --- UI State ---
enum Screen {
    // --- DASHBOARD SCREENS (Cycled with Button G0) ---
    SCREEN_HOME = 0,
    SCREEN_LIVE,
    SCREEN_RADAR,
    SCREEN_PASS,
    
    // --- MENU SCREENS (Accessed via 'c') ---
    SCREEN_MENU_MAIN,
    SCREEN_MENU_WIFI,
    SCREEN_WIFI_SCAN,
    SCREEN_MENU_SAT,
    SCREEN_MENU_LOC,
    SCREEN_GPS_INFO,
    
    SCREEN_COUNT // Keep this for the G0 cycling logic
};

Screen currentScreen = SCREEN_HOME;
bool needsRedraw = true;
unsigned long lastOrbitUpdateMs = 0;
unsigned long unixtime = 0;

// --- HELPER FUNCTIONS ---

String readFileFromSD(const char *path) {
    File file = SD.open(path);
    if (!file) return "ERROR";
    String content;
    while (file.available()) content += (char)file.read();
    file.close();
    return content;
}

bool connectWiFiAndTime() {
    if (wifiSsid.isEmpty()) return false;
    WiFi.begin(wifiSsid.c_str(), wifiPass.c_str());
    unsigned long s = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - s < 8000) delay(200);
    if (WiFi.status() != WL_CONNECTED) return false;
    
    configTime(tzOffsetHours * 3600, 0, "pool.ntp.org");
    struct tm t;
    return getLocalTime(&t, 2000);
}

// --- NEW FUNCTION: GPS TIME SYNC (Corrected) ---
void syncTimeFromGPS() {
    // Only sync if we have valid date/time and it is fresh (<1s old)
    if (gps.date.isValid() && gps.time.isValid() && gps.time.age() < 1000) {
        
        struct tm t = {0};
        t.tm_year = gps.date.year() - 1900;
        t.tm_mon  = gps.date.month() - 1;
        t.tm_mday = gps.date.day();
        t.tm_hour = gps.time.hour();
        t.tm_min  = gps.time.minute();
        t.tm_sec  = gps.time.second();
        
        // Standard portable replacement for timegm:
        // 1. Save current TZ environment variable
        String oldTz = getenv("TZ") ? getenv("TZ") : "";
        
        // 2. Set TZ to UTC temporarily. 
        // This forces mktime() to treat the struct tm as UTC.
        setenv("TZ", "UTC0", 1);
        tzset();
        
        // 3. Convert to time_t
        time_t utcTime = mktime(&t);
        
        // 4. Restore original TZ
        if (oldTz.length() > 0) {
            setenv("TZ", oldTz.c_str(), 1);
        } else {
            unsetenv("TZ");
        }
        tzset(); // Apply the restore

        // 5. Set System Time
        struct timeval tv = { .tv_sec = utcTime, .tv_usec = 0 };
        settimeofday(&tv, NULL);
        
        isTimeSet = true;
    }
}

bool downloadTLE() {
    if (WiFi.status() != WL_CONNECTED) return false;
        HTTPClient http;
        String url = "https://celestrak.org/NORAD/elements/gp.php?CATNR=" + String(satCatNumber) + "&FORMAT=TLE";    
    
    if (!http.begin(url)) return false;
        
    if (http.GET() != HTTP_CODE_OK) return false;
        String payload = http.getString();
        http.end();

    // Delete old file first so we don't append
    if (SD.exists(ISS_TLE_PATH)) {
        SD.remove(ISS_TLE_PATH);
    }
    
    SD.mkdir("/apps/iss_tracker");
    File f = SD.open(ISS_TLE_PATH, FILE_WRITE);
    if (f) { f.print(payload); f.close(); }
    
    parseTLEData(payload);
    return true;
}

String textInput(const String &initial, const char *prompt) {
    String value = initial;
    while (true) {
        M5Cardputer.update();
        canvas.fillScreen(COL_BG);
        canvas.drawRect(FRAME_MARGIN, FRAME_MARGIN, 230, 125, COL_ACCENT);
        canvas.setCursor(TEXT_LEFT, TEXT_TOP);
        canvas.println(prompt);
        canvas.setCursor(TEXT_LEFT, TEXT_TOP+30);
        canvas.setTextColor(COL_HEADER);
        canvas.println(value + "_");
        canvas.setTextColor(COL_TEXT);
        
        // Helper text
        canvas.setCursor(TEXT_LEFT, 100);
        canvas.setTextColor(COL_ACCENT);
        canvas.print("ENTER=Save  ESC=Cancel");
        
        canvas.pushSprite(0,0);

        if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
            Keyboard_Class::KeysState s = M5Cardputer.Keyboard.keysState();
            
            if (s.enter) return value; // Save
            
            if (s.del && value.length() > 0) value.remove(value.length()-1); // Backspace
            
            for (auto c : s.word) {
                if (c == 27) return initial; // ESC (ASCII 27) -> Cancel
                value += c;
            }
        }
    }
}

void setup() {
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);
    
    // Init Display
    canvas.setColorDepth(16);
    canvas.createSprite(240, 135);
    canvas.setFont(&fonts::Font2); // Using larger font
    canvas.setTextSize(1);

    // LED Init
    pixels.begin();
    pixels.setBrightness(LED_BRIGHTNESS);
    pixels.setPixelColor(0, 0); // Start Off
    pixels.show();
    
    // Init SD
    SPI.begin(SD_SPI_SCK_PIN, SD_SPI_MISO_PIN, SD_SPI_MOSI_PIN, SD_SPI_CS_PIN);
    if(!SD.begin(SD_SPI_CS_PIN, SPI, 25000000)) {
        canvas.drawString("SD ERROR", 10, 10);
        canvas.pushSprite(0,0);
        delay(1000);
    }

    // Load Config
    prefs.begin("iss_cfg", true);
    wifiSsid = prefs.getString("wifiSsid", WIFI_SSID);
    wifiPass = prefs.getString("wifiPass", WIFI_PSK);
    obsLatDeg = prefs.getDouble("lat", obsLatDeg);
    obsLonDeg = prefs.getDouble("lon", obsLonDeg);
    minElevation = prefs.getInt("minEl", DEFAULT_MIN_EL);
    tzOffsetHours = prefs.getInt("tzOffset", -6); 
    prefs.end();

    configTime(tzOffsetHours * 3600, 0, "pool.ntp.org");

    // --- BOOT SCREEN & GPS CHECK ---
    canvas.fillScreen(COL_BG);
    canvas.setTextDatum(middle_center);
    canvas.setTextColor(COL_HEADER);
    canvas.drawString("ISS/Satellite Tracker " APP_VERSION, canvas.width() / 2, 45);
    canvas.drawFastHLine(60, 58, 120, COL_ACCENT);
    
    int iconX = (canvas.width() / 2) - 16;
    canvas.pushImage(iconX, 68, 32, 32, ISS_ICON_32x32);

    // --- NEW GPS DETECTION ---
    canvas.setTextDatum(bottom_center);
    canvas.setTextColor(COL_ACCENT);
    canvas.drawString("Checking GPS Hardware...", canvas.width()/2, 125);
    canvas.pushSprite(0,0);

    // Always init Serial to check
    gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
    
    unsigned long startCheck = millis();
    bool gpsFound = false;
    // Listen for 1.5 seconds for ANY data
    while (millis() - startCheck < 1500) {
        if (gpsSerial.available()) {
            gpsFound = true;
            break;
        }
    }

    // Clear the "Checking" text area
    canvas.fillRect(0, 110, 240, 25, COL_BG);

    if (gpsFound) {
        useGpsModule = true;
        canvas.setTextColor(COL_SAT_PATH); // Green
        canvas.drawString("GPS Module Detected!", canvas.width()/2, 125);
    } else {
        useGpsModule = false;
        canvas.setTextColor(COL_SAT_NOW); // Red/Orange
        canvas.drawString("No GPS Found - Manual Mode", canvas.width()/2, 125);
    }
    canvas.pushSprite(0,0);
    delay(1000); 
    canvas.setTextDatum(top_left);
    // -----------------------

    // Load TLE
    String localTle = readFileFromSD(ISS_TLE_PATH);
    if (localTle != "ERROR") {
        parseTLEData(localTle);
    }
    
    if (connectWiFiAndTime()) {
         isTimeSet = true; // Mark time as set via NTP
         downloadTLE();
         WiFi.disconnect(true);
         WiFi.mode(WIFI_OFF);
    }

    setupOrbitLocation(obsLatDeg, obsLonDeg);
}

void loop() {
    M5Cardputer.update();

    // --- GPS PARSING ---
    if (useGpsModule) {
        while (gpsSerial.available() > 0) {
            gps.encode(gpsSerial.read());
        }
        if (gps.location.isUpdated()) {
            obsLatDeg = gps.location.lat();
            obsLonDeg = gps.location.lng();
            setupOrbitLocation(obsLatDeg, obsLonDeg);
            
            // --- TIME SYNC LOGIC ---
            // Sync from GPS every 60 seconds to keep the system clock accurate
            static unsigned long lastTimeSync = 0;
            if (millis() - lastTimeSync > 60000) { 
                syncTimeFromGPS();
                lastTimeSync = millis();
            }
        }
    }

    // --- 1. KEYBOARD INPUT HANDLING ---
    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
        Keyboard_Class::KeysState k = M5Cardputer.Keyboard.keysState();

        // --- BACK / ESCAPE LOGIC ---
        bool pressedBack = k.del; 
        for (auto c : k.word) { if (c == 27) pressedBack = true; }

        if (pressedBack) {
            if (currentScreen == SCREEN_MENU_WIFI || 
                currentScreen == SCREEN_MENU_SAT  || 
                currentScreen == SCREEN_MENU_LOC) {
                currentScreen = SCREEN_MENU_MAIN; 
            } 
            else if (currentScreen == SCREEN_GPS_INFO) {
                currentScreen = SCREEN_MENU_LOC;
            }
            else if (currentScreen == SCREEN_MENU_MAIN) {
                currentScreen = SCREEN_HOME;
            }
            needsRedraw = true;
        }

        // --- GLOBAL HOTKEYS ---
        for (auto c : k.word) {
            if (c == 'c' || c == 'C') {
                currentScreen = SCREEN_MENU_MAIN;
                needsRedraw = true;
            }
        }

        // --- DASHBOARD NAVIGATION (Arrows) ---
        if (currentScreen < SCREEN_MENU_MAIN) {
            for (auto c : k.word) {
                // Right Arrow ('/' or '>')
                if (c == '/' || c == '>') {
                    int next = (int)currentScreen + 1;
                    if (next > (int)SCREEN_PASS) next = SCREEN_HOME;
                    currentScreen = (Screen)next;
                    needsRedraw = true;
                }
                
                // Left Arrow (',' or '<')
                if (c == ',' || c == '<') {
                    int prev = (int)currentScreen - 1;
                    if (prev < 0) prev = SCREEN_PASS;
                    currentScreen = (Screen)prev;
                    needsRedraw = true;
                }
            }
        }

        // --- MENU SPECIFIC NAVIGATION ---
        if (!pressedBack && currentScreen != SCREEN_HOME) {
            
            // MAIN MENU
            if (currentScreen == SCREEN_MENU_MAIN) {
                for (auto c : k.word) {
                    if (c == '1') { currentScreen = SCREEN_MENU_WIFI; needsRedraw = true; }
                    if (c == '2') { currentScreen = SCREEN_MENU_SAT; needsRedraw = true; }
                    if (c == '3') { currentScreen = SCREEN_MENU_LOC; needsRedraw = true; }
                    if (c == '4') { 
                         String t = textInput(String(tzOffsetHours), "UTC Offset:");
                         tzOffsetHours = t.toInt();
                         prefs.begin("iss_cfg", false);
                         prefs.putInt("tzOffset", tzOffsetHours);
                         prefs.end();
                         // Re-apply timezone if changed
                         configTime(tzOffsetHours * 3600, 0, "pool.ntp.org");
                         needsRedraw = true;
                    }
                }
            }
            // WIFI MENU
            else if (currentScreen == SCREEN_MENU_WIFI) {
                for (auto c : k.word) {
                    if (c == '1') { // Scan
                        canvas.fillScreen(COL_BG);
                        canvas.setCursor(20, 50);
                        canvas.setTextSize(1);
                        canvas.println("Scanning WiFi...");
                        canvas.pushSprite(0,0);
                        WiFi.mode(WIFI_STA);
                        WiFi.disconnect();
                        wifiScanCount = WiFi.scanNetworks();
                        currentScreen = SCREEN_WIFI_SCAN;
                        needsRedraw = true;
                    }
                    if (c == '2') { // Manual
                        wifiSsid = textInput(wifiSsid, "SSID:");
                        // Then user would enter password logic here if we kept it
                    }
                }
            }
            // SCAN RESULTS SCREEN
            else if (currentScreen == SCREEN_WIFI_SCAN) {
                for (auto c : k.word) {
                    int selection = -1;
                    if (c >= '1' && c <= '9') selection = c - '1';
                    
                    if (selection >= 0 && selection < wifiScanCount) {
                        wifiSsid = WiFi.SSID(selection);
                        wifiPass = textInput("", "Password:");
                        prefs.begin("iss_cfg", false);
                        prefs.putString("wifiSsid", wifiSsid);
                        prefs.putString("wifiPass", wifiPass);
                        prefs.end();
                        currentScreen = SCREEN_MENU_MAIN;
                        needsRedraw = true;
                    }
                }
            }
            // SATELLITE MENU
            else if (currentScreen == SCREEN_MENU_SAT) {
                for (auto c : k.word) {
                    if (c == '1') { 
                        String m = textInput(String(minElevation), "Min El (deg):");
                        minElevation = m.toInt();
                        prefs.begin("iss_cfg", false);
                        prefs.putInt("minEl", minElevation);
                        prefs.end();
                        needsRedraw = true;
                    }
                    if (c == '2') { // Edit ID
                        String s = textInput(String(satCatNumber), "Sat Cat #:");
                        int newVal = s.toInt();
                        if (newVal > 0 && newVal != satCatNumber) {
                            satCatNumber = newVal;
                            prefs.begin("iss_cfg", false);
                            prefs.putInt("satCat", satCatNumber);
                            prefs.end();
                            
                            canvas.fillScreen(COL_BG);
                            canvas.drawString("Updating Sat Data...", 20, 50);
                            canvas.pushSprite(0,0);
                            
                            if (connectWiFiAndTime()) {
                                isTimeSet = true; // Mark time valid
                                if (downloadTLE()) {
                                    canvas.drawString("Success!", 80, 80);
                                    delay(500);
                                    canvas.setCursor(20, 100);
                                    canvas.setTextColor(COL_SAT_PATH);
                                    canvas.printf("Found: %s", satName.c_str());
                                } else {
                                    canvas.drawString("Download Failed!", 20, 80);
                                    delay(2000);
                                }
                                WiFi.disconnect(true);
                                WiFi.mode(WIFI_OFF);
                            } else {
                                canvas.drawString("WiFi Failed!", 20, 80);
                                delay(2000);
                            }
                        }
                        needsRedraw = true;
                    }
                    if (c == '3') { // Reset to ISS
                        if (satCatNumber != 25544) {
                            satCatNumber = 25544;
                            prefs.begin("iss_cfg", false);
                            prefs.putInt("satCat", satCatNumber);
                            prefs.end();
                            
                            canvas.fillScreen(COL_BG);
                            canvas.drawString("Resetting to ISS...", 20, 50);
                            canvas.pushSprite(0,0);
                            
                            if (connectWiFiAndTime()) {
                                isTimeSet = true;
                                if (downloadTLE()) {
                                    canvas.drawString("Success!", 80, 80);
                                    delay(500);
                                    canvas.setCursor(20, 100);
                                    canvas.setTextColor(COL_SAT_PATH);
                                    canvas.printf("Found: %s", satName.c_str());
                                } else {
                                    canvas.drawString("Download Failed!", 20, 80);
                                    delay(2000);
                                }
                                WiFi.disconnect(true);
                                WiFi.mode(WIFI_OFF);
                            } else {
                                canvas.drawString("WiFi Failed!", 20, 80);
                                delay(2000);
                            }
                        }
                        needsRedraw = true;
                    }
                    if (c == '4') { // Update TLE
                        canvas.fillScreen(COL_BG);
                        canvas.drawString("Updating...", 50, 50);
                        canvas.pushSprite(0,0);
                        if (connectWiFiAndTime()) {
                            isTimeSet = true; 
                            if (downloadTLE()) {
                                canvas.setCursor(20, 100);
                                canvas.setTextColor(COL_SAT_PATH);
                                canvas.printf("Found: %s", satName.c_str());
                            } else {
                                 canvas.drawString("Update Failed!", 50, 80);
                                 canvas.pushSprite(0,0);
                                 delay(2000);
                            }
                            WiFi.disconnect(true);
                            WiFi.mode(WIFI_OFF);
                        } else {
                             canvas.drawString("WiFi Failed!", 50, 80);
                             canvas.pushSprite(0,0);
                             delay(2000);
                        }
                        needsRedraw = true;
                    }
                }
            }
            // LOCATION MENU
            else if (currentScreen == SCREEN_MENU_LOC) {
                 for (auto c : k.word) {
                    if (c == '1') { 
                        useGpsModule = !useGpsModule;
                        needsRedraw = true;
                    }
                    if (c == '2') { 
                        if (!useGpsModule) {
                            String l = textInput(String(obsLatDeg), "Lat:");
                            obsLatDeg = l.toFloat();
                            prefs.begin("iss_cfg", false);
                            prefs.putDouble("lat", obsLatDeg);
                            prefs.end();
                            setupOrbitLocation(obsLatDeg, obsLonDeg);
                        }
                        needsRedraw = true;
                    }
                    if (c == '3') {
                        if (!useGpsModule) {
                            String lo = textInput(String(obsLonDeg), "Lon:");
                            obsLonDeg = lo.toFloat();
                            prefs.begin("iss_cfg", false);
                            prefs.putDouble("lon", obsLonDeg);
                            prefs.end();
                            setupOrbitLocation(obsLatDeg, obsLonDeg);
                        }
                        needsRedraw = true;
                    }
                    if (c == '4') {
                        if (useGpsModule) {
                            currentScreen = SCREEN_GPS_INFO;
                            needsRedraw = true;
                        }
                    }
                }
            }
        }
    }

    // --- 2. BUTTON INPUT (G0) ---
    if (M5Cardputer.BtnA.wasPressed()) {
        if (currentScreen >= SCREEN_MENU_MAIN) {
            currentScreen = SCREEN_HOME;
        } else {
            int next = (int)currentScreen + 1;
            if (next > (int)SCREEN_PASS) next = SCREEN_HOME;
            currentScreen = (Screen)next;
        }
        needsRedraw = true;
    }

    // --- 3. BACKGROUND TASKS ---
    unsigned long now = millis();
    if (now - lastOrbitUpdateMs >= 1000) {
        time_t t = time(nullptr);
        unixtime = (unsigned long)t;
        updateSatellitePos(unixtime);
        lastOrbitUpdateMs = now;
        
        // LED Logic
        bool currentlyVisible = (sat.satEl > 0);
        if (currentlyVisible) {
            pixels.setPixelColor(0, pixels.Color(0, 255, 0)); 
            losTimerActive = false; 
        } else {
            if (wasVisible) { losTimerActive = true; losStartTime = millis(); }

            if (losTimerActive) {
                if (millis() - losStartTime < LOS_DURATION_MS) pixels.setPixelColor(0, pixels.Color(255, 0, 0)); 
                else { pixels.setPixelColor(0, 0); losTimerActive = false; }
            } else {
                pixels.setPixelColor(0, 0); 
            }
        }
        pixels.show();
        wasVisible = currentlyVisible;

        if (currentScreen == SCREEN_LIVE || currentScreen == SCREEN_RADAR) {
            needsRedraw = true;
        }
    } 

    if (needsRedraw) {
        canvas.fillScreen(COL_BG);
        
        time_t t = time(nullptr);
        struct tm *tm = localtime(&t);

        switch (currentScreen) {
            case SCREEN_HOME:   drawHomeScreen(canvas); break;
            case SCREEN_LIVE:   drawLiveScreen(canvas, tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday, tm->tm_hour, tm->tm_min); break;
            case SCREEN_RADAR:  drawRadarScreen(canvas, unixtime); break;
            case SCREEN_PASS:   
                drawPassScreen(canvas, unixtime, minElevation, obsLatDeg, obsLonDeg); 
                break;            
            case SCREEN_MENU_MAIN: drawMainMenu(canvas); break;
            case SCREEN_MENU_WIFI: drawWifiMenu(canvas, wifiSsid); break;
            case SCREEN_WIFI_SCAN: drawWifiScanResults(canvas, wifiScanCount); break;
            case SCREEN_MENU_SAT:  drawSatMenu(canvas, minElevation, satCatNumber); break;
            case SCREEN_MENU_LOC:  
                drawLocationMenu(canvas, obsLatDeg, obsLonDeg, useGpsModule, gps.location.isValid(), gps.satellites.value()); 
                break;         
            case SCREEN_GPS_INFO:  drawGpsInfoScreen(canvas, gps); break;
            default: break;
            
        }
        canvas.pushSprite(0,0);
        needsRedraw = false;
    }
    
    delay(20);
}