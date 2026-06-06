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
// Draw a 5-char fixed-width number string centred at (cx,y), redrawing only
// the character cells that differ from oldStr.  Pass nullptr to force all.
// Size-4 GLCD font: each cell is 24×32 px (6*4 wide, 8*4 tall).
// ---------------------------------------------------------------------------
static void drawNumDiff(int cx, int y, const char* newStr, const char* oldStr, uint16_t col)
{
    const int W = 24, H = 32;
    int x0 = cx - (5 * W) / 2;
    tft.setTextSize(4);
    tft.setTextColor(col, TFT_BLACK);
    tft.setTextDatum(TL_DATUM);
    for (int i = 0; i < 5; i++)
    {
        if (!oldStr || oldStr[i] != newStr[i])
        {
            tft.fillRect(x0 + i * W, y, W, H, TFT_BLACK);
            if (newStr[i] != ' ')
            {
                char ch[2] = { newStr[i], '\0' };
                tft.drawString(ch, x0 + i * W, y);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Reading panels — incremental: full panel clear only on first draw or zone
// colour change; otherwise only the number digits that changed are redrawn.
// The degree symbol is drawn as a small circle (font \xb0 renders as a block).
// ---------------------------------------------------------------------------
static void drawTHReadings(float tempC, float hum, float prevTempC, float prevHum)
{
    uint16_t tc = thTempColor(tempC);
    uint16_t hc = thHumColor(hum);
    float displayTemp = (g_tempUnit == 1) ? (tempC * 9.0f / 5.0f + 32.0f) : tempC;

    bool tcColorChanged = !isnan(prevTempC) && (tc != thTempColor(prevTempC));
    bool hcColorChanged = !isnan(prevHum)   && (hc != thHumColor(prevHum));
    bool tempChanged = isnan(prevTempC) || fabsf(tempC - prevTempC) > 0.05f || tcColorChanged;
    bool humChanged  = isnan(prevHum)   || fabsf(hum  - prevHum)   > 0.05f || hcColorChanged;

    if (!tempChanged && !humChanged) return;

    // Static strings track what digits are currently on screen
    static char prevTempStr[8] = "";
    static char prevHumStr[8]  = "";

    char tempStr[8], humStr[8];
    dtostrf(displayTemp, 5, 1, tempStr);
    dtostrf(hum, 5, 1, humStr);

    if (tempChanged)
    {
        bool fullRedraw = isnan(prevTempC) || tcColorChanged;
        if (fullRedraw)
        {
            tft.fillRect(0, 48, 158, 169, TFT_BLACK);
            // Unit — "C"/"F" with a drawn circle for the degree symbol
            tft.setTextDatum(TC_DATUM);
            tft.setTextSize(2);
            tft.setTextColor(tft.color565(0, 148, 165), TFT_BLACK);
            tft.drawString(g_tempUnit == 1 ? "F" : "C", 85, 119);
            tft.drawCircle(73, 122, 3, tft.color565(0, 148, 165));
            // Band label + underline
            tft.setTextSize(1);
            tft.setTextColor(tc, TFT_BLACK);
            tft.drawString(thTempLabel(tempC), 79, 145);
            tft.drawFastHLine(20, 158, 118, tc);
            prevTempStr[0] = '\0';  // force all digits to redraw
        }
        drawNumDiff(79, 75, tempStr, prevTempStr[0] ? prevTempStr : nullptr, tc);
        strcpy(prevTempStr, tempStr);
    }

    if (humChanged)
    {
        bool fullRedraw = isnan(prevHum) || hcColorChanged;
        if (fullRedraw)
        {
            tft.fillRect(161, 48, 159, 169, TFT_BLACK);
            tft.setTextDatum(TC_DATUM);
            tft.setTextSize(2);
            tft.setTextColor(tft.color565(0, 148, 165), TFT_BLACK);
            tft.drawString("%RH", 240, 119);
            tft.setTextSize(1);
            tft.setTextColor(hc, TFT_BLACK);
            tft.drawString(thHumLabel(hum), 240, 145);
            tft.drawFastHLine(181, 158, 118, hc);
            prevHumStr[0] = '\0';
        }
        drawNumDiff(240, 75, humStr, prevHumStr[0] ? prevHumStr : nullptr, hc);
        strcpy(prevHumStr, humStr);
    }

    // Restore divider — may have been erased by a full-panel fillRect above
    tft.drawFastVLine(159, 32, 183, tft.color565(0, 40, 60));
    tft.drawFastVLine(160, 32, 183, tft.color565(0, 55, 75));

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
    drawBatteryStatus(tft.color565(0, 8, 20));

    static const unsigned long READ_INTERVAL = 2000;  // DHT11 max 1 Hz
    unsigned long lastRead    = 0;
    unsigned long lastReadAge = 0;
    bool firstRead = true;
    float prevTempC = NAN;
    float prevHum   = NAN;
    unsigned long lastAgeS = ULONG_MAX;  // last displayed age in whole seconds
    bool inError = false;

    while (true)
    {
        if (digitalRead(WIO_KEY_C) == LOW) {
            while (digitalRead(WIO_KEY_C) == LOW) delay(10);
            delay(50);
            return;
        }

        unsigned long now = millis();

        // Refresh age counter only when the displayed whole-second value changes
        if (!firstRead) {
            unsigned long ageS = (now - lastReadAge) / 1000;
            if (ageS != lastAgeS) {
                lastAgeS = ageS;
                drawTHStatus(ageS * 1000);
            }
        }

        if (now - lastRead < READ_INTERVAL) {
            delay(50);
            continue;
        }
        lastRead = now;

        float hum  = dht.readHumidity();
        float temp = dht.readTemperature();

        if (isnan(hum) || isnan(temp)) {
            if (!inError) {
                inError = true;
                drawTHError();
                prevTempC = NAN;
                prevHum   = NAN;
            }
        } else {
            if (inError) {
                inError = false;
                // Restore the divider now that we're back to two-panel mode
                tft.drawFastVLine(159, 32, 183, tft.color565(0, 40, 60));
                tft.drawFastVLine(160, 32, 183, tft.color565(0, 55, 75));
            }
            drawTHReadings(temp, hum, prevTempC, prevHum);
            prevTempC   = temp;
            prevHum     = hum;
            lastReadAge = now;
            lastAgeS    = ULONG_MAX;  // force status redraw next loop
            firstRead   = false;
            drawTHStatus(0);
        }

        drawBatteryStatus(tft.color565(0, 8, 20));
    }
}
