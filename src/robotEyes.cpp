// robotEyes.cpp — Cyberpunk robotic eyes, sound-reactive.
//
// Sprite-buffered: each eye drawn into a 150×150 sprite, pushed atomically.
// Zero flicker.  Each eye has: sclera ring, segmented iris, oval pupil,
// shine highlights, mechanical sliding eyelid panels, HUD corner brackets,
// and circuit trace decorations.
//
// States (mic peak-to-peak amplitude):
//   IDLE    < 40    — half-lidded drooping, dim cyan glow
//   CURIOUS 40-120  — wide open, cyan, animated iris scan-line
//   ALERT   120-280 — fully open, green, tiny pupil + sparkle
//   SHOCK   > 280   — furrowed, amber, oval mouth + screen glitch
//
// Controls: [C] back to menu

#include <Arduino.h>
#include "globals.h"

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------
static const int EYE_L_CX = 72;
static const int EYE_R_CX = 248;
static const int EYE_CY   = 120;

static const int SPR_W    = 150;
static const int SPR_H    = 150;
static const int SC       = 75;   // sprite centre x
static const int SCY      = 75;   // sprite centre y
static const uint16_t BG  = TFT_BLACK;

// ---------------------------------------------------------------------------
// States
// ---------------------------------------------------------------------------
enum EyeState { EYE_IDLE, EYE_CURIOUS, EYE_ALERT, EYE_SHOCK };

// Pre-computed endpoints for 8 iris spokes on a circle of radius 19.
static const int8_t SEG_DX[8] = { 19, 13,  0, -13, -19, -13,  0,  13 };
static const int8_t SEG_DY[8] = {  0, 13, 19,  13,   0, -13, -19, -13 };

// ---------------------------------------------------------------------------
// Mic
// ---------------------------------------------------------------------------
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

static EyeState levelToState(int pp)
{
    if (pp < 40)  return EYE_IDLE;
    if (pp < 120) return EYE_CURIOUS;
    if (pp < 280) return EYE_ALERT;
    return EYE_SHOCK;
}

// ---------------------------------------------------------------------------
// Draw one eye into the 150×150 sprite.
//   flipX : true for the right eye — mirrors lid angle and shine dot.
//
// Shape: large round socket (circle r=33), chord-clipped eyelid fill, round
// pupils.  All four states use the same basic anatomy — only lid height,
// pupil size, and sparkles change, keeping the look soft and expressive.
// ---------------------------------------------------------------------------
static void drawEyeSprite(EyeState state, bool flipX)
{
    // ---- State palette ----
    uint16_t rimCol, irisBase, irisHi, glowCol, darkGlow;
    switch (state) {
        case EYE_CURIOUS:
            rimCol   = tft.color565(  0, 160, 185);
            irisBase = tft.color565(  0,  85, 130);
            irisHi   = tft.color565(  0, 145, 175);
            glowCol  = tft.color565(  0,  40,  60);
            darkGlow = tft.color565(  0,  18,  30);
            break;
        case EYE_ALERT:
            rimCol   = tft.color565( 50, 180,  50);
            irisBase = tft.color565(  0,  80,  20);
            irisHi   = tft.color565( 90, 175,  65);
            glowCol  = tft.color565(  0,  32,   8);
            darkGlow = tft.color565(  0,  12,   3);
            break;
        case EYE_SHOCK:
            rimCol   = tft.color565(185, 120,   0);
            irisBase = tft.color565(110,  45,   0);
            irisHi   = tft.color565(185,  95,   0);
            glowCol  = tft.color565( 45,  18,   0);
            darkGlow = tft.color565( 18,   6,   0);
            break;
        default: // IDLE
            rimCol   = tft.color565(  0, 110, 140);
            irisBase = tft.color565(  0,  55,  82);
            irisHi   = tft.color565(  0,  90, 120);
            glowCol  = tft.color565(  0,  28,  42);
            darkGlow = tft.color565(  0,  10,  18);
            break;
    }

    const uint16_t lidPanel = tft.color565(  8,  16,  28);
    const uint16_t pBlack   = tft.color565(  0,   0,   0);
    const uint16_t shineW   = tft.color565(210, 248, 255);
    const uint16_t scleraC  = tft.color565(  5,  12,  22);

    spr.fillSprite(BG);

    // ---- Round socket ----
    const int R  = 33;   // socket radius
    const int IR = 23;   // iris radius

    // Glow halo (3 concentric circles, fading outward)
    spr.drawCircle(SC, SCY, R + 4, darkGlow);
    spr.drawCircle(SC, SCY, R + 3, darkGlow);
    spr.drawCircle(SC, SCY, R + 2, glowCol);

    // Sclera fill
    spr.fillCircle(SC, SCY, R, scleraC);

    // Iris fill
    spr.fillCircle(SC, SCY, IR, irisBase);

    // Radial spokes (scaled to IR)
    for (int s = 0; s < 8; s++) {
        int ex = SC  + SEG_DX[s] * IR / 19;
        int ey = SCY + SEG_DY[s] * IR / 19;
        spr.drawLine(SC, SCY, ex, ey, darkGlow);
    }

    // Iris rings
    spr.drawCircle(SC, SCY, IR,     irisHi);
    spr.drawCircle(SC, SCY, IR - 1, rimCol);
    spr.drawCircle(SC, SCY, IR - 5, glowCol);

    // CURIOUS: animated iris scan-line
    if (state == EYE_CURIOUS) {
        int sweep = (int)(millis() / 18) % (IR * 2);
        int sy    = SCY - IR + sweep;
        int d     = sy - SCY;
        int r2    = IR * IR - d * d;
        if (r2 > 0) {
            int hw = (int)sqrtf((float)r2);
            spr.drawFastHLine(SC - hw, sy, hw * 2, irisHi);
        }
    }

    // ---- Pupil — round for all states ----
    int PR;
    switch (state) {
        case EYE_ALERT:   PR = 4; break;   // tiny startled
        case EYE_SHOCK:   PR = 5; break;   // small focused
        case EYE_CURIOUS: PR = 9; break;   // big and bright
        default:          PR = 8; break;   // IDLE normal
    }
    spr.fillCircle(SC, SCY, PR, pBlack);

    // ---- Shine highlights ----
    int shX = flipX ? SC - 10 : SC + 10;
    spr.fillCircle(shX, SCY - 10, 4, shineW);
    spr.fillCircle(shX, SCY - 10, 2, tft.color565(255, 255, 255));
    // small secondary shine
    spr.fillCircle(flipX ? SC - 4 : SC + 4, SCY + 8, 2, glowCol);

    // Extra sparkle cross for CURIOUS and ALERT
    if (state == EYE_CURIOUS || state == EYE_ALERT) {
        spr.drawFastHLine(shX - 6, SCY - 10, 3, shineW);
        spr.drawFastHLine(shX + 4, SCY - 10, 3, shineW);
        spr.drawFastVLine(shX,     SCY - 16, 3, shineW);
        spr.drawFastVLine(shX,     SCY -  3, 3, shineW);
    }

    // ---- Eyelid — chord-clipped fill ----
    // lidY is the y-coordinate of the lid's bottom edge inside the sprite.
    // Every row from (SCY-R) down to lidY is filled with lidPanel colour,
    // clipped to the socket circle so the edge curves naturally.
    int lidY;
    switch (state) {
        case EYE_IDLE:    lidY = SCY + 3;  break;  // half-lidded, sleepy
        case EYE_CURIOUS: lidY = SCY - 27; break;  // wide open
        case EYE_ALERT:   lidY = SCY - 30; break;  // fully open
        case EYE_SHOCK:   lidY = SCY - 8;  break;  // slightly furrowed
        default:          lidY = SCY + 3;  break;
    }

    // Fill lid rows clipped to the socket circle
    for (int y = SCY - R; y <= lidY; y++) {
        int dy = y - SCY;
        int r2 = R * R - dy * dy;
        if (r2 <= 0) continue;
        int hw = (int)sqrtf((float)r2);
        spr.drawFastHLine(SC - hw, y, hw * 2 + 1, lidPanel);
    }

    // SHOCK: nasal-side wedge makes the inner corner droop slightly
    if (state == EYE_SHOCK) {
        const int slope = 10;
        for (int y = lidY + 1; y <= lidY + slope; y++) {
            float t  = (float)(y - lidY) / slope;
            int   dy = y - SCY;
            int   r2 = R * R - dy * dy;
            if (r2 <= 0) continue;
            int hw    = (int)sqrtf((float)r2);
            // inner/nasal side: right for left eye, left for right eye
            int fillW = (int)(hw * t);
            if (fillW > 0)
                spr.drawFastHLine(flipX ? SC - hw : SC, y, fillW, lidPanel);
        }
    }

    // Bright lid edge line (at the base of the lid)
    {
        int dy = lidY - SCY;
        int r2 = R * R - dy * dy;
        if (r2 > 0) {
            int hw = (int)sqrtf((float)r2);
            spr.drawFastHLine(SC - hw, lidY,     hw * 2 + 1, rimCol);
            spr.drawFastHLine(SC - hw, lidY + 1, hw * 2 + 1, glowCol);
        }
    }

    // Cute upper eyelash ticks (3 short lines from the top of the socket)
    if (state != EYE_ALERT) {
        int topY = SCY - R;
        spr.drawFastVLine(SC,      topY - 3, 4, rimCol);
        spr.drawFastVLine(SC - 10, topY - 1, 3, rimCol);
        spr.drawFastVLine(SC + 10, topY - 1, 3, rimCol);
    }

    // ---- Socket rim ----
    spr.drawCircle(SC, SCY, R,     rimCol);
    spr.drawCircle(SC, SCY, R + 1, glowCol);

    // ---- HUD corner brackets (L-shaped, just outside the socket circle) ----
    const int BL = 8;
    const int bx = SC  - R - 4;   // left X
    const int bx2= SC  + R + 4;   // right X
    const int by = SCY - R - 4;   // top Y
    const int by2= SCY + R + 4;   // bottom Y
    // NW
    spr.drawFastHLine(bx,      by, BL, rimCol);
    spr.drawFastVLine(bx,      by, BL, rimCol);
    // NE
    spr.drawFastHLine(bx2 - BL + 1, by, BL, rimCol);
    spr.drawFastVLine(bx2,           by, BL, rimCol);
    // SW
    spr.drawFastHLine(bx,            by2,          BL, rimCol);
    spr.drawFastVLine(bx,            by2 - BL + 1, BL, rimCol);
    // SE
    spr.drawFastHLine(bx2 - BL + 1, by2,          BL, rimCol);
    spr.drawFastVLine(bx2,           by2 - BL + 1, BL, rimCol);
}

// ---------------------------------------------------------------------------
// Static background — drawn once at entry; sprites are pushed on top each
// frame but never cover the nose bridge (x 148–172) or the top HUD band.
// ---------------------------------------------------------------------------
static void drawBackground()
{
    tft.fillScreen(BG);

    // Top HUD band
    uint16_t hudLine = tft.color565(0, 50, 70);
    tft.drawFastHLine(0, 20, 320, hudLine);
    tft.drawFastHLine(0, 21, 320, tft.color565(0, 25, 38));
    tft.setTextSize(1);
    tft.setTextColor(tft.color565(0, 110, 145), BG);
    tft.drawString("UNIT-7 // OPTICAL ARRAY", 8, 8);
    tft.setTextColor(tft.color565(0, 65, 90), BG);
    tft.drawString("[C] BACK", 262, 8);

    // Nose bridge (x = 160, safely between the two sprites)
    int mx       = 160;
    uint16_t vc  = tft.color565(0, 55, 75);
    uint16_t bc  = tft.color565(0, 80, 105);
    tft.drawFastVLine(mx,     EYE_CY - 26, 52, vc);
    tft.drawFastVLine(mx - 1, EYE_CY - 8,  16, tft.color565(0, 22, 32));
    tft.drawFastVLine(mx + 1, EYE_CY - 8,  16, tft.color565(0, 22, 32));
    tft.fillRect(mx - 5, EYE_CY - 5, 11, 10, tft.color565(3, 10, 20));
    tft.drawRect( mx - 5, EYE_CY - 5, 11, 10, bc);
    tft.fillRect( mx - 2, EYE_CY - 26, 5, 5, vc);
    tft.drawRect(  mx - 2, EYE_CY - 26, 5, 5, bc);
    tft.fillRect( mx - 2, EYE_CY + 22, 5, 5, vc);
    tft.drawRect(  mx - 2, EYE_CY + 22, 5, 5, bc);
}

// ---------------------------------------------------------------------------
// Mouth — drawn in the strip y=195-215 below the sprites.
// Audio level controls how wide/open the arc is.
// SHOCK state gets a surprised "O" instead of an arc.
// ---------------------------------------------------------------------------
static uint16_t stateRimCol(EyeState s) {
    switch (s) {
        case EYE_CURIOUS: return tft.color565(  0, 160, 185);
        case EYE_ALERT:   return tft.color565( 50, 180,  50);
        case EYE_SHOCK:   return tft.color565(185, 120,   0);
        default:          return tft.color565(  0, 110, 140);
    }
}
static uint16_t stateGlowCol(EyeState s) {
    switch (s) {
        case EYE_CURIOUS: return tft.color565(  0,  40,  60);
        case EYE_ALERT:   return tft.color565(  0,  32,   8);
        case EYE_SHOCK:   return tft.color565( 45,  18,   0);
        default:          return tft.color565(  0,  28,  42);
    }
}

static void drawMouth(EyeState state, int level)
{
    // Arc circle centre at y=185 (inside sprite area in the gap x=148-172
    // not covered by sprites). Arc bulges downward into the y=195-215 strip.
    // Only that strip is cleared each frame.
    const int cx   = 160;
    const int base = 185;
    uint16_t rimCol  = stateRimCol(state);
    uint16_t glowCol = stateGlowCol(state);

    // Clear mouth strip every frame so previous state doesn't linger
              tft.fillRect(110, 195, 100, 26, TFT_BLACK);  // height 26 covers ellipse glow at y=218

    if (state == EYE_SHOCK) {
        // Surprised oval mouth (wide, short)
        const int ow = 18, oh = 10;
        tft.fillEllipse(cx, 206, ow, oh, tft.color565(5, 12, 22));
        tft.drawEllipse(cx, 206, ow,     oh,     rimCol);
        tft.drawEllipse(cx, 206, ow + 1, oh + 1, rimCol);
        tft.drawEllipse(cx, 206, ow + 2, oh + 2, glowCol);
        return;
    }

    // Map audio level to openness: 0.0 = silent, 1.0 = max
    float open = (float)constrain(level, 0, 380) / 380.0f;

    // R: arc radius — controls how far the arc dips into the strip
    // halfAngle: half the arc span — controls width
    float halfAngle = 40.0f + open * 45.0f;   // 40° quiet -> 85° loud
    int   R         = 18  + (int)(open * 10); // R: 18 quiet -> 28 loud
                                               // max bottom: 185+28=213 < 216 ✓

    const float toRad = (float)M_PI / 180.0f;
    const int   steps = 24;
    float startRad = (90.0f - halfAngle) * toRad;
    float endRad   = (90.0f + halfAngle) * toRad;

    // Draw 4 concentric arcs: bright core + outer glow
    const int      offsets[4] = { 0,  1,  2,  3 };
    const uint16_t cols[4]    = { rimCol, rimCol, glowCol, glowCol };
    for (int th = 0; th < 4; th++) {
        int      r   = R + offsets[th];
        uint16_t col = cols[th];
        int px = cx + (int)(r * cosf(startRad));
        int py = base + (int)(r * sinf(startRad));
        for (int i = 1; i <= steps; i++) {
            float a  = startRad + (endRad - startRad) * (float)i / steps;
            int   nx = cx + (int)(r * cosf(a));
            int   ny = base + (int)(r * sinf(a));
            tft.drawLine(px, py, nx, ny, col);
            px = nx; py = ny;
        }
    }
}

// ---------------------------------------------------------------------------
// Shock glitch artefacts — random coloured scan-lines flashed across screen
// ---------------------------------------------------------------------------
static void drawShockGlitch()
{
    // Draw glitch lines only inside the two sprite regions so they are
    // fully overwritten when the sprites are pushed — no persistent artefacts.
    // Left sprite:  x = 0..147   Right sprite: x = 173..319
    // Both cover:   y = 45..194
    uint16_t gc = tft.color565(255, 175, 0);
    uint16_t wc = tft.color565(255, 220, 140);
    for (int g = 0; g < 5; g++) {
        // alternate between left and right eye region
        int rx = (g % 2 == 0) ? random(0, 120) : random(173, 300);
        int ry = random(45, 194);
        int rw = random(8, 50);
        tft.drawFastHLine(rx, ry, rw, gc);
        rx = (g % 2 == 0) ? random(0, 120) : random(173, 300);
        tft.drawFastHLine(rx, random(45, 194), random(4, 30), wc);
    }
    delay(25);
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------
void robotEyesScreen()
{
    while (digitalRead(WIO_5S_PRESS) == LOW) delay(10);

    spr.deleteSprite();
    bool sprOk = (spr.createSprite(SPR_W, SPR_H) != nullptr);

    drawBackground();

    EyeState state     = EYE_IDLE;
    EyeState prevState = (EyeState)99;
    EyeState candidate = EYE_IDLE;
    int      holdCount = 0;
    static const int HOLD_FRAMES = 10;
    float smooth    = 0.0f;
    unsigned long lastDraw = 0;

    drawMouth(state, 0);
    drawBatteryStatus(BG);

    while (true)
    {
        if (digitalRead(WIO_KEY_C) == LOW) {
            while (digitalRead(WIO_KEY_C) == LOW) delay(10);
            delay(50);
            spr.deleteSprite();
            spr.createSprite(TFT_HEIGHT, TFT_WIDTH);
            return;
        }

        int raw = sampleMic();
        // Asymmetric envelope: fast attack, slow decay
        if (raw > smooth)
            smooth = smooth * 0.45f + raw * 0.55f;
        else
            smooth = smooth * 0.88f + raw * 0.12f;
        int level = (int)smooth;
        EyeState rawState = levelToState(level);

        // Hysteresis: escalate instantly, de-escalate only after HOLD_FRAMES
        if (rawState > state) {
            state     = rawState;
            candidate = rawState;
            holdCount = 0;
        } else if (rawState < state) {
            if (rawState == candidate) {
                if (++holdCount >= HOLD_FRAMES) {
                    state     = candidate;
                    holdCount = 0;
                }
            } else {
                candidate = rawState;
                holdCount = 1;
            }
        } else {
            candidate = rawState;
            holdCount = 0;
        }

        if (state != prevState) {
            if (state == EYE_SHOCK) drawShockGlitch();
            drawBatteryStatus(BG);
            prevState = state;
        }

        // Redraw eyes at ~16 fps; CURIOUS redraws every frame for scan-line animation
        unsigned long now = millis();
        if (state != EYE_CURIOUS && now - lastDraw < 60) continue;
        if (state == EYE_CURIOUS  && now - lastDraw < 30) continue;

        if (sprOk) {
            drawEyeSprite(state, false);
            spr.pushSprite(EYE_L_CX - SC, EYE_CY - SCY);

            drawEyeSprite(state, true);
            spr.pushSprite(EYE_R_CX - SC, EYE_CY - SCY);
        }

        drawMouth(state, level);

        lastDraw = now;
    }
}
