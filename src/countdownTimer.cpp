// countdownTimer.cpp — Countdown timer.
//
// Controls:
//   [UP] / [DOWN]    Increment / decrement selected field (when stopped)
//   [LEFT] / [RIGHT] Cycle between HH / MM / SS fields
//   [PRESS]          Start / Pause  (when DONE: reset to setup)
//   [LEFT]           Reset to set time (when paused or done)
//   [C]              Back to menu

#include <Arduino.h>
#include "globals.h"

enum CdState { CD_SETUP, CD_RUNNING, CD_PAUSED, CD_DONE };

static CdState  cdState    = CD_SETUP;
static int      cdSetH     = 0;      // hours configured
static int      cdSetM     = 5;      // minutes configured (default 5 min)
static int      cdSetS     = 0;      // seconds configured
static int      cdRemH     = 0;      // hours remaining
static int      cdRemM     = 5;      // minutes remaining
static int      cdRemS     = 0;      // seconds remaining
static int      cdField    = 1;      // 0=HH 1=MM 2=SS; start on minutes
static uint32_t cdLastMs   = 0;      // millis() at last tick
static int      cdTotalSec = 300;    // snapshot at start, for progress bar

// Colour helpers — not compile-time constants; tft object must be ready first.
static uint16_t cdAcc()    { return tft.color565(210, 155,  0); }   // amber
static uint16_t cdDim()    { return tft.color565(140, 100,  0); }   // dim amber
static uint16_t cdFooter() { return tft.color565( 90,  65,  0); }   // dark amber
static uint16_t cdWarn()   { return tft.color565(210,  55, 20); }   // red-orange (expired)

// -------------------------------------------------------------------------
// Bit-bang the buzzer at ~2200 Hz.
// Avoids tone()/noTone() which reconfigure TC0 — the same timer used by the
// LCD backlight PWM driver — causing the screen to go dark.
static void cdBeep()
{
    if (g_volume == 0) return;
    // 4000 Hz — half-period = 125 µs.  Duty cycle scales with g_volume.
    const uint32_t halfUs = 125;
    uint32_t highUs = halfUs * (uint32_t)g_volume / 4;  // vol 1=31µs, 4=125µs
    uint32_t lowUs  = 2 * halfUs - highUs;
    pinMode(WIO_BUZZER, OUTPUT);
    for (int b = 0; b < 5; b++) {
        uint32_t t0 = millis();
        while (millis() - t0 < 200) {   // 200 ms tone
            digitalWrite(WIO_BUZZER, HIGH);
            delayMicroseconds(highUs);
            digitalWrite(WIO_BUZZER, LOW);
            delayMicroseconds(lowUs);
        }
        delay(100);   // silence between beeps
    }
    digitalWrite(WIO_BUZZER, LOW);
}

// -------------------------------------------------------------------------
static char cdPrevBuf[10] = "";

static void drawCdTime(bool force = false)
{
    char buf[10];
    sprintf(buf, "%02d:%02d:%02d", cdRemH, cdRemM, cdRemS);
    if (!force && strcmp(buf, cdPrevBuf) == 0) return;
    strcpy(cdPrevBuf, buf);

    uint16_t col = (cdState == CD_DONE)    ? cdWarn() :
                   (cdState == CD_RUNNING)  ? cdAcc()  :
                                              tft.color565(170, 120, 0);

    tft.setTextSize(5);
    tft.setTextColor(col, TFT_BLACK);
    // "HH:MM:SS" at size 5: 8 chars × 30px = 240px → centred: x=40
    tft.drawString(buf, 40, 50);

    // Field underlines (3px) — SETUP mode only.
    // Size 5: HH at x=40 (60px), MM at x=130 (60px), SS at x=220 (60px)
    if (cdState == CD_SETUP)
    {
        const int fx[3] = { 40, 130, 220 };
        for (int i = 0; i < 3; i++)
            tft.fillRect(fx[i], 93, 60, 3, (i == cdField) ? cdAcc() : TFT_BLACK);

        // Field labels centred under each segment (centres: HH=70, MM=160, SS=250)
        static const char* const fLabels[3] = { "HOURS", "MINUTES", "SECONDS" };
        const int cx[3] = { 70, 160, 250 };
        tft.setTextSize(1);
        for (int i = 0; i < 3; i++)
        {
            uint16_t lc = (i == cdField) ? cdAcc() : tft.color565(140, 105, 0);
            tft.setTextColor(lc, TFT_BLACK);
            int lw = (int)strlen(fLabels[i]) * 6;
            tft.fillRect(cx[i] - 42, 100, 84, 10, TFT_BLACK);
            tft.drawString(fLabels[i], cx[i] - lw / 2, 101);
        }
    }
    else
    {
        tft.fillRect(40, 93, 240, 3, TFT_BLACK);
    }
}

// -------------------------------------------------------------------------
static void drawCdProgress()
{
    if (cdState == CD_SETUP) return;

    int rem  = cdRemH * 3600 + cdRemM * 60 + cdRemS;
    int bw   = 280;
    int fill = (cdTotalSec > 0) ? (int)((long)rem * bw / cdTotalSec) : 0;
    if (fill < 0)  fill = 0;
    if (fill > bw) fill = bw;

    uint16_t bc = (cdTotalSec > 0 && rem <= cdTotalSec / 4) ? cdWarn() : cdAcc();
    tft.fillRect(20,        115, fill,      8, bc);
    tft.fillRect(20 + fill, 115, bw - fill, 8, tft.color565(20, 15, 0));
    tft.drawRect(19, 114, bw + 2, 10, tft.color565(50, 38, 0));
}

// -------------------------------------------------------------------------
// Updates only the status badge — avoids fillScreen flicker on pause/unpause.
static void drawCdStatus()
{
    tft.fillRect(0, 32, 160, 14, TFT_BLACK);
    const char* label;
    uint16_t    sc;
    switch (cdState) {
        case CD_RUNNING: label = "\xB7 RUNNING"; sc = cdAcc();                    break;
        case CD_PAUSED:  label = "| PAUSED";    sc = tft.color565(170, 120, 0);   break;
        case CD_DONE:    label = "! DONE";      sc = cdWarn();                    break;
        default:         label = "  SETTING";   sc = cdDim();                     break;
    }
    tft.setTextSize(1);
    tft.setTextColor(sc, TFT_BLACK);
    tft.drawString(label, 12, 35);
}

// -------------------------------------------------------------------------
static void drawCdFrame()
{
    tft.fillScreen(TFT_BLACK);

    // Header
    tft.fillRect(0, 0, 320, 30, tft.color565(14, 11, 0));
    tft.fillRect(0, 0, 3, 30, cdAcc());
    tft.setTextSize(2);
    tft.setTextColor(cdAcc(), tft.color565(14, 11, 0));
    tft.drawString("// COUNTDOWN", 10, 7);
    tft.setTextSize(1);
    tft.setTextColor(cdDim(), tft.color565(14, 11, 0));
    tft.drawString("TIMER", 160, 11);
    tft.drawFastHLine(0, 29, 320, tft.color565(50, 38, 0));
    for (int xi = 8; xi < 320; xi += 14)
        tft.drawFastVLine(xi, 27, 4, tft.color565(100, 75, 0));

    // Status badge
    const char* label;
    uint16_t    sc;
    switch (cdState) {
        case CD_RUNNING: label = "\xB7 RUNNING"; sc = cdAcc();                    break;
        case CD_PAUSED:  label = "| PAUSED";    sc = tft.color565(170, 120, 0);   break;
        case CD_DONE:    label = "! DONE";      sc = cdWarn();                    break;
        default:         label = "  SETTING";   sc = cdDim();                     break;
    }
    tft.setTextSize(1);
    tft.setTextColor(sc, TFT_BLACK);
    tft.drawString(label, 12, 35);

    // Content below time display
    if (cdState == CD_SETUP)
    {
        // Field labels are drawn by drawCdTime(); just the hint line here.
        tft.setTextSize(1);
        tft.setTextColor(tft.color565(150, 110, 0), TFT_BLACK);
        tft.drawString("[U/D] ADJUST   [L/R] FIELD   [PRESS] START", 8, 125);
    }
    else if (cdState == CD_DONE)
    {
        tft.setTextSize(3);
        tft.setTextColor(cdWarn(), TFT_BLACK);
        // "TIME'S UP!" = 10 chars × 18px = 180px; centre: x=(320-180)/2=70
        tft.drawString("TIME'S UP!", 70, 140);
        tft.setTextSize(1);
        tft.setTextColor(tft.color565(180, 70, 30), TFT_BLACK);
        tft.drawString("[PRESS] to reset   [L] to re-edit", 42, 178);
    }
    else
    {
        // RUNNING / PAUSED — show the original target time.
        tft.setTextSize(1);
        tft.setTextColor(tft.color565(150, 110, 0), TFT_BLACK);
        tft.drawString("TARGET", 12, 135);
        char setbuf[10];
        sprintf(setbuf, "%02d:%02d:%02d", cdSetH, cdSetM, cdSetS);
        tft.setTextSize(3);
        tft.setTextColor(cdDim(), TFT_BLACK);
        // HH:MM:SS at size 3: 8 chars × 18px = 144px; centre: x=(320-144)/2=88
        tft.drawString(setbuf, 88, 148);
    }

    drawCdProgress();

    // Footer
    tft.fillRect(0, 219, 3, 21, cdAcc());
    tft.setTextSize(1);
    tft.setTextColor(cdFooter(), TFT_BLACK);
    tft.drawString("[PRESS] START/PAUSE  [L] RESET  [C] BACK", 8, 225);

    drawBatteryStatus(TFT_BLACK);

    cdPrevBuf[0] = '\0';   // force time redraw after fillScreen
    drawCdTime(true);
}

// -------------------------------------------------------------------------
void countdownTimerScreen()
{
    while (digitalRead(WIO_5S_PRESS) == LOW) delay(10);

    // Always enter in setup; preserve the last values the user dialled in.
    cdState = CD_SETUP;
    cdRemH  = cdSetH;
    cdRemM  = cdSetM;
    cdRemS  = cdSetS;
    cdField = 1;   // default cursor on minutes

    bool frameNeeded = true;

    while (true)
    {
        if (frameNeeded) { drawCdFrame(); frameNeeded = false; }

        // --- Tick (running only) ---
        if (cdState == CD_RUNNING)
        {
            uint32_t now = millis();
            if (now - cdLastMs >= 1000)
            {
                cdLastMs = now;
                if (cdRemS > 0) {
                    cdRemS--;
                } else if (cdRemM > 0) {
                    cdRemM--; cdRemS = 59;
                } else if (cdRemH > 0) {
                    cdRemH--; cdRemM = 59; cdRemS = 59;
                } else {
                    cdState = CD_DONE;
                    cdBeep();
                    frameNeeded = true;
                }
                if (cdState == CD_RUNNING) {
                    drawCdTime();
                    drawCdProgress();
                }
            }
        }

        // --- Input ---

        // [PRESS]: start → pause → resume → (done) reset
        if (digitalRead(WIO_5S_PRESS) == LOW)
        {
            while (digitalRead(WIO_5S_PRESS) == LOW) delay(10);
            switch (cdState) {
                case CD_SETUP:
                    if (cdSetH > 0 || cdSetM > 0 || cdSetS > 0) {
                        cdTotalSec = cdSetH * 3600 + cdSetM * 60 + cdSetS;
                        cdRemH     = cdSetH;
                        cdRemM     = cdSetM;
                        cdRemS     = cdSetS;
                        cdState    = CD_RUNNING;
                        cdLastMs   = millis();
                        frameNeeded = true;
                    }
                    break;
                case CD_RUNNING:
                    cdState = CD_PAUSED;
                    // Partial update: only badge + timer colour change — no fillScreen.
                    drawCdStatus();
                    drawCdTime(true);
                    break;
                case CD_PAUSED:
                    cdState  = CD_RUNNING;
                    cdLastMs = millis();
                    // Partial update: only badge + timer colour change — no fillScreen.
                    drawCdStatus();
                    drawCdTime(true);
                    break;
                case CD_DONE:
                    cdState = CD_SETUP;
                    cdRemH  = cdSetH;
                    cdRemM  = cdSetM;
                    cdRemS  = cdSetS;
                    frameNeeded = true;
                    break;
            }
        }

        // [LEFT]: cycle field backward (setup) or reset (paused/done)
        else if (digitalRead(WIO_5S_LEFT) == LOW)
        {
            while (digitalRead(WIO_5S_LEFT) == LOW) delay(10);
            if (cdState == CD_SETUP) {
                cdField = (cdField + 2) % 3;   // -1 mod 3
                drawCdTime(true);              // force: only underline changed
            } else if (cdState == CD_PAUSED || cdState == CD_DONE) {
                cdState = CD_SETUP;
                cdRemH  = cdSetH;
                cdRemM  = cdSetM;
                cdRemS  = cdSetS;
                frameNeeded = true;
            }
        }

        // [RIGHT]: cycle field forward (setup only)
        else if (digitalRead(WIO_5S_RIGHT) == LOW)
        {
            while (digitalRead(WIO_5S_RIGHT) == LOW) delay(10);
            if (cdState == CD_SETUP) {
                cdField = (cdField + 1) % 3;
                drawCdTime(true);   // force: only underline changed
            }
        }

        // [UP]: increment selected field (setup only) — hold for auto-repeat
        else if (digitalRead(WIO_5S_UP) == LOW && cdState == CD_SETUP)
        {
            auto stepUp = [&]() {
                if      (cdField == 0) { cdSetH = (cdSetH + 1)  % 24; cdRemH = cdSetH; }
                else if (cdField == 1) { cdSetM = (cdSetM + 1)  % 60; cdRemM = cdSetM; }
                else                   { cdSetS = (cdSetS + 1)  % 60; cdRemS = cdSetS; }
                drawCdTime(true);
            };
            stepUp();
            uint32_t pressedAt = millis(), lastRep = millis();
            while (digitalRead(WIO_5S_UP) == LOW) {
                uint32_t now = millis();
                if (now - pressedAt >= 400 && now - lastRep >= 80) {
                    stepUp(); lastRep = now;
                }
                delay(10);
            }
        }

        // [DOWN]: decrement selected field (setup only) — hold for auto-repeat
        else if (digitalRead(WIO_5S_DOWN) == LOW && cdState == CD_SETUP)
        {
            auto stepDown = [&]() {
                if      (cdField == 0) { cdSetH = (cdSetH + 23) % 24; cdRemH = cdSetH; }
                else if (cdField == 1) { cdSetM = (cdSetM + 59) % 60; cdRemM = cdSetM; }
                else                   { cdSetS = (cdSetS + 59) % 60; cdRemS = cdSetS; }
                drawCdTime(true);
            };
            stepDown();
            uint32_t pressedAt = millis(), lastRep = millis();
            while (digitalRead(WIO_5S_DOWN) == LOW) {
                uint32_t now = millis();
                if (now - pressedAt >= 400 && now - lastRep >= 80) {
                    stepDown(); lastRep = now;
                }
                delay(10);
            }
        }

        // [B]: screenshot
        else if (digitalRead(WIO_KEY_B) == LOW)
        {
            while (digitalRead(WIO_KEY_B) == LOW) delay(10);
            takeScreenshot();
        }

        // [C]: back to menu (pause if running)
        else if (digitalRead(WIO_KEY_C) == LOW)
        {
            if (cdState == CD_RUNNING) cdState = CD_PAUSED;
            while (digitalRead(WIO_KEY_C) == LOW) delay(10);
            delay(50);
            return;
        }

        delay(20);
    }
}
