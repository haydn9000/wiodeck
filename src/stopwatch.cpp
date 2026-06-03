// stopwatch.cpp — Precision stopwatch with lap splits.
//
// Controls:
//   [PRESS]       Start / Pause
//   [RIGHT]       Record lap (while running, up to 8 laps)
//   [LEFT]        Reset (while paused)
//   [C]           Back to menu

#include <Arduino.h>
#include "globals.h"

static bool     swRunning  = false;
static uint32_t swStartMs  = 0;
static uint32_t swElapsed  = 0;      // accumulated ms when paused
static uint32_t swLaps[8];
static int      swLapCount = 0;

static uint32_t swCurrentMs()
{
    return swRunning ? swElapsed + (millis() - swStartMs) : swElapsed;
}

static void swFormatTime(char* buf, uint32_t ms)
{
    int cs  = (int)((ms / 10) % 100);
    int sec = (int)((ms / 1000) % 60);
    int mn  = (int)((ms / 60000) % 100);
    sprintf(buf, "%02d:%02d.%02d", mn, sec, cs);
}

static void swReset()
{
    swRunning  = false;
    swElapsed  = 0;
    swStartMs  = 0;
    swLapCount = 0;
}

// -------------------------------------------------------------------------
// Draw the static chrome: header, divider, lap list, footer.
// Called only on state changes to avoid full-screen flicker.
static void drawStopwatchFrame()
{
    tft.fillScreen(TFT_BLACK);

    // Header
    tft.fillRect(0, 0, 320, 30, tft.color565(0, 8, 20));
    tft.fillRect(0, 0, 3, 30, tft.color565(0, 220, 245));
    tft.setTextSize(2);
    tft.setTextColor(tft.color565(0, 220, 245), tft.color565(0, 8, 20));
    tft.drawString("// STOPWATCH", 10, 7);
    tft.setTextSize(1);
    tft.setTextColor(tft.color565(0, 148, 170), tft.color565(0, 8, 20));
    tft.drawString("LAP TIMER", 160, 11);
    tft.drawFastHLine(0, 29, 320, tft.color565(0, 80, 100));
    for (int xi = 8; xi < 320; xi += 14)
        tft.drawFastVLine(xi, 27, 4, tft.color565(0, 140, 165));

    // Status badge
    uint16_t stCol;
    const char* stLabel;
    if (swRunning)            { stCol = tft.color565(60, 210, 100);  stLabel = "\xB7 RUNNING"; }
    else if (swElapsed > 0)   { stCol = tft.color565(210, 175,   0); stLabel = "| PAUSED";   }
    else                      { stCol = tft.color565(0, 148, 170);  stLabel = "  READY";     }
    tft.setTextSize(1);
    tft.setTextColor(stCol, TFT_BLACK);
    tft.drawString(stLabel, 12, 35);

    // Lap counter (top-right)
    if (swLapCount > 0)
    {
        char lcBuf[12]; sprintf(lcBuf, "LAP %d", swLapCount + 1);
        tft.setTextColor(tft.color565(0, 160, 190), TFT_BLACK);
        tft.drawString(lcBuf, 262, 35);
    }

    // Divider between timer and laps
    tft.drawFastHLine(0, 102, 320, tft.color565(0, 50, 65));
    tft.drawFastVLine(0,   102, 1, tft.color565(0, 150, 190));
    tft.drawFastVLine(319, 102, 1, tft.color565(0, 150, 190));

    // Lap list (last 4 laps, most recent at bottom)
    if (swLapCount > 0)
    {
        tft.setTextSize(1);
        tft.setTextColor(tft.color565(0, 148, 165), TFT_BLACK);
        tft.drawString("LAP", 12, 107);
        tft.drawString("SPLIT", 52, 107);
        tft.drawString("TOTAL", 210, 107);

        int show     = swLapCount > 4 ? 4 : swLapCount;
        int startIdx = swLapCount - show;

        for (int i = 0; i < show; i++)
        {
            int      idx   = startIdx + i;
            int      rowY  = 118 + i * 22;
            bool     isLst = (idx == swLapCount - 1);
            uint16_t rc    = isLst ? tft.color565(0, 210, 230) : tft.color565(100, 140, 150);

            char numBuf[4]; sprintf(numBuf, "%d", idx + 1);
            tft.setTextColor(rc, TFT_BLACK);
            tft.drawString(numBuf, 12, rowY);

            uint32_t split = swLaps[idx] - (idx > 0 ? swLaps[idx - 1] : 0);
            char splitBuf[12]; swFormatTime(splitBuf, split);
            tft.drawString(splitBuf, 52, rowY);

            char totalBuf[12]; swFormatTime(totalBuf, swLaps[idx]);
            tft.setTextColor(tft.color565(100, 148, 162), TFT_BLACK);
            tft.drawString(totalBuf, 210, rowY);
        }
    }

    // Footer
    tft.fillRect(0, 219, 3, 21, tft.color565(0, 220, 245));
    tft.setTextSize(1);
    tft.setTextColor(tft.color565(0, 100, 118), TFT_BLACK);
    tft.drawString("[PRESS] RUN/STOP  [R] LAP  [L] RESET  [C] BACK", 8, 225);

    drawBatteryStatus(TFT_BLACK);
}

// -------------------------------------------------------------------------
// Redraw only the lap counter badge (top-right corner).
static void drawSwLapCounter()
{
    tft.fillRect(240, 32, 80, 14, TFT_BLACK);
    if (swLapCount > 0)
    {
        char lcBuf[12]; sprintf(lcBuf, "LAP %d", swLapCount + 1);
        tft.setTextSize(1);
        tft.setTextColor(tft.color565(0, 160, 190), TFT_BLACK);
        tft.drawString(lcBuf, 262, 35);
    }
}

// -------------------------------------------------------------------------
// Redraw a single lap row at screen slot slotIdx (0–3) for lap lapIdx.
static void drawSwLapRow(int slotIdx, int lapIdx, bool isLast)
{
    int rowY = 118 + slotIdx * 22;
    tft.fillRect(0, rowY, 320, 16, TFT_BLACK);
    tft.setTextSize(1);
    uint16_t rc = isLast ? tft.color565(0, 210, 230) : tft.color565(100, 140, 150);
    char numBuf[4]; sprintf(numBuf, "%d", lapIdx + 1);
    tft.setTextColor(rc, TFT_BLACK);
    tft.drawString(numBuf, 12, rowY);
    uint32_t split = swLaps[lapIdx] - (lapIdx > 0 ? swLaps[lapIdx - 1] : 0);
    char splitBuf[12]; swFormatTime(splitBuf, split);
    tft.drawString(splitBuf, 52, rowY);
    char totalBuf[12]; swFormatTime(totalBuf, swLaps[lapIdx]);
    tft.setTextColor(tft.color565(100, 148, 162), TFT_BLACK);
    tft.drawString(totalBuf, 210, rowY);
}

// -------------------------------------------------------------------------
// Redraw only the lap list area (divider + column headers + rows).
// Clears the region and repaints — no fillScreen involved.
static void drawSwLapArea()
{
    tft.fillRect(0, 102, 320, 117, TFT_BLACK);

    tft.drawFastHLine(0, 102, 320, tft.color565(0, 50, 65));
    tft.drawFastVLine(0,   102, 1, tft.color565(0, 150, 190));
    tft.drawFastVLine(319, 102, 1, tft.color565(0, 150, 190));

    if (swLapCount == 0) return;

    tft.setTextSize(1);
    tft.setTextColor(tft.color565(0, 148, 165), TFT_BLACK);
    tft.drawString("LAP",   12,  107);
    tft.drawString("SPLIT", 52,  107);
    tft.drawString("TOTAL", 210, 107);

    int show     = swLapCount > 4 ? 4 : swLapCount;
    int startIdx = swLapCount - show;

    for (int i = 0; i < show; i++)
    {
        int      idx   = startIdx + i;
        int      rowY  = 118 + i * 22;
        bool     isLst = (idx == swLapCount - 1);
        uint16_t rc    = isLst ? tft.color565(0, 210, 230) : tft.color565(100, 140, 150);

        char numBuf[4]; sprintf(numBuf, "%d", idx + 1);
        tft.setTextColor(rc, TFT_BLACK);
        tft.drawString(numBuf, 12, rowY);

        uint32_t split = swLaps[idx] - (idx > 0 ? swLaps[idx - 1] : 0);
        char splitBuf[12]; swFormatTime(splitBuf, split);
        tft.drawString(splitBuf, 52, rowY);

        char totalBuf[12]; swFormatTime(totalBuf, swLaps[idx]);
        tft.setTextColor(tft.color565(100, 148, 162), TFT_BLACK);
        tft.drawString(totalBuf, 210, rowY);
    }
}

// -------------------------------------------------------------------------
// Redraw only the time field — called every ~20 ms when running.
// Uses text with black bg so it self-clears without a fillScreen.
static char swPrevBuf[12] = "";

static void drawStopwatchTime(uint32_t ms)
{
    char buf[12];
    swFormatTime(buf, ms);

    // No-op when paused and value unchanged
    if (!swRunning && strcmp(buf, swPrevBuf) == 0) return;
    strcpy(swPrevBuf, buf);

    uint16_t col = swRunning ? tft.color565(0, 220, 245) : tft.color565(120, 160, 170);
    tft.setTextSize(4);
    tft.setTextColor(col, TFT_BLACK);
    // "MM:SS.cs" at size 4 ≈ 200px wide; start at x=58 to centre over 320px
    tft.drawString(buf, 58, 58);
}

// -------------------------------------------------------------------------
// Updates only the status badge — avoids fillScreen flicker on pause/unpause.
static void drawSwStatus()
{
    tft.fillRect(0, 32, 200, 14, TFT_BLACK);   // left half only — preserves lap counter
    tft.setTextSize(1);
    uint16_t stCol;
    const char* stLabel;
    if (swRunning)          { stCol = tft.color565(60, 210, 100);  stLabel = "\xB7 RUNNING"; }
    else if (swElapsed > 0) { stCol = tft.color565(210, 175,   0); stLabel = "| PAUSED";   }
    else                    { stCol = tft.color565(0, 148, 170);   stLabel = "  READY";    }
    tft.setTextColor(stCol, TFT_BLACK);
    tft.drawString(stLabel, 12, 35);
}

// -------------------------------------------------------------------------
void stopwatchScreen()
{
    while (digitalRead(WIO_5S_PRESS) == LOW) delay(10);

    swReset();
    swPrevBuf[0] = '\0';
    bool     frameNeeded = true;
    uint32_t lastMs      = 0xFFFFFFFF;

    while (true)
    {
        if (frameNeeded)
        {
            drawStopwatchFrame();
            frameNeeded = false;
        }

        uint32_t cur = swCurrentMs();
        if (cur != lastMs || swRunning)
        {
            drawStopwatchTime(cur);
            lastMs = cur;
        }

        // [PRESS] start / pause
        if (digitalRead(WIO_5S_PRESS) == LOW)
        {
            if (swRunning) { swElapsed = swCurrentMs(); swRunning = false; }
            else           { swStartMs = millis(); swRunning = true; }
            while (digitalRead(WIO_5S_PRESS) == LOW) delay(10);
            // Partial update: only badge + timer colour change — no fillScreen.
            swPrevBuf[0] = '\0';   // force timer colour refresh
            drawSwStatus();
            drawStopwatchTime(swCurrentMs());
        }
        // [RIGHT] lap — only while running, max 8 laps
        else if (digitalRead(WIO_5S_RIGHT) == LOW && swRunning && swLapCount < 8)
        {
            swLaps[swLapCount++] = swCurrentMs();
            while (digitalRead(WIO_5S_RIGHT) == LOW) delay(10);
            drawSwLapCounter();
            int n = swLapCount;
            if (n == 1)
            {
                // First lap: draw column headers then the single row.
                tft.setTextSize(1);
                tft.setTextColor(tft.color565(0, 148, 165), TFT_BLACK);
                tft.drawString("LAP",   12,  107);
                tft.drawString("SPLIT", 52,  107);
                tft.drawString("TOTAL", 210, 107);
                drawSwLapRow(0, 0, true);
            }
            else if (n <= 4)
            {
                // No scroll: recolour the old last row, draw the new one.
                drawSwLapRow(n - 2, n - 2, false);
                drawSwLapRow(n - 1, n - 1, true);
            }
            else
            {
                // Window scrolled — all 4 visible rows changed, redraw each.
                int startIdx = n - 4;
                for (int i = 0; i < 4; i++)
                    drawSwLapRow(i, startIdx + i, i == 3);
            }
        }
        // [LEFT] reset — only while paused
        else if (digitalRead(WIO_5S_LEFT) == LOW && !swRunning)
        {
            swReset();
            swPrevBuf[0] = '\0';
            lastMs = 0xFFFFFFFF;
            while (digitalRead(WIO_5S_LEFT) == LOW) delay(10);
            frameNeeded = true;
        }
        // [B] screenshot
        else if (digitalRead(WIO_KEY_B) == LOW)
        {
            while (digitalRead(WIO_KEY_B) == LOW) delay(10);
            takeScreenshot();
        }
        // [C] back
        else if (digitalRead(WIO_KEY_C) == LOW)
        {
            swReset();
            while (digitalRead(WIO_KEY_C) == LOW) delay(10);
            delay(50);
            return;
        }

        delay(20);
    }
}
