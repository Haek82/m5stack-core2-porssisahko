#include <M5Unified.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

#include "config.h"

// -------------------------------------------------------
// Constants
// -------------------------------------------------------
static const char* TZ_HELSINKI = "EET-2EEST,M3.5.0/3,M10.5.0/4";
static const char* NTP_SERVER  = "pool.ntp.org";
static const char* API_URL     = "https://api.porssisahko.net/v2/latest-prices.json";

// Screen dimensions (landscape)
static const int SCR_W = 320;
static const int SCR_H = 240;

// -------------------------------------------------------
// State
// -------------------------------------------------------
float  g_prices[24]    = {};   // today's hourly average c/kWh (bar chart)
float  g_qhPrices[96]  = {};   // today's 15-min slot prices: index = hour*4 + (min/15)
float  g_tmrPrices[24] = {};   // tomorrow's hourly average c/kWh (bar chart)
bool   g_hasTomorrow   = false;
bool   g_dataReady     = false;
unsigned long g_lastFetch = 0;

// -------------------------------------------------------
// Helpers
// -------------------------------------------------------

// Parse ISO-8601 UTC string "2025-02-24T22:15:00.000Z" → time_t (pure arithmetic)
static time_t parseUTCDate(const char* s) {
    int year = 0, mon = 0, day = 0, hour = 0, min = 0, sec = 0;
    sscanf(s, "%d-%d-%dT%d:%d:%d", &year, &mon, &day, &hour, &min, &sec);

    static const int dim[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    long days = 0;
    for (int y = 1970; y < year; y++)
        days += (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 366 : 365;
    bool leap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
    for (int m = 1; m < mon; m++) {
        days += dim[m - 1];
        if (m == 2 && leap) days++;
    }
    days += day - 1;
    return (time_t)(days * 86400L + hour * 3600L + min * 60L + sec);
}

// Pick display colour based on c/kWh value
// Uses color888(r,g,b) so LovyanGFX handles BGR panel ordering correctly.
static uint32_t priceColour(float cKwh) {
    if (cKwh < PRICE_LOW)  return M5.Display.color888(  0, 210,   0);  // green
    if (cKwh < PRICE_HIGH) return M5.Display.color888(220, 180,   0);  // amber/yellow
    return                         M5.Display.color888(220,  30,   0);  // red
}

// -------------------------------------------------------
// Fetch prices from porssisahko.net for today (Finnish local day)
// Response: { "prices": [ {"price": <c/kWh>, "startDate": "<UTC ISO8601>"}, ... ] }
// -------------------------------------------------------
static bool fetchPrices() {
    Serial.printf("[API] GET %s\n", API_URL);

    WiFiClientSecure client;
    client.setInsecure();   // no CA bundle on embedded device

    HTTPClient http;
    if (!http.begin(client, API_URL)) {
        Serial.println("[API] http.begin failed");
        return false;
    }

    int code = http.GET();
    if (code != 200) {
        Serial.printf("[API] HTTP %d\n", code);
        http.end();
        return false;
    }

    String payload = http.getString();
    http.end();

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err) {
        Serial.printf("[JSON] parse error: %s\n", err.c_str());
        return false;
    }

    JsonArray arr = doc["prices"].as<JsonArray>();
    if (arr.isNull()) {
        Serial.println("[API] no prices array");
        return false;
    }

    time_t now = time(nullptr);
    struct tm localNow;
    localtime_r(&now, &localNow);
    int todayYear  = localNow.tm_year;
    int todayMonth = localNow.tm_mon;
    int todayDay   = localNow.tm_mday;

    // Compute tomorrow's local date
    struct tm tmrTm  = localNow;
    tmrTm.tm_mday   += 1;
    tmrTm.tm_isdst   = -1;
    mktime(&tmrTm);
    int tmrYear  = tmrTm.tm_year;
    int tmrMonth = tmrTm.tm_mon;
    int tmrDay   = tmrTm.tm_mday;

    memset(g_prices,    0, sizeof(g_prices));
    memset(g_qhPrices,  0, sizeof(g_qhPrices));
    memset(g_tmrPrices, 0, sizeof(g_tmrPrices));
    g_hasTomorrow = false;

    float hourSum[24]  = {};
    int   hourCount[24]= {};
    float tmrSum[24]   = {};
    int   tmrCount[24] = {};

    for (JsonObject entry : arr) {
        const char* startDate = entry["startDate"].as<const char*>();
        float price = entry["price"].as<float>();   // already c/kWh

        if (!startDate) continue;

        time_t ts = parseUTCDate(startDate);
        struct tm localEntry;
        localtime_r(&ts, &localEntry);

        if (localEntry.tm_year == todayYear &&
            localEntry.tm_mon  == todayMonth &&
            localEntry.tm_mday == todayDay) {
            int h    = localEntry.tm_hour;
            int slot = h * 4 + (localEntry.tm_min / 15);
            if (slot >= 0 && slot < 96) g_qhPrices[slot] = price;
            if (h    >= 0 && h    < 24) { hourSum[h]  += price; hourCount[h]++; }
        } else if (localEntry.tm_year == tmrYear &&
                   localEntry.tm_mon  == tmrMonth &&
                   localEntry.tm_mday == tmrDay) {
            int h = localEntry.tm_hour;
            if (h >= 0 && h < 24) { tmrSum[h] += price; tmrCount[h]++; }
        }
    }

    for (int h = 0; h < 24; h++) {
        g_prices[h] = hourCount[h] > 0 ? hourSum[h] / hourCount[h] : 0.0f;
        if (tmrCount[h] > 0) {
            g_tmrPrices[h] = tmrSum[h] / tmrCount[h];
            g_hasTomorrow  = true;
        }
    }

    Serial.println("[API] prices updated OK");
    return true;
}

// -------------------------------------------------------
// Draw UI
// -------------------------------------------------------
static void drawScreen() {
    M5.Display.fillScreen(TFT_BLACK);

    time_t now = time(nullptr);
    struct tm localNow;
    localtime_r(&now, &localNow);
    int curHour = localNow.tm_hour;

    // ---- Header ----------------------------------------
    // "PORSSISAHKO  FI        14:35"
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.setCursor(4, 4);
    M5.Display.print("PORSSISAHKO  FI");   // ASCII-safe; see note below

    char timeBuf[6];
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", localNow.tm_hour, localNow.tm_min);
    M5.Display.setCursor(SCR_W - 37, 4);
    M5.Display.print(timeBuf);

    M5.Display.drawFastHLine(0, 16, SCR_W, TFT_DARKGREY);

    // ---- Waiting splash --------------------------------
    if (!g_dataReady) {
        M5.Display.setTextSize(2);
        M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
        M5.Display.setCursor(60, 110);
        M5.Display.print("Ladataan...");
        return;
    }

    // ---- Current price (large) — current 15-min slot -----
    int curMin15 = (localNow.tm_min / 15) * 15;          // 0, 15, 30, or 45
    int curSlot  = curHour * 4 + (localNow.tm_min / 15); // 0 … 95
    float cur    = g_qhPrices[curSlot];

    M5.Display.setTextColor(priceColour(cur), TFT_BLACK);
    M5.Display.setTextSize(5);

    char valBuf[12];
    snprintf(valBuf, sizeof(valBuf), "%.2f", cur);
    // rough centering: textsize 5 → ~30px per char
    int xCenter = (SCR_W - (int)strlen(valBuf) * 30) / 2 - 20;
    if (xCenter < 4) xCenter = 4;
    M5.Display.setCursor(xCenter, 25);
    M5.Display.print(valBuf);

    // unit label
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.setCursor(SCR_W - 80, 50);
    M5.Display.print("c/kWh");

    // hour label
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    char hourLbl[14];
    snprintf(hourLbl, sizeof(hourLbl), "klo %02d:%02d", curHour, curMin15);
    M5.Display.setCursor((SCR_W - 60) / 2, 78);
    M5.Display.print(hourLbl);

    M5.Display.drawFastHLine(0, 90, SCR_W, TFT_DARKGREY);

    // ---- Bar chart (y=92 … y=225) ----------------------
    const int CHART_X  = 18;   // left margin for min/max labels
    const int CHART_Y  = 93;
    const int CHART_W  = SCR_W - CHART_X;
    const int CHART_H  = 125;  // pixel height for bars
    const int LABEL_Y  = CHART_Y + CHART_H + 3;

    // Scale: 0 … max(today, tomorrow, 20 c/kWh)
    float maxP = 20.0f;
    for (int h = 0; h < 24; h++) {
        if (g_prices[h]    > maxP) maxP = g_prices[h];
        if (g_hasTomorrow && g_tmrPrices[h] > maxP) maxP = g_tmrPrices[h];
    }

    float midP   = maxP / 2.0f;
    int   midY   = CHART_Y + CHART_H / 2;
    int   numBars = g_hasTomorrow ? 48 : 24;
    float barW   = (float)CHART_W / numBars;

    // Centre line at maxP/2
    M5.Display.drawFastHLine(CHART_X, midY, CHART_W, M5.Display.color888(60, 60, 60));

    // Today's bars
    for (int h = 0; h < 24; h++) {
        float norm = g_prices[h] / maxP;
        int   barH = max(2, (int)(norm * CHART_H));
        int   x    = CHART_X + (int)(h * barW);
        int   w    = max(1, (int)barW - 1);
        int   y    = CHART_Y + CHART_H - barH;

        uint32_t col = (h == curHour) ? M5.Display.color888(255, 255, 255) : priceColour(g_prices[h]);
        M5.Display.fillRect(x, y, w, barH, col);

        if (h % 6 == 0) {
            M5.Display.setTextSize(1);
            M5.Display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
            M5.Display.setCursor(x + 1, LABEL_Y);
            M5.Display.printf("%02d", h);
        }
    }

    // Tomorrow's bars (available after ~14:00 Finnish time)
    if (g_hasTomorrow) {
        int divX = CHART_X + (int)(24 * barW);
        M5.Display.drawFastVLine(divX, CHART_Y, CHART_H, M5.Display.color888(80, 80, 80));

        for (int h = 0; h < 24; h++) {
            float norm = g_tmrPrices[h] / maxP;
            int   barH = max(2, (int)(norm * CHART_H));
            int   x    = CHART_X + (int)((h + 24) * barW);
            int   w    = max(1, (int)barW - 1);
            int   y    = CHART_Y + CHART_H - barH;

            M5.Display.fillRect(x, y, w, barH, priceColour(g_tmrPrices[h]));

            if (h % 6 == 0) {
                M5.Display.setTextSize(1);
                M5.Display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
                M5.Display.setCursor(x + 1, LABEL_Y);
                M5.Display.printf("%02d", h);
            }
        }
    }

    // Scale annotations on left margin: top = max, middle = max/2, bottom = 0
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
    M5.Display.setCursor(0, CHART_Y);
    M5.Display.printf("%.0f", maxP);
    M5.Display.setCursor(0, midY - 4);
    M5.Display.printf("%.0f", midP);
    M5.Display.setCursor(0, CHART_Y + CHART_H - 8);
    M5.Display.print("0");
}

// -------------------------------------------------------
// Setup
// -------------------------------------------------------
void setup() {
    Serial.begin(115200);

    auto cfg = M5.config();
    M5.begin(cfg);

    M5.Display.setRotation(1);       // landscape
    M5.Display.setBrightness(100);
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);

    // --- WiFi ---
    M5.Display.setTextSize(2);
    M5.Display.setCursor(10, 100);
    M5.Display.print("Yhdistetaan WiFiin...");

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    unsigned long t0 = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - t0 > 20000) {
            M5.Display.fillScreen(TFT_BLACK);
            M5.Display.setTextColor(TFT_RED, TFT_BLACK);
            M5.Display.setCursor(10, 100);
            M5.Display.print("WiFi-yhteys eponnistui!");
            Serial.println("WiFi failed");
            while (true) delay(1000);
        }
        delay(500);
    }
    Serial.printf("WiFi connected: %s\n", WiFi.localIP().toString().c_str());

    // --- NTP ---
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
    M5.Display.setCursor(10, 100);
    M5.Display.print("Haetaan aika (NTP)...");

    configTzTime(TZ_HELSINKI, NTP_SERVER, "time.google.com");

    struct tm tmp;
    while (!getLocalTime(&tmp, 10000)) {
        Serial.println("Waiting for NTP...");
    }
    Serial.printf("Time: %04d-%02d-%02d %02d:%02d\n",
        tmp.tm_year + 1900, tmp.tm_mon + 1, tmp.tm_mday,
        tmp.tm_hour, tmp.tm_min);

    // --- Initial price fetch ---
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
    M5.Display.setCursor(10, 100);
    M5.Display.print("Haetaan hinnat...");

    g_dataReady = fetchPrices();
    g_lastFetch = millis();

    drawScreen();
}

// -------------------------------------------------------
// Loop — refresh display every minute, re-fetch every hour
// -------------------------------------------------------
void loop() {
    M5.update();

    static unsigned long lastDraw = 0;
    unsigned long now = millis();

    if (now - lastDraw >= 60000UL) {
        lastDraw = now;

        if (now - g_lastFetch >= FETCH_INTERVAL_MS) {
            g_dataReady = fetchPrices();
            g_lastFetch = now;
        }

        drawScreen();
    }

    delay(100);
}
