// tempHumidity.cpp — Grove Temperature & Humidity Sensor (DHT11) screen.
//
// Connects to the right Grove port, data pin → A0.
// Reads temperature (°C) and humidity (%) every 2 seconds.
// Using the right Grove port keeps D0/D1 (left port) free for the Sonar screen.
//
// Controls: [C] back to menu

#include <Arduino.h>
#include "globals.h"
#include <DHT.h>

#define DHT_PIN  A0
#define DHT_TYPE DHT11

// ---------------------------------------------------------------------------
// Colour helpers — temperature and humidity bands
// ---------------------------------------------------------------------------
static uint16_t thTempColor(float t)
{
    if (t < 10.0f) return tft.color565(  0, 160, 210);  // cold  — blue
    if (t < 20.0f) return tft.color565(  0, 200, 190);  // cool  — cyan
    if (t < 28.0f) return tft.color565( 50, 200,  80);  // good  — green
    if (t < 35.0f) return tft.color565(220, 160,   0);  // warm  — amber
    return             tft.color565(200,  60,  30);      // hot   — red-orange
}

static uint16_t thHumColor(float h)
{
    if (h < 30.0f) return tft.color565(220, 160,   0);  // dry    — amber
    if (h < 60.0f) return tft.color565( 50, 200,  80);  // normal — green
    return             tft.color565(  0, 160, 210);      // humid  — blue
}

static const char* thTempLabel(float t)
{
    if (t < 10.0f) return "COLD";
    if (t < 20.0f) return "COOL";
    if (t < 28.0f) return "GOOD";
    if (t < 35.0f) return "WARM";
    return "HOT";
}

static const char* thHumLabel(float h)
{
    if (h < 30.0f) return "DRY";
    if (h < 60.0f) return "NORMAL";
    return "HUMID";
}

// ---------------------------------------------------------------------------
// Static chrome — drawn once on entry
// ---------------------------------------------------------------------------
static void drawTHChrome()
{
    tft.fillScreen(TFT_BLACK);

    // Header — matches all other screens: dark bg, left accent stripe, // TITLE,
    // bottom line + hat-tick VLines
    tft.fillRect(0, 0, 320, 30, tft.color565(0, 8, 20));
    tft.fillRect(0, 0, 3, 30, tft.color565(0, 220, 245));
    tft.setTextSize(2);
    tft.setTextColor(tft.color565(0, 220, 245), tft.color565(0, 8, 20));
    tft.drawString("// TEMP HUM", 10, 7);
    tft.drawFastHLine(0, 29, 320, tft.color565(0, 80, 100));
    for (int xi = 8; xi < 320; xi += 14)
        tft.drawFastVLine(xi, 27, 4, tft.color565(0, 140, 165));

    // Sensor source tag — kept left of the battery area (battery clears x≥244)
    tft.setTextSize(1);
    tft.setTextColor(tft.color565(0, 80, 100), tft.color565(0, 8, 20));
    tft.drawString("DHT11 / A0", 155, 11);

    // Vertical divider between the two panels
    tft.drawFastVLine(159, 32, 183, tft.color565(0, 40, 60));
    tft.drawFastVLine(160, 32, 183, tft.color565(0, 55, 75));

    // Panel section labels
    tft.setTextSize(1);
    tft.setTextColor(tft.color565(0, 148, 165), TFT_BLACK);
    tft.drawString("TEMPERATURE", 10, 35);
    tft.drawString("HUMIDITY",   172, 35);

    // Footer — matches all other screens
    tft.fillRect(0, 219, 320, 21, TFT_BLACK);
    tft.fillRect(0, 219, 3, 21, tft.color565(0, 220, 245));
    tft.drawFastHLine(0, 219, 320, tft.color565(0, 80, 100));
    tft.setTextSize(1);
    tft.setTextColor(tft.color565(0, 100, 118), TFT_BLACK);
    tft.drawString("[C] BACK", 252, 227);
}

// ---------------------------------------------------------------------------
// Reading panels — cleared and redrawn on each successful read
// ---------------------------------------------------------------------------
static void drawTHReadings(float tempC, float hum)
{
    uint16_t tc = thTempColor(tempC);
    uint16_t hc = thHumColor(hum);
    char buf[10];
    float displayTemp = (g_tempUnit == 1) ? (tempC * 9.0f / 5.0f + 32.0f) : tempC;

    // ---- Temperature (left, x=0..158) ----
    tft.fillRect(0, 48, 158, 169, TFT_BLACK);

    // Large value — centred in panel
    tft.setTextDatum(TC_DATUM);
    tft.setTextSize(4);
    tft.setTextColor(tc, TFT_BLACK);
    dtostrf(displayTemp, 5, 1, buf);
    tft.drawString(buf, 79, 75);

    // Degree + unit
    tft.setTextSize(2);
    tft.setTextColor(tft.color565(0, 148, 165), TFT_BLACK);
    tft.drawString(g_tempUnit == 1 ? "\xb0""F" : "\xb0""C", 79, 119);

    // Band label
    tft.setTextSize(1);
    tft.setTextColor(tc, TFT_BLACK);
    tft.drawString(thTempLabel(tempC), 79, 145);

    // Thin accent underline below band label
    tft.drawFastHLine(20, 158, 118, tc);

    // ---- Humidity (right, x=161..319) ----
    tft.fillRect(161, 48, 159, 169, TFT_BLACK);

    tft.setTextDatum(TC_DATUM);
    tft.setTextSize(4);
    tft.setTextColor(hc, TFT_BLACK);
    dtostrf(hum, 5, 1, buf);
    tft.drawString(buf, 240, 75);

    tft.setTextSize(2);
    tft.setTextColor(tft.color565(0, 148, 165), TFT_BLACK);
    tft.drawString("%RH", 240, 119);

    tft.setTextSize(1);
    tft.setTextColor(hc, TFT_BLACK);
    tft.drawString(thHumLabel(hum), 240, 145);

    tft.drawFastHLine(181, 158, 118, hc);

    tft.setTextDatum(TL_DATUM);
}

// ---------------------------------------------------------------------------
// Footer status line — shows last-read age; only the value cell is cleared
// ---------------------------------------------------------------------------
static void drawTHStatus(unsigned long age_ms)
{
    tft.fillRect(4, 220, 240, 18, TFT_BLACK);
    tft.setTextSize(1);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(tft.color565(0, 80, 100), TFT_BLACK);
    char buf[32];
    if (age_ms == 0)
        snprintf(buf, sizeof(buf), "UPDATED NOW");
    else
        snprintf(buf, sizeof(buf), "UPDATED  %lus AGO", age_ms / 1000);
    tft.drawString(buf, 8, 227);
}

// ---------------------------------------------------------------------------
// Error state — sensor absent or bad read
// ---------------------------------------------------------------------------
static void drawTHError()
{
    tft.fillRect(0, 48, 320, 169, TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(3);
    tft.setTextColor(tft.color565(220, 60, 30), TFT_BLACK);
    tft.drawString("READ ERROR", 160, 95);
    tft.setTextSize(2);
    tft.setTextColor(tft.color565(0, 148, 165), TFT_BLACK);
    tft.drawString("check sensor on A0", 160, 135);
    tft.setTextSize(1);
    tft.setTextColor(tft.color565(0, 80, 100), TFT_BLACK);
    tft.drawString("right Grove port", 160, 162);
    tft.setTextDatum(TL_DATUM);
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------
void tempHumidityScreen()
{
    while (digitalRead(WIO_5S_PRESS) == LOW) delay(10);

    DHT dht(DHT_PIN, DHT_TYPE);
    dht.begin();

    drawTHChrome();
    drawTHStatus(0);    // "UPDATED NOW" as placeholder
    drawBatteryStatus(tft.color565(0, 8, 20));

    // Show a waiting message in the reading area until the first read
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(1);
    tft.setTextColor(tft.color565(0, 80, 100), TFT_BLACK);
    tft.drawString("WAITING FOR FIRST READ...", 160, 128);
    tft.setTextDatum(TL_DATUM);

    static const unsigned long READ_INTERVAL = 2000;  // DHT11 max 1 Hz
    unsigned long lastRead    = 0;
    unsigned long lastReadAge = 0;
    bool firstRead = true;

    while (true)
    {
        if (digitalRead(WIO_KEY_C) == LOW) {
            while (digitalRead(WIO_KEY_C) == LOW) delay(10);
            delay(50);
            return;
        }

        unsigned long now = millis();

        // Refresh age counter every second between sensor reads
        if (!firstRead && (now % 1000) < 100) {
            drawTHStatus(now - lastReadAge);
        }

        if (now - lastRead < READ_INTERVAL) {
            delay(50);
            continue;
        }
        lastRead = now;

        float hum  = dht.readHumidity();
        float temp = dht.readTemperature();

        if (isnan(hum) || isnan(temp)) {
            drawTHError();
        } else {
            drawTHReadings(temp, hum);
            lastReadAge = now;
            firstRead   = false;
            drawTHStatus(0);
        }

        drawBatteryStatus(tft.color565(0, 8, 20));
    }
}
