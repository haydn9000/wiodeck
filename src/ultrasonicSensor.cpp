// ultrasonicSensor.cpp — Sonar screen (cyberpunk semicircular arc display).
// Sensor: Seeed Grove - Ultrasonic Ranger (SKU 101020010)
//   — single shared SIG pin for trigger + echo, 3.3 V/5 V compatible.
//   — Range: 2–350 cm, resolution: 1 cm.
//
// Wiring: right Grove port (same port as the DHT11 temperature/humidity sensor).
//   VCC → 5 V  (or 3.3 V — range reduced at lower voltage)
//   GND → GND
//   SIG → A0  (yellow Grove wire)
//
// ⚠  The Grove Ultrasonic Ranger uses a SINGLE shared SIG pin for both
//    trigger and echo — NOT the separate TRIG/ECHO HC-SR04 protocol.
//    Generic HC-SR04 is not compatible without a level shifter (5 V ECHO).
//
// ⚠  Do NOT use the left Grove port (near USB-C): those pins share the
//    SAMD51 SWD debug interface — a connected sensor will freeze the device
//    on reset/power-on.
//
// Features:
//   • Semicircular sonar arc — fills with zone colour as proximity increases
//   • Alert threshold marker tick on the arc ring
//   • Live distance in cm (large, colour-coded) and inches inside the arc
//   • MIN / MAX tracker (reset with PRESS)
//   • Adjustable alert threshold: UP / DOWN in 5 cm steps
//   • Parking-sensor beeps: rate increases as you approach threshold
//   • Ping counter in header
//
// Controls:
//   [5S UP / DOWN]  Raise / lower alert threshold
//   [PRESS]         Reset MIN / MAX
//   [C]             Back to menu

#include <Arduino.h>
#include <math.h>
#include "globals.h"

#ifndef WIO_BUZZER
#define WIO_BUZZER 15
#endif

// ---- Pin assignment (right Grove port, yellow wire — same port as DHT11) ----
static constexpr uint8_t SIG_PIN = A0;  // shared trig+echo, Grove Ultrasonic Ranger protocol

// ---- Sensor limits (Grove Ultrasonic Ranger: 2–350 cm, resolution 1 cm) ----
static constexpr float DIST_MAX_CM   = 350.0f;
static constexpr float ALERT_DEFAULT = 30.0f;
static constexpr float ALERT_MIN_CM  = 5.0f;
static constexpr float ALERT_MAX_CM  = 300.0f;

// ---- Arc geometry ----
// Semicircle, flat side down. Centre at (ARC_CX, ARC_CY).
// Sweeps from 9 o'clock to 3 o'clock (upper half only).
// 0% fill = empty (far/out-of-range), 100% fill = full (right at sensor).
static constexpr int ARC_CX = 160;
static constexpr int ARC_CY = 182;   // baseline — pushed down to maximise arc size
static constexpr int ARC_R  = 110;   // outer radius
static constexpr int ARC_T  = 14;    // ring thickness → inner radius = 96

// ---- Layout ----
static constexpr int HDR_H    = 30;
static constexpr int NUM_Y    = 124;   // top of distance number (size 4, 32px → bottom 156)
static constexpr int INCH_Y   = 160;   // inches row (size 2, 16px → bottom 176, clears baseline at 182)
static constexpr int PANEL_Y  = 196;   // MIN/MAX row
static constexpr int ALERT_Y  = 207;   // alert threshold row
static constexpr int FOOTER_Y = 222;   // footer

// ---- State ----
static float    usMin       = 9999.0f;
static float    usMax       = -1.0f;
static float    alertThresh = ALERT_DEFAULT;
static uint32_t pingCount   = 0;

// -------------------------------------------------------------------------
// Colour palette
// -------------------------------------------------------------------------
static inline uint16_t colSafe()    { return tft.color565(0,   220, 245); }  // neon cyan
static inline uint16_t colCaution() { return tft.color565(255, 180,   0); }  // neon amber
static inline uint16_t colDanger()  { return tft.color565(255,   0, 128); }  // neon magenta
static inline uint16_t colOOR()     { return tft.color565( 30,  45,  60); }  // dim (out of range)
static inline uint16_t colTrack()   { return tft.color565( 0,  55,  80); }  // arc track (unfilled)

static uint16_t zoneColour(float dist)
{
    if (dist < 0)                  return colOOR();
    if (dist < alertThresh)        return colDanger();
    if (dist < alertThresh * 3.0f) return colCaution();
    return colSafe();
}

// Proximity % for arc fill: 0 = at DIST_MAX (empty), 100 = right at sensor (full).
static int distToPct(float dist)
{
    if (dist < 0) return 0;
    float c = (dist > DIST_MAX_CM) ? DIST_MAX_CM : dist;
    return (int)((1.0f - c / DIST_MAX_CM) * 100.0f + 0.5f);
}

// -------------------------------------------------------------------------
// Measure distance using the Grove Ultrasonic Ranger single-pin protocol.
// SIG_PIN is driven OUTPUT for the 10 µs trigger pulse, then switched to
// INPUT_PULLDOWN to capture the echo — same pin, same wire.  The pull-down
// prevents the floating pin giving a spurious pulse before the echo arrives.
// Returns -1 on timeout or for readings below the 2 cm sensor minimum.
// -------------------------------------------------------------------------
static float measureDistance()
{
    // Trigger: 10 µs HIGH pulse on SIG_PIN (OUTPUT)
    pinMode(SIG_PIN, OUTPUT);
    digitalWrite(SIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(SIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(SIG_PIN, LOW);

    // Echo: switch to INPUT_PULLDOWN so the pin holds LOW until sensor drives HIGH
    pinMode(SIG_PIN, INPUT_PULLDOWN);
    long dur = pulseIn(SIG_PIN, HIGH, 23500);  // 23500 µs ≈ 400 cm max
    if (dur == 0) return -1.0f;
    float dist = (dur * 0.0343f) / 2.0f;
    return (dist < 2.0f) ? -1.0f : dist;  // below sensor minimum is not a valid reading
}

// -------------------------------------------------------------------------
// Short tone on the piezo buzzer.  Respects g_volume (0 = mute, 1–4 = duty).
// -------------------------------------------------------------------------
static void shortBeep(uint32_t durationMs)
{
    if (g_volume == 0) return;
    const uint32_t halfUs = 125;                          // 4000 Hz
    uint32_t highUs = halfUs * (uint32_t)g_volume / 4;    // vol 1=31µs, 4=125µs
    uint32_t lowUs  = 2 * halfUs - highUs;
    uint32_t t0 = millis();
    while (millis() - t0 < durationMs)
    {
        digitalWrite(WIO_BUZZER, HIGH);
        delayMicroseconds(highUs);
        digitalWrite(WIO_BUZZER, LOW);
        delayMicroseconds(lowUs);
    }
    digitalWrite(WIO_BUZZER, LOW);
}

// -------------------------------------------------------------------------
// Semicircular arc gauge.
// pct: 0 = empty, 100 = full. fgCol: fill colour.
// Draws: fill ring, 100/200/300 cm tick dots, threshold tick (Bresenham).
// -------------------------------------------------------------------------
static void drawSonarArc(int pct, uint16_t fgCol)
{
    const int ri = ARC_R - ARC_T;   // inner radius = 96

    bool fullFill  = (pct >= 100);
    bool emptyFill = (pct <= 0);
    float cosF = 0.0f, sinF = 0.0f;
    float fillRad = 0.0f;
    // delta: triangle tip depth in radians (base-to-apex ≈ one ring thickness arc)
    const float delta = (float)ARC_T / (float)(ARC_R - ARC_T / 2);  // ≈ 0.136 rad
    if (!fullFill && !emptyFill)
    {
        fillRad = (180.0f - (float)pct * 1.8f) * 0.017453293f;
        // Scanlines stop delta short of fillRad so the triangle apex lands
        // exactly on fillRad — the combined visual fill ends at the right place.
        float drawRad = fillRad + delta;
        cosF = cosf(drawRad);
        sinF = sinf(drawRad);
    }

    // ---- Scanline fill — arc ring only, no tick handling here ----
    for (int x = ARC_CX - ARC_R; x <= ARC_CX + ARC_R; x++)
    {
        int dx  = x - ARC_CX;
        int oSq = ARC_R * ARC_R - dx * dx;
        if (oSq < 0) continue;
        int outerY = ARC_CY - (int)sqrtf((float)oSq);
        if (outerY >= ARC_CY) continue;   // upper semicircle only

        int iSq    = ri * ri - dx * dx;
        int innerY = (iSq > 0) ? (ARC_CY - (int)sqrtf((float)iSq)) : ARC_CY;
        int len    = innerY - outerY;
        if (len <= 0) continue;

        if (emptyFill) { tft.drawFastVLine(x, outerY, len, colTrack()); continue; }
        if (fullFill)  { tft.drawFastVLine(x, outerY, len, fgCol);      continue; }

        // Per-pixel angular test — same algorithm as sysStats drawGaugeArc.
        // Pixel at vertical position y is in the filled zone when its angle
        // from the centre >= fillRad, i.e. (ARC_CY-y)·cosF >= dx·sinF.
        float thr     = (float)dx * sinF;
        int   dyOuter = ARC_CY - outerY;
        int   dyInner = ARC_CY - (innerY - 1);
        bool  topFill = ((float)dyOuter * cosF >= thr);
        bool  botFill = ((float)dyInner * cosF >= thr);

        if (topFill == botFill) {
            tft.drawFastVLine(x, outerY, len, topFill ? fgCol : colTrack());
        } else {
            // Boundary crosses this column — find the split y.
            int ySplit;
            if (fabsf(cosF) > 1e-4f) {
                ySplit = ARC_CY - (int)roundf(thr / cosF);
                if (ySplit < outerY) ySplit = outerY;
                if (ySplit > innerY) ySplit = innerY;
            } else {
                // Near-vertical boundary (fill angle ≈ 90°, 12 o'clock).
                // Column is filled if it lies to the left of centre.
                ySplit = (dx <= 0) ? innerY : outerY;
            }
            if (ySplit > outerY)
                tft.drawFastVLine(x, outerY, ySplit - outerY, topFill ? fgCol : colTrack());
            if (ySplit < innerY)
                tft.drawFastVLine(x, ySplit,  innerY - ySplit, botFill ? fgCol : colTrack());
        }
    }

    // ---- Tip triangle at fill boundary ----
    // Base is at drawRad (= fillRad + delta, where scanlines stop).
    // Apex is at fillRad.  Combined, the visual fill ends exactly at fillRad.
    if (!emptyFill && !fullFill && pct >= 3 && pct <= 97)
    {
        float baseRad = fillRad + delta;  // where scanlines ended
        int x0 = ARC_CX + (int)(ARC_R * cosf(baseRad) + 0.5f);
        int y0 = ARC_CY - (int)(ARC_R * sinf(baseRad) + 0.5f);
        int x1 = ARC_CX + (int)(ri    * cosf(baseRad) + 0.5f);
        int y1 = ARC_CY - (int)(ri    * sinf(baseRad) + 0.5f);
        int x2 = ARC_CX + (int)((ARC_R - ARC_T / 2) * cosf(fillRad) + 0.5f);
        int y2 = ARC_CY - (int)((ARC_R - ARC_T / 2) * sinf(fillRad) + 0.5f);
        if (y0 < ARC_CY && y1 < ARC_CY && y2 < ARC_CY)
            tft.fillTriangle(x0, y0, x1, y1, x2, y2, fgCol);
    }

    // ---- Tick marks inside ring at 100 / 200 / 300 cm (ring-clipped) ----
    // Using the same d² bounds check as the threshold tick so no pixel can
    // escape the ring boundary and leave a stray dot.
    {
        const float tickAngDeg[3] = { 45.0f, 90.0f, 135.0f };
        const int   ri2 = ri * ri;
        const int   ro2 = ARC_R * ARC_R;
        for (int i = 0; i < 3; i++)
        {
            float rad = tickAngDeg[i] * 0.017453293f;
            float ca  = cosf(rad), sa = sinf(rad);
            for (int dx2 = -2; dx2 <= 2; dx2++)
            for (int dy2 = -2; dy2 <= 2; dy2++)
            {
                if (dx2*dx2 + dy2*dy2 > 4) continue;   // circle shape
                int mid = ARC_R - ARC_T / 2;
                int tx  = ARC_CX + (int)(mid * ca + 0.5f) + dx2;
                int ty  = ARC_CY - (int)(mid * sa + 0.5f) + dy2;
                int ddx = tx - ARC_CX, ddy = ty - ARC_CY;
                int d2  = ddx * ddx + ddy * ddy;
                if (d2 >= ri2 && d2 <= ro2 && ty < ARC_CY)
                    tft.drawPixel(tx, ty, tft.color565(0, 140, 170));
            }
        }
    }

    // ---- Alert threshold tick: radial line, 3px wide, ring-clipped ----
    // Every pixel is bounds-checked to [ri²..ARC_R²] so scanlines always
    // repaint it on the next redraw — no stray dots at arc edges.
    {
        float tPct  = (1.0f - alertThresh / DIST_MAX_CM) * 100.0f;
        float angle = (180.0f - tPct * 1.8f) * 0.017453293f;
        float ca = cosf(angle), sa = sinf(angle);
        const int ri2 = ri * ri;
        const int ro2 = ARC_R * ARC_R;
        // Step in 0.6px increments — avoids gaps at 45° where consecutive
        // integer r values can map to the same pixel.
        for (float rf = (float)ri + 0.3f; rf <= (float)ARC_R; rf += 0.6f)
        {
            int bx = ARC_CX + (int)(rf * ca + 0.5f);
            int by = ARC_CY - (int)(rf * sa + 0.5f);
            if (by >= ARC_CY) continue;
            for (int off = -1; off <= 1; off++)
            {
                int tx = bx + (int)roundf((float)off * sa);
                int ty = by - (int)roundf((float)off * ca);
                int ddx = tx - ARC_CX, ddy = ty - ARC_CY;
                int d2 = ddx * ddx + ddy * ddy;
                if (d2 >= ri2 && d2 <= ro2 && ty < ARC_CY)
                    tft.drawPixel(tx, ty, colDanger());
            }
        }
    }

    // ---- Baseline ----
    // Removed from here — now drawn once in drawSonarChrome() to prevent
    // per-frame flicker caused by the inch clear-rect overlapping this line.
}

// -------------------------------------------------------------------------
// Distance readout centred inside the arc.
// Only the targeted text cells are cleared — arc ring is never touched.
// -------------------------------------------------------------------------
static void drawSonarDistance(float dist)
{
    // Clear interior text zones — safe width at y=NUM_Y-2=122: dist-from-baseline=60,
    // inner-circle half-width=sqrt(96²-60²)≈75px → x=85–235 (width 150).
    // Height 38 closes the gap to INCH_Y=160 (122+38=160), covering error text tops.
    tft.fillRect(85, NUM_Y - 2, 150, 38, TFT_BLACK);  // number + "cm" label + top of error text
    tft.fillRect(80, INCH_Y,    160, 20, TFT_BLACK);  // inches — stops at 180, 2px before baseline

    tft.setTextDatum(TC_DATUM);

    if (dist < 0)
    {
        tft.setTextSize(4);
        tft.setTextColor(tft.color565(30, 50, 65), TFT_BLACK);
        tft.drawString("---", ARC_CX, NUM_Y);
    }
    else
    {
        uint16_t col = zoneColour(dist);

        char buf[10];
        snprintf(buf, sizeof(buf), (dist < 100.0f) ? "%.1f" : "%.0f", dist);

        tft.setTextSize(4);
        tft.setTextColor(col, TFT_BLACK);
        tft.drawString(buf, ARC_CX, NUM_Y);

        // "cm" unit — right of number at mid-height
        int numW = (int)strlen(buf) * 24;   // approx char width at size 4
        tft.setTextDatum(TL_DATUM);
        tft.setTextSize(2);
        tft.setTextColor(tft.color565(0, 140, 165), TFT_BLACK);
        tft.drawString("cm", ARC_CX + numW / 2 + 4, NUM_Y + 14);

        // Inches — size 2, same zone colour as cm for easy reading
        float inches = dist / 2.54f;
        snprintf(buf, sizeof(buf), (inches < 100.0f) ? "%.1f\"" : "%.0f\"", inches);
        tft.setTextDatum(TC_DATUM);
        tft.setTextSize(2);
        tft.setTextColor(tft.color565(0, 140, 165), TFT_BLACK);
        tft.drawString(buf, ARC_CX, INCH_Y);
    }
    tft.setTextDatum(TL_DATUM);
}

// -------------------------------------------------------------------------
// Combined arc + distance redraw (always called together).
// -------------------------------------------------------------------------
static void drawSonarReadout(float dist)
{
    drawSonarArc(distToPct(dist), zoneColour(dist));
    drawSonarDistance(dist);
}

// -------------------------------------------------------------------------
// MIN / MAX values — labels are static chrome, only values are cleared.
// -------------------------------------------------------------------------
static void drawMinMax()
{
    tft.fillRect(36,  PANEL_Y, 118, 9, TFT_BLACK);
    tft.fillRect(196, PANEL_Y, 118, 9, TFT_BLACK);

    tft.setTextSize(1);
    tft.setTextDatum(TL_DATUM);
    char buf[20];

    tft.setTextColor(colSafe(), TFT_BLACK);
    if (usMin < 9999.0f) snprintf(buf, sizeof(buf), "%.1f cm", usMin);
    else                  strcpy(buf, "---");
    tft.drawString(buf, 36, PANEL_Y);

    if (usMax >= 0) snprintf(buf, sizeof(buf), "%.1f cm", usMax);
    else             strcpy(buf, "---");
    tft.drawString(buf, 196, PANEL_Y);
}

// -------------------------------------------------------------------------
// Alert threshold value — label and hint are static chrome.
// -------------------------------------------------------------------------
static void drawAlertRow(bool active)
{
    tft.fillRect(46, ALERT_Y - 1, 142, 10, TFT_BLACK);
    tft.setTextSize(1);
    tft.setTextDatum(TL_DATUM);
    char buf[20];
    snprintf(buf, sizeof(buf), "< %.0f cm", alertThresh);
    tft.setTextColor(active ? colDanger() : colSafe(), TFT_BLACK);
    tft.drawString(buf, 46, ALERT_Y);
}

// -------------------------------------------------------------------------
// Ping counter in header — only the value cell is cleared.
// -------------------------------------------------------------------------
static void drawPingCount()
{
    // Tag zone: x=156..239, y=3..24 — stays clear of the battery widget at x=244+
    tft.fillRect(156, 3, 82, 24, tft.color565(0, 8, 20));
    char buf[20];
    snprintf(buf, sizeof(buf), "PING %05lu", (unsigned long)pingCount);
    tft.setTextSize(1);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(tft.color565(0, 80, 100), tft.color565(0, 8, 20));
    tft.drawString(buf, 160, 11);
}

// -------------------------------------------------------------------------
// Sensor-absent error state — shown after 5 consecutive measurement timeouts.
// Drawn into the same cleared region as drawSonarDistance so the arc ring is
// never touched.  Cleared automatically when a valid reading resumes.
// -------------------------------------------------------------------------
static void drawSonarError()
{
    drawSonarArc(0, colOOR());                         // empty arc
    tft.fillRect(85, NUM_Y - 2, 150, 38, TFT_BLACK);  // clear number area
    tft.fillRect(80, INCH_Y,    160, 20, TFT_BLACK);  // clear inches area

    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(2);
    tft.setTextColor(tft.color565(220, 60, 30), TFT_BLACK);
    tft.drawString("NO SENSOR", ARC_CX, 148);
    tft.setTextSize(1);
    tft.setTextColor(tft.color565(0, 148, 165), TFT_BLACK);
    tft.drawString("check SIG on A0 (yellow)", ARC_CX, 162);
    tft.setTextColor(tft.color565(0, 80, 100), TFT_BLACK);
    tft.drawString("right Grove port", ARC_CX, 172);
    tft.setTextDatum(TL_DATUM);
}

// -------------------------------------------------------------------------
// Static chrome — drawn ONCE on entry.
// -------------------------------------------------------------------------
static void drawSonarChrome()
{
    tft.fillScreen(TFT_BLACK);

    // ---- Header (same style as all other screens) ----
    tft.fillRect(0, 0, 320, HDR_H, tft.color565(0, 8, 20));
    tft.fillRect(0, 0, 3, HDR_H, tft.color565(0, 220, 245));
    tft.setTextSize(2);
    tft.setTextColor(tft.color565(0, 220, 245), tft.color565(0, 8, 20));
    tft.drawString("// SONAR", 10, 7);
    tft.drawFastHLine(0, 29, 320, tft.color565(0, 80, 100));
    for (int xi = 8; xi < 320; xi += 14)
        tft.drawFastVLine(xi, 27, 4, tft.color565(0, 140, 165));

    // Ping count placeholder
    drawPingCount();

    // ---- Range labels outside arc at 100 / 200 / 300 cm positions ----
    // 100cm → 45°, 200cm → 90°, 300cm → 135°
    // Each has a short outward tick mark (ARC_R+1 to ARC_R+7) to guide the eye,
    // then a size-2 label at ARC_R+12.
    const float labelAngDeg[3] = { 45.0f, 90.0f, 135.0f };
    const char* rangeLabels[3] = { "1M",  "2M",  "3M" };
    for (int i = 0; i < 3; i++)
    {
        float rad = labelAngDeg[i] * 0.017453293f;
        float ca  = cosf(rad), sa = sinf(rad);

        // Outward tick mark: 6 pixels extending beyond the arc outer edge
        for (int r = ARC_R + 2; r <= ARC_R + 7; r++)
        {
            int tx = ARC_CX + (int)((float)r * ca + 0.5f);
            int ty = ARC_CY - (int)((float)r * sa + 0.5f);
            if (ty < ARC_CY) tft.drawPixel(tx, ty, tft.color565(0, 160, 190));
        }

        // Label at ARC_R+12, centred on the radial direction, size 2
        int lx = ARC_CX + (int)((float)(ARC_R + 14) * ca + 0.5f) - 12;
        int ly = ARC_CY - (int)((float)(ARC_R + 14) * sa + 0.5f) - 8;
        tft.setTextSize(2);
        tft.setTextColor(tft.color565(0, 200, 230), TFT_BLACK);
        tft.drawString(rangeLabels[i], lx, ly);
    }

    // ---- Small centre dot on baseline ----
    tft.fillCircle(ARC_CX, ARC_CY, 3, tft.color565(0, 60,  90));
    tft.fillCircle(ARC_CX, ARC_CY, 1, tft.color565(0, 220, 245));

    // ---- Baseline — drawn once here so arc redraws never flicker it ----
    tft.drawFastHLine(ARC_CX - ARC_R - 1, ARC_CY, ARC_R * 2 + 2, tft.color565(0, 55, 75));

    // ---- Data panel divider ----
    tft.drawFastHLine(0, PANEL_Y - 4, 320, tft.color565(0, 40, 60));

    // ---- MIN/MAX static labels ----
    tft.setTextSize(1);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(tft.color565(0, 148, 165), TFT_BLACK);
    tft.drawString("MIN", 10,  PANEL_Y);
    tft.drawString("MAX", 170, PANEL_Y);

    // ---- Alert row static labels ----
    tft.setTextColor(tft.color565(0, 148, 165), TFT_BLACK);
    tft.drawString("ALERT", 10, ALERT_Y);
    tft.setTextColor(tft.color565(0, 60, 75), TFT_BLACK);
    tft.drawString("[UP/DN]", 220, ALERT_Y);

    // ---- Footer ----
    tft.fillRect(0, FOOTER_Y, 320, 240 - FOOTER_Y, tft.color565(0, 8, 20));
    tft.fillRect(0, FOOTER_Y, 3,   240 - FOOTER_Y, tft.color565(0, 220, 245));
    tft.setTextSize(1);
    tft.setTextColor(tft.color565(0, 100, 118), tft.color565(0, 8, 20));
    tft.drawString("[PRESS] RESET MIN/MAX", 8,   FOOTER_Y + 5);
    tft.drawString("[C] BACK",             264,  FOOTER_Y + 5);
}

// =========================================================================
// Public entry point
// =========================================================================
void ultrasonicScreen()
{
    while (digitalRead(WIO_5S_PRESS) == LOW) { delay(10); }

    pinMode(SIG_PIN, OUTPUT);       // measureDistance() will toggle as needed
    digitalWrite(SIG_PIN, LOW);
    pinMode(WIO_BUZZER, OUTPUT);
    digitalWrite(WIO_BUZZER, LOW);

    // Reset per-session state
    usMin       = 9999.0f;
    usMax       = -1.0f;
    alertThresh = ALERT_DEFAULT;
    pingCount   = 0;

    float    lastDist   = -2.0f;    // sentinel: force first draw
    int      lastPct    = -99;
    uint16_t lastCol    = 0;
    bool     prevAlert  = false;
    uint32_t lastBeepMs = 0;
    uint32_t lastMeasMs = 0;
    int      consecutiveTimeouts = 0;
    int      consecutiveValid    = 0;
    bool     errorShown          = false;
    bool     everGotReading      = false;  // once true, timeouts show --- not NO SENSOR

    drawSonarChrome();
    drawBatteryStatus(tft.color565(0, 8, 20));
    drawSonarReadout(-1.0f);
    drawMinMax();
    drawAlertRow(false);

    while (true)
    {
        // KEY_C — back to menu
        if (digitalRead(WIO_KEY_C) == LOW)
        {
            while (digitalRead(WIO_KEY_C) == LOW) { delay(10); }
            delay(50);
            return;
        }

        // PRESS — reset MIN / MAX
        if (digitalRead(WIO_5S_PRESS) == LOW)
        {
            usMin = 9999.0f;
            usMax = -1.0f;
            while (digitalRead(WIO_5S_PRESS) == LOW) { delay(10); }
            drawMinMax();
        }

        // UP / DOWN — adjust alert threshold
        if (digitalRead(WIO_5S_UP) == LOW)
        {
            alertThresh += 5.0f;
            if (alertThresh > ALERT_MAX_CM) alertThresh = ALERT_MAX_CM;
            drawSonarArc(distToPct(lastDist), zoneColour(lastDist));
            drawAlertRow(lastDist > 0 && lastDist < alertThresh);
            delay(200);
        }
        if (digitalRead(WIO_5S_DOWN) == LOW)
        {
            alertThresh -= 5.0f;
            if (alertThresh < ALERT_MIN_CM) alertThresh = ALERT_MIN_CM;
            drawSonarArc(distToPct(lastDist), zoneColour(lastDist));
            drawAlertRow(lastDist > 0 && lastDist < alertThresh);
            delay(200);
        }

        // ---- Measure every 100 ms ----
        if (millis() - lastMeasMs >= 100)
        {
            lastMeasMs = millis();
            float dist = measureDistance();
            pingCount++;

            // Sensor-absent detection: show error overlay after 5 timeouts.
            // Once a valid reading has been seen this session, timeouts are
            // "out of range" not "no sensor" — just show --- without overlay.
            if (dist < 0)
            {
                consecutiveValid = 0;
                if (!everGotReading && ++consecutiveTimeouts >= 5 && !errorShown)
                {
                    errorShown = true;
                    drawSonarError();
                    lastDist = -1.0f;
                    lastPct  = 0;
                    lastCol  = colOOR();
                }
            }
            else
            {
                everGotReading      = true;
                consecutiveTimeouts = 0;
                if (errorShown)
                {
                    if (++consecutiveValid >= 3)
                    {
                        errorShown       = false;
                        consecutiveValid = 0;
                        lastDist         = -2.0f;  // force full readout redraw
                    }
                }
                else
                {
                    consecutiveValid = 0;
                }
            }

            // Update MIN / MAX
            bool mmChanged = false;
            if (dist > 0)
            {
                if (dist < usMin) { usMin = dist; mmChanged = true; }
                if (dist > usMax) { usMax = dist; mmChanged = true; }
            }

            // Parking-sensor beep
            bool nowAlert = (dist > 0 && dist < alertThresh);
            if (nowAlert)
            {
                float    ratio    = dist / alertThresh;
                uint32_t interval = (ratio < 0.25f) ? 150 :
                                    (ratio < 0.50f) ? 300 :
                                    (ratio < 0.75f) ? 550 : 900;
                if (millis() - lastBeepMs >= interval)
                {
                    lastBeepMs = millis();
                    shortBeep(40);
                }
            }

            bool alertChanged = (nowAlert != prevAlert);
            prevAlert = nowAlert;

            // Selective redraw — arc + distance only when value/colour changes
            // and only when the error overlay is not being shown.
            int      newPct = distToPct(dist);
            uint16_t newCol = zoneColour(dist);
            bool distChanged = (fabsf(dist - lastDist) > 0.4f ||
                                (dist <  0 && lastDist >= 0)   ||
                                (dist >= 0 && lastDist <  0));
            bool arcChanged  = distChanged || (newPct != lastPct) || (newCol != lastCol);

            if (!errorShown && arcChanged)
            {
                lastDist = dist;
                lastPct  = newPct;
                lastCol  = newCol;
                drawSonarReadout(dist);
            }
            if (alertChanged) drawAlertRow(nowAlert);
            if (mmChanged)    drawMinMax();
            drawPingCount();
        }

        delay(10);
    }
}
