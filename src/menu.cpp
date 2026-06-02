#include <Arduino.h>
#include "globals.h"

//========================================================================= MENU
// 2-column grid, up to PAGE_SIZE items per page.
// Only the two changed cells are repainted on navigation — no fillScreen flash.
// Full drawMenu() is triggered on first draw and on return from any sub-screen.

static const int PAGE_SIZE = 10;

static void drawMenuCell(int i)
{
  int page      = menuIndex / PAGE_SIZE;
  int pageStart = page * PAGE_SIZE;
  int pageEnd   = (pageStart + PAGE_SIZE < MENU_COUNT) ? pageStart + PAGE_SIZE : MENU_COUNT;
  if (i < pageStart || i >= pageEnd) return;

  int pageIndex = i - pageStart;
  int row = pageIndex / 2;
  int col = pageIndex % 2;
  int bx  = (col == 0) ? 10 : 162;
  int by  = 32 + row * 37;
  int bw  = 148;
  int bh  = 35;

  if (i == menuIndex)
  {
    uint16_t selBg  = tft.color565(0, 35, 50);
    uint16_t selCol = tft.color565(0, 220, 245);
    tft.fillRect(bx, by, bw, bh, selBg);
    tft.drawRect(bx, by, bw, bh, selCol);
    int t = 5;
    tft.drawFastHLine(bx,      by,      t, selCol); tft.drawFastVLine(bx,      by,      t, selCol);
    tft.drawFastHLine(bx+bw-t, by,      t, selCol); tft.drawFastVLine(bx+bw-1, by,      t, selCol);
    tft.drawFastHLine(bx,      by+bh-1, t, selCol); tft.drawFastVLine(bx,      by+bh-t, t, selCol);
    tft.drawFastHLine(bx+bw-t, by+bh-1, t, selCol); tft.drawFastVLine(bx+bw-1, by+bh-t, t, selCol);
    tft.setTextSize(2);
    tft.setTextColor(selCol, selBg);
    tft.drawString(">",          bx + 5,  by + 9);
    tft.drawString(menuItems[i], bx + 18, by + 9);
  }
  else
  {
    tft.fillRect(bx, by, bw, bh, tft.color565(0, 8, 15));
    tft.drawRect(bx, by, bw, bh, tft.color565(15, 30, 42));
    tft.setTextSize(2);
    tft.setTextColor(tft.color565(80, 110, 120), tft.color565(0, 8, 15));
    tft.drawString(menuItems[i], bx + 10, by + 9);
  }
}

void drawMenu()
{
  int totalPages = (MENU_COUNT + PAGE_SIZE - 1) / PAGE_SIZE;
  int curPage    = menuIndex / PAGE_SIZE;

  // ---- Header: overdraw in-place (same content — no visible change) ----
  tft.fillRect(0, 0, 320, 30, tft.color565(0, 8, 20));
  tft.fillRect(0, 0, 3, 30, tft.color565(0, 220, 245));
  tft.setTextSize(2);
  tft.setTextColor(tft.color565(0, 220, 245), tft.color565(0, 8, 20));
  tft.drawString("// MENU", 10, 7);
  tft.drawFastHLine(0, 29, 320, tft.color565(0, 80, 100));
  for (int xi = 8; xi < 320; xi += 14)
    tft.drawFastVLine(xi, 27, 4, tft.color565(0, 140, 165));
  tft.setTextSize(1);
  tft.setTextColor(tft.color565(0, 148, 170), tft.color565(0, 8, 20));
  if (totalPages > 1) {
    char pgBuf[16];
    snprintf(pgBuf, sizeof(pgBuf), "PG %d/%d", curPage + 1, totalPages);
    tft.drawString(pgBuf, 160, 11);
  } else {
    tft.drawString("NAVIGATE", 160, 11);
  }

  // ---- Footer: overdraw in-place before clearing content area ----
  tft.fillRect(0, 219, 320, 21, TFT_BLACK);
  tft.fillRect(0, 219, 3, 21, tft.color565(0, 220, 245));
  tft.setTextSize(1);
  tft.setTextColor(tft.color565(0, 100, 118), TFT_BLACK);
  if (totalPages > 1)
    tft.drawString("[UP/DN/L/R] NAV   [PRESS] SEL   [DN] NEXT PG", 8, 225);
  else
    tft.drawString("[UP/DN] ROW   [L/R] COL   [PRESS] SELECT", 8, 225);

  drawBatteryStatus(tft.color565(0, 8, 20));

  // ---- Content area: fill only the cell region, then draw cells ----
  tft.fillRect(0, 30, 320, 189, TFT_BLACK);
  int pageStart = curPage * PAGE_SIZE;
  int pageEnd   = (pageStart + PAGE_SIZE < MENU_COUNT) ? pageStart + PAGE_SIZE : MENU_COUNT;
  for (int i = pageStart; i < pageEnd; i++) drawMenuCell(i);
}

void navigation()
{
  if (menuNeedsRedraw) { menuNeedsRedraw = false; drawMenu(); return; }

  int prev = menuIndex;

  if (digitalRead(WIO_5S_UP) == LOW)
  {
    if (menuIndex >= 2) menuIndex -= 2;
    delay(200);
  }
  else if (digitalRead(WIO_5S_DOWN) == LOW)
  {
    if (menuIndex + 2 < MENU_COUNT) menuIndex += 2;
    delay(200);
  }
  else if (digitalRead(WIO_5S_LEFT) == LOW)
  {
    if (menuIndex % 2 == 1) menuIndex--;
    delay(200);
  }
  else if (digitalRead(WIO_5S_RIGHT) == LOW)
  {
    if (menuIndex % 2 == 0 && menuIndex + 1 < MENU_COUNT) menuIndex++;
    delay(200);
  }
  else if (digitalRead(WIO_5S_PRESS) == LOW)
  {
    switch (menuIndex)
    {
      case 0:  pomodoroScreen();          break;
      case 1:  stopwatchScreen();         break;
      case 2:  countdownTimerScreen();    break;
      case 3:  sysStatsScreen();          break;
      case 4:  processWatchScreen();      break;
      case 5:  claudeUsageScreen();       break;
      case 6:  ultrasonicScreen();        break;
      case 7:  tempHumidityScreen();      break;
      case 8:  wifiAnalyserScreen();      break;
      case 9:  bleScannerScreen();        break;
      case 10: matrixRainScreen();        break;
      case 11: robotEyesScreen();         break;
      case 12: sdCardViewerScreen();      break;
      case 13: settingsScreen();          break;
    }
    delay(200);
    menuNeedsRedraw = true;
    return;
  }

  if (menuIndex != prev)
  {
    if (menuIndex / PAGE_SIZE != prev / PAGE_SIZE)
      drawMenu();
    else
    { drawMenuCell(prev); drawMenuCell(menuIndex); }
  }
}
