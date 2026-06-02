// pomodoro.cpp — Classic Pomodoro focus timer.
//
// Cycle: 4× (WORK 25 min → SHORT BREAK 5 min) → LONG BREAK 15 min → repeat.
//
// Controls:
//   [PRESS]   Start / Pause
//   [LEFT]    Skip current phase (advance immediately)
//   [RIGHT]   Reset current phase countdown
//   [C]       Back to menu

#include <Arduino.h>
#include "globals.h"

#ifndef WIO_BUZZER
#define WIO_BUZZER 15   // PB08 on SAMD51 — on-board piezo buzzer
#endif

// Phase IDs
#define POM_WORK  0
#define POM_SHORT 1
#define POM_LONG  2

// Session durations in milliseconds
static const uint32_t POM_DUR[3] = {
    25UL * 60 * 1000,   //  WORK       25 min
     5UL * 60 * 1000,   //  SHORT BREAK  5 min
    15UL * 60 * 1000,   //  LONG BREAK  15 min
};
static const int POM_CYCLE = 4;   // work sessions before a long break

// State (persists across menu navigation so a timer isn't lost)
static int      pomPhase   = POM_WORK;
static int      pomCount   = 0;      // completed work sessions this cycle
static bool     pomRunning = false;
static uint32_t pomStart   = 0;      // millis() when last resumed
static uint32_t pomElapMs  = 0;      // accumulated ms while paused

// -------------------------------------------------------------------------
static uint32_t pomElapsed()   { return pomRunning ? pomElapMs + (millis() - pomStart) : pomElapMs; }
static uint32_t pomTimeLeft()  { uint32_t e = pomElapsed(); return e >= POM_DUR[pomPhase] ? 0 : POM_DUR[pomPhase] - e; }

static void pomFormat(char* buf, uint32_t ms)
{
    uint32_t secs = (ms + 999) / 1000;
    if (secs > 5999) secs = 5999;
    uint8_t m = (uint8_t)(secs / 60);
    uint8_t s = (uint8_t)(secs % 60);
    buf[0] = '0' + m / 10;
    buf[1] = '0' + m % 10;
    buf[2] = ':';
    buf[3] = '0' + s / 10;
    buf[4] = '0' + s % 10;
    buf[5] = '\0';
}

// -------------------------------------------------------------------------
// Phase-specific colour palette

static uint16_t pomAccent()
{
    switch (pomPhase) {
        case POM_WORK:  return tft.color565(220, 80,  40);   // red-orange
        case POM_SHORT: return tft.color565( 60, 210, 100);  // green
        case POM_LONG:  return tft.color565(  0, 210, 230);  // cyan
    }
    return tft.color565(0, 220, 245);
}

static uint16_t pomAccentDim()
{
    switch (pomPhase) {
        case POM_WORK:  return tft.color565(165, 65, 28);
        case POM_SHORT: return tft.color565( 55, 168, 80);
        case POM_LONG:  return tft.color565(  0, 158, 178);
    }
    return tft.color565(0, 148, 170);
}

static uint16_t pomFooterColor()
{
    switch (pomPhase) {
        case POM_WORK:  return tft.color565(110, 43, 19);
        case POM_SHORT: return tft.color565( 37, 112, 54);
        case POM_LONG:  return tft.color565(  0, 106, 119);
    }
    return tft.color565(0, 100, 118);
}

static uint16_t pomHeaderBg()
{
    switch (pomPhase) {
        case POM_WORK:  return tft.color565(20,  5,  2);
        case POM_SHORT: return tft.color565( 2, 18,  5);
        case POM_LONG:  return tft.color565( 2,  8, 18);
    }
    return tft.color565(0, 8, 20);
}

static const char* pomPhaseName()
{
    switch (pomPhase) {
        case POM_WORK:  return "WORK";
        case POM_SHORT: return "SHORT BREAK";
        case POM_LONG:  return "LONG BREAK";
    }
    return "";
}

static const char* pomNextPhaseName()
{
    if (pomPhase == POM_WORK)
        return (pomCount + 1 >= POM_CYCLE) ? "LONG BREAK" : "SHORT BREAK";
    return "WORK";
}

// -------------------------------------------------------------------------
// Software tone: bit-bangs the buzzer GPIO directly, no hardware timer involved.
// Avoids any conflict with the TC0-based LCD backlight PWM.
static void pomBeep(uint16_t freqHz, uint16_t durationMs)
{
    if (g_volume == 0) return;
    uint32_t halfUs = 500000UL / freqHz;
    uint32_t highUs = halfUs * (uint32_t)g_volume / 4;  // vol 1=12.5% duty, 4=50%
    uint32_t lowUs  = 2 * halfUs - highUs;
    uint32_t endMs  = millis() + durationMs;
    while ((int32_t)(millis() - (int32_t)endMs) < 0) {
        digitalWrite(WIO_BUZZER, HIGH);
        delayMicroseconds(highUs);
        digitalWrite(WIO_BUZZER, LOW);
        delayMicroseconds(lowUs);
    }
}

static void pomAlert(int beeps)
{
    pinMode(WIO_BUZZER, OUTPUT);
    for (int i = 0; i < beeps; i++) {
        pomBeep(1200, 150);
        delay(80);
    }
    digitalWrite(WIO_BUZZER, LOW);
}

static void pomAdvance()
{
    pomElapMs  = 0;
    pomStart   = 0;
    pomRunning = false;

    if (pomPhase == POM_WORK) {
        pomCount++;
        if (pomCount >= POM_CYCLE) {
            pomPhase = POM_LONG;
            pomCount = 0;
            pomAlert(3);
        } else {
            pomPhase = POM_SHORT;
            pomAlert(2);
        }
    } else {
        pomPhase = POM_WORK;
        pomAlert(2);
    }
}

// -------------------------------------------------------------------------
// Update only the countdown and progress bar — called every loop tick.
static char pomPrevBuf[8] = "";
static int  pomPrevFill  = -1;   // last painted fill width; -1 = needs full draw

static void drawPomFrame()
{
    uint16_t ac  = pomAccent();
    uint16_t acd = pomAccentDim();
    uint16_t hbg = pomHeaderBg();

    tft.fillScreen(TFT_BLACK);

    // --- Header ---
    tft.fillRect(0, 0, 320, 30, hbg);
    tft.fillRect(0, 0, 3, 30, ac);
    tft.setTextSize(2);
    tft.setTextColor(ac, hbg);
    tft.drawString("// POMODORO", 10, 7);
    tft.setTextSize(1);
    tft.setTextColor(acd, hbg);
    tft.drawString("FOCUS", 160, 11);
    tft.drawFastHLine(0, 29, 320, acd);
    for (int xi = 8; xi < 320; xi += 14)
        tft.drawFastVLine(xi, 27, 4, ac);

    // --- Phase name + cycle dots ---
    tft.setTextSize(1);
    tft.setTextColor(ac, TFT_BLACK);
    tft.drawString(pomPhaseName(), 12, 35);

    // 4 cycle dots on the right (filled = completed work sessions)
    for (int d = 0; d < POM_CYCLE; d++) {
        int dx = 216 + d * 17;
        if (d < pomCount)
            tft.fillRect(dx, 33, 10, 10, ac);
        else
            tft.drawRect(dx, 33, 10, 10, acd);
    }

    // Session or break label
    tft.setTextSize(1);
    tft.setTextColor(acd, TFT_BLACK);
    if (pomPhase == POM_WORK) {
        char sb[24]; sprintf(sb, "SESSION %d OF %d", pomCount + 1, POM_CYCLE);
        tft.drawString(sb, 12, 47);
    } else {
        tft.drawString(pomPhase == POM_SHORT ? "5 MIN RECHARGE" : "15 MIN RECHARGE", 12, 47);
    }

    // --- Big countdown (timer is drawn by drawPomTick) ---
    // placeholder space: y=58 to y=90 (size 4, 32 px tall)

    // --- Progress bar outline (fill updated incrementally by drawPomTick) ---
    tft.fillRect(10, 100, 300, 10, TFT_BLACK);   // clear interior
    tft.drawRect( 9,  99, 302, 12, acd);          // border 1 px outside fill area
    pomPrevFill = -1;                             // force fresh fill on first tick

    // --- Divider ---
    tft.drawFastHLine(0, 116, 320, acd);
    tft.drawFastVLine(0,   116, 1, ac);
    tft.drawFastVLine(319, 116, 1, ac);

    // --- Next phase hint ---
    tft.setTextSize(1);
    tft.setTextColor(acd, TFT_BLACK);
    char nb[32]; sprintf(nb, "NEXT:  %s", pomNextPhaseName());
    tft.drawString(nb, 12, 124);

    // --- Status prompt ---
    tft.setTextSize(1);
    if (pomRunning) {
        tft.setTextColor(ac, TFT_BLACK);
        tft.drawString(">> TIMER RUNNING", 12, 140);
    } else if (pomElapMs > 0) {
        tft.setTextColor(tft.color565(210, 175, 0), TFT_BLACK);
        tft.drawString("|| PAUSED — PRESS TO RESUME", 12, 140);
    } else {
        tft.setTextColor(acd, TFT_BLACK);
        tft.drawString("PRESS TO BEGIN", 12, 140);
    }

    // --- Footer ---
    tft.fillRect(0, 219, 3, 21, ac);
    tft.setTextSize(1);
    tft.setTextColor(pomFooterColor(), TFT_BLACK);
    tft.drawString("[PRESS] RUN/STOP  [L] SKIP  [R] RESET  [C] BACK", 8, 225);

    drawBatteryStatus(TFT_BLACK);
}

// -------------------------------------------------------------------------
static void drawPomTick()
{
    uint16_t ac  = pomAccent();
    uint32_t tl  = pomTimeLeft();

    // Big countdown — "MM:SS" (5 chars at size 4 ≈ 125 px → centre x=98)
    char buf[8]; pomFormat(buf, tl);
    if (strcmp(buf, pomPrevBuf) != 0 || !pomRunning) {
        strcpy(pomPrevBuf, buf);
        uint16_t tc = pomRunning ? ac : tft.color565(175, 200, 210);
        tft.setTextSize(4);
        tft.setTextColor(tc, TFT_BLACK);
        tft.drawString(buf, 98, 58);
    }

    // Progress bar — incremental repaint; border already drawn in drawPomFrame().
    // Only the changed segment is touched, eliminating erase-then-redraw flicker.
    float pct = POM_DUR[pomPhase] > 0
                ? (float)pomElapsed() / (float)POM_DUR[pomPhase]
                : 0.0f;
    if (pct > 1.0f) pct = 1.0f;
    int fill = (int)(300 * pct);

    if (fill > pomPrevFill) {
        // Bar grew: paint only the new segment
        int x = (pomPrevFill < 0) ? 10 : 10 + pomPrevFill;
        int w = (pomPrevFill < 0) ? fill : fill - pomPrevFill;
        if (w > 0) tft.fillRect(x, 100, w, 10, ac);
    } else if (fill < pomPrevFill) {
        // Bar shrank (reset / phase change): erase the vacated segment
        tft.fillRect(10 + fill, 100, pomPrevFill - fill, 10, TFT_BLACK);
    }
    pomPrevFill = fill;
}

// -------------------------------------------------------------------------
// Updates only the status badge — avoids fillScreen flicker on pause/unpause.
static void drawPomStatus()
{
    tft.fillRect(0, 136, 320, 16, TFT_BLACK);
    tft.setTextSize(1);
    if (pomRunning) {
        tft.setTextColor(pomAccent(), TFT_BLACK);
        tft.drawString(">> TIMER RUNNING", 12, 140);
    } else if (pomElapMs > 0) {
        tft.setTextColor(tft.color565(210, 175, 0), TFT_BLACK);
        tft.drawString("|| PAUSED \u2014 PRESS TO RESUME", 12, 140);
    } else {
        tft.setTextColor(pomAccentDim(), TFT_BLACK);
        tft.drawString("PRESS TO BEGIN", 12, 140);
    }
}

// -------------------------------------------------------------------------
void pomodoroScreen()
{
    while (digitalRead(WIO_5S_PRESS) == LOW) delay(10);

    // Reset to fresh state on each entry
    pomPhase   = POM_WORK;
    pomCount   = 0;
    pomRunning = false;
    pomElapMs  = 0;
    pomPrevBuf[0] = '\0';

    bool     frameNeeded = true;
    uint32_t lastSec     = 0xFFFFFFFF;

    while (true)
    {
        if (frameNeeded) { drawPomFrame(); drawPomTick(); lastSec = pomTimeLeft() / 1000; frameNeeded = false; }

        // Phase complete?
        if (pomRunning && pomTimeLeft() == 0) {
            pomAdvance();
            pomPrevBuf[0] = '\0';
            frameNeeded = true;
            continue;
        }

        // Tick the timer display every second
        uint32_t tl  = pomTimeLeft();
        uint32_t sec = tl / 1000;
        if (sec != lastSec || pomRunning) {
            drawPomTick();
            lastSec = sec;
        }

        // --- Input ---
        if (digitalRead(WIO_5S_PRESS) == LOW)
        {
            if (pomRunning) { pomElapMs = pomElapsed(); pomRunning = false; }
            else            { pomStart  = millis();     pomRunning = true;  }
            while (digitalRead(WIO_5S_PRESS) == LOW) delay(10);
            // Partial update: only badge + timer colour change — no fillScreen.
            pomPrevBuf[0] = '\0';   // force timer colour refresh in drawPomTick
            drawPomStatus();
            drawPomTick();
            lastSec = pomTimeLeft() / 1000;
        }
        else if (digitalRead(WIO_5S_LEFT) == LOW)   // skip phase
        {
            while (digitalRead(WIO_5S_LEFT) == LOW) delay(10);
            pomAdvance();
            pomPrevBuf[0] = '\0';
            drawPomFrame();
            drawPomTick();
            lastSec = pomTimeLeft() / 1000;
        }
        else if (digitalRead(WIO_5S_RIGHT) == LOW)  // reset phase
        {
            while (digitalRead(WIO_5S_RIGHT) == LOW) delay(10);
            pomElapMs  = 0;
            pomRunning = false;
            pomPrevBuf[0] = '\0';
            drawPomFrame();
            drawPomTick();
            lastSec = pomTimeLeft() / 1000;
        }
        else if (digitalRead(WIO_KEY_B) == LOW)
        {
            while (digitalRead(WIO_KEY_B) == LOW) delay(10);
            takeScreenshot();
        }
        else if (digitalRead(WIO_KEY_C) == LOW)
        {
            // Preserve timer state so user can return mid-session
            while (digitalRead(WIO_KEY_C) == LOW) delay(10);
            delay(50);
            return;
        }

        delay(20);
    }
}
