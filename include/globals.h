#pragma once
#include <TFT_eSPI.h>
#include "lcd_backlight.hpp"

// ---- Global objects (defined in main.cpp) ----
extern TFT_eSPI tft;
extern TFT_eSprite spr;
extern LCDBackLight backLight;
extern int maxBrightness;
extern char optionTest;
extern bool bleInitDone;

// ---- Menu state (defined in main.cpp) ----
extern int menuIndex;
constexpr int MENU_COUNT = 12;
extern const char* menuItems[];
extern bool menuNeedsRedraw;

// ---- menu.cpp ----
void drawMenu();
void navigation();

// ---- sensors.cpp ----
void sensorsScreen();

// ---- settings.cpp ----
extern int g_volume;   // 0=mute, 1–4; used by pomodoro, countdown, and sonar buzzer
void drawSettingsHeader(const char* tag);
void updateSettingsTag(const char* tag);
void loadSettings();
void settingsScreen();

// ---- deviceInfo.cpp ----
void deviceInfoScreen();

// ---- battery.cpp ----
bool batteryBegin();
bool batteryPresent();
bool batteryCharging();
bool refreshBatteryCache();   // time-gated I2C read (30-s); returns true if changed
int batteryLevel();
int batteryVoltage();
int batteryCurrent();
int batteryRemaining();
int batteryFullCapacity();
int batteryPower();
int batteryHealth();
void drawBatteryStatus(uint16_t bgColor);

// ---- claudeUsage.cpp ----
bool parseUsageJson(const char* json);
void checkSerial();
void claudeUsageScreen();

// ---- sysStats.cpp ----
bool parseSysStatsJson(const char* json);
void sysStatsScreen();

// ---- processWatch.cpp ----
bool parseProcessJson(const char* json);
void processWatchScreen();

// ---- stopwatch.cpp ----
void stopwatchScreen();

// ---- countdownTimer.cpp ----
void countdownTimerScreen();

// ---- bleScanner.cpp ----
void bleScannerScreen();

// ---- matrixRain.cpp ----
void matrixRainScreen();

// ---- pomodoro.cpp ----
void pomodoroScreen();

// ---- sdCardViewer.cpp ----
void sdCardViewerScreen();

// ---- wifiAnalyser.cpp ----
void wifiAnalyserScreen();

// ---- ultrasonicSensor.cpp ----
void ultrasonicScreen();

// ---- screenshot.cpp ----
void takeScreenshot();

// ---- bluetooth.cpp ----
void bleInit();
void checkBLE();
bool isBLEConnected();
void bleSetActive(bool active);
void bleReinit();
void bleHardReset();  // full deinit+reinit for BLE scan screen entry
bool bleReinitPending();  // one-shot pop: true+cleared if WiFi scan dirtied BLE
const char* getBLEAddress();
