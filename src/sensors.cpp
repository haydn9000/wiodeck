#include <Arduino.h>
#include <Wire.h>
#include "globals.h"
#include "LIS3DHTR.h"

//========================================================================= SENSORS — DASHBOARD
// Displays live readings from the accelerometer (LIS3DHTR), light sensor,
// and microphone. All three update every 200 ms without a full screen clear.

static LIS3DHTR<TwoWire> lis;
static bool lisReady = false;

// --- Geometry ---
static const int BAR_X  = 20;
static const int BAR_W  = 194;
static const int BAR_H  = 18;
static const int RVAL_X = 222;

static const int Y_ACCEL_LBL = 38;
static const int Y_XBAR      = 48;
static const int Y_YBAR      = 70;
static const int Y_ZBAR      = 92;
static const int Y_LIGHT_LBL = 120;
static const int Y_LBAR      = 130;
static const int Y_MIC_LBL   = 162;
static const int Y_MBAR      = 172;

// --- Sample the mic for ~30 ms, return peak-to-peak amplitude.
static int sampleMic()
{
    int lo = 4095, hi = 0;
    for (int i = 0; i < 30; i++) {
        int v = analogRead(WIO_MIC);
        if (v < lo) lo = v;
        if (v > hi) hi = v;
        delay(1);
    }
    return hi - lo;
}

// Bipolar bar: val in [-range, +range], filled from centre.
static void drawBiBar(int x, int y, int w, int h,
                      float val, float range, uint16_t posCol, uint16_t negCol)
{
    int cx   = x + w / 2;
    int half = w / 2 - 1;
    int fill = (int)(val / range * half);
    if (fill >  half) fill =  half;
    if (fill < -half) fill = -half;

    tft.fillRect(x + 1, y + 1, w - 2, h - 2, TFT_BLACK);
    if (fill > 0)
        tft.fillRect(cx,        y + 1, fill,  h - 2, posCol);
    else if (fill < 0)
        tft.fillRect(cx + fill, y + 1, -fill, h - 2, negCol);

    tft.drawFastVLine(cx, y + 1, h - 2, tft.color565(0, 60, 80));
}

// Unipolar bar: val in [0, maxVal], filled left to right.
static void drawUniBar(int x, int y, int w, int h, int val, int maxVal, uint16_t col)
{
    int fill = (val >= maxVal) ? w - 2 : (int)((long)val * (w - 2) / maxVal);
    tft.fillRect(x + 1,        y + 1, fill,         h - 2, col);
    tft.fillRect(x + 1 + fill, y + 1, w - 2 - fill, h - 2, TFT_BLACK);
}

// Static frame — drawn once on entry.
static void drawSensorsFrame()
{
    tft.fillScreen(TFT_BLACK);

    tft.fillRect(0, 0, 320, 30, tft.color565(0, 8, 20));
    tft.fillRect(0, 0, 3, 30, tft.color565(0, 200, 230));
    tft.setTextSize(2);
    tft.setTextColor(tft.color565(0, 220, 245), tft.color565(0, 8, 20));
    tft.drawString("// SENSORS", 10, 7);
    tft.setTextSize(1);
    tft.setTextColor(tft.color565(0, 158, 188), tft.color565(0, 8, 20));
    tft.drawString("DASHBOARD", 160, 11);
    tft.drawFastHLine(0, 29, 320, tft.color565(0, 100, 130));
    for (int xi = 8; xi < 320; xi += 14)
        tft.drawFastVLine(xi, 27, 4, tft.color565(0, 155, 185));

    const uint16_t colBorder = tft.color565(20, 32, 52);
    const uint16_t colDiv    = tft.color565(0, 60, 80);
    tft.setTextSize(1);

    tft.setTextColor(tft.color565(0, 150, 175), TFT_BLACK);
    tft.drawString("ACCELEROMETER", BAR_X, Y_ACCEL_LBL);
    const int accelYs[] = { Y_XBAR, Y_YBAR, Y_ZBAR };
    for (int i = 0; i < 3; i++) {
        tft.drawRect(BAR_X, accelYs[i], BAR_W, BAR_H, colBorder);
        tft.drawFastVLine(BAR_X + BAR_W / 2, accelYs[i] + 1, BAR_H - 2, colDiv);
    }

    tft.setTextColor(tft.color565(180, 140, 0), TFT_BLACK);
    tft.drawString("LIGHT SENSOR", BAR_X, Y_LIGHT_LBL);
    tft.drawRect(BAR_X, Y_LBAR, BAR_W, BAR_H, colBorder);

    tft.setTextColor(tft.color565(50, 180, 50), TFT_BLACK);
    tft.drawString("MICROPHONE", BAR_X, Y_MIC_LBL);
    tft.drawRect(BAR_X, Y_MBAR, BAR_W, BAR_H, colBorder);

    tft.fillRect(0, 219, 3, 21, tft.color565(0, 200, 230));
    tft.setTextSize(1);
    tft.setTextColor(tft.color565(0, 100, 118), TFT_BLACK);
    tft.drawString("[C] BACK", 8, 225);

    drawBatteryStatus(TFT_BLACK);
}

// Repaint only bar interiors and value text — no full screen clear.
static void updateSensors()
{
    const uint16_t colAccelP = tft.color565(0,   220, 245);
    const uint16_t colAccelN = tft.color565(220,  40, 190);
    const uint16_t colLight  = tft.color565(255, 200,   0);
    const uint16_t colMic    = tft.color565(80,  255,  80);
    const uint16_t colVal    = tft.color565(170, 225, 235);
    const uint16_t axCols[]  = {
        tft.color565(0,   200, 220),
        tft.color565(200,  40, 180),
        tft.color565(210, 175,   0),
    };

    float ax = 0.0f, ay = 0.0f, az = 0.0f;
    if (lisReady) {
        ax = lis.getAccelerationX();
        ay = lis.getAccelerationY();
        az = lis.getAccelerationZ();
    }
    const float accelVals[] = { ax, ay, az };
    const char* axNames[]   = { "X", "Y", "Z" };
    const int   accelYs[]   = { Y_XBAR, Y_YBAR, Y_ZBAR };

    for (int i = 0; i < 3; i++) {
        drawBiBar(BAR_X, accelYs[i], BAR_W, BAR_H,
                  accelVals[i], 2.0f, colAccelP, colAccelN);
        tft.fillRect(RVAL_X, accelYs[i], 320 - RVAL_X, BAR_H, TFT_BLACK);
        tft.setTextSize(1);
        tft.setTextColor(axCols[i], TFT_BLACK);
        tft.drawString(axNames[i], RVAL_X, accelYs[i] + 5);
        char buf[10];
        snprintf(buf, sizeof(buf), "%+.2fg", accelVals[i]);
        tft.setTextColor(colVal, TFT_BLACK);
        tft.drawString(buf, RVAL_X + 12, accelYs[i] + 5);
    }

    int lightRaw = analogRead(WIO_LIGHT);
    drawUniBar(BAR_X, Y_LBAR, BAR_W, BAR_H, lightRaw, 1023, colLight);
    tft.fillRect(RVAL_X, Y_LBAR, 320 - RVAL_X, BAR_H, TFT_BLACK);
    {
        char buf[12]; snprintf(buf, sizeof(buf), "%4d", lightRaw);
        tft.setTextSize(1);
        tft.setTextColor(colVal, TFT_BLACK);
        tft.drawString(buf, RVAL_X, Y_LBAR + 5);
    }

    int micPP = sampleMic();
    drawUniBar(BAR_X, Y_MBAR, BAR_W, BAR_H, micPP, 512, colMic);
    tft.fillRect(RVAL_X, Y_MBAR, 320 - RVAL_X, BAR_H, TFT_BLACK);
    {
        char buf[12]; snprintf(buf, sizeof(buf), "%4d", micPP);
        tft.setTextSize(1);
        tft.setTextColor(colVal, TFT_BLACK);
        tft.drawString(buf, RVAL_X, Y_MBAR + 5);
    }
}

void sensorsScreen()
{
    while (digitalRead(WIO_5S_PRESS) == LOW) delay(10);

    // Lazy-init accelerometer on first entry (Wire1 = internal I2C bus).
    if (!lisReady) {
        Wire1.begin();
        lis.begin(Wire1);
        lis.setOutputDataRate(LIS3DHTR_DATARATE_25HZ);
        lis.setFullScaleRange(LIS3DHTR_RANGE_2G);
        lisReady = lis.isConnection();
    }

    drawSensorsFrame();
    updateSensors();

    uint32_t lastUpdate = millis();

    while (true)
    {
        if (millis() - lastUpdate >= 200) {
            lastUpdate = millis();
            updateSensors();
        }

        if (digitalRead(WIO_KEY_B) == LOW) {
            while (digitalRead(WIO_KEY_B) == LOW) delay(10);
            takeScreenshot();
        }

        if (digitalRead(WIO_KEY_C) == LOW) {
            while (digitalRead(WIO_KEY_C) == LOW) delay(10);
            delay(50);
            return;
        }
        delay(10);
    }
}
