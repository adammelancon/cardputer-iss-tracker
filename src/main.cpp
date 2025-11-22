#include <M5Cardputer.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <SD.h>
#include <Preferences.h>
#include <time.h>

#include "config.h"
#include "orbit.h"
#include "ui.h"
#include "credentials.h"
#include "iss_icon.h" 

// --- GLOBALS ---
M5Canvas canvas(&M5Cardputer.Display);
Preferences prefs;

String wifiSsid = WIFI_SSID;
String wifiPass = WIFI_PSK;

double obsLatDeg = 30.22; 
double obsLonDeg = -92.02;
int minElevation = DEFAULT_MIN_EL;
int tzOffsetHours = -6; 
String satName = "";
bool tleParsedOK = false;

// --- UI State ---
enum Screen {
    SCREEN_HOME = 0,
    SCREEN_LIVE,
    SCREEN_RADAR,
    SCREEN_PASS,
    SCREEN_OPTIONS,
    SCREEN_COUNT,
    SCREEN_WIFI_MENU,
    SCREEN_LOCATION_MENU
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
        canvas.pushSprite(0,0);

        if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
            Keyboard_Class::KeysState s = M5Cardputer.Keyboard.keysState();
            if (s.enter) return value;
            if (s.del && value.length() > 0) value.remove(value.length()-1);
            for (auto c : s.word) value += c;
        }
    }
}

void setup() {
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);
    
    // Init Display
    canvas.setColorDepth(16);
    canvas.createSprite(240, 135);
    canvas.setFont(&fonts::Font0);
    canvas.setTextSize(1);
    
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

    // --- NEW BOOT SCREEN ---
    canvas.fillScreen(COL_BG);
    
    // Draw Text Centered
    canvas.setTextDatum(middle_center);
    canvas.setTextColor(COL_HEADER);
    canvas.drawString("ISS Tracker", canvas.width() / 2, 45);
    
    // Draw Line
    canvas.drawFastHLine(60, 58, 120, COL_ACCENT);
    
    // Draw Icon Centered (reverted to pure center)
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

    if (M5Cardputer.BtnA.wasPressed()) {
        int next = (int)currentScreen + 1;
        if (next >= (int)SCREEN_COUNT) next = 0;
        currentScreen = (Screen)next;
        needsRedraw = true;
    }

    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
        Keyboard_Class::KeysState k = M5Cardputer.Keyboard.keysState();
        
        if (currentScreen == SCREEN_OPTIONS) {
            for (auto c : k.word) {
                if (c == '1') { // Wifi
                    wifiSsid = textInput(wifiSsid, "SSID:");
                    wifiPass = textInput(wifiPass, "Pass:");
                    prefs.begin("iss_cfg", false);
                    prefs.putString("wifiSsid", wifiSsid);
                    prefs.putString("wifiPass", wifiPass);
                    prefs.end();
                    needsRedraw=true;
                }
                if (c == '2') { // Loc
                    String l = textInput(String(obsLatDeg), "Lat:");
                    obsLatDeg = l.toFloat();
                    String lo = textInput(String(obsLonDeg), "Lon:");
                    obsLonDeg = lo.toFloat();
                    prefs.begin("iss_cfg", false);
                    prefs.putDouble("lat", obsLatDeg);
                    prefs.putDouble("lon", obsLonDeg);
                    prefs.end();
                    setupOrbitLocation(obsLatDeg, obsLonDeg);
                    needsRedraw=true;
                }
                if (c == '3') { // Update
                    canvas.fillScreen(COL_BG);
                    canvas.drawString("Updating...", 50, 50);
                    canvas.pushSprite(0,0);
                    connectWiFiAndTime();
                    downloadTLE();
                    needsRedraw = true;
                }
                if (c == '4') { // Min El
                    String m = textInput(String(minElevation), "Min El (deg):");
                    minElevation = m.toInt();
                    if (minElevation < 0) minElevation = 0;
                    if (minElevation > 90) minElevation = 90;
                    prefs.begin("iss_cfg", false);
                    prefs.putInt("minEl", minElevation);
                    prefs.end();
                    needsRedraw=true;
                }
                if (c == '5') { // Timezone
                    String t = textInput(String(tzOffsetHours), "Offset (ex: -6):");
                    tzOffsetHours = t.toInt();
                    
                    // Save
                    prefs.begin("iss_cfg", false);
                    prefs.putInt("tzOffset", tzOffsetHours);
                    prefs.end();
                    
                    configTime(tzOffsetHours * 3600, 0, "pool.ntp.org");
                    
                    needsRedraw=true;
                }
            }
        }
    }

    unsigned long now = millis();
    if (now - lastOrbitUpdateMs >= 1000) {
        time_t t = time(nullptr);
        unixtime = (unsigned long)t;
        updateSatellitePos(unixtime);
        lastOrbitUpdateMs = now;
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
            case SCREEN_OPTIONS:drawOptionsScreen(canvas, minElevation, tzOffsetHours); break; 
            default: break;
        }
        canvas.pushSprite(0,0);
        needsRedraw = false;
    }
    
    delay(20);
}