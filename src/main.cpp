#include <M5Cardputer.h>
#include <SPI.h>
#include <SD.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>
#include <Sgp4.h>
#include <M5GFX.h>
#include "credentials.h"

// Editable Wi-Fi settings (start with defaults from credentials.h)
String wifiSsid = WIFI_SSID;
String wifiPass = WIFI_PSK;


// ---------- SD Card Pins (Cardputer ADV) ----------
#define SD_SPI_SCK_PIN  40
#define SD_SPI_MISO_PIN 39
#define SD_SPI_MOSI_PIN 14
#define SD_SPI_CS_PIN   12

// ---------- Paths / WiFi / NTP ----------
const char *ISS_TLE_PATH = "/apps/iss_tracker/iss.tle";



const char *NTP_SERVER = "pool.ntp.org";

// Your QTH (approx Lafayette, LA – tweak if you like)
const double OBS_LAT_DEG = 30.22;   // degrees
const double OBS_LON_DEG = -92.02;  // degrees
const double OBS_ALT_M   = 15.0;    // meters

// --- UI Layout Constants ---
const int FRAME_MARGIN = 5;         // outer border offset
const int TEXT_LEFT    = 22;        // tuned for Cardputer ADV screen
const int TEXT_TOP     = 15;        // tuned for Cardputer ADV screen
const int LINE_SPACING = 12;        // Font0 is 8px tall → 12px keeps readable spacing




// We'll use Central Time (CST/CDT) just for the display of sat time
int tzOffsetHours = -6;  // adjust if you want

// ---------- TLE fields ----------
String satName      = "";
String tleLine1     = "";
String tleLine2     = "";
bool   tleParsedOK  = false;

char tleLine1Buf[130];
char tleLine2Buf[130];


// Parsed orbital elements (mainly for the options screen)
float  tleIncDeg     = 0.0f;
float  tleRAANDeg    = 0.0f;
float  tleEcc        = 0.0f;
float  tleArgPerDeg  = 0.0f;
float  tleMeanAnDeg  = 0.0f;
float  tleMeanMotion = 0.0f;

// ---------- SGP4 ----------
Sgp4  sat;
bool  sgp4Ready      = false;
unsigned long unixtime = 0;
unsigned long lastOrbitUpdateMs = 0;

// Time display helpers
int    dispYear, dispMon, dispDay, dispHr, dispMin;
double dispSec;

// ---------- UI state ----------
enum Screen {
    SCREEN_HOME = 0,
    SCREEN_LIVE,
    SCREEN_OPTIONS,
    SCREEN_WIFI_MENU,       // <-- new
    // (we'll do input in a blocking helper instead of separate screens)
    SCREEN_COUNT
};

Screen currentScreen = SCREEN_HOME;
bool   needsRedraw   = true;

// Forward declarations for drawing
void drawHomeScreen();
void drawLiveScreen();
void drawOptionsScreen();
void drawCurrentScreen();

// ---------- Helpers ----------

String readFileFromSD(const char *path) {
    File file = SD.open(path);
    if (!file) {
        return String("ERROR: Could not open file ") + path;
    }
    String content;
    while (file.available()) {
        content += (char)file.read();
    }
    file.close();
    return content;
}

String trimCRLF(const String &s) {
    String out = s;
    while (out.endsWith("\r") || out.endsWith("\n")) {
        out.remove(out.length() - 1);
    }
    return out;
}

void parseTLE(const String &rawTLE) {
    satName     = "";
    tleLine1    = "";
    tleLine2    = "";
    tleParsedOK = false;
    sgp4Ready   = false;

    int firstNL  = rawTLE.indexOf('\n');
    if (firstNL < 0) return;
    int secondNL = rawTLE.indexOf('\n', firstNL + 1);
    if (secondNL < 0) return;

    satName  = trimCRLF(rawTLE.substring(0, firstNL));
    tleLine1 = trimCRLF(rawTLE.substring(firstNL + 1, secondNL));
    tleLine2 = trimCRLF(rawTLE.substring(secondNL + 1));

    if (tleLine1.length() < 69 || tleLine2.length() < 69) {
        return;
    }

    // TLE line 2 positions (1-based):
    //  9-16  inclination (deg)
    // 18-25  RAAN (deg)
    // 27-33  eccentricity (no decimal)
    // 35-42  arg of perigee (deg)
    // 44-51  mean anomaly (deg)
    // 53-63  mean motion (rev/day)
    String incStr    = tleLine2.substring(8, 16);
    String raanStr   = tleLine2.substring(17, 25);
    String eccStr    = tleLine2.substring(26, 33);
    String argPerStr = tleLine2.substring(34, 42);
    String meanAnStr = tleLine2.substring(43, 51);
    String mmStr     = tleLine2.substring(52, 63);

    eccStr.trim();
    incStr.trim();
    raanStr.trim();
    argPerStr.trim();
    meanAnStr.trim();
    mmStr.trim();

    tleIncDeg     = incStr.toFloat();
    tleRAANDeg    = raanStr.toFloat();
    tleEcc        = eccStr.toFloat();
    while (tleEcc > 1.0f) tleEcc /= 10.0f;
    tleArgPerDeg  = argPerStr.toFloat();
    tleMeanAnDeg  = meanAnStr.toFloat();
    tleMeanMotion = mmStr.toFloat();

    tleParsedOK = true;

    // Initialize SGP4 from this TLE
    if (tleParsedOK) {
        sat.site(OBS_LAT_DEG, OBS_LON_DEG, OBS_ALT_M);
        sat.init(
            satName.c_str(),  // const char[] is fine for name
            tleLine1Buf,      // writable char[130]
            tleLine2Buf       // writable char[130]
        );
        sgp4Ready = true;
    }
}

// Connect Wi-Fi and sync NTP time
bool connectWiFiAndTime() {
    if (wifiSsid.isEmpty()) return false;

    WiFi.mode(WIFI_STA);
    WiFi.begin(wifiSsid.c_str(), wifiPass.c_str());

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
        delay(200);
    }
    if (WiFi.status() != WL_CONNECTED) {
        return false;
    }

    // NTP (UTC; we'll handle timezone ourselves)
    configTime(0, 0, NTP_SERVER);

    struct tm timeinfo;
    bool ok = false;
    for (int i = 0; i < 20; ++i) {
        if (getLocalTime(&timeinfo, 1000)) {
            ok = true;
            break;
        }
    }
    return ok;
}

// Download ISS TLE from CelesTrak and save to SD
bool downloadTLEToSD() {
    if (WiFi.status() != WL_CONNECTED) return false;

    HTTPClient http;
    // ISS NORAD ID 25544
    const char *url =
        "https://celestrak.org/NORAD/elements/gp.php?CATNR=25544&FORMAT=TLE";

    if (!http.begin(url)) {
        return false;
    }

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        http.end();
        return false;
    }

    String payload = http.getString();
    http.end();

    // (Optional sanity check)
    if (!payload.startsWith("ISS") && !payload.startsWith("25544")) {
        // Still write it, but you could bail here if you wanted.
    }

    // Ensure folder exists; parent is /apps/iss_tracker
    // SD lib can't create nested dirs in one call, so we do it manually:
    SD.mkdir("/apps");
    SD.mkdir("/apps/iss_tracker");

    File f = SD.open(ISS_TLE_PATH, FILE_WRITE);
    if (!f) {
        return false;
    }
    f.print(payload);
    f.close();

    parseTLE(payload);
    return true;
}

// ---------- Screen drawing ----------

void drawHomeScreen() {
    auto &d = M5Cardputer.Display;

    d.clear();
    d.drawRect(FRAME_MARGIN, FRAME_MARGIN,
               d.width() - FRAME_MARGIN*2,
               d.height() - FRAME_MARGIN*2,
               0xF000);

    int y = TEXT_TOP;
    d.setCursor(TEXT_LEFT, y);

    d.println("   ISS Tracker");
    y += LINE_SPACING;
    d.setCursor(TEXT_LEFT, y);
    d.println("   -----------");

    y += LINE_SPACING * 2;
    d.setCursor(TEXT_LEFT, y);
    d.println("Top Btn (G0): switch");
    y += LINE_SPACING;
    d.setCursor(TEXT_LEFT, y);
    d.println("screens");

    y += LINE_SPACING * 2;
    d.setCursor(TEXT_LEFT, y);
    d.println("HOME    - this help");
    y += LINE_SPACING;
    d.setCursor(TEXT_LEFT, y);
    d.println("LIVE    - ISS position");
    y += LINE_SPACING;
    d.setCursor(TEXT_LEFT, y);
    d.println("OPTIONS - TLE info");

    y += LINE_SPACING * 2;
    d.setCursor(TEXT_LEFT, y);
    if (!tleParsedOK) {
        d.println("TLE: not loaded.");
    } else {
        d.println("TLE OK for:");
        y += LINE_SPACING;
        d.setCursor(TEXT_LEFT, y);
        d.println("  " + satName);
    }
}



void drawLiveScreen() {
    auto &d = M5Cardputer.Display;

    d.clear();
    d.drawRect(FRAME_MARGIN, FRAME_MARGIN,
               d.width() - FRAME_MARGIN*2,
               d.height() - FRAME_MARGIN*2,
               0xF000);

    int y = TEXT_TOP;
    d.setCursor(TEXT_LEFT, y);
    d.println("   ISS Live Position");
    y += LINE_SPACING;
    d.setCursor(TEXT_LEFT, y);
    d.println("   -----------------");

    y += LINE_SPACING * 2;

    if (!tleParsedOK || !sgp4Ready) {
        d.setCursor(TEXT_LEFT, y);
        d.println("No valid TLE.");
        y += LINE_SPACING;
        d.setCursor(TEXT_LEFT, y);
        d.println("See OPTIONS.");
        return;
    }

    invjday(
        sat.satJd,
        tzOffsetHours,
        true,
        dispYear, dispMon, dispDay, dispHr, dispMin, dispSec
    );

    d.setCursor(TEXT_LEFT, y);
    d.printf("%04d-%02d-%02d %02d:%02d\n",
             dispYear, dispMon, dispDay, dispHr, dispMin);

    y += LINE_SPACING * 2;
    d.setCursor(TEXT_LEFT, y);
    d.printf("Lat:  %.2f deg\n", sat.satLat);
    y += LINE_SPACING;
    d.setCursor(TEXT_LEFT, y);
    d.printf("Lon:  %.2f deg\n", sat.satLon);
    y += LINE_SPACING;
    d.setCursor(TEXT_LEFT, y);
    d.printf("Alt:  %.0f km\n", sat.satAlt);

    y += LINE_SPACING * 2;
    d.setCursor(TEXT_LEFT, y);
    d.printf("Az:   %.1f deg\n", sat.satAz);
    y += LINE_SPACING;
    d.setCursor(TEXT_LEFT, y);
    d.printf("El:   %.1f deg\n", sat.satEl);

    y += LINE_SPACING * 2;
    d.setCursor(TEXT_LEFT, y);
    d.print("Vis: ");
    if (sat.satVis == -2)      d.println("Below horizon");
    else if (sat.satVis == -1) d.println("Daylight");
    else                       d.printf("%d\n", sat.satVis);
}



void drawOptionsScreen() {
    auto &d = M5Cardputer.Display;

    d.clear();
    d.drawRect(FRAME_MARGIN, FRAME_MARGIN,
               d.width() - FRAME_MARGIN*2,
               d.height() - FRAME_MARGIN*2,
               0xF000);

    int y = TEXT_TOP;
    d.setCursor(TEXT_LEFT, y);
    d.println("   TLE / Options");
    y += LINE_SPACING;
    d.setCursor(TEXT_LEFT, y);
    d.println("   -------------");

    y += LINE_SPACING * 2;
    d.setCursor(TEXT_LEFT, y);
    d.println("Press ENTER for Wi-Fi setup");


    y += LINE_SPACING;

    if (!tleParsedOK) {
        d.setCursor(TEXT_LEFT, y);
        d.println("No valid TLE.");
        y += LINE_SPACING;
        d.setCursor(TEXT_LEFT, y);
        d.println("Check:");
        y += LINE_SPACING;
        d.setCursor(TEXT_LEFT, y);
        d.println(ISS_TLE_PATH);
        return;
    }

    d.setCursor(TEXT_LEFT, y);
    d.println("Sat: " + satName);

    y += LINE_SPACING;
    d.setCursor(TEXT_LEFT, y);
    d.printf("Inc:  %.3f deg\n", tleIncDeg);
    y += LINE_SPACING;
    d.setCursor(TEXT_LEFT, y);
    d.printf("RAAN: %.3f deg\n", tleRAANDeg);
    y += LINE_SPACING;
    d.setCursor(TEXT_LEFT, y);
    d.printf("Ecc:  %.7f\n", tleEcc);
    y += LINE_SPACING;
    d.setCursor(TEXT_LEFT, y);
    d.printf("ArgP: %.3f deg\n", tleArgPerDeg);
    y += LINE_SPACING;
 /*
    d.setCursor(TEXT_LEFT, y);
    d.printf("MAn:  %.3f deg\n", tleMeanAnDeg);
    y += LINE_SPACING;
    d.setCursor(TEXT_LEFT, y);
    d.printf("n:    %.8f rev/d\n", tleMeanMotion);

    y += LINE_SPACING * 2;
    d.setCursor(TEXT_LEFT, y);
    d.println("Raw TLE:");
    y += LINE_SPACING;

    // Print raw file but with clipping protection
    String raw = readFileFromSD(ISS_TLE_PATH);

    d.setCursor(TEXT_LEFT, y);
    d.println(raw);
    */
}

void drawWifiMenuScreen() {
    auto &d = M5Cardputer.Display;

    d.clear();
    d.drawRect(FRAME_MARGIN, FRAME_MARGIN,
               d.width() - FRAME_MARGIN * 2,
               d.height() - FRAME_MARGIN * 2,
               0xF000);

    int y = TEXT_TOP;
    d.setCursor(TEXT_LEFT, y);
    d.println("   Wi-Fi Setup");
    y += LINE_SPACING;
    d.setCursor(TEXT_LEFT, y);
    d.println("   ----------");

    y += LINE_SPACING * 2;
    d.setCursor(TEXT_LEFT, y);
    d.print("SSID: ");
    if (wifiSsid.isEmpty()) d.println("(not set)");
    else                    d.println(wifiSsid);

    y += LINE_SPACING;
    d.setCursor(TEXT_LEFT, y);
    d.print("Pass: ");
    if (wifiPass.isEmpty()) d.println("(not set)");
    else                    d.println("********");

    y += LINE_SPACING * 2;
    d.setCursor(TEXT_LEFT, y);
    d.println("1) Edit SSID");
    y += LINE_SPACING;
    d.setCursor(TEXT_LEFT, y);
    d.println("2) Edit password");
    y += LINE_SPACING;
    d.setCursor(TEXT_LEFT, y);
    d.println("3) Connect");
    y += LINE_SPACING;
    d.setCursor(TEXT_LEFT, y);
    d.println("0) Back");
}


void drawCurrentScreen() {
    switch (currentScreen) {
        case SCREEN_HOME:      drawHomeScreen();      break;
        case SCREEN_LIVE:      drawLiveScreen();      break;
        case SCREEN_OPTIONS:   drawOptionsScreen();   break;
        case SCREEN_WIFI_MENU: drawWifiMenuScreen();  break;
        default:               drawHomeScreen();      break;
    }
}


String textInput(const String &initial, const char *prompt) {
    auto &d = M5Cardputer.Display;
    String value = initial;

    while (true) {
        M5Cardputer.update();

        if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
            Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();

            // regular characters
            for (auto c : status.word) {
                value += c;
            }

            // backspace
            if (status.del && !value.isEmpty()) {
                value.remove(value.length() - 1);
            }

            // enter = done
            if (status.enter) {
                return value;
            }

            // redraw prompt + current text
            d.clear();
            d.drawRect(FRAME_MARGIN, FRAME_MARGIN,
                       d.width() - FRAME_MARGIN * 2,
                       d.height() - FRAME_MARGIN * 2,
                       0xF000);

            int y = TEXT_TOP;
            d.setCursor(TEXT_LEFT, y);
            d.println(prompt);
            y += LINE_SPACING * 2;

            d.setCursor(TEXT_LEFT, y);
            d.println(value);
        }

        delay(10);
    }
}



// ---------- Arduino setup / loop ----------

void setup() {
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);   // enable keyboard


    auto &d = M5Cardputer.Display;
    d.setFont(&fonts::Font0);       // smaller, clean font
    d.setTextColor(0x07E0);
    d.setTextSize(1);

    d.clear();
    d.drawRect(FRAME_MARGIN,
            FRAME_MARGIN,
            d.width()  - FRAME_MARGIN * 2,
            d.height() - FRAME_MARGIN * 2,
            0xF000);

    d.setCursor(FRAME_MARGIN + TEXT_LEFT, FRAME_MARGIN + TEXT_TOP);
    d.println("ISS Tracker booting...");

    // SD init
    SPI.begin(SD_SPI_SCK_PIN, SD_SPI_MISO_PIN, SD_SPI_MOSI_PIN, SD_SPI_CS_PIN);
    if (!SD.begin(SD_SPI_CS_PIN, SPI, 25000000)) {
        d.println("SD init failed!");
        delay(2000);
    } else {
        d.println("SD init OK");

        // Try loading local TLE first
        String raw = readFileFromSD(ISS_TLE_PATH);
        if (!raw.startsWith("ERROR")) {
            parseTLE(raw);
            if (tleParsedOK) {
                d.println("Loaded local TLE.");
            } else {
                d.println("Local TLE parse fail.");
            }
        } else {
            d.println("No local TLE file.");
        }
    }

    // Wi-Fi + TLE update
    d.println();
    d.println("Connecting WiFi...");
    if (connectWiFiAndTime()) {
        d.println("WiFi OK, updating TLE...");
        if (downloadTLEToSD()) {
            d.println("TLE updated from net.");
        } else {
            d.println("TLE download failed.");
        }
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
    } else {
        d.println("WiFi/time failed, using");
        d.println("local TLE if available.");
    }

    // Prime time + SGP4
    time_t nowEpoch = time(nullptr);
    if (nowEpoch > 1000000000UL) {
        unixtime = nowEpoch;
        if (sgp4Ready) {
            sat.findsat(unixtime);
        }
    }

    delay(1500);
    drawCurrentScreen();
}

void loop() {
    M5Cardputer.update();

    // Keyboard-driven navigation
    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
        Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();

        if (currentScreen == SCREEN_OPTIONS) {
            // ENTER from Options -> Wi-Fi menu
            if (status.enter) {
                currentScreen = SCREEN_WIFI_MENU;
                needsRedraw = true;
            }
        } else if (currentScreen == SCREEN_WIFI_MENU) {
            // handle numeric choices (and 0 = back)
            for (auto c : status.word) {
                if (c == '1') {
                    wifiSsid = textInput(wifiSsid, "Enter SSID:");
                    currentScreen = SCREEN_WIFI_MENU;
                    needsRedraw = true;
                } else if (c == '2') {
                    wifiPass = textInput(wifiPass, "Enter password:");
                    currentScreen = SCREEN_WIFI_MENU;
                    needsRedraw = true;
                } else if (c == '3') {
                    auto &d = M5Cardputer.Display;
                    d.clear();
                    d.setCursor(TEXT_LEFT, TEXT_TOP);
                    d.println("Connecting to Wi-Fi...");
                    bool ok = connectWiFiAndTime();
                    d.println(ok ? "Connected!" : "Failed.");
                    delay(1500);
                    currentScreen = SCREEN_WIFI_MENU;
                    needsRedraw = true;
                } else if (c == '0') {   // 0 = Back
                    currentScreen = SCREEN_OPTIONS;
                    needsRedraw = true;
                }
            }
        }

    }


    // Top button cycles screens
    if (M5Cardputer.BtnA.wasPressed()) {
        int next = static_cast<int>(currentScreen) + 1;
        if (next >= static_cast<int>(SCREEN_COUNT)) next = 0;
        currentScreen = static_cast<Screen>(next);
        needsRedraw   = true;
    }

    // Update satellite position about once a second
    unsigned long nowMs = millis();
    if (sgp4Ready && tleParsedOK && nowMs - lastOrbitUpdateMs >= 1000) {
        time_t nowEpoch = time(nullptr);
        if (nowEpoch > 1000000000UL) {
            unixtime = nowEpoch;
            sat.findsat(unixtime);
            lastOrbitUpdateMs = nowMs;

            if (currentScreen == SCREEN_LIVE) {
                needsRedraw = true;
            }
        }
    }

    if (needsRedraw) {
        drawCurrentScreen();
        needsRedraw = false;
    }

    delay(20);
}
