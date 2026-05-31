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
    tft.setTextSize(1);
    tft.setTextColor(tft.color565(0, 158, 188), tft.color565(0, 8, 20));
    int tw = tft.textWidth(tag);
    tft.drawString(tag, 316 - tw, 12);
    tft.drawFastHLine(0, 29, 320, tft.color565(0, 100, 130));
    for (int xi = 8; xi < 320; xi += 14)
        tft.drawFastVLine(xi, 27, 4, tft.color565(0, 155, 185));
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
    tft.fillScreen(TFT_BLACK);
    drawSettingsHeader("BACKLIGHT");
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
    tft.fillScreen(TFT_BLACK);
    drawSettingsHeader("VOLUME");
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
// SETTINGS MENU
// ============================================================================

static const int NUM_SETTINGS = 4;
// Row geometry: four 44-px tall rows with 3-px gaps between them.
static const int S_ROW_H  = 44;
static const int S_ROW_Y0 = 35;
static const int S_ROW_Y1 = 82;    // 35 + 44 + 3
static const int S_ROW_Y2 = 129;   // 82 + 44 + 3
static const int S_ROW_Y3 = 176;   // 129 + 44 + 3

static void drawSettingsRow(int idx, int sel)
{
    const int bx = 10, bw = 300;
    int by = (idx == 0) ? S_ROW_Y0 : (idx == 1) ? S_ROW_Y1 : (idx == 2) ? S_ROW_Y2 : S_ROW_Y3;

    bool selected    = (idx == sel);
    uint16_t bgCol   = selected ? tft.color565(0, 30, 45)  : tft.color565(0, 8, 15);
    uint16_t bdCol   = selected ? tft.color565(0, 220, 245) : tft.color565(15, 30, 42);
    uint16_t lblCol  = selected ? tft.color565(0, 220, 245) : tft.color565(60, 90, 100);
    uint16_t subCol  = selected ? tft.color565(0, 148, 170) : tft.color565(30, 50, 60);
    uint16_t valCol  = selected ? tft.color565(0, 200, 220) : tft.color565(40, 70, 80);

    tft.fillRect(bx, by, bw, S_ROW_H, bgCol);
    tft.drawRect(bx, by, bw, S_ROW_H, bdCol);
    const int t = 8;
    tft.drawFastHLine(bx,        by,             t, bdCol);
    tft.drawFastVLine(bx,        by,             t, bdCol);
    tft.drawFastHLine(bx+bw-t,   by,             t, bdCol);
    tft.drawFastVLine(bx+bw-1,   by,             t, bdCol);
    tft.drawFastHLine(bx,        by+S_ROW_H-1,   t, bdCol);
    tft.drawFastVLine(bx,        by+S_ROW_H-t,   t, bdCol);
    tft.drawFastHLine(bx+bw-t,   by+S_ROW_H-1,   t, bdCol);
    tft.drawFastVLine(bx+bw-1,   by+S_ROW_H-t,   t, bdCol);

    // Row label (size 2) + current value badge (size 2, right-aligned)
    tft.setTextSize(2);
    tft.setTextColor(lblCol, bgCol);
    if      (idx == 0) tft.drawString("BACKLIGHT", bx + 16, by + 6);
    else if (idx == 1) tft.drawString("VOLUME",    bx + 16, by + 6);
    else if (idx == 2) tft.drawString("SENSORS",   bx + 16, by + 6);
    else               tft.drawString("DEVICE",    bx + 16, by + 6);

    if (idx < 2) {
        // Value badge, right-aligned
        tft.setTextSize(2);
        tft.setTextColor(valCol, bgCol);
        char valBuf[8];
        if (idx == 0) {
            sprintf(valBuf, "%d%%", brightness);
        } else {
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
        }
        int tw = tft.textWidth(valBuf);
        tft.drawString(valBuf, bx + bw - 16 - tw, by + 6);
    }

    // Sub-label (size 1)
    tft.setTextSize(1);
    tft.setTextColor(subCol, bgCol);
    if      (idx == 0) tft.drawString("Adjust display brightness",    bx + 16, by + 28);
    else if (idx == 1) tft.drawString("Alert & timer sounds",          bx + 16, by + 28);
    else if (idx == 2) tft.drawString("Accel, light & mic dashboard",  bx + 16, by + 28);
    else               tft.drawString("MCU, memory & firmware info",   bx + 16, by + 28);
}

static void redrawSettingsScreen(int sel)
{
    tft.fillScreen(TFT_BLACK);
    drawSettingsHeader("SELECT");
    for (int i = 0; i < NUM_SETTINGS; i++) drawSettingsRow(i, sel);
    tft.fillRect(0, 225, 3, 15, tft.color565(0, 200, 230));
    tft.setTextSize(1);
    tft.setTextColor(tft.color565(0, 100, 118), TFT_BLACK);
    tft.drawString("[UP/DN] NAV   [PRESS/RT] SELECT   [C] BACK", 8, 228);
    drawBatteryStatus(TFT_BLACK);
}

void settingsScreen()
{
    int sel = 0;
    redrawSettingsScreen(sel);

    // Wait for the button that opened this screen to be released.
    while (digitalRead(WIO_5S_PRESS) == LOW || digitalRead(WIO_KEY_A) == LOW) delay(10);
    delay(50);

    while (true)
    {
        if (digitalRead(WIO_5S_DOWN) == LOW && sel < NUM_SETTINGS - 1) {
            int old = sel++;
            drawSettingsRow(old, sel);
            drawSettingsRow(sel, sel);
            while (digitalRead(WIO_5S_DOWN) == LOW) delay(10);
            delay(100);
        } else if (digitalRead(WIO_5S_UP) == LOW && sel > 0) {
            int old = sel--;
            drawSettingsRow(old, sel);
            drawSettingsRow(sel, sel);
            while (digitalRead(WIO_5S_UP) == LOW) delay(10);
            delay(100);
        } else if (digitalRead(WIO_5S_PRESS) == LOW || digitalRead(WIO_5S_RIGHT) == LOW) {
            while (digitalRead(WIO_5S_PRESS) == LOW || digitalRead(WIO_5S_RIGHT) == LOW) delay(10);
            delay(50);
            if (sel == 0)      brightnessSubScreen();
            else if (sel == 1) volumeSubScreen();
            else if (sel == 2) sensorsScreen();
            else               deviceInfoScreen();
            redrawSettingsScreen(sel);
        } else if (digitalRead(WIO_KEY_A) == LOW || digitalRead(WIO_KEY_C) == LOW) {
            while (digitalRead(WIO_KEY_A) == LOW || digitalRead(WIO_KEY_C) == LOW) delay(10);
            delay(50);
            return;
        } else if (digitalRead(WIO_KEY_B) == LOW) {
            while (digitalRead(WIO_KEY_B) == LOW) delay(10);
            takeScreenshot();
        }
        delay(20);
    }
}
