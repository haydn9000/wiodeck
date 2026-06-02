#include <Arduino.h>
#include <malloc.h>
#include "globals.h"

//========================================================================= DEVICE INFO
// Shows hardware specs, live memory usage, unique serial number, and firmware build info.

// ATSAMD51 unique 128-bit serial number — four 32-bit words in the flash information block.
static uint32_t snWord(int i)
{
    static const uint32_t addrs[4] = {
        0x008061FC, 0x00806010, 0x00806014, 0x00806018
    };
    return *(volatile uint32_t*)addrs[i];
}

// Linker symbols placed by the GNU ARM linker script.
extern char __etext;           // end of .text section in flash
extern char __data_start__;    // start of initialised-data in RAM
extern char __data_end__;      // end  of initialised-data in RAM
extern char __bss_end__;       // end of .bss = base of heap

void deviceInfoScreen()
{
    // --- Gather live stats once ---
    struct mallinfo mi  = mallinfo();
    int freeHeapKB      = mi.fordblks / 1024;
    int usedHeapKB      = mi.uordblks / 1024;

    // Flash bytes used: code (.text) + initial values for globals (.data copy in flash)
    uint32_t flashUsed  = (uint32_t)&__etext
                        + ((uint32_t)&__data_end__ - (uint32_t)&__data_start__);
    int flashUsedKB     = (int)(flashUsed / 1024);

    // Static RAM: everything from the start of RAM to the end of .bss (data + bss)
    uint32_t staticRam  = (uint32_t)&__bss_end__ - 0x20000000UL;
    int staticRamKB     = (int)(staticRam / 1024);

    uint32_t upSec      = millis() / 1000;
    uint32_t upH        = upSec / 3600;
    uint32_t upM        = (upSec % 3600) / 60;
    uint32_t upS        = upSec % 60;

    // --- Draw ---
    tft.fillRect(0, 30, 320, 190, TFT_BLACK);
    updateSettingsTag("INFO");

    const uint16_t secCol = tft.color565(0, 175, 200);
    const uint16_t lblCol = tft.color565(60, 95, 108);
    const uint16_t valCol = tft.color565(0, 220, 245);
    const uint16_t dimCol = tft.color565(45, 72, 82);
    const int LX = 20, VX = 110;

    tft.setTextSize(1);
    char buf[40];
    int y = 34;

    // --- HARDWARE ---
    tft.setTextColor(secCol, TFT_BLACK);
    tft.drawString("HARDWARE", LX, y); y += 11;

    tft.setTextColor(lblCol, TFT_BLACK); tft.drawString("MCU",  LX, y);
    tft.setTextColor(valCol, TFT_BLACK); tft.drawString("ATSAMD51P19",                 VX, y); y += 10;

    tft.setTextColor(lblCol, TFT_BLACK); tft.drawString("ARCH", LX, y);
    snprintf(buf, sizeof(buf), "Cortex-M4F @ %lu MHz", F_CPU / 1000000UL);
    tft.setTextColor(valCol, TFT_BLACK); tft.drawString(buf,                           VX, y); y += 13;

    // --- MEMORY ---
    tft.setTextColor(secCol, TFT_BLACK);
    tft.drawString("MEMORY", LX, y); y += 11;

    tft.setTextColor(lblCol, TFT_BLACK); tft.drawString("FLASH", LX, y);
    snprintf(buf, sizeof(buf), "%d / 512 KB used", flashUsedKB);
    tft.setTextColor(valCol, TFT_BLACK); tft.drawString(buf,                           VX, y); y += 10;

    tft.setTextColor(lblCol, TFT_BLACK); tft.drawString("SRAM",  LX, y);
    snprintf(buf, sizeof(buf), "%d / 192 KB static", staticRamKB);
    tft.setTextColor(valCol, TFT_BLACK); tft.drawString(buf,                           VX, y); y += 10;

    tft.setTextColor(lblCol, TFT_BLACK); tft.drawString("HEAP",  LX, y);
    snprintf(buf, sizeof(buf), "%d KB free  %d KB used", freeHeapKB, usedHeapKB);
    tft.setTextColor(valCol, TFT_BLACK); tft.drawString(buf,                           VX, y); y += 13;

    // --- SERIAL ---
    tft.setTextColor(secCol, TFT_BLACK);
    tft.drawString("SERIAL", LX, y); y += 11;

    snprintf(buf, sizeof(buf), "%08lX  %08lX", snWord(0), snWord(1));
    tft.setTextColor(dimCol, TFT_BLACK); tft.drawString(buf,                           LX, y); y += 10;
    snprintf(buf, sizeof(buf), "%08lX  %08lX", snWord(2), snWord(3));
    tft.setTextColor(dimCol, TFT_BLACK); tft.drawString(buf,                           LX, y); y += 13;

    // --- FIRMWARE ---
    tft.setTextColor(secCol, TFT_BLACK);
    tft.drawString("FIRMWARE", LX, y); y += 11;

    tft.setTextColor(lblCol, TFT_BLACK); tft.drawString("BUILT",  LX, y);
    snprintf(buf, sizeof(buf), "%s  %s", __DATE__, __TIME__);
    tft.setTextColor(valCol, TFT_BLACK); tft.drawString(buf,                           VX, y); y += 10;

    tft.setTextColor(lblCol, TFT_BLACK); tft.drawString("UP",     LX, y);
    snprintf(buf, sizeof(buf), "%luh %02lum %02lus", upH, upM, upS);
    tft.setTextColor(valCol, TFT_BLACK); tft.drawString(buf,                           VX, y);

    // --- Footer ---
    tft.fillRect(0, 219, 3, 21, tft.color565(0, 200, 230));
    tft.setTextColor(tft.color565(0, 100, 118), TFT_BLACK);
    tft.drawString("[C] BACK", 8, 225);
    drawBatteryStatus(TFT_BLACK);

    while (digitalRead(WIO_5S_PRESS) == LOW) delay(10);
    delay(50);

    while (true)
    {
        if (digitalRead(WIO_KEY_B) == LOW) {
            while (digitalRead(WIO_KEY_B) == LOW) delay(10);
            takeScreenshot();
        }
        if (digitalRead(WIO_5S_PRESS) == LOW || digitalRead(WIO_KEY_C) == LOW) {
            while (digitalRead(WIO_5S_PRESS) == LOW || digitalRead(WIO_KEY_C) == LOW) delay(10);
            delay(50);
            return;
        }
        delay(20);
    }
}
