#include <Arduino.h>
#include "globals.h"


//========================================================================= CLAUDE USAGE
// Mirrors the data model from the Clawdmeter project.
// Populate usageData from a serial/BLE source; the screen shows "No data" until valid = true.

struct UsageData {
    float session_pct;
    int   session_reset_mins;
    float weekly_pct;
    int   weekly_reset_mins;
    char  status[16];
    char  session_reset_str[56];
    char  weekly_reset_str[56];
    bool  valid;
};

// Global usage state — updated by checkSerial() whenever a new JSON line arrives.
UsageData usageData = { 0.0f, 0, 0.0f, 0, "unknown", "", "", false };

// Incremented every time a successful JSON parse completes.
// claudeUsageScreen() watches this to trigger a redraw on every new packet.
static uint16_t dataVersion = 0;

// -------------------------------------------------------------------------
// Parses one line of compact JSON from the sender script.
// Expected format:
//   {"s":45,"sr":142,"w":67,"wr":2580,"st":"allowed","srt":"Resets 3:50pm (EDT)","wrt":"Resets May 18, 8pm (EDT)","ok":true}
bool parseUsageJson(const char* json)
{
    const char* p;

    p = strstr(json, "\"s\":");
    if (!p) return false;
    usageData.session_pct = (float)atoi(p + 4);

    p = strstr(json, "\"sr\":");
    if (!p) return false;
    usageData.session_reset_mins = atoi(p + 5);

    p = strstr(json, "\"w\":");
    if (!p) return false;
    usageData.weekly_pct = (float)atoi(p + 4);

    p = strstr(json, "\"wr\":");
    if (!p) return false;
    usageData.weekly_reset_mins = atoi(p + 5);

    p = strstr(json, "\"st\":\"");
    if (!p) return false;
    p += 6;
    int i = 0;
    while (*p && *p != '"' && i < 15)
        usageData.status[i++] = *p++;
    usageData.status[i] = '\0';

    p = strstr(json, "\"srt\":\"");
    if (p) {
        p += 7;
        int i = 0;
        while (*p && *p != '"' && i < 55) usageData.session_reset_str[i++] = *p++;
        usageData.session_reset_str[i] = '\0';
    }

    p = strstr(json, "\"wrt\":\"");
    if (p) {
        p += 7;
        int i = 0;
        while (*p && *p != '"' && i < 55) usageData.weekly_reset_str[i++] = *p++;
        usageData.weekly_reset_str[i] = '\0';
    }

    usageData.valid = true;
    dataVersion++;
    return true;
}

// -------------------------------------------------------------------------
// Called every loop() iteration. Reads a complete newline-terminated JSON
// string from Serial (non-blocking) and updates usageData if one is ready.
static char serialBuf[512];
static int  serialPos = 0;

void checkSerial()
{
    while (Serial.available())
    {
        char c = (char)Serial.read();
        if (c == '\n' || c == '\r')
        {
            if (serialPos > 0)
            {
                serialBuf[serialPos] = '\0';
                if (strstr(serialBuf, "\"cpu\":"))
                    parseSysStatsJson(serialBuf);
                else if (strstr(serialBuf, "\"p\":[" ))
                    parseProcessJson(serialBuf);
                else
                    parseUsageJson(serialBuf);
                serialPos = 0;
            }
        }
        else if (serialPos < (int)sizeof(serialBuf) - 1)
        {
            serialBuf[serialPos++] = c;
        }
        else
        {
            serialPos = 0;
        }
    }
}

// -------------------------------------------------------------------------
// Draws a small Claude-style 6-point asterisk (3 lines at 0°/60°/120°).
static void drawClaudeStar(int cx, int cy, uint16_t color)
{
    tft.drawLine(cx - 5, cy,     cx + 5, cy,     color);
    tft.drawLine(cx - 5, cy + 1, cx + 5, cy + 1, color);
    tft.drawLine(cx - 2, cy - 4, cx + 2, cy + 4, color);
    tft.drawLine(cx + 2, cy - 4, cx - 2, cy + 4, color);
}

// -------------------------------------------------------------------------
// Returns a colour for a given usage percentage using the Claude brand palette.
static uint16_t usageColor(float pct)
{
    if (pct < 60.0f) return tft.color565(60, 210, 100);   // neon green
    if (pct < 85.0f) return tft.color565(210, 175, 0);    // amber
    return tft.color565(210, 65, 55);                      // warning red
}

// -------------------------------------------------------------------------
// Static chrome for one usage row: row label + bar border.
// Called once per row during the initial full draw only.
static void drawUsageRowChrome(const char* label, int y)
{
    tft.setTextSize(1);
    tft.setTextColor(tft.color565(190, 152, 135), TFT_BLACK);
    tft.drawString(label, 20, y);
    tft.drawRect(20, y + 13, 228, 22, tft.color565(35, 40, 35));
}

// -------------------------------------------------------------------------
// Dynamic data for one usage row: bar fill, pct, reset text.
// Safe to call on every data update — never touches the label or bar border.
// Pct uses a fixed 4-char format with bg colour so no fillRect clear is needed.
static void drawUsageRowData(float pct, int resetMins, const char* resetStr, int y)
{
    if (pct < 0.0f) pct = 0.0f;
    float barPct = pct > 100.0f ? 100.0f : pct;
    uint16_t col = usageColor(pct);

    // Bar interior — fills inside the 1px border drawn by drawUsageRowChrome,
    // so the border is never overwritten and never needs to be redrawn.
    int barY = y + 13;
    const int innerW = 226, innerH = 20;   // interior: x=21..246, y=barY+1..barY+20
    int fillW = (int)(innerW * barPct / 100.0f);
    if (fillW > 0)         tft.fillRect(21,          barY + 1, fillW,           innerH, col);
    if (fillW < innerW)    tft.fillRect(21 + fillW,  barY + 1, innerW - fillW,  innerH, TFT_BLACK);

    // Pct — fixed 4-char right-aligned format; bg-colour draw overwrites old value
    char pctBuf[8];
    sprintf(pctBuf, "%3.0f%%", pct);
    tft.setTextSize(2);
    tft.setTextColor(col, TFT_BLACK);
    tft.drawString(pctBuf, 256, barY + 3);

    // Reset lines — two 8px strips; must clear because string length can vary
    char resetBuf[40];
    if (resetMins >= 1440) {
        int days  = resetMins / 1440;
        int hours = (resetMins % 1440) / 60;
        int mins  = resetMins % 60;
        if (hours > 0)
            sprintf(resetBuf, "Resets in %dd %dh %dm", days, hours, mins);
        else
            sprintf(resetBuf, "Resets in %dd %dm", days, mins);
    } else if (resetMins >= 60) {
        int hours = resetMins / 60;
        int mins  = resetMins % 60;
        sprintf(resetBuf, "Resets in %dh %dm", hours, mins);
    } else if (resetMins > 0) {
        sprintf(resetBuf, "Resets in %dm", resetMins);
    } else {
        sprintf(resetBuf, "Resetting now");
    }
    // Reset lines — fixed-width (38 chars = 228 px at size 1) so the drawString
    // bg fill overwrites any shorter previous string without a separate fillRect.
    char line[42];
    tft.setTextSize(1);
    snprintf(line, sizeof(line), "%-38s", resetBuf);
    tft.setTextColor(tft.color565(215, 175, 148), TFT_BLACK);
    tft.drawString(line, 20, barY + 26);

    snprintf(line, sizeof(line), "%-38s", (resetStr && resetStr[0] != '\0') ? resetStr : "");
    tft.setTextColor(tft.color565(185, 148, 128), TFT_BLACK);
    tft.drawString(line, 20, barY + 36);
}

// -------------------------------------------------------------------------
static void drawCybBoxCoral(int x, int y, int w, int h, uint16_t col, int t)
{
    tft.drawFastHLine(x,       y,       t, col);
    tft.drawFastVLine(x,       y,       t, col);
    tft.drawFastHLine(x+w-t,   y,       t, col);
    tft.drawFastVLine(x+w-1,   y,       t, col);
    tft.drawFastHLine(x,       y+h-1,   t, col);
    tft.drawFastVLine(x,       y+h-t,   t, col);
    tft.drawFastHLine(x+w-t,   y+h-1,   t, col);
    tft.drawFastVLine(x+w-1,   y+h-t,   t, col);
}

// -------------------------------------------------------------------------
// Renders the full Claude Usage screen.
static void drawClaudeUsage()
{
    tft.fillScreen(TFT_BLACK);

    // --- Header strip (coral cyberpunk) ---
    tft.fillRect(0, 0, 320, 30, tft.color565(16, 6, 3));
    tft.fillRect(0, 0, 3, 30, tft.color565(217, 119, 87));          // coral accent bar
    tft.setTextSize(2);
    tft.setTextColor(tft.color565(217, 119, 87), tft.color565(16, 6, 3));
    tft.drawString("// CLAUDE", 10, 7);
    tft.setTextSize(1);
    tft.setTextColor(tft.color565(168, 82, 52), tft.color565(16, 6, 3));
    tft.drawString("AI USAGE", 160, 11);
    tft.drawFastHLine(0, 29, 320, tft.color565(100, 45, 25));
    for (int xi = 8; xi < 320; xi += 14)
        tft.drawFastVLine(xi, 27, 4, tft.color565(155, 72, 40));

    if (!usageData.valid)
    {
        uint16_t nc   = tft.color565(217, 119, 87);
        uint16_t nbg  = tft.color565(14,   5,  2);
        uint16_t ndim = tft.color565(160, 100, 72);
        tft.fillRect(10, 48, 300, 160, nbg);
        drawCybBoxCoral(10, 48, 300, 160, nc, 12);
        tft.drawFastHLine(11, 49, 298, tft.color565(40, 15, 8));    // scan-line accent

        drawClaudeStar(160, 68, nc);

        tft.setTextSize(2);
        tft.setTextColor(nc, nbg);
        tft.drawString("> NO_DATA", 50, 90);

        tft.setTextSize(1);
        tft.setTextColor(ndim, nbg);
        tft.drawString("AWAITING DATA STREAM...", 50, 118);
        tft.drawString("USB:  claude_sender.py <port>", 50, 134);
        tft.drawString("BLE:  claude_sender.py --ble", 50, 150);

        tft.setTextColor(tft.color565(110, 62, 38), nbg);
        tft.drawString(Serial ? "STATUS: USB ACTIVE" : "STATUS: WAITING...", 50, 166);
        char addrLine[40];
        snprintf(addrLine, sizeof(addrLine), "ADDR:  %s",
                 bleInitDone ? getBLEAddress() : "INIT...");
        tft.drawString(addrLine, 50, 182);
    }
    else
    {
        drawUsageRowChrome("SESSION  (5h window)", 38);
        drawUsageRowData(usageData.session_pct, usageData.session_reset_mins, usageData.session_reset_str, 38);
        drawUsageRowChrome("WEEKLY   (7d window)", 113);
        drawUsageRowData(usageData.weekly_pct, usageData.weekly_reset_mins, usageData.weekly_reset_str, 113);

        bool limited  = (strncmp(usageData.status, "limited",  7) == 0);
        bool rejected = (strncmp(usageData.status, "rejected", 8) == 0);
        const char* statusText = limited ? "LIMITED" : (rejected ? "REJECTED" : "ALLOWED");
        uint16_t badgeColor    = (limited || rejected) ? tft.color565(210, 65, 55)  : tft.color565(175, 140, 60);
        uint16_t badgeBg       = (limited || rejected) ? tft.color565(55, 18, 15)   : tft.color565(40, 32, 8);
        int badgeW = rejected ? 70 : 58;
        const int badgeH = 16, badgeY = 188;
        int badgeX = (320 - badgeW) / 2;

        // s5h/s7d: fixed 4-char format at fixed positions; bg colour overwrites old value
        char s5h[8]; sprintf(s5h, "%3d%%", (int)usageData.session_pct);
        tft.setTextSize(2);
        tft.setTextColor(usageColor(usageData.session_pct), TFT_BLACK);
        tft.drawString(s5h, 28, badgeY);

        char s7d[8]; sprintf(s7d, "%3d%%", (int)usageData.weekly_pct);
        tft.setTextColor(usageColor(usageData.weekly_pct), TFT_BLACK);
        tft.drawString(s7d, 252, badgeY);   // 252 + 4*12 = 300

        tft.setTextSize(1);
        tft.fillRect(badgeX, badgeY, badgeW, badgeH, badgeBg);
        drawCybBoxCoral(badgeX, badgeY, badgeW, badgeH, badgeColor, 6);
        tft.setTextColor(badgeColor, badgeBg);
        tft.drawString(statusText, badgeX + 8, badgeY + 4);
        drawClaudeStar(badgeX - 11, badgeY + 8, badgeColor);

    }

    // Footer
    tft.fillRect(0, 219, 3, 21, tft.color565(217, 119, 87));        // coral accent bar
    tft.setTextSize(1);
    tft.setTextColor(tft.color565(130, 68, 42), TFT_BLACK);
    tft.drawString("[C] BACK", 8, 225);

    drawBatteryStatus(TFT_BLACK);
}

// -------------------------------------------------------------------------
// Targeted in-place update — only touches dynamic pixels; static elements
// (header, footer, row labels, bar borders) are never redrawn so they never
// flash.  Only called when usageData.valid and a new data packet arrived.
static void drawClaudeUsageUpdate()
{
    // Row data only — chrome (label + border) was drawn once by drawClaudeUsage()
    drawUsageRowData(usageData.session_pct, usageData.session_reset_mins, usageData.session_reset_str, 38);
    drawUsageRowData(usageData.weekly_pct,  usageData.weekly_reset_mins,  usageData.weekly_reset_str,  113);

    bool limited  = (strncmp(usageData.status, "limited",  7) == 0);
    bool rejected = (strncmp(usageData.status, "rejected", 8) == 0);
    const char* statusText = limited ? "LIMITED" : (rejected ? "REJECTED" : "ALLOWED");
    uint16_t badgeColor    = (limited || rejected) ? tft.color565(210, 65, 55)  : tft.color565(175, 140, 60);
    uint16_t badgeBg       = (limited || rejected) ? tft.color565(55, 18, 15)   : tft.color565(40, 32, 8);
    int badgeW = rejected ? 70 : 58;
    const int badgeH = 16, badgeY = 188;
    int badgeX = (320 - badgeW) / 2;

    // s5h/s7d: fixed 4-char format at fixed positions; bg colour overwrites old value
    // without needing a separate fillRect clear.
    char s5h[8]; sprintf(s5h, "%3d%%", (int)usageData.session_pct);
    tft.setTextSize(2);
    tft.setTextColor(usageColor(usageData.session_pct), TFT_BLACK);
    tft.drawString(s5h, 28, badgeY);

    char s7d[8]; sprintf(s7d, "%3d%%", (int)usageData.weekly_pct);
    tft.setTextColor(usageColor(usageData.weekly_pct), TFT_BLACK);
    tft.drawString(s7d, 252, badgeY);   // 252 + 4*12 = x300

    // Badge: clear only the narrow star+badge area (max 82px wide) rather than
    // a full-screen-width strip.  REJECTED badgeX=125, star at 114; covers both sizes.
    tft.fillRect(113, badgeY, 83, badgeH, TFT_BLACK);
    tft.setTextSize(1);
    tft.fillRect(badgeX, badgeY, badgeW, badgeH, badgeBg);
    drawCybBoxCoral(badgeX, badgeY, badgeW, badgeH, badgeColor, 6);
    tft.setTextColor(badgeColor, badgeBg);
    tft.drawString(statusText, badgeX + 8, badgeY + 4);
    drawClaudeStar(badgeX - 11, badgeY + 8, badgeColor);

    drawBatteryStatus(TFT_BLACK);
}

// -------------------------------------------------------------------------
// Claude Usage sub-screen. Accepts data from USB serial and BLE simultaneously.
// Blocks until KEY_C is pressed.
void claudeUsageScreen()
{
    while (digitalRead(WIO_5S_PRESS) == LOW) { delay(10); }

    // Reset stale data from a previous session.
    usageData.valid = false;
    dataVersion++;

    bool bleActivated = false;
    if (bleInitDone)
    {
        bleSetActive(true);
        bleActivated = true;
    }

    drawClaudeUsage();

    uint16_t drawnVersion = dataVersion;
    bool     drawnValid   = false;
    uint32_t lastDataMs   = 0;

    while (true)
    {
        checkSerial();
        checkBLE();

        // BLE init completes ~3s after boot — activate advertising once ready.
        if (!bleActivated && bleInitDone)
        {
            bleSetActive(true);
            bleActivated = true;
        }

        // Refresh lastDataMs eagerly so a just-arrived packet can't be
        // immediately evicted by the timeout check in the same iteration.
        if (usageData.valid && dataVersion != drawnVersion)
            lastDataMs = millis();

        // Timeout: revert to no-data panel if stream stops for >90 s.
        // claude_sender.py polls every 60 s, so 90 s gives a 1.5× buffer.
        if (usageData.valid && lastDataMs && millis() - lastDataMs > 90000)
        {
            usageData.valid = false;
            dataVersion++;
        }

        if (dataVersion != drawnVersion)
        {
            bool validChanged = (usageData.valid != drawnValid);
            drawnVersion = dataVersion;
            drawnValid   = usageData.valid;

            if (validChanged || !usageData.valid)
                drawClaudeUsage();
            else
                drawClaudeUsageUpdate();
        }

        if (digitalRead(WIO_KEY_B) == LOW)
        {
            while (digitalRead(WIO_KEY_B) == LOW) { delay(10); }
            takeScreenshot();
        }

        if (digitalRead(WIO_KEY_C) == LOW)
        {
            while (digitalRead(WIO_KEY_C) == LOW) { delay(10); }
            delay(50);
            bleSetActive(false);
            return;
        }
    }
}
