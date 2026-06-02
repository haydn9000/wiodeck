# CLAUDE.md — Wio Terminal Workbench

## Project overview

A personal toolkit for the Seeed Wio Terminal. Each screen is a `.cpp` file with a single blocking function; the joystick-navigated menu wires them together. Currently ships with fourteen main-menu screens grouped by purpose:

| # | Label | Screen |
|---|-------|--------|
| 0 | POMODORO | Pomodoro timer |
| 1 | STOPWATCH | Stopwatch with lap splits |
| 2 | COUNTDOWN | Countdown timer |
| 3 | SYS STATS | PC system stats (CPU/RAM/GPU/net) |
| 4 | PROCS | Top-5 CPU processes |
| 5 | CLAUDE | Claude API usage |
| 6 | SONAR | HC-SR04 ultrasonic distance sensor |
| 7 | TEMP HUM | Grove DHT11 temperature & humidity |
| 8 | AP SCAN | Wi-Fi analyser |
| 9 | BLE SCAN | BLE device scanner |
| 10 | MATRIX | Matrix digital rain |
| 11 | ROBO EYE | Sound-reactive robot eyes |
| 12 | SD VIEW | SD card BMP viewer |
| 13 | SETTINGS | Settings (backlight / volume / temp unit / sensors / battery / device info) |

The Settings screen hosts six sub-screens across two pages — **Page 0:** Backlight, Volume, Temp Unit (°C/°F); **Page 1:** Sensors (live accelerometer/light/mic dashboard), Battery (BQ27441 SoC/voltage/current/health), Device Info (MCU specs, memory usage, serial number, firmware build). All settings persist to flash. New screens slot in without touching anything outside `main.cpp`, `menu.cpp`, and `globals.h`.

Built with PlatformIO (`atmelsam` platform, `arduino` framework, `seeed_wio_terminal` board).

## Hardware reference

**MCU:** Microchip ATSAMD51P19 — ARM Cortex-M4F @ 120MHz (overclockable to 200MHz), 512KB flash, 192KB RAM, 4MB external flash.

**Display:** 2.4" ILI9341 LCD, 320×240px. Driven via `Seeed_Arduino_LCD` (a pre-configured TFT_eSPI fork). Always use rotation 3 for landscape.

**Wireless co-processor:** RTL8720DN — Wi-Fi 802.11 a/b/g/n (2.4GHz + 5GHz) and BLE 5.0. Runs independently from the SAMD51; communicates over RPC. BLE stack requires `Seeed_Arduino_rpcUnified` + `Seeed_Arduino_rpcBLE`.

**Onboard peripherals available for future screens:**

| Peripheral | Notes |
|---|---|
| LIS3DHTR accelerometer | I2C — motion, tilt, tap detection |
| Microphone | Analog — 1.0–10V, -42dB |
| Speaker | PWM — ≥78dB @10cm |
| Light sensor | Analog — 400–1050nm |
| IR emitter | 940nm |
| microSD slot | SPI — up to 16GB; needs `Seeed_Arduino_FS` |

**Interfaces:** Grove ×2, 40-pin RPi-compatible header, 20-pin FPC, USB-C (power + OTG host/client).

## Bootloader & recovery

- **Normal reset:** flip the power/reset switch once.
- **Bootloader mode:** double-tap the reset switch quickly — the blue LED will pulse slowly. Use this if the device stops being detected after a bad flash (e.g. USB mishandled in firmware).
- **SWD debug pads** (on PCB back): SWCLK, SWDIO, SWO, RST, GND, 3V3 for the SAMD51; separate pads for the RTL8720DN.

## Build

```
pio run                   # build only
pio run --target upload   # build + upload over USB (bossac)
```

## Project structure

```
src/
  main.cpp             — Global definitions, setup(), loop(), top-level screen dispatch
  menu.cpp             — Main menu render + joystick navigation; register new screens here
  sensors.cpp          — Sensor dashboard (accelerometer, light sensor, microphone)
  settings.cpp         — Settings menu (backlight / volume / temp unit / sensors / battery / device-info sub-screens); persists to flash
  battery.cpp          — BQ27441-G1A I²C driver + drawBatteryStatus() overlay
  bluetooth.cpp        — BLE GATT peripheral (WT-001); deferred init, on-demand advertising
  claudeUsage.cpp      — Claude Usage screen: JSON parser, serial reader, usage display
  sysStats.cpp         — Sys Stats screen: arc gauges for CPU/RAM/GPU/network via sysstat_sender.py
  pomodoro.cpp         — Pomodoro timer: 4×(25 min work / 5 min break) + 15 min long break; buzzer alerts
  stopwatch.cpp        — Stopwatch with lap splits
  countdownTimer.cpp   — Countdown timer: HH:MM:SS input, hold-to-repeat, buzzer on expiry
  processWatch.cpp     — Top-5 CPU processes by usage, fed via process_sender.py
  wifiAnalyser.cpp    — Wi-Fi analyser: list view (SSID/band/ch/dBm) + 2.4 GHz and 5 GHz channel maps
  bleScanner.cpp       — BLE device scanner: nearby devices + RSSI bars
  sdCardViewer.cpp     — SD card BMP viewer: folder picker, browse and display 16/24/32-bit BMP images
  screenshot.cpp       — KEY_B handler: saves 24-bit BGR BMP to microSD as SCREENSHOTS/SCRN####.BMP
  matrixRain.cpp       — Animated Matrix-style digital rain
  ultrasonicSensor.cpp — Sonar screen: HC-SR04 distance sensor, cyberpunk semicircular arc gauge
  robotEyes.cpp        — Sound-reactive robot eyes: 4 states (IDLE/CURIOUS/ALERT/SHOCK) driven by mic amplitude, sprite-buffered, arc mouth
  tempHumidity.cpp     — Grove DHT11 temperature & humidity screen: colour-coded readings, 2 s refresh, right Grove A0; honours g_tempUnit (°C/°F)
  deviceInfo.cpp       — Device info sub-screen: MCU, memory, serial number, firmware build
include/
  globals.h            — extern declarations and function prototypes for all .cpp files
  lcd_backlight.hpp    — SAMD51 TC0 PWM backlight driver (Seeed original, Boost licence)
  RawImage.h           — Seeed template for loading pre-converted raw bitmap format
tools/
  claude_sender.py     — Feeds Claude usage data over USB serial (default) or BLE (--ble flag)
  process_sender.py    — Feeds top CPU processes over USB serial or BLE (--ble flag)
  sysstat_sender.py    — Feeds PC system stats (CPU/RAM/GPU/net) over USB serial or BLE (--ble flag)
  bitmap-converter/    — PySide6 GUI for converting images to Wio Terminal bitmap format
```

## Adding a new screen

### 1. Create `src/myScreen.cpp`

```cpp
#include <Arduino.h>
#include "globals.h"

void myScreen()
{
    // Wait for the joystick press that launched this screen to be released.
    while (digitalRead(WIO_5S_PRESS) == LOW) { delay(10); }

    tft.fillScreen(TFT_BLACK);
    // ... draw your screen ...
    drawBatteryStatus(TFT_BLACK);  // optional corner overlay

    while (true)
    {
        // ... handle input ...
        if (digitalRead(WIO_KEY_C) == LOW)
        {
            while (digitalRead(WIO_KEY_C) == LOW) { delay(10); }
            delay(50);
            return;  // back to menu
        }
        delay(20);
    }
}
```

### 2. Declare it in `include/globals.h`

```cpp
// myScreen.cpp
void myScreen();
```

### 3. Register it in `src/menu.cpp`

Add a label to `menuItems[]` in `main.cpp` (increment `MENU_COUNT` in `globals.h` too), then add a `case` in `navigation()` in `menu.cpp`:

```cpp
// In main.cpp — menuItems array
const char* menuItems[] = { "POMODORO", "STOPWATCH", "COUNTDOWN", "SYS STATS", "PROCS",
                             "CLAUDE", "SONAR", "TEMP HUM", "AP SCAN", "BLE SCAN",
                             "MATRIX", "ROBO EYE", "SD VIEW", "SETTINGS", "My Screen" };

// In globals.h — update the count
constexpr int MENU_COUNT = 15;

// In menu.cpp — navigation() switch
case 14: myScreen(); break;
```

## Shared infrastructure

Every screen gets access to these via `globals.h`:

| Symbol | Type | Description |
|---|---|---|
| `tft` | `TFT_eSPI` | Display driver — 320×240, rotation 3 (landscape) |
| `spr` | `TFT_eSprite` | Sprite buffer for flicker-free sub-region drawing |
| `backLight` | `LCDBackLight` | SAMD51 TC0 PWM backlight — call `setBrightness(0–100)` |
| `drawBatteryStatus(bg)` | function | Draws charge % in top-right corner; no-op if no chassis |
| `bleSetActive(bool)` | function | Start / stop BLE advertising from any screen |
| `checkBLE()` | function | Process pending BLE writes; call in your screen's loop |
| `checkSerial()` | function | Process pending serial data; call in your screen's loop |

## Architecture notes

### globals.h as the translation-unit glue
Arduino merges all `.ino` files and auto-generates forward declarations. PlatformIO compiles each `.cpp` separately. `globals.h` takes that role: every `extern` global declaration and every cross-file function prototype lives there. Definitions live in `main.cpp`.

### Screen dispatch
`optionTest` (char in `main.cpp`) drives the top-level `switch` in `loop()`:
- `'A'` — brightness (KEY_A shortcut, bypasses menu)
- `'C'` — main menu (default)

Each sub-screen is a **blocking loop** that returns when the user exits. On return `menuNeedsRedraw = true` causes `drawMenu()` to redraw once.

**KEY_A only works at the menu level.** Once inside a sub-screen the blocking loop owns execution — `loop()` never runs, so the KEY_A check there is never reached. To support brightness adjustment inside a screen, add a `WIO_KEY_A` handler inside that screen's own loop and call `setBrightness()` directly.

### Redraw strategy
`drawMenu()` is only called when `menuNeedsRedraw` is set — never on idle — to avoid flicker from `fillScreen()`. Apply the same pattern in new screens: redraw only on state change, not every loop iteration.

### BLE lifecycle
`BLEDevice::init()` talks to the RTL8720DN and can stall if called too early. It is deferred until `millis() > 3000` in `loop()`, after the first frame has rendered in `setup()`. Any screen that wants BLE calls `bleSetActive(true)` on entry and `bleSetActive(false)` on exit; advertising stops automatically.

`bleSetActive(true)` calls `ble_start()` automatically if the GAP state machine has not been started yet (e.g. immediately after boot, or after a WiFi scan resets the flag).

**BLE Scanner entry** always calls `bleHardReset()`: `ble_deinit()` + 500 ms settle + full `bleInit()` (re-registers GATT profile on RTL8720DN). This guarantees a clean scan state regardless of prior WiFi activity or advertising. `doBleScan()` then calls `ble_start()` (via the `ble_start_flags` check) before issuing `le_scan_timer_start()`.

### WiFi + BLE interaction
The RTL8720DN runs WiFi and BLE as independent firmware modules. `wifi_off()` / `wifi_on()` (called by `WiFi.mode()`) reset the radio coexistence state but do not touch the BLE firmware module. After exiting the WiFi analyser, `bleReinit()` resets `ble_start_flags` so the next BLE operation re-issues `ble_start()`. BLE data screens (Claude, Sys Stats, Process Watch) re-start advertising correctly via the `ble_start()` guard in `bleSetActive(true)`.

### Sonar screen
`ultrasonicSensor.cpp` drives a **Seeed Grove Ultrasonic Distance Sensor** (SKU 101020010) via the **left Grove UART port** (TRIG → D1, ECHO → D0). This sensor is 3.3 V/5 V compatible and plugs directly into the Grove connector — use 5 V VCC for full ~400 cm range, 3.3 V caps it at ~150–200 cm. The generic HC-SR04 is **not recommended**: its ECHO line outputs 5 V and the SAMD51 is not 5 V tolerant. If used, fit a 1 kΩ/2 kΩ voltage divider on ECHO or use the HC-SR04+ (3.3 V variant).

The semicircular arc gauge fills clockwise as proximity increases (far = empty, close = full). Zone colours: neon cyan (safe, > 3× threshold), amber (caution, 1–3× threshold), magenta (danger, < threshold). The threshold tick is drawn as a ring-clipped radial line so it never leaves stray pixels outside the arc boundary on redraw. `pulseIn` returns -1 on timeout (nothing in range) — this is normal behaviour with no sensor connected.

### Temp + Humidity screen
`tempHumidity.cpp` reads a **[Grove DHT11](https://wiki.seeedstudio.com/Grove-TemperatureAndHumidity_Sensor/)** sensor on the **right Grove port** (data → A0). Displays temperature (°C) and humidity (%) with colour-coded value bands in two centred panels; readings refresh every 2 seconds. The right Grove port is used by default to keep D0/D1 free for Sonar — but since both screens are never active simultaneously, the DHT11 can alternatively be wired to the left Grove port (data → D0) by changing `#define DHT_PIN` in `tempHumidity.cpp`.

The sensor source tag (`DHT11 / A0`) is placed in the header at x=155 to stay clear of the battery indicator (which occupies x≥244). The error state uses a three-tier layout — size-3 `READ ERROR` heading, size-2 pin hint, size-1 port detail — so a missing sensor is immediately visible.

### SD card viewer
`sdCardViewer.cpp` reads BMP files from any folder on the microSD card. Navigation is three levels deep:
1. **Folder picker** — lists all non-system directories plus `/ (root)`; UP/DOWN to navigate, PRESS/RIGHT to open.
2. **File picker** — lists all `.BMP` files in the selected folder; UP/DOWN to navigate, PRESS/RIGHT to view, KEY_A to go back to folder picker.
3. **Image viewer** — shows the selected BMP full-screen; LEFT/RIGHT to move between images, KEY_A to go back to the file picker, KEY_C to return to the menu.

`bmpIndex` is preserved when returning from the image viewer to the file picker, so the list re-opens on the last-viewed file.

Supported formats: 16-bit BI_RGB (RGB555) and BI_BITFIELDS (RGB565, detected via colour masks), 24-bit BI_RGB, 32-bit BI_RGB/BI_BITFIELDS (Windows default export). Pixel conversion: `color565()` returns little-endian values; SAMD51 DMA (`pushImage()`) sends memory byte-order to the ILI9341 which expects big-endian — rows are byte-swapped with `(c >> 8) | (c << 8)` before the `pushImage()` call. File entries from `entry.name()` return the full FatFS path (`0:/SCRN0001.BMP`); the viewer strips to the bare filename via `strrchr(full, '/')` before building the open path (which must not include a leading `/`).

Windows system folders (`System Volume Information`, `RECYCLER`, `$RECYCLE.BIN`, dot-prefixed entries) are silently excluded from the folder list.

### Battery detection
The SAMD51 Wire bus can return a false ACK on the first probe. `batteryBegin()` follows the address probe with an actual register read to confirm the BQ27441 is present. Returns false silently on hardware without the compatible chassis — `drawBatteryStatus()` becomes a no-op.

## Library dependencies

| Library | Purpose |
|---|---|
| `Seeed_Arduino_LCD` | TFT_eSPI fork pre-configured for Wio Terminal |
| `Seeed_Arduino_FS` | Filesystem + SD card support |
| `Seeed_Arduino_rpcUnified` | RTL8720DN RPC transport (transitive dep of rpcBLE + rpcWiFi) |
| `Seeed_Arduino_rpcBLE` | BLE GATT stack for the RTL8720DN co-processor |
| `Seeed_Arduino_rpcWiFi` | WiFi driver for the RTL8720DN co-processor |

**TFT_eSPI conflict:** if you see "Multiple libraries found for TFT_eSPI.h", a community TFT_eSPI install is shadowing the Seeed fork. Remove any non-Seeed TFT_eSPI from your Arduino libraries folder — the Seeed fork is the one configured for this hardware.

## Button reference

| Input | Mode | Notes |
|---|---|---|
| KEY_A / B / C | `INPUT` | External pull-downs on Wio Terminal hardware |
| 5S UP / DOWN / LEFT / RIGHT / PRESS | `INPUT_PULLUP` | Active LOW |
