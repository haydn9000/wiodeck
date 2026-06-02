// screenshot.cpp — Capture the current display to a 24-bit BGR BMP on the SD card.
//
// Call takeScreenshot() from any screen's input loop.  [KEY_B] is the convention.
// Files are saved to SCREENSHOTS/SCRN0001.BMP, … SCRN9999.BMP (folder auto-created).
//
// Feedback (backlight blink):
//   2 blinks = error (no SD card, card full, or file-open failure)
//   no blink  = success (the ~3-second write delay is the feedback)
//
// BMP format: 24-bit BI_RGB BGR, 320×240, bottom-to-top row order.
// readRect() returns byte-swapped RGB565 (swap back before extracting channels).
//
// SD is initialised once in setup() (main.cpp); no SD.begin() needed here.

#include <Arduino.h>
#include "globals.h"
#include <Seeed_FS.h>
#include <SD/Seeed_SD.h>

// ---------------------------------------------------------------------------
// Little-endian write helpers for BMP header fields.

static void w16(File &f, uint16_t v)
{
    uint8_t b[2] = { (uint8_t)v, (uint8_t)(v >> 8) };
    f.write(b, 2);
}

static void w32(File &f, uint32_t v)
{
    uint8_t b[4] = {
        (uint8_t)v,
        (uint8_t)(v >> 8),
        (uint8_t)(v >> 16),
        (uint8_t)(v >> 24)
    };
    f.write(b, 4);
}

// ---------------------------------------------------------------------------
// Visual feedback via backlight blink — no screen content is touched.

static void scrnBlink(int times)
{
    uint8_t saved = backLight.getBrightness();
    for (int i = 0; i < times; i++) {
        backLight.setBrightness(0);
        delay(150);
        backLight.setBrightness(saved);
        delay(150);
    }
}

// ---------------------------------------------------------------------------
void takeScreenshot()
{
    // SD was initialised once in setup(). No SD.begin() needed here.

    // Ensure SCREENSHOTS directory exists.
    if (!SD.exists("SCREENSHOTS"))
        SD.mkdir("SCREENSHOTS");

    // Find the next unused SCREENSHOTS/SCRN####.BMP filename.
    // Do NOT use a leading '/' — FS::exists/open prepend "0:/" themselves.
    char fname[28];  // "SCREENSHOTS/SCRN9999.BMP" + null
    bool found = false;
    for (uint16_t i = 1; i <= 9999; i++) {
        sprintf(fname, "SCREENSHOTS/SCRN%04u.BMP", (unsigned)i);
        if (!SD.exists(fname)) { found = true; break; }
    }
    if (!found) { scrnBlink(2); return; }

    File f = SD.open(fname, FILE_WRITE);   // FA_CREATE_ALWAYS: creates or truncates
    if (!f) { scrnBlink(2); return; }

    // 24-bit BI_RGB BMP — 320×240, rows stored bottom-to-top.
    const int      W        = 320;
    const int      H        = 240;
    const uint32_t rowBytes = (uint32_t)W * 3;          // 960 (4-byte aligned)
    const uint32_t pixBytes = rowBytes * (uint32_t)H;   // 230,400
    const uint32_t hdrSize  = 14 + 40;                  // 54 bytes (no colour masks)
    const uint32_t fileSize = hdrSize + pixBytes;        // 230,454

    // --- BITMAPFILEHEADER (14 bytes) ---
    w16(f, 0x4D42);
    w32(f, fileSize);
    w16(f, 0); w16(f, 0);
    w32(f, hdrSize);

    // --- BITMAPINFOHEADER (40 bytes) ---
    w32(f, 40);
    w32(f, (uint32_t)W);
    w32(f, (uint32_t)H);        // positive = bottom-to-top
    w16(f, 1);
    w16(f, 24);                 // biBitCount = 24
    w32(f, 0);                  // biCompression = BI_RGB
    w32(f, pixBytes);
    w32(f, 3780); w32(f, 3780);
    w32(f, 0);    w32(f, 0);

    // Pixel data.
    // readRect() byte-swaps each pixel (for pushRect() round-trip compatibility).
    // Undo the swap, then extract BGR channels for the 24-bit BMP row buffer.
    static uint16_t src[320];
    static uint8_t  dst[960];   // 320 pixels × 3 bytes

    for (int y = H - 1; y >= 0; y--) {
        tft.readRect(0, y, W, 1, src);
        for (int x = 0; x < W; x++) {
            // Undo readRect's byte-swap to recover true RGB565.
            uint16_t px = (src[x] >> 8) | (src[x] << 8);
            // Extract 8-bit channels (drop least-significant padding bits).
            dst[x * 3 + 0] = (uint8_t)((px << 3) & 0xF8);    // B
            dst[x * 3 + 1] = (uint8_t)((px >> 3) & 0xFC);    // G
            dst[x * 3 + 2] = (uint8_t)((px >> 8) & 0xF8);    // R
        }
        f.write(dst, rowBytes);
    }

    f.close();
    // No success blink: the ~3-second write pause is feedback enough.
    // SD.begin() power noise already causes one visible dip; adding a blink
    // would look like two flickers and confuse the error indicator.
}
