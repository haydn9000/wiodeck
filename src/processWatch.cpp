// processWatch.cpp — Top-5 CPU process monitor.
// Data arrives via USB serial or BLE as compact JSON:
//   {"p":[{"n":"chrome.exe","c":25.5,"m":1.2},...], "tc":13, "tm":45, "ok":1}
// Run tools/process_sender.py on the host.

#include <Arduino.h>
#include "globals.h"

struct ProcessEntry { char name[29]; float cpu; float mem; };   // mem in MB (< 1000) or MB float (≥ 1000 displayed as GB)
struct ProcessData  { ProcessEntry procs[5]; int count; bool valid;
                      float total_cpu; float total_mem; };

static ProcessData procData        = {};
static uint16_t    procDataVersion = 0;

// -------------------------------------------------------------------------
bool parseProcessJson(const char* json)
{
    const char* p = strstr(json, "\"p\":[");
    if (!p) return false;
    p += 5;

    ProcessData tmp = {};

    while (*p && *p != ']' && tmp.count < 5)
    {
        while (*p && (*p == ',' || *p == ' ')) p++;
        if (*p != '{') { if (*p) p++; continue; }
        p++;

        ProcessEntry e = {};

        while (*p && *p != '}')
        {
            while (*p == ' ' || *p == ',') p++;
            if (*p != '"') { if (*p) p++; continue; }
            p++;
            char key = *p;
            while (*p && *p != ':') p++;
            if (!*p) break;
            p++;
            while (*p == ' ') p++;

            if (key == 'n' && *p == '"')
            {
                p++;
                int i = 0;
                while (*p && *p != '"' && i < 28) e.name[i++] = *p++;
                e.name[i] = '\0';
                if (*p == '"') p++;
            }
            else if (key == 'c') { e.cpu = atof(p); while (*p && *p != ',' && *p != '}') p++; }
            else if (key == 'm') { e.mem = atof(p); while (*p && *p != ',' && *p != '}') p++; }
            else                 {                   while (*p && *p != ',' && *p != '}') p++; }
        }

        if (e.name[0] != '\0') tmp.procs[tmp.count++] = e;
        p = strchr(p, '}');
        if (p) p++;
    }

    tmp.valid = (tmp.count > 0);

    const char* tcPtr = strstr(json, "\"tc\":");
    if (tcPtr) tmp.total_cpu = atof(tcPtr + 5);
    const char* tmPtr = strstr(json, "\"tm\":");
    if (tmPtr) tmp.total_mem = atof(tmPtr + 5);

    procData  = tmp;
    procDataVersion++;
    return tmp.valid;
}

// -------------------------------------------------------------------------
static uint16_t cpuBarColor(float pct)
{
    if (pct < 25.0f) return tft.color565(0, 210, 230);
    if (pct < 60.0f) return tft.color565(210, 175, 0);
    return tft.color565(210, 65, 55);
}

// -------------------------------------------------------------------------
static void drawProcessWatch()
{
    tft.fillScreen(TFT_BLACK);

    // Header
    tft.fillRect(0, 0, 320, 30, tft.color565(0, 8, 20));
    tft.fillRect(0, 0, 3, 30, tft.color565(0, 220, 245));
    tft.setTextSize(2);
    tft.setTextColor(tft.color565(0, 220, 245), tft.color565(0, 8, 20));
    tft.drawString("// PROCESSES", 10, 7);
    tft.setTextSize(1);
    tft.setTextColor(tft.color565(0, 148, 170), tft.color565(0, 8, 20));
    tft.drawString("WATCH", 160, 11);
    tft.drawFastHLine(0, 29, 320, tft.color565(0, 80, 100));
    for (int xi = 8; xi < 320; xi += 14)
        tft.drawFastVLine(xi, 27, 4, tft.color565(0, 140, 165));

    if (!procData.valid)
    {
        // No-data panel — full height below header, matching sysStats layout
        uint16_t nc   = tft.color565(0, 220, 245);
        uint16_t nbg  = tft.color565(0, 8, 18);
        uint16_t ndim = tft.color565(0, 130, 155);
        tft.fillRect(10, 48, 300, 160, nbg);
        int t = 12;
        tft.drawFastHLine(10,  48,  t, nc); tft.drawFastVLine(10,  48,  t, nc);
        tft.drawFastHLine(298, 48,  t, nc); tft.drawFastVLine(309, 48,  t, nc);
        tft.drawFastHLine(10,  207, t, nc); tft.drawFastVLine(10,  195, t, nc);
        tft.drawFastHLine(298, 207, t, nc); tft.drawFastVLine(309, 195, t, nc);
        tft.drawFastHLine(11, 49, 298, tft.color565(0, 40, 60));
        tft.setTextSize(2);
        tft.setTextColor(nc, nbg);
        tft.drawString("> NO_DATA", 50, 90);
        tft.setTextSize(1);
        tft.setTextColor(ndim, nbg);
        tft.drawString("AWAITING DATA STREAM...", 50, 118);
        tft.drawString("USB:  process_sender.py <port>", 50, 134);
        tft.drawString("BLE:  process_sender.py --ble", 50, 150);
        tft.setTextColor(tft.color565(0, 80, 100), nbg);
        tft.drawString(Serial ? "STATUS: USB ACTIVE" : "STATUS: WAITING...", 50, 166);
        char addrLine[40];
        snprintf(addrLine, sizeof(addrLine), "ADDR:  %s",
                 bleInitDone ? getBLEAddress() : "INIT...");
        tft.drawString(addrLine, 50, 182);
    }
    else
    {
        // --- System totals strip (y=30-43) ---
        uint16_t stBg = tft.color565(0, 5, 12);
        tft.fillRect(0, 30, 320, 14, stBg);
        tft.setTextSize(1);
        tft.setTextColor(tft.color565(0, 175, 200), stBg);
        tft.drawString("CPU", 10, 33);
        tft.drawString("RAM", 162, 33);
        uint16_t cc = cpuBarColor(procData.total_cpu);
        uint16_t rc = procData.total_mem < 50.0f ? tft.color565(60, 210, 100) :
                      procData.total_mem < 80.0f ? tft.color565(210, 175, 0)  :
                                                    tft.color565(210, 65, 55);
        int bfC = (int)(55 * procData.total_cpu / 100.0f); if (bfC > 55) bfC = 55;
        int bfR = (int)(55 * procData.total_mem / 100.0f); if (bfR > 55) bfR = 55;
        if (bfC > 0) tft.fillRect(28, 34, bfC, 6, cc);
        tft.drawRect(28, 34, 55, 6, tft.color565(0, 30, 45));
        if (bfR > 0) tft.fillRect(180, 34, bfR, 6, rc);
        tft.drawRect(180, 34, 55, 6, tft.color565(0, 30, 45));
        char cbuf[10]; sprintf(cbuf, "%.1f%%", procData.total_cpu);
        tft.setTextColor(cc, stBg); tft.drawString(cbuf, 87, 33);
        char rbuf[10]; sprintf(rbuf, "%.1f%%", procData.total_mem);
        tft.setTextColor(rc, stBg); tft.drawString(rbuf, 239, 33);

        // Column headers
        tft.setTextSize(1);
        tft.setTextColor(tft.color565(0, 175, 190), TFT_BLACK);
        tft.drawString("PROCESS", 20, 46);
        tft.drawString("CPU%", 202, 46);
        tft.drawString("MEM", 272, 46);
        tft.drawFastHLine(0, 54, 320, tft.color565(0, 30, 42));

        for (int i = 0; i < procData.count && i < 5; i++)
        {
            int rowY = 57 + i * 32;
            ProcessEntry& e = procData.procs[i];
            uint16_t col = cpuBarColor(e.cpu);

            tft.setTextSize(1);
            tft.setTextColor(tft.color565(210, 240, 255), TFT_BLACK);
            tft.drawString(e.name, 20, rowY);

            int barX = 20, barY = rowY + 11, barW = 175, barH = 11;
            float fill = e.cpu > 100.0f ? 100.0f : e.cpu;
            if (fill > 0.0f)
                tft.fillRect(barX, barY, (int)(barW * fill / 100.0f), barH, col);
            tft.drawRect(barX, barY, barW, barH, tft.color565(20, 40, 50));

            char cpuBuf[8]; sprintf(cpuBuf, "%.1f", e.cpu);
            tft.setTextColor(col, TFT_BLACK);
            tft.drawString(cpuBuf, 202, barY + 1);

            char memBuf[8];
            if (e.mem < 1000.0f) sprintf(memBuf, "%.1fM", e.mem);
            else                 sprintf(memBuf, "%.1fG", e.mem / 1024.0f);
            tft.setTextColor(tft.color565(200, 225, 245), TFT_BLACK);
            tft.drawString(memBuf, 272, barY + 1);
        }
    }

    // Footer
    tft.fillRect(0, 219, 3, 21, tft.color565(0, 220, 245));
    tft.setTextSize(1);
    tft.setTextColor(tft.color565(0, 100, 118), TFT_BLACK);
    tft.drawString("[C] BACK", 8, 225);

    drawBatteryStatus(TFT_BLACK);
}

// -------------------------------------------------------------------------
// Targeted in-place update — only rewrites changing pixels so the header,
// footer and column labels never flash.  Only called when procData.valid.
static void drawProcessWatchUpdate()
{
    uint16_t stBg = tft.color565(0, 5, 12);

    // --- Totals strip: erase only bars and text, leave "CPU"/"RAM" labels ---
    tft.fillRect(28,  34, 55, 6, stBg);   // CPU bar bg
    tft.fillRect(180, 34, 55, 6, stBg);   // RAM bar bg
    tft.fillRect(87,  33, 44, 7, stBg);   // CPU% text  ("100.0%" = 6 ch)
    tft.fillRect(239, 33, 44, 7, stBg);   // RAM% text

    uint16_t cc = cpuBarColor(procData.total_cpu);
    uint16_t rc = procData.total_mem < 50.0f ? tft.color565(60, 210, 100) :
                  procData.total_mem < 80.0f ? tft.color565(210, 175, 0)  :
                                               tft.color565(210, 65, 55);
    int bfC = (int)(55 * procData.total_cpu / 100.0f); if (bfC > 55) bfC = 55;
    int bfR = (int)(55 * procData.total_mem / 100.0f); if (bfR > 55) bfR = 55;
    if (bfC > 0) tft.fillRect(28,  34, bfC, 6, cc);
    tft.drawRect(28,  34, 55, 6, tft.color565(0, 30, 45));
    if (bfR > 0) tft.fillRect(180, 34, bfR, 6, rc);
    tft.drawRect(180, 34, 55, 6, tft.color565(0, 30, 45));
    char cbuf[10]; sprintf(cbuf, "%.1f%%", procData.total_cpu);
    tft.setTextColor(cc, stBg); tft.drawString(cbuf, 87, 33);
    char rbuf[10]; sprintf(rbuf, "%.1f%%", procData.total_mem);
    tft.setTextColor(rc, stBg); tft.drawString(rbuf, 239, 33);

    // --- Process rows: erase and redraw only each row's dynamic fields ---
    for (int i = 0; i < 5; i++)
    {
        int rowY = 57 + i * 32;
        int barY = rowY + 11;

        if (i < procData.count)
        {
            ProcessEntry& e = procData.procs[i];
            uint16_t col = cpuBarColor(e.cpu);

            tft.fillRect(20,  rowY,     295, 9,  TFT_BLACK);  // name (28 ch max)
            tft.fillRect(20,  barY,     175, 11, TFT_BLACK);  // bar
            tft.fillRect(202, barY + 1, 31,  7,  TFT_BLACK);  // cpu% text
            tft.fillRect(272, barY + 1, 44,  7,  TFT_BLACK);  // mem text

            tft.setTextSize(1);
            tft.setTextColor(tft.color565(210, 240, 255), TFT_BLACK);
            tft.drawString(e.name, 20, rowY);

            float fill = e.cpu > 100.0f ? 100.0f : e.cpu;
            if (fill > 0.0f)
                tft.fillRect(20, barY, (int)(175 * fill / 100.0f), 11, col);
            tft.drawRect(20, barY, 175, 11, tft.color565(20, 40, 50));

            char cpuBuf[8]; sprintf(cpuBuf, "%.1f", e.cpu);
            tft.setTextColor(col, TFT_BLACK);
            tft.drawString(cpuBuf, 202, barY + 1);

            char memBuf[8];
            if (e.mem < 1000.0f) sprintf(memBuf, "%.1fM", e.mem);
            else                 sprintf(memBuf, "%.1fG", e.mem / 1024.0f);
            tft.setTextColor(tft.color565(200, 225, 245), TFT_BLACK);
            tft.drawString(memBuf, 272, barY + 1);
        }
        else
        {
            // Row may have held a process last update — erase it
            tft.fillRect(20,  rowY,     295, 9,  TFT_BLACK);
            tft.fillRect(20,  barY,     295, 11, TFT_BLACK);
        }
    }

    drawBatteryStatus(TFT_BLACK);
}

// -------------------------------------------------------------------------
void processWatchScreen()
{
    while (digitalRead(WIO_5S_PRESS) == LOW) delay(10);

    procData.valid = false;
    procDataVersion++;

    bool bleActivated = false;
    if (bleInitDone) {
        bleSetActive(true);
        bleActivated = true;
    }

    uint16_t drawnVersion = 0;
    bool     drawnValid   = false;
    uint32_t lastDataMs   = 0;
    drawProcessWatch();
    drawnVersion = procDataVersion;
    drawnValid   = procData.valid;

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
        if (procData.valid && procDataVersion != drawnVersion)
            lastDataMs = millis();

        // Timeout: revert to no-data panel if stream stops for >6 s.
        // process_sender.py polls every 2 s, so 6 s = 3 missed packets.
        if (procData.valid && lastDataMs && millis() - lastDataMs > 6000)
        {
            procData.valid = false;
            procDataVersion++;
        }

        if (procDataVersion != drawnVersion)
        {
            bool validChanged = (procData.valid != drawnValid);
            drawnVersion = procDataVersion;
            drawnValid   = procData.valid;

            if (validChanged || !procData.valid)
                drawProcessWatch();       // full redraw on state transition
            else
                drawProcessWatchUpdate(); // targeted update — no flash
        }

        if (digitalRead(WIO_KEY_B) == LOW)
        {
            while (digitalRead(WIO_KEY_B) == LOW) delay(10);
            takeScreenshot();
        }

        if (digitalRead(WIO_KEY_C) == LOW)
        {
            while (digitalRead(WIO_KEY_C) == LOW) delay(10);
            delay(50);
            bleSetActive(false);
            return;
        }
        delay(20);
    }
}