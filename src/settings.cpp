#include <Arduino.h>
#include <FlashStorage_SAMD.h>
#include "globals.h"

// ---- Persistent settings storage --------------------------------------------
// Flash layout (byte offsets):
//   0  SETTINGS_MAGIC  uint16_t  — 0xBE76 distinguishes written from blank flash
//   2  brightness      int       — 5..100 in steps of 5
//   6  volume          int       — 0 (mute) to 4
static const uint16_t SETTINGS_MAGIC = 0xBE76;

static int brightness  = 25;
static const int MIN_BRIGHTNESS = 5;
int g_volume = 3;   // 0=mute, 1–4 (defined here, extern in globals.h)

static void saveSettings()
{
    EEPROM.put(0, SETTINGS_MAGIC);
    EEPROM.put(2, brightness);
    EEPROM.put(6, g_volume);
    EEPROM.commit();
}

void loadSettings()
{
    uint16_t magic;
    EEPROM.get(0, magic);
    if (magic == SETTINGS_MAGIC) {
        int b, v;
        EEPROM.get(2, b);
        EEPROM.get(6, v);
        brightness = (b >= MIN_BRIGHTNESS && b <= 100) ? b : 25;
        g_volume   = (v >= 0 && v <= 4) ? v : 3;
    } else {
        brightness = 25;
        g_volume   = 3;
    }
    backLight.setBrightness(brightness);
}

// ---- Shared drawing helpers -------------------------------------------------

void drawSettingsHeader(const char* tag)
{
    tft.fillRect(0, 0, 320, 30, tft.color565(0, 8, 20));
    tft.fillRect(0, 0, 3, 30, tft.color565(0, 200, 230));
    tft.setTextSize(2);
    tft.setTextColor(tft.color565(0, 220, 245), tft.color565(0, 8, 20));
    tft.drawString("// SETTINGS", 10, 7);
    tft.drawFastHLine(0, 29, 320, tft.color565(0, 100, 130));
    for (int xi = 8; xi < 320; xi += 14)
        tft.drawFastVLine(xi, 27, 4, tft.color565(0, 155, 185));
    tft.setTextSize(1);
    tft.setTextColor(tft.color565(0, 158, 188), tft.color565(0, 8, 20));
    tft.drawString(tag, 160, 11);
}

// Update only the right-aligned tag text — everything else stays on screen.
void updateSettingsTag(const char* tag)
{
    // Clear the tag zone (x=156..240, mid-header)
    tft.fillRect(156, 3, 84, 22, tft.color565(0, 8, 20));
    tft.setTextSize(1);
    tft.setTextColor(tft.color565(0, 158, 188), tft.color565(0, 8, 20));
    tft.drawString(tag, 160, 11);
}

// Horizontal segmented bar: nSegs total, litSegs filled.
static void drawSegBar(int bx, int by, int bw, int bh, int nSegs, int litSegs, uint16_t litCol)
{
    const int SEG_GAP = 2;
    int segW = bw / nSegs;
    for (int s = 0; s < nSegs; s++) {
        uint16_t col = (s < litSegs) ? litCol : tft.color565(12, 20, 38);
        tft.fillRect(bx + s * segW, by, segW - SEG_GAP, bh, col);
    }
}

// Corner L-tick brackets around a rectangle.
static void drawCornerBrackets(int bx, int by, int bw, int bh, int t, uint16_t col)
{
    tft.drawFastHLine(bx,          by,       t, col);
    tft.drawFastVLine(bx,          by,       t, col);
    tft.drawFastHLine(bx + bw - t, by,       t, col);
    tft.drawFastVLine(bx + bw - 1, by,       t, col);
    tft.drawFastHLine(bx,          by+bh-1,  t, col);
    tft.drawFastVLine(bx,          by+bh-t,  t, col);
    tft.drawFastHLine(bx + bw - t, by+bh-1,  t, col);
    tft.drawFastVLine(bx + bw - 1, by+bh-t,  t, col);
}

// ============================================================================
// BRIGHTNESS SUB-SCREEN
// ============================================================================

static void refreshBrightnessDisplay()
{
    tft.fillRect(0, 52, 320, 64, TFT_BLACK);
    uint16_t valCol = (brightness < 40) ? tft.color565(0, 170, 200) :
                      (brightness < 75) ? tft.color565(0, 220, 245) :
                                          tft.color565(200, 240, 255);
    tft.setTextSize(6);
    tft.setTextColor(valCol, TFT_BLACK);
    char buf[8];
    sprintf(buf, "%d%%", brightness);
    int textW = tft.textWidth(buf);
    tft.drawString(buf, (320 - textW) / 2, 52);

    uint16_t litCol = (brightness < 40) ? tft.color565(0, 170, 200) :
                      (brightness < 75) ? tft.color565(0, 210, 240) :
                                          tft.color565(180, 230, 255);
    drawSegBar(20, 140, 280, 26, 20, brightness / 5, litCol);
}

static void brightnessSubScreen()
{
    tft.fillRect(0, 30, 320, 210, TFT_BLACK);  // content + footer (y=30..239)
    updateSettingsTag("BACKLIGHT");
    tft.setTextSize(1);
    tft.setTextColor(tft.color565(0, 130, 155), TFT_BLACK);
    tft.drawString("DISPLAY BRIGHTNESS", 106, 36);
    tft.drawFastHLine(60, 116, 200, tft.color565(0, 45, 65));
    drawCornerBrackets(16, 136, 288, 34, 12, tft.color565(0, 160, 190));
    tft.setTextSize(1);
    tft.setTextColor(tft.color565(0, 148, 170), TFT_BLACK);
    tft.drawString("MIN", 20,  140 + 26 + 8);
    tft.drawString("MAX", 282, 140 + 26 + 8);
    tft.fillRect(0, 219, 3, 21, tft.color565(0, 200, 230));
    tft.setTextSize(1);
    tft.setTextColor(tft.color565(0, 100, 118), TFT_BLACK);
    tft.drawString("[<][>] ADJUST     [PRESS] / [C] BACK", 8, 225);
    refreshBrightnessDisplay();
    drawBatteryStatus(TFT_BLACK);

    while (digitalRead(WIO_5S_PRESS) == LOW) delay(10);
    delay(50);

    while (true)
    {
        if (digitalRead(WIO_5S_RIGHT) == LOW && brightness < 100) {
            brightness += 5;
            backLight.setBrightness(brightness);
            refreshBrightnessDisplay();
            delay(150);
        } else if (digitalRead(WIO_5S_LEFT) == LOW && brightness > MIN_BRIGHTNESS) {
            brightness -= 5;
            if (brightness < MIN_BRIGHTNESS) brightness = MIN_BRIGHTNESS;
            backLight.setBrightness(brightness);
            refreshBrightnessDisplay();
            delay(150);
        } else if (digitalRead(WIO_KEY_B) == LOW) {
            while (digitalRead(WIO_KEY_B) == LOW) delay(10);
            takeScreenshot();
        } else if (digitalRead(WIO_5S_PRESS) == LOW || digitalRead(WIO_KEY_C) == LOW) {
            while (digitalRead(WIO_5S_PRESS) == LOW || digitalRead(WIO_KEY_C) == LOW) delay(10);
            delay(50);
            saveSettings();
            return;
        }
    }
}

// ============================================================================
// VOLUME SUB-SCREEN
// ============================================================================

// Bit-bang a short preview tone at ~1200 Hz, respecting current g_volume duty cycle.
// Used in the volume sub-screen so the user can hear the level change.
static void volPreviewTone()
{
    if (g_volume == 0) return;
    const uint32_t halfUs = 500000UL / 1200;          // ~416 µs
    uint32_t highUs = halfUs * (uint32_t)g_volume / 4;
    uint32_t lowUs  = 2 * halfUs - highUs;
    pinMode(WIO_BUZZER, OUTPUT);
    uint32_t t0 = millis();
    while (millis() - t0 < 80) {                      // 80 ms preview
        digitalWrite(WIO_BUZZER, HIGH);
        delayMicroseconds(highUs);
        digitalWrite(WIO_BUZZER, LOW);
        delayMicroseconds(lowUs);
    }
    digitalWrite(WIO_BUZZER, LOW);
}

static void refreshVolumeDisplay()
{
    tft.fillRect(0, 45, 320, 85, TFT_BLACK);
    if (g_volume == 0) {
        tft.setTextSize(5);
        tft.setTextColor(tft.color565(220, 80, 30), TFT_BLACK);
        const char* txt = "MUTE";
        tft.drawString(txt, (320 - tft.textWidth(txt)) / 2, 62);
    } else {
        uint16_t valCol = (g_volume <= 2) ? tft.color565(0, 180, 210) : tft.color565(0, 220, 245);
        tft.setTextSize(6);
        tft.setTextColor(valCol, TFT_BLACK);
        char buf[4];
        sprintf(buf, "%d", g_volume);
        tft.drawString(buf, (320 - tft.textWidth(buf)) / 2, 50);
    }
    uint16_t litCol = (g_volume == 0) ? tft.color565(80, 20, 5) :
                      (g_volume <= 2)  ? tft.color565(0, 170, 200) :
                                         tft.color565(0, 210, 240);
    drawSegBar(30, 140, 260, 26, 4, g_volume, litCol);
}

static void volumeSubScreen()
{
    tft.fillRect(0, 30, 320, 210, TFT_BLACK);  // content + footer (y=30..239)
    updateSettingsTag("VOLUME");
    tft.setTextSize(1);
    tft.setTextColor(tft.color565(0, 130, 155), TFT_BLACK);
    tft.drawString("ALERT & TIMER SOUNDS", 100, 36);
    tft.drawFastHLine(60, 116, 200, tft.color565(0, 45, 65));
    drawCornerBrackets(26, 136, 268, 34, 12, tft.color565(0, 160, 190));
    tft.setTextSize(1);
    tft.setTextColor(tft.color565(0, 148, 170), TFT_BLACK);
    tft.drawString("OFF", 30,  140 + 26 + 8);
    tft.drawString("MAX", 272, 140 + 26 + 8);
    tft.fillRect(0, 219, 3, 21, tft.color565(0, 200, 230));
    tft.setTextSize(1);
    tft.setTextColor(tft.color565(0, 100, 118), TFT_BLACK);
    tft.drawString("[<][>] ADJUST     [PRESS] / [C] BACK", 8, 225);
    refreshVolumeDisplay();
    drawBatteryStatus(TFT_BLACK);

    while (digitalRead(WIO_5S_PRESS) == LOW) delay(10);
    delay(50);

    while (true)
    {
        if (digitalRead(WIO_5S_RIGHT) == LOW && g_volume < 4) {
            g_volume++;
            refreshVolumeDisplay();
            volPreviewTone();
            delay(150);
        } else if (digitalRead(WIO_5S_LEFT) == LOW && g_volume > 0) {
            g_volume--;
            refreshVolumeDisplay();
            volPreviewTone();
            delay(150);
        } else if (digitalRead(WIO_KEY_B) == LOW) {
            while (digitalRead(WIO_KEY_B) == LOW) delay(10);
            takeScreenshot();
        } else if (digitalRead(WIO_5S_PRESS) == LOW || digitalRead(WIO_KEY_C) == LOW) {
            while (digitalRead(WIO_5S_PRESS) == LOW || digitalRead(WIO_KEY_C) == LOW) delay(10);
            delay(50);
            saveSettings();
            return;
        }
    }
}

// ============================================================================
// BATTERY INFO SUB-SCREEN
//
// Layout (y coords, content area y=30..218):
//   36  — Pill badge  (h=22)          6px margin from header
//   66  — Large SoC % (textSize 3)    8px margin from pill
//   98  — Charge fill bar  (h=12)     8px margin from SoC
//  116  — mAh caption (textSize 2)    6px margin from bar
//  140  — Divider                     8px margin from mAh
//  146  — VOLTAGE / CURRENT labels    6px margin from divider
//  160  — values (textSize 2)         6px margin from labels
//  182  — Divider                     6px margin from values
//  188  — POWER DRAW / HEALTH labels  6px margin from divider
//  202  — values (textSize 2)         6px margin from labels → ends y=218
// ============================================================================

// force=true on initial draw; false on timed refresh (only changed values are redrawn)
static void refreshBatteryInfo(bool force = false)
{
    // Sentinel statics — track what is currently on screen
    static int  prevSoc     = -999;
    static int  prevVolts   = -999;
    static int  prevCurrent = -32767;
    static int  prevRemain  = -999;
    static int  prevFull    = -999;
    static int  prevPower   = -32767;
    static int  prevHealth  = -999;
    static int  prevChg     = -1;    // -1 = unknown, 0 = false, 1 = true

    int  soc     = batteryLevel();
    int  volts   = batteryVoltage();
    int  current = batteryCurrent();
    int  remain  = batteryRemaining();
    int  full    = batteryFullCapacity();
    int  power   = batteryPower();
    int  health  = batteryHealth();
    bool chg     = batteryCharging();
    int  ichg    = chg ? 1 : 0;

    if (!batteryPresent())
    {
        if (force)
        {
            tft.fillRect(0, 30, 320, 190, TFT_BLACK);
            tft.setTextSize(2);
            tft.setTextColor(tft.color565(210, 80, 60), TFT_BLACK);
            const char* msg = "NO BATTERY CHASSIS";
            tft.drawString(msg, (320 - tft.textWidth(msg)) / 2, 100);
            tft.setTextSize(1);
            tft.setTextColor(tft.color565(100, 50, 40), TFT_BLACK);
            const char* sub = "BQ27441-G1A not detected on I2C";
            tft.drawString(sub, (320 - tft.textWidth(sub)) / 2, 126);
        }
        return;
    }

    // ---- Pill badge (CHARGING / DISCHARGING width differs — clear full row) ----
    if (force || ichg != prevChg)
    {
        tft.fillRect(0, 30, 320, 36, TFT_BLACK);   // y=30..66
        const char* status = chg ? "CHARGING" : "DISCHARGING";
        uint16_t    stCol  = chg ? tft.color565(80, 220, 120) : tft.color565(0, 200, 230);
        tft.setTextSize(1);
        int sw = tft.textWidth(status) + 22;
        tft.fillRoundRect((320 - sw) / 2, 36, sw, 22, 6, tft.color565(0, 22, 32));
        tft.drawRoundRect((320 - sw) / 2, 36, sw, 22, 6, stCol);
        tft.setTextColor(stCol, tft.color565(0, 22, 32));
        tft.drawString(status, (320 - tft.textWidth(status)) / 2, 43);
        prevChg = ichg;
    }

    // ---- Large SoC % + fill bar (digit count and fill width both vary) ----
    if (force || soc != prevSoc)
    {
        uint16_t barCol = (soc <  0)  ? tft.color565(0, 200, 230)
                        : (soc <= 25) ? TFT_RED
                        : (soc <= 50) ? tft.color565(255, 165, 0)
                        :               tft.color565(80, 210, 100);
        char socBuf[8];
        if (soc >= 0) snprintf(socBuf, sizeof(socBuf), "%d%%", soc);
        else          strncpy(socBuf, "--", sizeof(socBuf));

        tft.fillRect(0, 66, 320, 32, TFT_BLACK);   // y=66..98
        tft.setTextSize(3);
        tft.setTextColor(barCol, TFT_BLACK);
        tft.drawString(socBuf, (320 - tft.textWidth(socBuf)) / 2, 66);

        const int BAR_X = 16, BAR_Y = 98, BAR_W = 288, BAR_H = 12;
        tft.fillRect(BAR_X, BAR_Y, BAR_W, BAR_H, tft.color565(0, 20, 30));
        int filled = soc < 0 ? 0 : (BAR_W * soc / 100);
        if (filled > 0) tft.fillRect(BAR_X, BAR_Y, filled, BAR_H, barCol);
        tft.drawRect(BAR_X - 1, BAR_Y - 1, BAR_W + 2, BAR_H + 2, tft.color565(0, 55, 75));

        prevSoc = soc;
    }

    // ---- mAh caption (remain / full can both change) ----
    if (force || remain != prevRemain || full != prevFull)
    {
        char capBuf[24];
        if (remain >= 0 && full >= 0) snprintf(capBuf, sizeof(capBuf), "%d / %d mAh", remain, full);
        else if (remain >= 0)         snprintf(capBuf, sizeof(capBuf), "%d mAh", remain);
        else                          strncpy(capBuf, "--", sizeof(capBuf));

        tft.fillRect(0, 116, 320, 18, TFT_BLACK);
        tft.setTextSize(2);
        tft.setTextColor(tft.color565(0, 175, 200), TFT_BLACK);
        tft.drawString(capBuf, (320 - tft.textWidth(capBuf)) / 2, 116);

        prevRemain = remain;
        prevFull   = full;
    }

    // ---- Static elements: drawn once on entry, never again ----
    if (force)
    {
        const uint16_t LBL = tft.color565(0, 175, 200);
        const uint16_t DIV = tft.color565(0, 38, 55);
        tft.drawFastHLine(16, 140, 288, DIV);
        tft.setTextSize(1);
        tft.setTextColor(LBL, TFT_BLACK);
        tft.drawString("VOLTAGE",    16,  146);
        tft.drawString("CURRENT",   168,  146);
        tft.drawFastVLine(152, 140, 44, DIV);
        tft.drawFastHLine(16, 182, 288, DIV);
        tft.drawString("POWER DRAW",  16, 188);
        tft.drawString("HEALTH",     168, 188);
        tft.drawFastVLine(152, 182, 44, DIV);
    }

    char buf[16];

    // ---- VOLTAGE ----
    if (force || volts != prevVolts)
    {
        if (volts >= 0) snprintf(buf, sizeof(buf), "%d mV", volts);
        else            strncpy(buf, "--", sizeof(buf));
        tft.fillRect(16, 160, 134, 18, TFT_BLACK);
        tft.setTextSize(2);
        tft.setTextColor(tft.color565(0, 210, 235), TFT_BLACK);
        tft.drawString(buf, 16, 160);
        prevVolts = volts;
    }

    // ---- CURRENT ----
    if (force || current != prevCurrent)
    {
        if (current != -32768) snprintf(buf, sizeof(buf), "%d mA", current);
        else                   strncpy(buf, "--", sizeof(buf));
        tft.fillRect(168, 160, 134, 18, TFT_BLACK);
        tft.setTextSize(2);
        tft.setTextColor(current != -32768 && current < 0 ? tft.color565(255, 165, 0)
                                                           : tft.color565(80, 220, 120), TFT_BLACK);
        tft.drawString(buf, 168, 160);
        prevCurrent = current;
    }

    // ---- POWER DRAW ----
    if (force || power != prevPower)
    {
        if (power != -32768) snprintf(buf, sizeof(buf), "%d mW", power);
        else                 strncpy(buf, "--", sizeof(buf));
        tft.fillRect(16, 202, 134, 18, TFT_BLACK);
        tft.setTextSize(2);
        tft.setTextColor(power != -32768 && power < 0 ? tft.color565(255, 165, 0)
                                                       : tft.color565(80, 220, 120), TFT_BLACK);
        tft.drawString(buf, 16, 202);
        prevPower = power;
    }

    // ---- HEALTH ----
    if (force || health != prevHealth)
    {
        if (health >= 0) snprintf(buf, sizeof(buf), "%d%%", health);
        else             strncpy(buf, "--", sizeof(buf));
        uint16_t sohCol = (health <  0)  ? tft.color565(0, 210, 235)
                        : (health >= 80) ? tft.color565(80, 220, 120)
                        : (health >= 50) ? tft.color565(255, 165, 0)
                        :                  TFT_RED;
        tft.fillRect(168, 202, 134, 18, TFT_BLACK);
        tft.setTextSize(2);
        tft.setTextColor(sohCol, TFT_BLACK);
        tft.drawString(buf, 168, 202);
        prevHealth = health;
    }
}

static void batteryInfoScreen()
{
    tft.fillRect(0, 30, 320, 210, TFT_BLACK);  // content + footer (y=30..239)
    updateSettingsTag("BATTERY");
    refreshBatteryInfo(true);   // force full draw on entry
    tft.fillRect(0, 219, 3, 21, tft.color565(0, 200, 230));
    tft.setTextSize(1);
    tft.setTextColor(tft.color565(0, 100, 118), TFT_BLACK);
    tft.drawString("[PRESS] / [C] BACK", 8, 225);
    drawBatteryStatus(TFT_BLACK);

    while (digitalRead(WIO_5S_PRESS) == LOW) delay(10);
    delay(50);

    uint32_t lastRefresh = millis();

    while (true)
    {
        if (millis() - lastRefresh >= 2000)
        {
            refreshBatteryInfo();   // only redraws changed values
            lastRefresh = millis();
        }
        if (digitalRead(WIO_KEY_B) == LOW)
        {
            while (digitalRead(WIO_KEY_B) == LOW) delay(10);
            takeScreenshot();
        }
        if (digitalRead(WIO_5S_PRESS) == LOW || digitalRead(WIO_KEY_C) == LOW)
        {
            while (digitalRead(WIO_5S_PRESS) == LOW || digitalRead(WIO_KEY_C) == LOW) delay(10);
            delay(50);
            return;
        }
        delay(20);
    }
}

// ============================================================================
// SETTINGS MENU  (2 pages)
//
//  Page 0 — BACKLIGHT / VOLUME / BATTERY   (items 0-2)
//  Page 1 — SENSORS / DEVICE               (items 3-4)
//
//  UP/DOWN navigates; crossing the page boundary triggers a full redraw.
//  Page dots are shown in the footer.
// ============================================================================

static const int NUM_SETTINGS  = 5;
static const int S_PAGE0_COUNT = 3;   // items on page 0 (items 0,1,2)
// items 3,4 live on page 1

// Row geometry: 3 rows of 58px + 5px gaps fit in the ~190px content area.
static const int S_ROW_H   = 58;
static const int S_ROW_GAP = 5;
static const int S_ROW_Y0  = 35;

static int settingsPage(int idx) { return (idx < S_PAGE0_COUNT) ? 0 : 1; }
static int settingsSlot(int idx) { return (idx < S_PAGE0_COUNT) ? idx : idx - S_PAGE0_COUNT; }

static void drawSettingsPageDots(int page)
{
    uint16_t c0 = (page == 0) ? tft.color565(0, 220, 245) : tft.color565(0, 40, 58);
    uint16_t c1 = (page == 1) ? tft.color565(0, 220, 245) : tft.color565(0, 40, 58);
    tft.fillCircle(294, 228, 3, c0);
    tft.fillCircle(304, 228, 3, c1);
}

// slot = visual row position on this page (0-based); idx = global item index.
static void drawSettingsRow(int slot, int idx, int sel)
{
    const int bx = 10, bw = 300;
    int by = S_ROW_Y0 + slot * (S_ROW_H + S_ROW_GAP);

    bool selected   = (idx == sel);
    uint16_t bgCol  = selected ? tft.color565(0, 30, 45)  : tft.color565(0, 8, 15);
    uint16_t bdCol  = selected ? tft.color565(0, 220, 245) : tft.color565(15, 30, 42);
    uint16_t lblCol = selected ? tft.color565(0, 220, 245) : tft.color565(60, 90, 100);
    uint16_t subCol = selected ? tft.color565(0, 148, 170) : tft.color565(30, 50, 60);
    uint16_t valCol = selected ? tft.color565(0, 200, 220) : tft.color565(40, 70, 80);

    tft.fillRect(bx, by, bw, S_ROW_H, bgCol);
    tft.drawRect(bx, by, bw, S_ROW_H, bdCol);
    const int t = 10;
    tft.drawFastHLine(bx,        by,             t, bdCol);
    tft.drawFastVLine(bx,        by,             t, bdCol);
    tft.drawFastHLine(bx+bw-t,   by,             t, bdCol);
    tft.drawFastVLine(bx+bw-1,   by,             t, bdCol);
    tft.drawFastHLine(bx,        by+S_ROW_H-1,   t, bdCol);
    tft.drawFastVLine(bx,        by+S_ROW_H-t,   t, bdCol);
    tft.drawFastHLine(bx+bw-t,   by+S_ROW_H-1,   t, bdCol);
    tft.drawFastVLine(bx+bw-1,   by+S_ROW_H-t,   t, bdCol);

    // Title (size 2)
    tft.setTextSize(2);
    tft.setTextColor(lblCol, bgCol);
    if      (idx == 0) tft.drawString("BACKLIGHT", bx + 16, by + 8);
    else if (idx == 1) tft.drawString("VOLUME",    bx + 16, by + 8);
    else if (idx == 2) tft.drawString("BATTERY",   bx + 16, by + 8);
    else if (idx == 3) tft.drawString("SENSORS",   bx + 16, by + 8);
    else               tft.drawString("DEVICE",    bx + 16, by + 8);

    // Value badge right-aligned (BACKLIGHT and VOLUME only)
    if (idx == 0 || idx == 1)
    {
        char valBuf[8];
        if (idx == 0)
        {
            sprintf(valBuf, "%d%%", brightness);
        }
        else
        {
            const char* lbl = (g_volume == 0) ? "MUTE" : (g_volume == 4) ? "MAX" : "";
            if (lbl[0] != '\0') {
                strncpy(valBuf, lbl, sizeof(valBuf) - 1);
                valBuf[sizeof(valBuf)-1] = '\0';
            } else {
                sprintf(valBuf, "%d", g_volume);
            }
            if (g_volume == 0 && selected)
                tft.setTextColor(tft.color565(220, 80, 30), bgCol);
            else if (g_volume == 0)
                tft.setTextColor(tft.color565(100, 30, 10), bgCol);
            else
                tft.setTextColor(valCol, bgCol);
        }
        tft.setTextSize(2);
        tft.setTextColor((idx == 1 && g_volume == 0 && selected)  ? tft.color565(220, 80, 30)
                       : (idx == 1 && g_volume == 0)              ? tft.color565(100, 30, 10)
                       :                                            valCol,
                       bgCol);
        int tw = tft.textWidth(valBuf);
        tft.drawString(valBuf, bx + bw - 16 - tw, by + 8);
    }

    // Sub-label (size 1)
    tft.setTextSize(1);
    tft.setTextColor(subCol, bgCol);
    if      (idx == 0) tft.drawString("Adjust display brightness",     bx + 16, by + 34);
    else if (idx == 1) tft.drawString("Alert & timer sounds",           bx + 16, by + 34);
    else if (idx == 2) tft.drawString("SoC, voltage, current & health", bx + 16, by + 34);
    else if (idx == 3) tft.drawString("Accel, light & mic dashboard",   bx + 16, by + 34);
    else               tft.drawString("MCU, memory & firmware info",    bx + 16, by + 34);
}

static void redrawSettingsScreen(int sel)
{
    int page  = settingsPage(sel);
    int start = (page == 0) ? 0 : S_PAGE0_COUNT;
    int count = (page == 0) ? S_PAGE0_COUNT : NUM_SETTINGS - S_PAGE0_COUNT;

    // ---- Header: overdraw in-place (no visible flash) ----
    drawSettingsHeader("SELECT");
    drawBatteryStatus(tft.color565(0, 8, 20));

    // ---- Footer: overdraw in-place before clearing content area ----
    tft.fillRect(0, 219, 320, 21, TFT_BLACK);
    tft.fillRect(0, 220, 3, 20, tft.color565(0, 200, 230));
    tft.setTextSize(1);
    tft.setTextColor(tft.color565(0, 100, 118), TFT_BLACK);
    tft.drawString("[UP/DN] NAV   [PRESS/RT] SELECT   [C] BACK", 8, 228);
    drawSettingsPageDots(page);

    // ---- Content area: fill only the rows region, then draw rows ----
    tft.fillRect(0, 30, 320, 189, TFT_BLACK);
    for (int i = 0; i < count; i++)
        drawSettingsRow(i, start + i, sel);
}

void settingsScreen()
{
    int sel = 0;
    redrawSettingsScreen(sel);

    while (digitalRead(WIO_5S_PRESS) == LOW || digitalRead(WIO_KEY_A) == LOW) delay(10);
    delay(50);

    while (true)
    {
        if (digitalRead(WIO_5S_DOWN) == LOW && sel < NUM_SETTINGS - 1)
        {
            // initial step
            { int o = sel++; bool p = settingsPage(o) != settingsPage(sel);
              if (p) redrawSettingsScreen(sel);
              else { drawSettingsRow(settingsSlot(o), o, sel); drawSettingsRow(settingsSlot(sel), sel, sel); } }
            uint32_t heldSince = millis(), lastStep = millis();
            bool repeating = false;
            while (digitalRead(WIO_5S_DOWN) == LOW)
            {
                uint32_t now = millis();
                if (!repeating && now - heldSince >= 400) { repeating = true; lastStep = now; }
                if (repeating && now - lastStep >= 150 && sel < NUM_SETTINGS - 1)
                {
                    int o = sel++; bool p = settingsPage(o) != settingsPage(sel);
                    if (p) redrawSettingsScreen(sel);
                    else { drawSettingsRow(settingsSlot(o), o, sel); drawSettingsRow(settingsSlot(sel), sel, sel); }
                    lastStep = now;
                }
                delay(10);
            }
            delay(50);
        }
        else if (digitalRead(WIO_5S_UP) == LOW && sel > 0)
        {
            // initial step
            { int o = sel--; bool p = settingsPage(o) != settingsPage(sel);
              if (p) redrawSettingsScreen(sel);
              else { drawSettingsRow(settingsSlot(o), o, sel); drawSettingsRow(settingsSlot(sel), sel, sel); } }
            uint32_t heldSince = millis(), lastStep = millis();
            bool repeating = false;
            while (digitalRead(WIO_5S_UP) == LOW)
            {
                uint32_t now = millis();
                if (!repeating && now - heldSince >= 400) { repeating = true; lastStep = now; }
                if (repeating && now - lastStep >= 150 && sel > 0)
                {
                    int o = sel--; bool p = settingsPage(o) != settingsPage(sel);
                    if (p) redrawSettingsScreen(sel);
                    else { drawSettingsRow(settingsSlot(o), o, sel); drawSettingsRow(settingsSlot(sel), sel, sel); }
                    lastStep = now;
                }
                delay(10);
            }
            delay(50);
        }
        else if (digitalRead(WIO_5S_PRESS) == LOW || digitalRead(WIO_5S_RIGHT) == LOW)
        {
            while (digitalRead(WIO_5S_PRESS) == LOW || digitalRead(WIO_5S_RIGHT) == LOW) delay(10);
            delay(50);
            if      (sel == 0) brightnessSubScreen();
            else if (sel == 1) volumeSubScreen();
            else if (sel == 2) batteryInfoScreen();
            else if (sel == 3) sensorsScreen();
            else               deviceInfoScreen();
            redrawSettingsScreen(sel);
        }
        else if (digitalRead(WIO_KEY_A) == LOW || digitalRead(WIO_KEY_C) == LOW)
        {
            while (digitalRead(WIO_KEY_A) == LOW || digitalRead(WIO_KEY_C) == LOW) delay(10);
            delay(50);
            return;
        }
        else if (digitalRead(WIO_KEY_B) == LOW)
        {
            while (digitalRead(WIO_KEY_B) == LOW) delay(10);
            takeScreenshot();
        }
        delay(20);
    }
}
