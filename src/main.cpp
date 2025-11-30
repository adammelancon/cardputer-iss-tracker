#include <M5Cardputer.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <SD.h>
#include <Preferences.h>
#include <time.h>
#include <Adafruit_NeoPixel.h>

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

double obsLatDeg = 30.22; 
double obsLonDeg = -92.02;
int minElevation = DEFAULT_MIN_EL;
int tzOffsetHours = -6; 
String satName = "";
bool tleParsedOK = false;

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
    // SCREEN_OPTIONS,  <-- REMOVING THIS
    
    // --- MENU SCREENS (Accessed via 'c') ---
    SCREEN_MENU_MAIN,
    SCREEN_MENU_WIFI,
    SCREEN_MENU_SAT,
    SCREEN_MENU_LOC,
    
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

bool downloadTLE() {
    if (WiFi.status() != WL_CONNECTED) return false;
    HTTPClient http;
    if (!http.begin("https://celestrak.org/NORAD/elements/gp.php?CATNR=25544&FORMAT=TLE")) return false;
    if (http.GET() != HTTP_CODE_OK) return false;
    String payload = http.getString();
    http.end();
    
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
        canvas.print("ENTER=Save  ESC=Cancel");  // <--- Updated Instruction
        
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

    // Load TLE
    String localTle = readFileFromSD(ISS_TLE_PATH);
    if (localTle != "ERROR") {
        parseTLEData(localTle);
    }

    // --- BOOT SCREEN ---
    canvas.fillScreen(COL_BG);
    
    // Draw Text Centered
    canvas.setTextDatum(middle_center);
    canvas.setTextColor(COL_HEADER);
    canvas.drawString("ISS Tracker", canvas.width() / 2, 45);
    
    // Draw Line
    canvas.drawFastHLine(60, 58, 120, COL_ACCENT);
    
    // Draw Icon Centered
    int iconX = (canvas.width() / 2) - 16;
    canvas.pushImage(iconX, 68, 32, 32, ISS_ICON_32x32);
    
    // Reset Datum for normal text
    canvas.setTextDatum(top_left);
    canvas.pushSprite(0,0);
    // -----------------------
    
    if (connectWiFiAndTime()) {
         downloadTLE();
         WiFi.disconnect(true);
         WiFi.mode(WIFI_OFF);
    }

    setupOrbitLocation(obsLatDeg, obsLonDeg);
}

void loop() {
    M5Cardputer.update();

    // --- 1. KEYBOARD INPUT HANDLING ---
    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
        Keyboard_Class::KeysState k = M5Cardputer.Keyboard.keysState();

        // --- BACK / ESCAPE LOGIC ---
        // Check for DEL key (Top Right) OR 'ESC' char (27)
        bool pressedBack = k.del; 
        for (auto c : k.word) { if (c == 27) pressedBack = true; }

        if (pressedBack) {
            // Smart Back Navigation
            if (currentScreen == SCREEN_MENU_WIFI || 
                currentScreen == SCREEN_MENU_SAT  || 
                currentScreen == SCREEN_MENU_LOC) {
                currentScreen = SCREEN_MENU_MAIN; // Submenu -> Main Menu
            } 
            else if (currentScreen == SCREEN_MENU_MAIN) {
                currentScreen = SCREEN_HOME;      // Main Menu -> Home
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

        // --- MENU SPECIFIC NAVIGATION ---
        // Only process specific menu keys if we didn't just press Back
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
                         configTime(tzOffsetHours * 3600, 0, "pool.ntp.org");
                         needsRedraw = true;
                    }
                }
            }
            // WIFI MENU
            else if (currentScreen == SCREEN_MENU_WIFI) {
                for (auto c : k.word) {
                    if (c == '2') { 
                        wifiSsid = textInput(wifiSsid, "SSID:");
                        wifiPass = textInput(wifiPass, "Pass:");
                        prefs.begin("iss_cfg", false);
                        prefs.putString("wifiSsid", wifiSsid);
                        prefs.putString("wifiPass", wifiPass);
                        prefs.end();
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
                    if (c == '2') { 
                        canvas.fillScreen(COL_BG);
                        canvas.drawString("Updating...", 50, 50);
                        canvas.pushSprite(0,0);
                        connectWiFiAndTime();
                        downloadTLE();
                        needsRedraw = true;
                    }
                }
            }
            // LOCATION MENU
            else if (currentScreen == SCREEN_MENU_LOC) {
                 for (auto c : k.word) {
                    if (c == '1') { 
                        String l = textInput(String(obsLatDeg), "Lat:");
                        obsLatDeg = l.toFloat();
                        prefs.begin("iss_cfg", false);
                        prefs.putDouble("lat", obsLatDeg);
                        prefs.end();
                        setupOrbitLocation(obsLatDeg, obsLonDeg);
                        needsRedraw = true;
                    }
                    if (c == '2') {
                        String lo = textInput(String(obsLonDeg), "Lon:");
                        obsLonDeg = lo.toFloat();
                        prefs.begin("iss_cfg", false);
                        prefs.putDouble("lon", obsLonDeg);
                        prefs.end();
                        setupOrbitLocation(obsLatDeg, obsLonDeg);
                        needsRedraw = true;
                    }
                }
            }
        }
    }

    // --- 2. BUTTON INPUT (G0) ---
    if (M5Cardputer.BtnA.wasPressed()) {
        // If inside a menu, G0 acts as "Home"
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
            case SCREEN_PASS:   drawPassScreen(canvas, unixtime, minElevation); break; 
            
            case SCREEN_MENU_MAIN: drawMainMenu(canvas); break;
            case SCREEN_MENU_WIFI: drawWifiMenu(canvas, wifiSsid); break;
            case SCREEN_MENU_SAT:  drawSatMenu(canvas, minElevation); break;
            case SCREEN_MENU_LOC:  drawLocationMenu(canvas, obsLatDeg, obsLonDeg); break;
            
            default: break;
        }
        canvas.pushSprite(0,0);
        needsRedraw = false;
    }
    
    delay(20);
}