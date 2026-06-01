#include <Arduino.h>
#include <math.h>
#include "globals.h"

//========================================================================= SYS STATS
// Receives JSON from sysstat_sender.py over USB serial.
// Displays CPU/RAM/GPU/GPU MEM usage + temperatures as smooth arc gauges.

struct SysStatsData {
    int   cpu_pct;
    int   cpu_temp;     // -1 = unavailable
    int   ram_pct;
    char  ram_str[20];  // "8.2/16GB"
    int   gpu_pct;      // -1 = unavailable
    int   gpu_temp;     // -1 = unavailable
    int   gpu_mem_pct;  // -1 = unavailable
    char  gpu_name[32]; // empty string if unavailable
    float net_down;     // MB/s
    float net_up;       // MB/s
    bool  valid;
};

static SysStatsData sysData = { 0, -1, 0, "", -1, -1, -1, "", 0.0f, 0.0f, false };
static uint16_t     sysDataVersion = 0;

// -------------------------------------------------------------------------
bool parseSysStatsJson(const char* json)
{
    const char* p;

    p = strstr(json, "\"cpu\":");  if (!p) return false;
    sysData.cpu_pct = atoi(p + 6);

    p = strstr(json, "\"ct\":");
    sysData.cpu_temp = p ? atoi(p + 5) : -1;

    p = strstr(json, "\"ram\":");  if (!p) return false;
    sysData.ram_pct = atoi(p + 6);

    p = strstr(json, "\"rb\":\"");
    if (p) {
        p += 6;
        int i = 0;
        while (*p && *p != '"' && i < 19) sysData.ram_str[i++] = *p++;
        sysData.ram_str[i] = '\0';
    }

    p = strstr(json, "\"gpu\":");
    sysData.gpu_pct = p ? atoi(p + 6) : -1;

    p = strstr(json, "\"gt\":");
    sysData.gpu_temp = p ? atoi(p + 5) : -1;

    p = strstr(json, "\"gm\":");
    sysData.gpu_mem_pct = p ? atoi(p + 5) : -1;

    p = strstr(json, "\"gn\":\"");
    if (p) {
        p += 6;
        int i = 0;
        while (*p && *p != '"' && i < 31) sysData.gpu_name[i++] = *p++;
        sysData.gpu_name[i] = '\0';
    }

    p = strstr(json, "\"nd\":");
    sysData.net_down = p ? strtof(p + 5, nullptr) : 0.0f;

    p = strstr(json, "\"nu\":");
    sysData.net_up = p ? strtof(p + 5, nullptr) : 0.0f;

    sysData.valid = true;
    sysDataVersion++;
    return true;
}

// =========================================================================
// Gauge constants — 2×2 grid, semicircle (flat side down).
//   0% = 9 o'clock   50% = 12 o'clock   100% = 3 o'clock

static const int GR = 55;   // outer radius
static const int GT = 9;    // arc thickness → inner radius = 46

static const int G_CPU_CX = 80,  G_CPU_CY = 92;
static const int G_RAM_CX = 240, G_RAM_CY = 92;
static const int G_GPU_CX = 80,  G_GPU_CY = 192;
static const int G_VMX    = 240, G_VMY    = 192;

static const int G_NET_Y = 111;

// -------------------------------------------------------------------------
static uint16_t tempColor(int t)
{
    if (t < 60) return tft.color565(80, 255, 80);    // neon green — cool
    if (t < 80) return tft.color565(255, 210, 0);    // neon amber — warm
    return tft.color565(255, 50, 80);                // neon red — hot
}

// -------------------------------------------------------------------------
// Pixel-perfect arc via column scanlines with per-pixel angular fill.
// Fill sweeps clockwise from 9 o'clock to the value angle.
// pct = -1 → background arc only (N/A state).
static void drawGaugeArc(int cx, int cy, int pct, uint16_t fgCol)
{
    const uint16_t bgCol = tft.color565(0, 45, 75);   // arc track (unfilled)
    const int ri = GR - GT;

    bool fullFill  = (pct >= 100);
    bool emptyFill = (pct <= 0);
    float cosF = 0.0f, sinF = 0.0f;
    if (!fullFill && !emptyFill) {
        float fillRad = (180.0f - (float)pct * 1.8f) * 0.017453293f;
        cosF = cosf(fillRad);
        sinF = sinf(fillRad);
    }

    for (int x = cx - GR; x <= cx + GR; x++) {
        int dx  = x - cx;
        int oSq = GR * GR - dx * dx;
        if (oSq < 0) continue;
        int outerY = cy - (int)sqrtf((float)oSq);
        if (outerY >= cy) continue;   // upper semicircle only

        // innerY = cy in end-cap region (|dx| >= ri): the ring really does
        // extend to the baseline there, giving smooth rounded terminations.
        int iSq = ri * ri - dx * dx;
        int innerY = (iSq > 0) ? (cy - (int)sqrtf((float)iSq)) : cy;

        int len = innerY - outerY;
        if (len <= 0) continue;

        if (emptyFill) { tft.drawFastVLine(x, outerY, len, bgCol); continue; }
        if (fullFill)  { tft.drawFastVLine(x, outerY, len, fgCol); continue; }

        // Per-pixel angular test: pixel at y is in the filled zone when its
        // angle from center >= fillRad, equivalent to (cy-y)·cosF >= dx·sinF.
        // This follows the radial sweep rather than a straight vertical cut.
        float thr      = (float)dx * sinF;
        int   dyOuter  = cy - outerY;
        int   dyInner  = cy - (innerY - 1);
        bool  topFill  = ((float)dyOuter * cosF >= thr);
        bool  botFill  = ((float)dyInner * cosF >= thr);

        if (topFill == botFill) {
            tft.drawFastVLine(x, outerY, len, topFill ? fgCol : bgCol);
        } else {
            // Boundary crosses this column — split it at the radial boundary.
            int ySplit;
            if (fabsf(cosF) > 1e-4f) {
                ySplit = cy - (int)roundf(thr / cosF);
                if (ySplit < outerY) ySplit = outerY;
                if (ySplit > innerY) ySplit = innerY;
            } else {
                ySplit = (dx <= 0) ? innerY : outerY;   // vertical boundary
            }
            if (ySplit > outerY)
                tft.drawFastVLine(x, outerY, ySplit - outerY, topFill ? fgCol : bgCol);
            if (ySplit < innerY)
                tft.drawFastVLine(x, ySplit,  innerY - ySplit, botFill ? fgCol : bgCol);
        }
    }
}

// -------------------------------------------------------------------------
// Clears the gauge interior using horizontal scanlines clipped to the
// inner circle, so the arc ring is never touched.
static void clearGaugeInterior(int cx, int cy)
{
    const int ri = GR - GT - 1;   // 1 px margin inside inner radius
    for (int dy = -(GR - 1); dy <= -2; dy++) {
        int rr = ri * ri - dy * dy;
        if (rr <= 0) continue;
        int hw = (int)sqrtf((float)rr);
        tft.drawFastHLine(cx - hw, cy + dy, 2 * hw + 1, TFT_BLACK);
    }
}

// -------------------------------------------------------------------------
// Draws a complete gauge: arc, interior text, and below-arc info.
//   pct     : 0-100, or -1 for N/A
//   temp    : °C to show below arc, or -1 to suppress
//   subinfo : shown below arc instead of temp (e.g. "8.2/16GB"); ignored if temp >= 0
static void drawGauge(int cx, int cy,
                      const char* label, int pct, uint16_t fgCol,
                      int temp, const char* subinfo)
{
    drawGaugeArc(cx, cy, pct, fgCol);
    clearGaugeInterior(cx, cy);

    const uint16_t colLabel = tft.color565(0, 170, 200);   // dim neon cyan

    // Value — fixed 4-char width ("%3d%%") so old digits are always overwritten.
    // 4 chars × 12 px/char (size 2) = 48 px wide; centred at cx → x = cx-24.
    tft.setTextSize(2);
    if (pct >= 0) {
        char vbuf[8]; snprintf(vbuf, sizeof(vbuf), "%3d%%", pct);
        tft.setTextColor(fgCol, TFT_BLACK);
        tft.drawString(vbuf, cx - 24, cy - 34);
    } else {
        tft.setTextColor(tft.color565(30, 55, 75), TFT_BLACK);
        tft.drawString(" N/A", cx - 24, cy - 34);
    }

    // Label — centred below value
    tft.setTextSize(1);
    tft.setTextColor(colLabel, TFT_BLACK);
    tft.drawString(label, cx - (int)strlen(label) * 3, cy - 14);

    // Below-arc line (temperature or sub-info)
    tft.fillRect(cx - GR, cy + 2, 2 * GR + 1, 10, TFT_BLACK);
    tft.setTextSize(1);
    if (temp >= 0) {
        char tbuf[8]; snprintf(tbuf, sizeof(tbuf), "%dC", temp);
        tft.setTextColor(tempColor(temp), TFT_BLACK);
        tft.drawString(tbuf, cx - (int)strlen(tbuf) * 3, cy + 3);
    } else if (subinfo && subinfo[0]) {
        tft.setTextColor(colLabel, TFT_BLACK);
        tft.drawString(subinfo, cx - (int)strlen(subinfo) * 3, cy + 3);
    }
}

// -------------------------------------------------------------------------
static void updateGauges()
{
    drawGauge(G_CPU_CX, G_CPU_CY, "CPU",  sysData.cpu_pct,     tft.color565(0,   220, 245),
              sysData.cpu_temp, nullptr);
    drawGauge(G_RAM_CX, G_RAM_CY, "RAM",  sysData.ram_pct,     tft.color565(80,  255, 100),
              -1, sysData.ram_str);
    drawGauge(G_GPU_CX, G_GPU_CY, "GPU",  sysData.gpu_pct,     tft.color565(240,  40, 200),
              sysData.gpu_temp, nullptr);
    drawGauge(G_VMX,    G_VMY,    "GPU MEM", sysData.gpu_mem_pct, tft.color565(255, 200,   0),
              -1, nullptr);

    // Network — colour-coded DN/UP strip with arrow indicators
    tft.fillRect(10, G_NET_Y, 300, 9, TFT_BLACK);
    tft.setTextSize(1);

    auto fmtNet = [](float mb, char* out, int sz) {
        if      (mb >= 1.0f)    snprintf(out, sz, "%.1f MB/s", mb);
        else if (mb >= 0.001f)  snprintf(out, sz, "%d KB/s",   (int)(mb * 1024.0f + 0.5f));
        else                    snprintf(out, sz, "%d B/s",    (int)(mb * 1048576.0f + 0.5f));
    };
    char dn[14], up[14];
    fmtNet(sysData.net_down, dn, sizeof(dn));
    fmtNet(sysData.net_up,   up, sizeof(up));

    uint16_t dnCol = tft.color565(0,   210, 230);   // neon cyan  — download
    uint16_t upCol = tft.color565(220,  40, 190);   // neon magenta — upload

    // Down-arrow (▼) + label + value
    tft.fillTriangle(20, G_NET_Y,  26, G_NET_Y,  23, G_NET_Y + 6, dnCol);
    tft.setTextColor(dnCol, TFT_BLACK);
    tft.drawString("DN", 30, G_NET_Y);
    tft.drawString(dn, 48, G_NET_Y);

    // Up-arrow (▲): 6px wide
    tft.fillTriangle(175, G_NET_Y + 6,  181, G_NET_Y + 6,  178, G_NET_Y, upCol);
    tft.setTextColor(upCol, TFT_BLACK);
    tft.drawString("UP", 185, G_NET_Y);
    tft.drawString(up, 203, G_NET_Y);
}

// -------------------------------------------------------------------------
// Draws cyberpunk corner-bracket borders around a box (no fill).
static void drawCybBox(int x, int y, int w, int h, uint16_t col)
{
    const int t = 12;
    tft.drawFastHLine(x,       y,       t, col);   // TL horiz
    tft.drawFastVLine(x,       y,       t, col);   // TL vert
    tft.drawFastHLine(x+w-t,   y,       t, col);   // TR horiz
    tft.drawFastVLine(x+w-1,   y,       t, col);   // TR vert
    tft.drawFastHLine(x,       y+h-1,   t, col);   // BL horiz
    tft.drawFastVLine(x,       y+h-t,   t, col);   // BL vert
    tft.drawFastHLine(x+w-t,   y+h-1,   t, col);   // BR horiz
    tft.drawFastVLine(x+w-1,   y+h-t,   t, col);   // BR vert
}

// -------------------------------------------------------------------------
static void drawSysStats()
{
    tft.fillScreen(TFT_BLACK);

    // --- Header strip ---
    tft.fillRect(0, 0, 320, 30, tft.color565(0, 8, 20));
    tft.fillRect(0, 0, 3, 30, tft.color565(0, 200, 230));          // left accent bar
    tft.setTextSize(2);
    tft.setTextColor(tft.color565(0, 220, 245), tft.color565(0, 8, 20));
    tft.drawString("// SYS_STATS", 10, 7);
    tft.setTextSize(1);
    tft.setTextColor(tft.color565(0, 158, 188), tft.color565(0, 8, 20));
    tft.drawString("LIVE", 289, 12);

    // Ticked divider
    tft.drawFastHLine(0, 29, 320, tft.color565(0, 100, 130));
    for (int xi = 8; xi < 320; xi += 14)
        tft.drawFastVLine(xi, 27, 4, tft.color565(0, 155, 185));

    if (!sysData.valid)
    {
        // Terminal-style no-signal panel
        uint16_t nc   = tft.color565(0,  210, 240);
        uint16_t nbg  = tft.color565(0,    8,  18);
        uint16_t ndim = tft.color565(0,  130, 155);
        tft.fillRect(10, 48, 300, 160, nbg);
        drawCybBox(10, 48, 300, 160, nc);
        tft.drawFastHLine(11, 49, 298, tft.color565(0, 40, 60));   // scan-line accent

        tft.setTextSize(2);
        tft.setTextColor(nc, nbg);
        tft.drawString("> NO_DATA", 50, 90);

        tft.setTextSize(1);
        tft.setTextColor(ndim, nbg);
        tft.drawString("AWAITING DATA STREAM...", 50, 118);
        tft.drawString("USB:  sysstat_sender.py <port>", 50, 134);
        tft.drawString("BLE:  sysstat_sender.py --ble", 50, 150);

        tft.setTextColor(tft.color565(0, 80, 100), nbg);
        tft.drawString(Serial ? "STATUS: USB ACTIVE" : "STATUS: WAITING...", 50, 166);
        char addrLine[40];
        snprintf(addrLine, sizeof(addrLine), "ADDR:  %s",
                 bleInitDone ? getBLEAddress() : "INIT...");
        tft.drawString(addrLine, 50, 182);
    }
    else
    {
        // Gauge corner-bracket boxes — each in its gauge's neon colour.
        // updateGauges() only redraws inside cx±GR, so boxes survive partial updates.
        const int bxc[] = { G_CPU_CX, G_RAM_CX, G_GPU_CX, G_VMX };
        const int byc[] = { G_CPU_CY, G_RAM_CY, G_GPU_CY, G_VMY };
        const uint16_t gcol[] = {
            tft.color565(0,   220, 245),   // CPU  — neon cyan
            tft.color565(80,  255, 100),   // RAM  — neon green
            tft.color565(240,  40, 200),   // GPU  — neon magenta
            tft.color565(255, 200,   0),   // GPU MEM — neon yellow
        };
        for (int i = 0; i < 4; i++) {
            int bx = bxc[i] - GR - 4;
            int by = byc[i] - GR - 4;
            int bw = 2 * (GR + 4);
            int bh = GR + 17;
            tft.fillRect(bx, by, bw, bh, TFT_BLACK);
            drawCybBox(bx, by, bw, bh, gcol[i]);
        }

        // Neon dividers either side of the network strip (survives updateGauges redraws)
        tft.drawFastHLine(10, 106, 300, tft.color565(0, 50, 70));
        tft.drawFastHLine(10, 120, 300, tft.color565(0, 50, 70));

        updateGauges();

        // GPU name centred below the bottom gauge row
        if (sysData.gpu_name[0] != '\0') {
            tft.setTextSize(1);
            tft.setTextColor(tft.color565(170, 25, 140), TFT_BLACK);
            int nx = 160 - (int)strlen(sysData.gpu_name) * 3;
            tft.drawString(sysData.gpu_name, nx < 0 ? 0 : nx, 212);
        }
    }

    // Footer
    tft.fillRect(0, 219, 3, 21, tft.color565(0, 200, 230));        // left accent bar
    tft.setTextSize(1);
    tft.setTextColor(tft.color565(0, 100, 118), TFT_BLACK);
    tft.drawString("[C] EXIT", 8, 225);
    drawBatteryStatus(TFT_BLACK);
}

// -------------------------------------------------------------------------
void sysStatsScreen()
{
    while (digitalRead(WIO_5S_PRESS) == LOW) { delay(10); }

    sysData.valid = false;
    sysDataVersion++;

    bool bleActivated = false;
    if (bleInitDone) {
        bleSetActive(true);
        bleActivated = true;
    }

    drawSysStats();

    uint16_t drawnVersion = sysDataVersion;
    bool     validFrame   = false;
    uint32_t lastDataMs   = 0;

    while (true)
    {
        checkSerial();
        checkBLE();

        // BLE init may still be pending on first entry (~3s after boot).
        if (!bleActivated && bleInitDone) {
            bleSetActive(true);
            bleActivated = true;
        }

        // Refresh lastDataMs eagerly so a just-arrived packet can't be
        // immediately evicted by the timeout check in the same iteration.
        if (sysData.valid && sysDataVersion != drawnVersion)
            lastDataMs = millis();

        // Timeout: revert to no-data panel if stream stops for >6 s.
        // sysstat_sender.py polls every 2 s, so 6 s = 3 missed packets.
        if (sysData.valid && lastDataMs && millis() - lastDataMs > 6000)
        {
            sysData.valid = false;
            sysDataVersion++;
        }

        if (sysDataVersion != drawnVersion)
        {
            drawnVersion = sysDataVersion;
            if (!validFrame && sysData.valid) {
                validFrame = true;
                drawSysStats();
            } else if (validFrame && sysData.valid) {
                updateGauges();
            } else if (!sysData.valid) {
                validFrame = false;
                drawSysStats();   // shows the no-data panel
            }
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
        delay(10);
    }
}