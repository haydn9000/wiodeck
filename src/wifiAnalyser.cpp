// wifiAnalyser.cpp — Wi-Fi analyser: list view + 2.4 GHz and 5 GHz channel maps.
//
// LIST view  — networks sorted by signal (SSID, band, ch, dBm, bar); capped to avoid cut-off
// MAP  view  — channel congestion chart; UP=2.4 GHz map, DOWN=5 GHz map
//
// Controls:
//   [LEFT/RIGHT]   Toggle list ↔ channel map
//   [UP]           (map) Switch to 2.4 GHz channel map
//   [DOWN]         (map) Switch to 5 GHz channel map
//   [PRESS]        Rescan
//   [B]            Screenshot
//   [C]            Back to menu
//
// 2.4 GHz has 13 non-overlapping channels (1–13); channels 1, 6 and 11 are the
// classic non-overlapping trio for 802.11b/g/n. The yellow triangle marks the
// least-congested of those three.
//
// All channel stats are derived from the deduplicated network list, so the
// numbers in the map always match the network count shown in the list view.

#include <Arduino.h>
#include "globals.h"
#include "rpcWiFi.h"

// ---------------------------------------------------------------------------
static const int WIFI_MAX    = 32;
static const int CH_24_MAX   = 13;
static const int CH_5G_SLOTS = 24;

struct WifiResult {
    char ssid[33];
    int  rssi;
    int  channel;
    bool open;
};

struct ChanData   { int apCount; int bestRssi; };
struct ChanData5G { int channel; int apCount; int bestRssi; };

static WifiResult  wfResults[WIFI_MAX];
static int         wfCount        = 0;
static ChanData    ch24[CH_24_MAX];
static ChanData5G  ch5g[CH_5G_SLOTS];
static int         ch5gCount      = 0;   // unique 5 GHz channels with APs
static int         fiveGhzCount   = 0;   // total unique 5 GHz networks
static int         fiveGhzBestRssi = -120;

static enum { VIEW_LIST, VIEW_MAP }  viewMode = VIEW_LIST;
static enum { MAP_24G,   MAP_5G  }   mapBand  = MAP_24G;

// ---------------------------------------------------------------------------
// SHARED HELPERS
// ---------------------------------------------------------------------------

static uint16_t rssiColor(int rssi)
{
    if (rssi >= -60) return tft.color565(60,  210, 100);
    if (rssi >= -75) return tft.color565(210, 175,   0);
    return                  tft.color565(210,  65,  55);
}

static void drawWifiHeader(const char* tag, const char* modeLabel, int count = -1)
{
    tft.fillRect(0, 0, 320, 30, tft.color565(0, 8, 20));
    tft.fillRect(0, 0, 3,   30, tft.color565(30, 210, 80));
    tft.setTextSize(2);
    tft.setTextColor(tft.color565(30, 210, 80), tft.color565(0, 8, 20));
    tft.drawString(tag, 10, 7);
    tft.setTextSize(1);
    tft.setTextColor(tft.color565(20, 148, 60), tft.color565(0, 8, 20));
    // Tag zone: show "count NETS  modeLabel" or just modeLabel, left of battery
    char tagBuf[24];
    if (count >= 0)
        snprintf(tagBuf, sizeof(tagBuf), "%d NET%s  %s", count, count == 1 ? "" : "S", modeLabel);
    else
        snprintf(tagBuf, sizeof(tagBuf), "%s", modeLabel);
    tft.drawString(tagBuf, 160, 11);
    tft.drawFastHLine(0, 29, 320, tft.color565(0, 80, 40));
}

static void drawWifiFooter(const char* msg)
{
    tft.fillRect(0, 215, 320, 25, TFT_BLACK);
    tft.fillRect(0, 219, 3, 21, tft.color565(30, 210, 80));
    tft.setTextSize(1);
    tft.setTextColor(tft.color565(0, 100, 40), TFT_BLACK);
    tft.drawString(msg, 8, 225);
}

// Filled upward-pointing triangle — recommendation marker
static void drawRecTriangle(int tipX, int tipY, uint16_t col)
{
    tft.fillTriangle(tipX,     tipY,
                     tipX - 6, tipY + 10,
                     tipX + 6, tipY + 10,
                     col);
}

static void drawWifiScanning()
{
    tft.fillScreen(TFT_BLACK);
    drawWifiHeader("// WIFI", "SCAN");

    uint16_t nc  = tft.color565(30, 210, 80);
    uint16_t nbg = tft.color565(0, 10, 5);
    tft.fillRect(10, 48, 300, 140, nbg);
    const int t = 12;
    tft.drawFastHLine(10,  48,  t, nc); tft.drawFastVLine(10,  48,  t, nc);
    tft.drawFastHLine(298, 48,  t, nc); tft.drawFastVLine(309, 48,  t, nc);
    tft.drawFastHLine(10,  187, t, nc); tft.drawFastVLine(10,  175, t, nc);
    tft.drawFastHLine(298, 187, t, nc); tft.drawFastVLine(309, 175, t, nc);
    tft.drawFastHLine(11, 49, 298, tft.color565(0, 40, 20));
    tft.setTextSize(2);
    tft.setTextColor(nc, nbg);
    tft.drawString("> SCANNING...", 24, 80);
    tft.setTextSize(1);
    tft.setTextColor(tft.color565(0, 148, 60), nbg);
    tft.drawString("SEARCHING 2.4GHz + 5GHz BANDS", 24, 110);
    tft.drawString("PRESS [C] TO CANCEL", 24, 125);
    drawWifiFooter("[C] CANCEL");
    drawBatteryStatus(TFT_BLACK);
}

// ---------------------------------------------------------------------------
// LIST VIEW
// ---------------------------------------------------------------------------

static void drawListView()
{
    tft.fillScreen(TFT_BLACK);
    drawWifiHeader("// WIFI", "LIST", wfCount);

    if (wfCount <= 0) {
        uint16_t nc  = tft.color565(30, 210, 80);
        uint16_t nbg = tft.color565(0, 10, 5);
        tft.fillRect(10, 48, 300, 140, nbg);
        const int t = 12;
        tft.drawFastHLine(10,  48,  t, nc); tft.drawFastVLine(10,  48,  t, nc);
        tft.drawFastHLine(298, 48,  t, nc); tft.drawFastVLine(309, 48,  t, nc);
        tft.drawFastHLine(10,  187, t, nc); tft.drawFastVLine(10,  175, t, nc);
        tft.drawFastHLine(298, 187, t, nc); tft.drawFastVLine(309, 175, t, nc);
        tft.setTextSize(2);
        tft.setTextColor(nc, nbg);
        tft.drawString("> NO_DATA", 50, 90);
        tft.setTextSize(1);
        tft.setTextColor(tft.color565(0, 148, 60), nbg);
        tft.drawString("NO NETWORKS FOUND NEARBY", 50, 118);
        tft.drawString("PRESS [PRESS] TO RESCAN", 50, 134);
    } else {
        tft.setTextSize(1);
        tft.setTextColor(tft.color565(20, 148, 60), TFT_BLACK);
        tft.drawString("NETWORK",  20, 35);
        tft.drawString("CH",      170, 35);
        tft.drawString("BAND",    192, 35);
        tft.drawString("dBm",     237, 35);
        tft.drawString("SIG",     272, 35);

        // Cap rows so the last entry doesn't clip into the footer.
        // Row i starts at y=46+i*15; size-1 text is 8px tall.
        // Row 11 ends at 46+11*15+8 = 219 — right at the footer. Cap at 11.
        int show = wfCount < 11 ? wfCount : 11;
        for (int i = 0; i < show; i++) {
            WifiResult& r   = wfResults[i];
            uint16_t    rc  = rssiColor(r.rssi);
            int         rowY = 46 + i * 15;

            char ssidBuf[18];
            strncpy(ssidBuf, r.ssid, 17);
            ssidBuf[17] = '\0';
            tft.setTextColor(tft.color565(200, 235, 215), TFT_BLACK);
            tft.drawString(ssidBuf, 20, rowY);

            char chBuf[4];
            snprintf(chBuf, sizeof(chBuf), "%d", r.channel);
            tft.setTextColor(tft.color565(120, 180, 130), TFT_BLACK);
            tft.drawString(chBuf, 170, rowY);

            bool is5G = r.channel > 14;
            tft.setTextColor(is5G ? tft.color565(120, 200, 255) : tft.color565(80, 220, 120), TFT_BLACK);
            tft.drawString(is5G ? "5G" : "2.4", 192, rowY);

            char rssiBuf[8];
            snprintf(rssiBuf, sizeof(rssiBuf), "%d", r.rssi);
            tft.setTextColor(rc, TFT_BLACK);
            tft.drawString(rssiBuf, 237, rowY);

            int barFill = (int)((float)(r.rssi + 100) / 70.0f * 40.0f);
            if (barFill < 0)  barFill = 0;
            if (barFill > 40) barFill = 40;
            if (barFill > 0) tft.fillRect(265, rowY + 1, barFill, 7, rc);
            tft.fillRect(265 + barFill, rowY + 1, 40 - barFill, 7, tft.color565(10, 22, 14));
            tft.drawRect(265, rowY, 40, 9, tft.color565(20, 50, 28));
        }
    }

    drawWifiFooter("[</>] MAP   [PRESS] RESCAN   [C] BACK");
    drawBatteryStatus(TFT_BLACK);
}

// ---------------------------------------------------------------------------
// CHANNEL MAP — 2.4 GHz  (channels 1–13)
// ---------------------------------------------------------------------------

static void drawChannelMap24()
{
    tft.fillScreen(TFT_BLACK);
    drawWifiHeader("// WIFI", "2.4G", wfCount);

    const int CHART_X = 14;
    const int CHART_Y = 195;
    const int CHART_H = 140;
    const int CHART_W = 292;
    const int BAR_W   = 18;
    const int BAR_GAP = 4;

    const uint16_t axisCol  = tft.color565(20, 50, 28);
    const uint16_t labelCol = tft.color565(20, 148, 60);
    const uint16_t dimCol   = tft.color565(30, 60, 40);

    int maxCount = 1;
    for (int c = 0; c < CH_24_MAX; c++)
        if (ch24[c].apCount > maxCount) maxCount = ch24[c].apCount;

    tft.drawFastHLine(CHART_X - 2, CHART_Y + 1, CHART_W + 4, axisCol);

    // Grid lines
    for (int g = 1; g <= 4; g++) {
        int gy = CHART_Y - (int)(CHART_H * g / 4.0f);
        tft.drawFastHLine(CHART_X - 2, gy, CHART_W + 4, tft.color565(12, 28, 16));
    }

    // Find recommended non-overlapping channel (1, 6, 11)
    int bestCh = 0, bestScore = 999;
    for (int c : {0, 5, 10}) {
        int score = ch24[c].apCount * 2;
        if (c > 0)           score += ch24[c-1].apCount;
        if (c < CH_24_MAX-1) score += ch24[c+1].apCount;
        if (score < bestScore) { bestScore = score; bestCh = c; }
    }

    for (int c = 0; c < CH_24_MAX; c++) {
        int bx = CHART_X + c * (BAR_W + BAR_GAP);

        if (ch24[c].apCount > 0) {
            int barH = (int)((float)ch24[c].apCount / maxCount * CHART_H);
            if (barH < 6) barH = 6;
            uint16_t barCol = rssiColor(ch24[c].bestRssi);

            tft.fillRect(bx, CHART_Y - barH, BAR_W, barH, barCol);
            tft.fillRect(bx, CHART_Y - barH, BAR_W, 2, tft.color565(220, 240, 220));

            if (barH >= 14) {
                tft.setTextSize(1);
                tft.setTextColor(TFT_BLACK, barCol);
                char cnt[3]; snprintf(cnt, 3, "%d", ch24[c].apCount);
                int tw = tft.textWidth(cnt);
                tft.drawString(cnt, bx + (BAR_W - tw) / 2, CHART_Y - barH + 3);
            }
        } else {
            tft.drawRect(bx, CHART_Y - 6, BAR_W, 6, dimCol);
        }

        tft.setTextSize(1);
        tft.setTextColor(ch24[c].apCount > 0 ? labelCol : dimCol, TFT_BLACK);
        char lbl[3]; snprintf(lbl, 3, "%d", c + 1);
        int tw = tft.textWidth(lbl);
        tft.drawString(lbl, bx + (BAR_W - tw) / 2, CHART_Y + 4);
    }

    // Recommendation triangle: sits just above the top of the recommended bar.
    // Falls back to the chart floor if that channel is empty.
    {
        int recBx   = CHART_X + bestCh * (BAR_W + BAR_GAP);
        int recBarH = (ch24[bestCh].apCount > 0)
                        ? (int)((float)ch24[bestCh].apCount / maxCount * CHART_H)
                        : 6;
        if (recBarH < 6) recBarH = 6;
        int tipY = CHART_Y - recBarH - 14;   // 14px clear gap above bar cap
        if (tipY < 54) tipY = 54;            // don't overlap the title row
        drawRecTriangle(recBx + BAR_W / 2, tipY, tft.color565(255, 230, 0));
    }

    // Title only — 5G hint lives in the footer where it belongs
    tft.setTextSize(1);
    tft.setTextColor(labelCol, TFT_BLACK);
    tft.drawString("2.4 GHz  CHANNEL MAP", CHART_X, 32);

    if (fiveGhzCount > 0)
        drawWifiFooter("[</>] LIST [DN] 5G MAP [PRESS] RESCAN [C] BACK");
    else
        drawWifiFooter("[</>] LIST   [PRESS] RESCAN   [C] BACK");
    drawBatteryStatus(TFT_BLACK);
}

// ---------------------------------------------------------------------------
// CHANNEL MAP — 5 GHz  (dynamic: only channels with APs)
// ---------------------------------------------------------------------------

static void drawChannelMap5G()
{
    tft.fillScreen(TFT_BLACK);
    drawWifiHeader("// WIFI", "5G  ", wfCount);

    const int CHART_X = 14;
    const int CHART_Y = 195;
    const int CHART_H = 140;
    const int CHART_W = 292;

    const uint16_t labelCol = tft.color565(40, 140, 210);
    const uint16_t dimCol   = tft.color565(30, 50, 70);
    const uint16_t axisCol  = tft.color565(20, 40, 60);

    tft.setTextSize(1);
    tft.setTextColor(labelCol, TFT_BLACK);
    tft.drawString("5 GHz  CHANNEL MAP", CHART_X, 32);

    if (ch5gCount == 0) {
        tft.setTextSize(2);
        tft.setTextColor(tft.color565(120, 200, 255), TFT_BLACK);
        const char* msg = "NO 5GHz APs";
        tft.drawString(msg, (320 - tft.textWidth(msg)) / 2, 100);
        tft.setTextSize(1);
        tft.setTextColor(dimCol, TFT_BLACK);
        tft.drawString("[UP] BACK TO 2.4GHz MAP", CHART_X, 125);
    } else {
        // Sort by channel number
        for (int i = 0; i < ch5gCount - 1; i++)
            for (int j = i + 1; j < ch5gCount; j++)
                if (ch5g[j].channel < ch5g[i].channel) {
                    ChanData5G tmp = ch5g[i]; ch5g[i] = ch5g[j]; ch5g[j] = tmp;
                }

        int maxCount = 1;
        for (int c = 0; c < ch5gCount; c++)
            if (ch5g[c].apCount > maxCount) maxCount = ch5g[c].apCount;

        // Dynamic bar sizing
        const int totalGap = (ch5gCount > 1) ? (ch5gCount - 1) * 3 : 0;
        int barW = (CHART_W - totalGap) / ch5gCount;
        if (barW > 30) barW = 30;
        if (barW <  8) barW =  8;
        int barGap = (ch5gCount > 1) ? (CHART_W - ch5gCount * barW) / (ch5gCount - 1) : 0;
        if (barGap < 2) barGap = 2;

        tft.drawFastHLine(CHART_X - 2, CHART_Y + 1, CHART_W + 4, axisCol);
        for (int g = 1; g <= 4; g++) {
            int gy = CHART_Y - (int)(CHART_H * g / 4.0f);
            tft.drawFastHLine(CHART_X - 2, gy, CHART_W + 4, tft.color565(8, 18, 32));
        }

        for (int c = 0; c < ch5gCount; c++) {
            int bx = CHART_X + c * (barW + barGap);
            int barH = (int)((float)ch5g[c].apCount / maxCount * CHART_H);
            if (barH < 6) barH = 6;
            uint16_t barCol = rssiColor(ch5g[c].bestRssi);

            tft.fillRect(bx, CHART_Y - barH, barW, barH, barCol);
            tft.fillRect(bx, CHART_Y - barH, barW, 2, tft.color565(200, 225, 255));

            if (barH >= 14) {
                tft.setTextSize(1);
                tft.setTextColor(TFT_BLACK, barCol);
                char cnt[3]; snprintf(cnt, 3, "%d", ch5g[c].apCount);
                int tw = tft.textWidth(cnt);
                tft.drawString(cnt, bx + (barW - tw) / 2, CHART_Y - barH + 3);
            }

            tft.setTextSize(1);
            tft.setTextColor(labelCol, TFT_BLACK);
            char lbl[4]; snprintf(lbl, 4, "%d", ch5g[c].channel);
            int tw = tft.textWidth(lbl);
            tft.drawString(lbl, bx + (barW - tw) / 2, CHART_Y + 4);
        }
    }

    drawWifiFooter("[</>] LIST [UP] 2.4G [PRESS] RESCAN [C] BACK");
    drawBatteryStatus(TFT_BLACK);
}

// ---------------------------------------------------------------------------
// SCAN + DATA COLLECTION
// ---------------------------------------------------------------------------

static bool doWifiScan()
{
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);
    WiFi.scanNetworks(true);

    unsigned long scanStart = millis();
    int total = 0;
    while (true) {
        int n = WiFi.scanComplete();
        if (n >= 0)               { total = n; break; }
        if (n == WIFI_SCAN_FAILED)  break;
        if (millis() - scanStart > 15000) break;
        if (digitalRead(WIO_KEY_C) == LOW) {
            WiFi.scanDelete();
            while (digitalRead(WIO_KEY_C) == LOW) delay(10);
            return false;
        }
        delay(20);
    }
    WiFi.disconnect();

    // Reset all buffers
    wfCount         = 0;
    ch5gCount       = 0;
    fiveGhzCount    = 0;
    fiveGhzBestRssi = -120;
    for (int c = 0; c < CH_24_MAX;   c++) { ch24[c].apCount = 0; ch24[c].bestRssi = -120; }
    for (int c = 0; c < CH_5G_SLOTS; c++) { ch5g[c] = {0, 0, -120}; }

    // Build deduplicated list
    for (int i = 0; i < total; i++) {
        String ssid    = WiFi.SSID(i);
        int    rssi    = WiFi.RSSI(i);
        int    channel = WiFi.channel(i);
        bool   open    = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);

        bool found = false;
        for (int j = 0; j < wfCount; j++) {
            if (ssid == wfResults[j].ssid) {
                if (rssi > wfResults[j].rssi) {
                    wfResults[j].rssi    = rssi;
                    wfResults[j].channel = channel;
                }
                found = true;
                break;
            }
        }
        if (!found && wfCount < WIFI_MAX) {
            const char* src = ssid.length() ? ssid.c_str() : "(hidden)";
            strncpy(wfResults[wfCount].ssid, src, 32);
            wfResults[wfCount].ssid[32] = '\0';
            wfResults[wfCount].rssi    = rssi;
            wfResults[wfCount].channel = channel;
            wfResults[wfCount].open    = open;
            wfCount++;
        }
    }
    WiFi.scanDelete();

    // Sort list by RSSI descending
    for (int i = 0; i < wfCount - 1; i++)
        for (int j = i + 1; j < wfCount; j++)
            if (wfResults[j].rssi > wfResults[i].rssi) {
                WifiResult tmp = wfResults[i];
                wfResults[i]  = wfResults[j];
                wfResults[j]  = tmp;
            }

    // Accumulate channel stats from the deduplicated list so counts always
    // equal wfCount (no raw-vs-unique discrepancy).
    for (int i = 0; i < wfCount; i++) {
        int ch   = wfResults[i].channel;
        int rssi = wfResults[i].rssi;
        if (ch >= 1 && ch <= CH_24_MAX) {
            ch24[ch-1].apCount++;
            if (rssi > ch24[ch-1].bestRssi) ch24[ch-1].bestRssi = rssi;
        } else if (ch > 14) {
            fiveGhzCount++;
            if (rssi > fiveGhzBestRssi) fiveGhzBestRssi = rssi;
            bool found = false;
            for (int j = 0; j < ch5gCount; j++) {
                if (ch5g[j].channel == ch) {
                    ch5g[j].apCount++;
                    if (rssi > ch5g[j].bestRssi) ch5g[j].bestRssi = rssi;
                    found = true; break;
                }
            }
            if (!found && ch5gCount < CH_5G_SLOTS) {
                ch5g[ch5gCount++] = { ch, 1, rssi };
            }
        }
    }

    return true;
}

static void refreshView()
{
    if      (viewMode == VIEW_LIST)               drawListView();
    else if (viewMode == VIEW_MAP && mapBand == MAP_24G) drawChannelMap24();
    else                                          drawChannelMap5G();
}

// ---------------------------------------------------------------------------
// MAIN ENTRY POINT
// ---------------------------------------------------------------------------

void wifiAnalyserScreen()
{
    while (digitalRead(WIO_5S_PRESS) == LOW) delay(10);
    viewMode = VIEW_LIST;
    mapBand  = MAP_24G;

    drawWifiScanning();
    if (!doWifiScan()) {
        bleReinit();
        delay(50);
        return;
    }
    refreshView();

    while (true)
    {
        // Toggle list ↔ map
        if (digitalRead(WIO_5S_LEFT) == LOW || digitalRead(WIO_5S_RIGHT) == LOW) {
            while (digitalRead(WIO_5S_LEFT) == LOW || digitalRead(WIO_5S_RIGHT) == LOW) delay(10);
            viewMode = (viewMode == VIEW_LIST) ? VIEW_MAP : VIEW_LIST;
            if (viewMode == VIEW_MAP) mapBand = MAP_24G;
            refreshView();
            delay(50);
        }

        // In map mode: UP = 2.4 GHz chart, DOWN = 5 GHz chart
        if (viewMode == VIEW_MAP) {
            if (digitalRead(WIO_5S_UP) == LOW) {
                while (digitalRead(WIO_5S_UP) == LOW) delay(10);
                mapBand = MAP_24G;
                refreshView();
                delay(50);
            }
            if (digitalRead(WIO_5S_DOWN) == LOW) {
                while (digitalRead(WIO_5S_DOWN) == LOW) delay(10);
                mapBand = MAP_5G;
                refreshView();
                delay(50);
            }
        }

        // Rescan
        if (digitalRead(WIO_5S_PRESS) == LOW) {
            while (digitalRead(WIO_5S_PRESS) == LOW) delay(10);
            drawWifiScanning();
            if (!doWifiScan()) {
                bleReinit();
                delay(50);
                return;
            }
            refreshView();
        }

        if (digitalRead(WIO_KEY_B) == LOW) {
            while (digitalRead(WIO_KEY_B) == LOW) delay(10);
            takeScreenshot();
        }

        if (digitalRead(WIO_KEY_C) == LOW) {
            while (digitalRead(WIO_KEY_C) == LOW) delay(10);
            bleReinit();
            delay(50);
            return;
        }

        delay(20);
    }
}
