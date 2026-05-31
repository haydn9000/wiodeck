// bleScanner.cpp — BLE device scanner.
//
// Controls:
//   [UP / DOWN]   Scroll device list (hold to keep scrolling)
//   [PRESS]       Rescan (5-second scan)
//   [C]           Back to menu
//
// Shows up to 32 devices per scan, 7 rows visible, scrollable.
// Each row: name (or MAC), MAC on line 2, dBm, signal bar.
// Green dot on left edge = connectable device.

#include <Arduino.h>
#include "globals.h"
#include <rpcBLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

// -------------------------------------------------------------------------
// Constants
// -------------------------------------------------------------------------
static const int BLE_MAX_RESULTS  = 32;
static const int BLE_ROWS_VISIBLE = 7;    // 2-line rows
static const int BLE_SCAN_SECS    = 5;
static const int ROW_H            = 25;   // 2 text lines + 3px gap between rows
static const int LIST_Y0          = 46;   // first row top-y (leaves room for col header)

struct BLEResult
{
    char  name[21];
    char  addr[18];
    int   rssi;
    bool  connectable;
};

static BLEResult bleResults[BLE_MAX_RESULTS];
static int       bleResultCount = 0;
static int       bleScrollTop   = 0;

static volatile bool g_bleScanDone = false;
static void bleScanCallback(BLEScanResults) { g_bleScanDone = true; }

// -------------------------------------------------------------------------
// Colours
// -------------------------------------------------------------------------
static uint16_t rssiColor(int rssi)
{
    if (rssi >= -60) return tft.color565(60, 210, 100);
    if (rssi >= -80) return tft.color565(210, 175,   0);
    return                  tft.color565(210,  65,  55);
}

// -------------------------------------------------------------------------
// Header
// -------------------------------------------------------------------------
static void drawBleHeader(int found)
{
    tft.fillRect(0, 0, 320, 30, tft.color565(0, 8, 20));
    tft.fillRect(0, 0, 3, 30, tft.color565(0, 220, 245));
    tft.setTextSize(2);
    tft.setTextColor(tft.color565(0, 220, 245), tft.color565(0, 8, 20));
    tft.drawString("// BLE SCAN", 10, 7);
    if (found >= 0)
    {
        char buf[8]; snprintf(buf, sizeof(buf), "%d", found);
        tft.setTextSize(1);
        tft.setTextColor(tft.color565(0, 148, 170), tft.color565(0, 8, 20));
        tft.drawString(buf, 285, 12);
    }
    tft.drawFastHLine(0, 29, 320, tft.color565(0, 80, 100));
    for (int xi = 8; xi < 320; xi += 14)
        tft.drawFastVLine(xi, 27, 4, tft.color565(0, 140, 165));
}

// -------------------------------------------------------------------------
// Scanning splash
// -------------------------------------------------------------------------
static void drawBleScanning()
{
    tft.fillScreen(TFT_BLACK);
    drawBleHeader(-1);

    uint16_t nc  = tft.color565(0, 220, 245);
    uint16_t nbg = tft.color565(0, 8, 18);
    tft.fillRect(10, 48, 300, 140, nbg);
    int t = 12;
    tft.drawFastHLine(10, 48, t, nc);  tft.drawFastVLine(10, 48, t, nc);
    tft.drawFastHLine(298, 48, t, nc); tft.drawFastVLine(309, 48, t, nc);
    tft.drawFastHLine(10, 187, t, nc); tft.drawFastVLine(10, 175, t, nc);
    tft.drawFastHLine(298, 187, t, nc);tft.drawFastVLine(309, 175, t, nc);
    tft.drawFastHLine(11, 49, 298, tft.color565(0, 40, 60));

    tft.setTextSize(2);
    tft.setTextColor(nc, nbg);
    tft.drawString("> SCANNING...", 24, 80);
    tft.setTextSize(1);
    tft.setTextColor(tft.color565(0, 158, 178), nbg);
    char buf[40]; snprintf(buf, sizeof(buf), "SCANNING FOR %d SECONDS", BLE_SCAN_SECS);
    tft.drawString(buf, 24, 110);
    tft.drawString("ADVERTISING PAUSED DURING SCAN", 24, 125);

    tft.fillRect(0, 219, 3, 21, tft.color565(0, 220, 245));
    tft.setTextSize(1);
    tft.setTextColor(tft.color565(0, 100, 118), TFT_BLACK);
    tft.drawString("[C] CANCEL", 8, 225);
}

// -------------------------------------------------------------------------
// Column headers (drawn between the header bar and the first row)
// -------------------------------------------------------------------------
static void drawBleColHeaders()
{
    tft.setTextSize(1);
    tft.setTextColor(tft.color565(0, 148, 165), TFT_BLACK);
    tft.drawString("DEVICE / ADDRESS", 10, 32);
    tft.drawString("dBm", 248, 32);
    tft.drawString("SIG", 273, 32);
    tft.drawFastHLine(0, 43, 320, tft.color565(0, 50, 70));
}

// -------------------------------------------------------------------------
// Single row — 2-line layout:
//   Line 1:  [dot]  Name or MAC (18ch)              dBm  [bar]
//   Line 2:         MAC address (if name on line 1)
// -------------------------------------------------------------------------
static void drawBleRow(int idx)
{
    if (idx < 0 || idx >= bleResultCount) return;
    BLEResult& r = bleResults[idx];
    int visRow   = idx - bleScrollTop;
    if (visRow < 0 || visRow >= BLE_ROWS_VISIBLE) return;

    int      rowY    = LIST_Y0 + visRow * ROW_H;
    uint16_t rc      = rssiColor(r.rssi);
    bool     hasName = (r.name[0] != '\0');

    tft.fillRect(0, rowY, 320, ROW_H, TFT_BLACK);

    // --- Line 1 ---
    int y1 = rowY + 3;

    // Connectable dot
    if (r.connectable)
        tft.fillCircle(3, y1 + 4, 2, tft.color565(0, 200, 100));

    // Name or MAC (18 chars max to stay well left of dBm column)
    tft.setTextSize(1);
    char disp[19];
    strncpy(disp, hasName ? r.name : r.addr, 18);
    disp[18] = '\0';
    tft.setTextColor(hasName ? tft.color565(200, 235, 245)
                             : tft.color565(110, 140, 155), TFT_BLACK);
    tft.drawString(disp, 10, y1);

    // dBm
    char rssiBuf[8]; snprintf(rssiBuf, sizeof(rssiBuf), "%d", r.rssi);
    tft.setTextColor(rc, TFT_BLACK);
    tft.drawString(rssiBuf, 248, y1);

    // Signal bar (44px)
    int barFill = (int)((float)(r.rssi - (-100)) / 70.0f * 44.0f);
    if (barFill < 0)  barFill = 0;
    if (barFill > 44) barFill = 44;
    if (barFill > 0) tft.fillRect(273, y1, barFill, 7, rc);
    tft.drawRect(273, y1, 44, 7, tft.color565(20, 40, 50));

    // --- Line 2 ---
    int y2 = rowY + 14;

    // MAC address on line 2 (only when name occupies line 1)
    if (hasName)
    {
        tft.setTextColor(tft.color565(70, 95, 110), TFT_BLACK);
        tft.drawString(r.addr, 10, y2);
    }
}

// -------------------------------------------------------------------------
// Rows-only repaint — clears just the list zone and redraws rows + arrows.
// Used for scroll updates so header/footer/col-headers are not touched.
// -------------------------------------------------------------------------
static void drawBleListArea()
{
    // Clear only the rows zone
    tft.fillRect(0, LIST_Y0, 320, BLE_ROWS_VISIBLE * ROW_H, TFT_BLACK);

    int visEnd = bleScrollTop + BLE_ROWS_VISIBLE;
    if (visEnd > bleResultCount) visEnd = bleResultCount;
    for (int i = bleScrollTop; i < visEnd; i++)
        drawBleRow(i);

    // Scroll arrows — clear slots first, then paint filled triangles if needed
    const uint16_t arrowCol = tft.color565(0, 160, 190);
    tft.fillRect(308, LIST_Y0, 12, 10, TFT_BLACK);
    tft.fillRect(308, LIST_Y0 + (BLE_ROWS_VISIBLE - 1) * ROW_H, 12, 10, TFT_BLACK);
    if (bleScrollTop > 0)
        tft.fillTriangle(313, LIST_Y0 + 1,
                         308, LIST_Y0 + 9,
                         318, LIST_Y0 + 9, arrowCol);
    if (bleScrollTop + BLE_ROWS_VISIBLE < bleResultCount)
        tft.fillTriangle(313, LIST_Y0 + (BLE_ROWS_VISIBLE - 1) * ROW_H + 9,
                         308, LIST_Y0 + (BLE_ROWS_VISIBLE - 1) * ROW_H + 1,
                         318, LIST_Y0 + (BLE_ROWS_VISIBLE - 1) * ROW_H + 1, arrowCol);
}

// -------------------------------------------------------------------------
// Full results screen — header, col headers, rows, footer
// -------------------------------------------------------------------------
static void drawBleScanResults()
{
    tft.fillScreen(TFT_BLACK);
    drawBleHeader(bleResultCount);

    if (bleResultCount == 0)
    {
        uint16_t nc  = tft.color565(0, 220, 245);
        uint16_t nbg = tft.color565(0, 8, 18);
        tft.fillRect(10, 48, 300, 140, nbg);
        int t = 12;
        tft.drawFastHLine(10, 48, t, nc);  tft.drawFastVLine(10, 48, t, nc);
        tft.drawFastHLine(298, 48, t, nc); tft.drawFastVLine(309, 48, t, nc);
        tft.drawFastHLine(10, 187, t, nc); tft.drawFastVLine(10, 175, t, nc);
        tft.drawFastHLine(298, 187, t, nc);tft.drawFastVLine(309, 175, t, nc);
        tft.drawFastHLine(11, 49, 298, tft.color565(0, 40, 60));
        tft.setTextSize(2);
        tft.setTextColor(nc, nbg);
        tft.drawString("> NO_DATA", 50, 90);
        tft.setTextSize(1);
        tft.setTextColor(tft.color565(0, 158, 178), nbg);
        tft.drawString("NO DEVICES FOUND NEARBY", 50, 118);
        tft.drawString("PRESS [PRESS] TO RESCAN", 50, 134);
    }
    else
    {
        drawBleColHeaders();
        drawBleListArea();
    }

    bool canScroll = bleResultCount > BLE_ROWS_VISIBLE;
    tft.fillRect(0, 219, 3, 21, tft.color565(0, 220, 245));
    tft.setTextSize(1);
    tft.setTextColor(tft.color565(0, 100, 118), TFT_BLACK);
    if (canScroll)
        tft.drawString("[UP/DN] SCROLL   [PRESS] RESCAN   [C] BACK", 8, 225);
    else
        tft.drawString("[PRESS] RESCAN   [C] BACK", 8, 225);

    drawBatteryStatus(TFT_BLACK);
}

// -------------------------------------------------------------------------
// Scan execution
// -------------------------------------------------------------------------
static bool doBleScan()
{
    bleResultCount = 0;
    bleScrollTop   = 0;
    if (!bleInitDone) return true;

    bleSetActive(false);
    delay(200);

    g_bleScanDone = false;
    BLEScan* scan = BLEDevice::getScan();
    scan->setActiveScan(true);
    // Mirror the ble_start() check that the blocking start() overload performs lazily.
    if (!BLEDevice::ble_start_flags)
    {
        BLEDevice::ble_start_flags = true;
        ble_start();
    }
    scan->start(BLE_SCAN_SECS, bleScanCallback, false);

    unsigned long scanStart = millis();
    unsigned long timeout   = (unsigned long)(BLE_SCAN_SECS * 1000 + 2000);
    while (!g_bleScanDone && millis() - scanStart < timeout)
    {
        if (digitalRead(WIO_KEY_C) == LOW)
        {
            scan->stop();
            while (digitalRead(WIO_KEY_C) == LOW) delay(10);
            return false;
        }
        delay(20);
    }

    if (!g_bleScanDone) { scan->stop(); return true; }

    BLEScanResults raw = scan->getResults();
    int total = raw.getCount();
    for (int i = 0; i < total && bleResultCount < BLE_MAX_RESULTS; i++)
    {
        BLEAdvertisedDevice dev = raw.getDevice(i);
        BLEResult& r = bleResults[bleResultCount];

        r.rssi = dev.getRSSI();

        std::string addr = dev.getAddress().toString();
        strncpy(r.addr, addr.c_str(), 17);
        r.addr[17] = '\0';

        std::string nm = dev.getName();
        if (!nm.empty()) { strncpy(r.name, nm.c_str(), 20); r.name[20] = '\0'; }
        else r.name[0] = '\0';

        // Manufacturer data has no public company-ID getter in the library;
        // device category comes from appearance/services instead.

        // Beacons are non-connectable (Eddystone / Exposure Notification)
        r.connectable = !(
            dev.isAdvertisingService(BLEUUID((uint16_t)0xFEAA)) ||
            dev.isAdvertisingService(BLEUUID((uint16_t)0xFD6F)));

        bleResultCount++;
    }

    // Sort by RSSI descending
    for (int i = 0; i < bleResultCount - 1; i++)
        for (int j = i + 1; j < bleResultCount; j++)
            if (bleResults[j].rssi > bleResults[i].rssi)
            {
                BLEResult tmp = bleResults[i];
                bleResults[i] = bleResults[j];
                bleResults[j] = tmp;
            }
    return true;
}

// -------------------------------------------------------------------------
// Screen entry point
// -------------------------------------------------------------------------
void bleScannerScreen()
{
    while (digitalRead(WIO_5S_PRESS) == LOW) delay(10);

    // Full BLE deinit + reinit: guarantees clean GAP state after WiFi or prior scans.
    bleHardReset();

    drawBleScanning();
    if (!doBleScan()) { delay(50); return; }
    drawBleScanResults();

    while (true)
    {
        if (digitalRead(WIO_5S_UP) == LOW)
        {
            if (bleScrollTop > 0) { bleScrollTop--; drawBleListArea(); }
            delay(300);
            while (digitalRead(WIO_5S_UP) == LOW)
            {
                if (bleScrollTop > 0) { bleScrollTop--; drawBleListArea(); }
                delay(150);
            }
            delay(50);
        }
        else if (digitalRead(WIO_5S_DOWN) == LOW)
        {
            if (bleScrollTop + BLE_ROWS_VISIBLE < bleResultCount) { bleScrollTop++; drawBleListArea(); }
            delay(300);
            while (digitalRead(WIO_5S_DOWN) == LOW)
            {
                if (bleScrollTop + BLE_ROWS_VISIBLE < bleResultCount) { bleScrollTop++; drawBleListArea(); }
                delay(150);
            }
            delay(50);
        }
        else if (digitalRead(WIO_5S_PRESS) == LOW)
        {
            while (digitalRead(WIO_5S_PRESS) == LOW) delay(10);
            drawBleScanning();
            if (!doBleScan()) { delay(50); return; }
            drawBleScanResults();
        }
        else if (digitalRead(WIO_KEY_B) == LOW)
        {
            while (digitalRead(WIO_KEY_B) == LOW) delay(10);
            takeScreenshot();
        }
        else if (digitalRead(WIO_KEY_C) == LOW)
        {
            while (digitalRead(WIO_KEY_C) == LOW) delay(10);
            delay(50);
            return;
        }
        delay(20);
    }
}