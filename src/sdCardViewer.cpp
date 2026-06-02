// sdCardViewer.cpp — Browse BMP images stored on the SD card.
//
// Controls (folder picker):
//   [UP/DOWN] Navigate folders
//   [PRESS]   Select folder
//   [C]       Back to menu
//
// Controls (image viewer):
//   [LEFT]    Previous image
//   [RIGHT]   Next image
//   [A]       Folder picker
//   [B]       Screenshot
//   [C]       Back to menu

#include <Arduino.h>
#include "globals.h"
#include <Seeed_FS.h>
#include <SD/Seeed_SD.h>

static const int MAX_BMPS    = 50;
static const int MAX_FOLDERS = 20;

static char    bmpFiles[MAX_BMPS][64];       // bare filename, up to 63 chars
static int     bmpCount = 0;
static int     bmpIndex = 0;

static char    folderList[MAX_FOLDERS][32];  // bare folder name; "" = SD root
static int     folderCount     = 0;
static int     folderPickerIdx = 0;          // highlighted row in picker
static char    currentFolder[32] = "";       // active folder ("" = root)

static uint8_t  rowBuf[1280];  // one BMP row: 320 px × 4 bytes max (32bpp)
static uint16_t rgbRow[320];   // one display row: RGB565

// -------------------------------------------------------------------------
// folder: "" = SD root, otherwise bare folder name (e.g. "SCREENSHOTS")
static void scanBmps(const char* folder)
{
    bmpCount = 0;
    char dirPath[36];
    if (folder[0] == '\0')
        strncpy(dirPath, "/", sizeof(dirPath));
    else
        strncpy(dirPath, folder, sizeof(dirPath) - 1);
    dirPath[sizeof(dirPath) - 1] = '\0';

    File dir = SD.open(dirPath);
    if (!dir) return;

    while (bmpCount < MAX_BMPS)
    {
        File entry = dir.openNextFile();
        if (!entry) break;
        if (!entry.isDirectory())
        {
            // entry.name() may return a full path — strip to bare filename.
            const char* full = entry.name();
            const char* slash = strrchr(full, '/');
            const char* n = slash ? slash + 1 : full;
            int len = (int)strlen(n);
            if (len >= 5 && len <= 63)
            {
                const char* ext = n + len - 4;
                if (ext[0] == '.' &&
                    (ext[1] == 'B' || ext[1] == 'b') &&
                    (ext[2] == 'M' || ext[2] == 'm') &&
                    (ext[3] == 'P' || ext[3] == 'p'))
                {
                    strncpy(bmpFiles[bmpCount], n, 63);
                    bmpFiles[bmpCount][63] = '\0';
                    bmpCount++;
                }
            }
        }
        entry.close();
    }
    dir.close();
}

// -------------------------------------------------------------------------
// Populate folderList[] with bare directory names from the SD root.
// folderList[0] is always "" (the root itself).
static void scanFolders()
{
    folderCount = 0;
    strncpy(folderList[0], "", sizeof(folderList[0]));
    folderCount = 1;

    File root = SD.open("/");
    if (!root) return;

    while (folderCount < MAX_FOLDERS)
    {
        File entry = root.openNextFile();
        if (!entry) break;
        if (entry.isDirectory())
        {
            const char* full = entry.name();
            const char* slash = strrchr(full, '/');
            const char* n = slash ? slash + 1 : full;
            int len = (int)strlen(n);
            // Skip Windows system folders and dot-prefixed hidden folders.
            if (strncmp(n, "System Volume Information", 25) == 0) { entry.close(); continue; }
            if (strncmp(n, "RECYCLER",  8) == 0) { entry.close(); continue; }
            if (strncmp(n, "$RECYCLE",  8) == 0) { entry.close(); continue; }
            if (n[0] == '.') { entry.close(); continue; }
            if (len >= 1 && len <= 31)
            {
                strncpy(folderList[folderCount], n, 31);
                folderList[folderCount][31] = '\0';
                folderCount++;
            }
        }
        entry.close();
    }
    root.close();
}

// -------------------------------------------------------------------------
static void drawViewerHeader()
{
    tft.fillRect(0, 0, 320, 30, tft.color565(0, 8, 20));
    tft.fillRect(0, 0, 3, 30, tft.color565(0, 220, 245));
    tft.setTextSize(2);
    tft.setTextColor(tft.color565(0, 220, 245), tft.color565(0, 8, 20));
    tft.drawString("// SD VIEWER", 10, 7);
    tft.setTextSize(1);
    tft.setTextColor(tft.color565(0, 148, 170), tft.color565(0, 8, 20));
    tft.drawString("PHOTOS", 160, 11);
    tft.drawFastHLine(0, 29, 320, tft.color565(0, 80, 100));
}

// -------------------------------------------------------------------------
// 20 px status bar overlaid at the bottom of the screen.
static void drawViewerStatus()
{
    tft.fillRect(0, 220, 320, 20, TFT_BLACK);
    tft.fillRect(0, 220, 3, 20, tft.color565(0, 210, 235));

    char idx[10];
    snprintf(idx, sizeof(idx), "%d/%d", bmpIndex + 1, bmpCount);
    tft.setTextSize(1);
    tft.setTextColor(tft.color565(0, 210, 235), TFT_BLACK);
    tft.drawString(idx, 8, 227);

    tft.setTextColor(tft.color565(180, 220, 230), TFT_BLACK);
    tft.drawString(bmpFiles[bmpIndex], 50, 227);

    tft.setTextColor(tft.color565(0, 80, 95), TFT_BLACK);
    tft.drawString("[A] FOLDER  [C] BACK", 176, 227);
}

// -------------------------------------------------------------------------
static void drawNoBmps()
{
    tft.fillScreen(TFT_BLACK);
    drawViewerHeader();

    uint16_t nc  = tft.color565(0, 220, 245);
    uint16_t nbg = tft.color565(0, 8, 18);
    tft.fillRect(10, 48, 300, 140, nbg);
    int t = 12;
    tft.drawFastHLine(10,  48,  t, nc); tft.drawFastVLine(10,  48,  t, nc);
    tft.drawFastHLine(298, 48,  t, nc); tft.drawFastVLine(309, 48,  t, nc);
    tft.drawFastHLine(10,  187, t, nc); tft.drawFastVLine(10,  175, t, nc);
    tft.drawFastHLine(298, 187, t, nc); tft.drawFastVLine(309, 175, t, nc);
    tft.drawFastHLine(11, 49, 298, tft.color565(0, 40, 60));
    tft.setTextSize(2);
    tft.setTextColor(nc, nbg);
    tft.drawString("> NO_FILES", 50, 90);
    tft.setTextSize(1);
    tft.setTextColor(tft.color565(0, 158, 178), nbg);
    char noMsg[40];
    if (currentFolder[0] == '\0')
        strncpy(noMsg, "NO .BMP FILES IN ROOT", sizeof(noMsg));
    else
        snprintf(noMsg, sizeof(noMsg), "NO .BMP IN %s", currentFolder);
    tft.drawString(noMsg, 50, 118);
    tft.drawString("[A] FOLDERS  [C] BACK", 50, 134);
    drawBatteryStatus(TFT_BLACK);
}

// -------------------------------------------------------------------------
// Folder picker: scrollable list of directories, highlights folderPickerIdx.
static void drawFolderPicker()
{
    tft.fillScreen(TFT_BLACK);
    tft.fillRect(0, 0, 320, 30, tft.color565(0, 8, 20));
    tft.fillRect(0, 0, 3,   30, tft.color565(0, 220, 245));
    tft.setTextSize(2);
    tft.setTextColor(tft.color565(0, 220, 245), tft.color565(0, 8, 20));
    tft.drawString("// SD VIEWER", 10, 7);
    tft.setTextSize(1);
    tft.setTextColor(tft.color565(0, 148, 170), tft.color565(0, 8, 20));
    tft.drawString("FOLDER", 160, 11);
    tft.drawFastHLine(0, 29, 320, tft.color565(0, 80, 100));

    const int ROW_H   = 22;
    const int START_Y = 36;
    const int VISIBLE = 8;

    int scrollTop = folderPickerIdx - VISIBLE / 2;
    if (scrollTop < 0) scrollTop = 0;
    if (scrollTop > folderCount - VISIBLE) scrollTop = folderCount - VISIBLE;
    if (scrollTop < 0) scrollTop = 0;

    for (int i = 0; i < VISIBLE && (scrollTop + i) < folderCount; i++)
    {
        int fi   = scrollTop + i;
        int y    = START_Y + i * ROW_H;
        bool sel = (fi == folderPickerIdx);

        uint16_t bg  = sel ? tft.color565(0, 50, 70)    : TFT_BLACK;
        uint16_t fg  = sel ? tft.color565(0, 220, 245)  : tft.color565(120, 180, 200);
        uint16_t acc = sel ? tft.color565(0, 220, 245)  : tft.color565(0, 50, 70);

        tft.fillRect(4, y, 312, ROW_H - 2, bg);
        tft.fillRect(4, y, 3,   ROW_H - 2, acc);

        tft.setTextSize(1);
        tft.setTextColor(fg, bg);
        const char* label = (folderList[fi][0] == '\0') ? "/ (root)" : folderList[fi];
        tft.drawString(label, 12, y + 7);
    }

    tft.fillRect(0, 220, 320, 20, TFT_BLACK);
    tft.fillRect(0, 220, 3,   20, tft.color565(0, 210, 235));
    tft.setTextSize(1);
    tft.setTextColor(tft.color565(0, 80, 95), TFT_BLACK);
    tft.drawString("[^/v] NAV  [PRESS] SELECT  [C] BACK", 8, 227);
    drawBatteryStatus(TFT_BLACK);
}

// Last-detected BMP geometry — shown in the error screen.
static int32_t  g_lastW = 0, g_lastH = 0;
static uint16_t g_lastBpp = 0;

// -------------------------------------------------------------------------
// Load a 320×240 BMP from folder/name and draw it full-screen.
// folder: "" = SD root; otherwise bare folder name (e.g. "SCREENSHOTS").
// Returns true on success.
static bool loadAndDrawBmp(const char* folder, const char* name)
{
    // Build path without leading '/' — Seeed_Arduino_FS prepends "0:/" itself.
    char path[96];
    if (folder[0] == '\0')
        strncpy(path, name, sizeof(path) - 1);
    else
        snprintf(path, sizeof(path), "%s/%s", folder, name);
    path[sizeof(path) - 1] = '\0';

    File f = SD.open(path, FILE_READ);
    if (!f) return false;

    uint8_t hdr[54];
    if (f.read(hdr, 54) != 54) { f.close(); return false; }
    if (hdr[0] != 'B' || hdr[1] != 'M') { f.close(); return false; }

    uint32_t dataOff = (uint32_t)hdr[10] | ((uint32_t)hdr[11] << 8)
                     | ((uint32_t)hdr[12] << 16) | ((uint32_t)hdr[13] << 24);
    int32_t  w       = (int32_t)((uint32_t)hdr[18] | ((uint32_t)hdr[19] << 8)
                     | ((uint32_t)hdr[20] << 16) | ((uint32_t)hdr[21] << 24));
    int32_t  h       = (int32_t)((uint32_t)hdr[22] | ((uint32_t)hdr[23] << 8)
                     | ((uint32_t)hdr[24] << 16) | ((uint32_t)hdr[25] << 24));
    uint16_t bpp     = (uint16_t)hdr[28] | ((uint16_t)hdr[29] << 8);
    // compression: 0=BI_RGB, 3=BI_BITFIELDS (Windows 32bpp screenshots use this)
    uint32_t comp    = (uint32_t)hdr[30] | ((uint32_t)hdr[31] << 8)
                     | ((uint32_t)hdr[32] << 16) | ((uint32_t)hdr[33] << 24);

    // Store for error display.
    g_lastW = w; g_lastH = h; g_lastBpp = bpp;

    // Accept 16bpp, 24bpp, and 32bpp BMP formats.
    bool is24 = (bpp == 24 && comp == 0);
    bool is32 = (bpp == 32 && (comp == 0 || comp == 3));

    // 16-bit: BI_RGB = RGB555, BI_BITFIELDS = read masks to detect RGB565 vs RGB555.
    bool is16     = false;
    bool is16_565 = false;  // true = RGB565, false = RGB555
    if (bpp == 16)
    {
        if (comp == 0)
        {
            is16 = true;       // BI_RGB 16-bit defaults to RGB555
        }
        else if (comp == 3)
        {
            // Masks are at bytes 54–65 in the file; we've read exactly 54 so far.
            uint8_t masks[12];
            if (f.read(masks, 12) == 12)
            {
                uint32_t rMask = (uint32_t)masks[0]  | ((uint32_t)masks[1]  << 8)
                               | ((uint32_t)masks[2]  << 16) | ((uint32_t)masks[3]  << 24);
                uint32_t gMask = (uint32_t)masks[4]  | ((uint32_t)masks[5]  << 8)
                               | ((uint32_t)masks[6]  << 16) | ((uint32_t)masks[7]  << 24);
                uint32_t bMask = (uint32_t)masks[8]  | ((uint32_t)masks[9]  << 8)
                               | ((uint32_t)masks[10] << 16) | ((uint32_t)masks[11] << 24);
                is16_565 = (rMask == 0xF800 && gMask == 0x07E0 && bMask == 0x001F);
                is16 = true;
            }
        }
    }

    int32_t absH = h < 0 ? -h : h;

    if ((!is24 && !is32 && !is16) || w != 320 || absH != 240)
    {
        f.close();
        return false;
    }

    // Positive height = standard bottom-to-top BMP storage.
    bool bottomToTop = (h > 0);
    int  stride      = is32 ? 1280 : (is16 ? 640 : 960);   // bytes per row

    f.seek(dataOff);

    // Read rows sequentially and push each to its correct display y position.
    for (int fileRow = 0; fileRow < 240; fileRow++)
    {
        f.read(rowBuf, stride);
        int dispY = bottomToTop ? (239 - fileRow) : fileRow;
        if (is32)
        {
            for (int x = 0; x < 320; x++)  // BGRA → RGB565 big-endian (ignore alpha)
            {
                uint16_t c = tft.color565(rowBuf[x * 4 + 2], rowBuf[x * 4 + 1], rowBuf[x * 4 + 0]);
                rgbRow[x] = (c >> 8) | (c << 8);   // swap to big-endian for SAMD51 DMA
            }
        }
        else if (is16)
        {
            for (int x = 0; x < 320; x++)
            {
                uint16_t pixel = (uint16_t)rowBuf[x * 2] | ((uint16_t)rowBuf[x * 2 + 1] << 8);
                uint16_t c;
                if (is16_565)
                {
                    // RGB565 — already in the right format, just byte-swap for SAMD51 DMA
                    c = pixel;
                }
                else
                {
                    // RGB555: xRRRRRGGGGGBBBBB → convert to RGB565 via color565()
                    uint8_t r = (pixel >> 10) & 0x1F;
                    uint8_t g = (pixel >>  5) & 0x1F;
                    uint8_t b =  pixel        & 0x1F;
                    c = tft.color565(r << 3, g << 3, b << 3);
                }
                rgbRow[x] = (c >> 8) | (c << 8);   // swap to big-endian for SAMD51 DMA
            }
        }
        else
        {
            for (int x = 0; x < 320; x++)  // BGR → RGB565 big-endian
            {
                uint16_t c = tft.color565(rowBuf[x * 3 + 2], rowBuf[x * 3 + 1], rowBuf[x * 3 + 0]);
                rgbRow[x] = (c >> 8) | (c << 8);   // swap to big-endian for SAMD51 DMA
            }
        }
        tft.pushImage(0, dispY, 320, 1, rgbRow);
    }

    f.close();
    return true;
}

// -------------------------------------------------------------------------
static void showCurrentBmp()
{
    // Loading hint — drawn before the image pixels arrive.
    tft.fillRect(0, 0, 320, 20, TFT_BLACK);
    tft.setTextSize(1);
    tft.setTextColor(tft.color565(0, 140, 165), TFT_BLACK);
    char msg[32];
    snprintf(msg, sizeof(msg), "Loading %s...", bmpFiles[bmpIndex]);
    tft.drawString(msg, 5, 5);

    bool ok = loadAndDrawBmp(currentFolder, bmpFiles[bmpIndex]);

    if (!ok)
    {
        tft.fillScreen(TFT_BLACK);
        drawViewerHeader();
        tft.setTextSize(2);
        tft.setTextColor(tft.color565(210, 80, 60), TFT_BLACK);
        tft.drawString("UNSUPPORTED", 30, 70);
        tft.setTextSize(1);
        tft.setTextColor(tft.color565(160, 100, 80), TFT_BLACK);
        tft.drawString("Requires 320x240, 16/24/32-bit BMP", 30, 100);
        char info[48];
        snprintf(info, sizeof(info), "Detected: %ldx%ld  %u-bit",
                 g_lastW, (g_lastH < 0 ? -g_lastH : g_lastH), g_lastBpp);
        tft.drawString(info, 30, 116);
        tft.drawString(bmpFiles[bmpIndex], 30, 132);
    }

    drawViewerStatus();
    // No battery overlay when viewing a full-screen BMP image.
}

// -------------------------------------------------------------------------
void sdCardViewerScreen()
{
    while (digitalRead(WIO_5S_PRESS) == LOW) delay(10);

    tft.fillScreen(TFT_BLACK);
    drawViewerHeader();
    tft.setTextSize(1);
    tft.setTextColor(tft.color565(0, 160, 185), TFT_BLACK);
    tft.drawString("Scanning SD card...", 10, 50);

    // Re-initialise SD in case BLE/WiFi operations disturbed the SPI state.
    SD.end();
    SD.begin(SDCARD_SS_PIN, SDCARD_SPI);

    scanFolders();
    folderPickerIdx = 0;

    // ---- outer loop: folder picker ----------------------------------------
    while (true)
    {
        drawFolderPicker();

        while (true)
        {
            if (digitalRead(WIO_5S_UP) == LOW)
            {
                while (digitalRead(WIO_5S_UP) == LOW) delay(10);
                if (folderPickerIdx > 0) { folderPickerIdx--; drawFolderPicker(); }
            }
            if (digitalRead(WIO_5S_DOWN) == LOW)
            {
                while (digitalRead(WIO_5S_DOWN) == LOW) delay(10);
                if (folderPickerIdx < folderCount - 1) { folderPickerIdx++; drawFolderPicker(); }
            }
            // PRESS or joystick RIGHT to confirm selection
            if (digitalRead(WIO_5S_PRESS) == LOW || digitalRead(WIO_5S_RIGHT) == LOW)
            {
                while (digitalRead(WIO_5S_PRESS) == LOW || digitalRead(WIO_5S_RIGHT) == LOW) delay(10);
                break;
            }
            if (digitalRead(WIO_KEY_C) == LOW)
            {
                while (digitalRead(WIO_KEY_C) == LOW) delay(10);
                delay(50);
                return;
            }
            delay(20);
        }

        // Load images from the selected folder.
        strncpy(currentFolder, folderList[folderPickerIdx], sizeof(currentFolder) - 1);
        currentFolder[sizeof(currentFolder) - 1] = '\0';

        tft.fillScreen(TFT_BLACK);
        drawViewerHeader();
        tft.setTextSize(1);
        tft.setTextColor(tft.color565(0, 160, 185), TFT_BLACK);
        char scanMsg[48];
        if (currentFolder[0] == '\0')
            strncpy(scanMsg, "Scanning root...", sizeof(scanMsg));
        else
            snprintf(scanMsg, sizeof(scanMsg), "Scanning %s...", currentFolder);
        tft.drawString(scanMsg, 10, 50);

        scanBmps(currentFolder);

        if (bmpCount == 0)
        {
            drawNoBmps();
            while (true)
            {
                if (digitalRead(WIO_KEY_A) == LOW)
                {
                    while (digitalRead(WIO_KEY_A) == LOW) delay(10);
                    break;  // back to folder picker
                }
                if (digitalRead(WIO_KEY_C) == LOW)
                {
                    while (digitalRead(WIO_KEY_C) == LOW) delay(10);
                    delay(50);
                    return;
                }
                delay(20);
            }
            continue;
        }

        bmpIndex = 0;
        showCurrentBmp();

        // ---- inner loop: image viewer ---------------------------------------
        while (true)
        {
            if (digitalRead(WIO_5S_LEFT) == LOW)
            {
                while (digitalRead(WIO_5S_LEFT) == LOW) delay(10);
                if (bmpIndex > 0) { bmpIndex--; showCurrentBmp(); }
            }
            if (digitalRead(WIO_5S_RIGHT) == LOW)
            {
                while (digitalRead(WIO_5S_RIGHT) == LOW) delay(10);
                if (bmpIndex < bmpCount - 1) { bmpIndex++; showCurrentBmp(); }
            }
            if (digitalRead(WIO_KEY_A) == LOW)
            {
                while (digitalRead(WIO_KEY_A) == LOW) delay(10);
                break;  // back to folder picker
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
                return;
            }
            delay(20);
        }
        // fall through to outer loop → redraw folder picker
    }
}
