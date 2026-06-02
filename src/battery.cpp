#include <Arduino.h>
#include <Wire.h>
#include "globals.h"

// =========================================================================
// BATTERY — BQ27441-G1A fuel gauge (Wio Terminal Battery Chassis 650mAh)
//
// Texas Instruments BQ27441-G1A at I2C address 0x55.
// Uses the chip's Standard Commands interface — no external library needed.
//
// CALIBRATION: The BQ27441 ships with factory-default Design Capacity
// (~1000 mAh). batteryBegin() writes the correct 650 mAh Design Capacity
// into data flash on startup so that FCC and SOH readings are accurate.
// The write is skipped if the chip is already configured correctly.
// =========================================================================

#define BQ27441_ADDR        0x55

#define BQ27441_CMD_FLAGS    0x06  // Bit 0: DSG, Bit 4: CFGUPMODE, Bit 9: FC
#define BQ27441_CMD_SOC      0x1C  // State of charge (0–100 %)
#define BQ27441_CMD_VOLTAGE  0x04  // Pack voltage (mV)
#define BQ27441_CMD_CURRENT  0x10  // Average current (mA, signed)
#define BQ27441_CMD_REMAIN   0x0C  // Remaining capacity (mAh)
#define BQ27441_CMD_FULL     0x0E  // Full charge capacity (mAh)
#define BQ27441_CMD_POWER    0x18  // Average power (mW, signed)
#define BQ27441_CMD_SOH      0x2E  // State of health (low byte = %, high byte = status flags)

#define BQ27441_DESIGN_CAPACITY_MAH  650
#define BQ27441_STATE_SUBCLASS       82   // "State" data flash subclass
#define BQ27441_STATE_CAP_OFFSET     10   // Design Capacity offset within subclass
#define BQ27441_STATE_ENERGY_OFFSET  12   // Design Energy offset within subclass

static bool g_batteryFound = false;

// Read a 16-bit standard command from the BQ27441 (little-endian).
// Returns 0xFFFF on failure.
static uint16_t bq27441Read(uint8_t cmd)
{
    Wire.beginTransmission(BQ27441_ADDR);
    Wire.write(cmd);
    if (Wire.endTransmission(false) != 0) return 0xFFFF;
    uint8_t n = Wire.requestFrom((uint8_t)BQ27441_ADDR, (uint8_t)2, (uint8_t)true);
    if (n < 2) return 0xFFFF;
    uint16_t lo = Wire.read();
    uint16_t hi = Wire.read();
    return (hi << 8) | lo;
}

// Write a 16-bit sub-command to the Control register (0x00).
static void bq27441Control(uint16_t subcmd)
{
    Wire.beginTransmission(BQ27441_ADDR);
    Wire.write(0x00);
    Wire.write((uint8_t)(subcmd & 0xFF));
    Wire.write((uint8_t)(subcmd >> 8));
    Wire.endTransmission();
}

// Returns the ControlStatus word (SS flag is bit 13).
static uint16_t bq27441ControlStatus()
{
    bq27441Control(0x0000);  // request CONTROL_STATUS
    delay(1);
    return bq27441Read(0x00);
}

// Unseal the device with the default factory key (0x8000, 0x8000).
static void bq27441Unseal()
{
    bq27441Control(0x8000);
    bq27441Control(0x8000);
    delay(5);
}

// Seal the device.
static void bq27441Seal()
{
    bq27441Control(0x0020);  // SEALED sub-command
    delay(5);
}

// Write a single byte to the given BQ27441 register.
static void bq27441WriteByte(uint8_t reg, uint8_t val)
{
    Wire.beginTransmission(BQ27441_ADDR);
    Wire.write(reg);
    Wire.write(val);
    Wire.endTransmission();
}

// Configure the BQ27441 with the correct Design Capacity and Design Energy.
// Must be called after the chip has been confirmed present (batteryBegin helper).
//
// Pre-check: the DESIGN CAP standard command (0x3C) is readable in sealed mode
// without entering config mode.  If the stored value already matches, the entire
// config cycle is skipped — zero overhead on every boot after the first.
// On first boot (factory chip with wrong default), the full unseal → CFGUPDATE
// → flash write → SOFT_RESET sequence runs once, then never again.
static bool bq27441ConfigureCapacity()
{
    const uint16_t cap    = BQ27441_DESIGN_CAPACITY_MAH;
    const uint16_t energy = (uint16_t)((float)cap * 3.7f);  // 2405 mWh for 650 mAh LiPo

    // Fast check — readable without unsealing or entering config mode.
    // Returns immediately on every subsequent boot once the chip is programmed.
    uint16_t storedCap = bq27441Read(0x3C);  // DESIGN CAP standard command
    if (storedCap == cap) return false;       // already configured, nothing to do

    // ---- First-boot calibration path ----

    bool wasSealed = (bq27441ControlStatus() & 0x2000) != 0;
    if (wasSealed) bq27441Unseal();

    // Enter config update mode
    bq27441Control(0x0013);  // SET_CFGUPDATE

    // Wait for CFGUPMODE flag (Flags bit 4), up to 2 s
    uint32_t t = millis();
    bool inCfg = false;
    while (millis() - t < 2000)
    {
        uint16_t flags = bq27441Read(BQ27441_CMD_FLAGS);
        if (flags != 0xFFFF && (flags & 0x0010)) { inCfg = true; break; }
        delay(10);
    }
    if (!inCfg) { if (wasSealed) bq27441Seal(); return false; }

    // Enable block data access and point to the "State" subclass (82), block 0
    bq27441WriteByte(0x61, 0x00);  // BlockDataControl
    delay(1);
    bq27441WriteByte(0x3E, BQ27441_STATE_SUBCLASS);
    delay(1);
    bq27441WriteByte(0x3F, 0x00);  // block 0 (offsets 0–31)
    delay(1);

    // Read the full 32-byte block (needed to recompute the checksum)
    uint8_t block[32];
    Wire.beginTransmission(BQ27441_ADDR);
    Wire.write(0x40);  // BlockData start
    Wire.endTransmission(false);
    uint8_t n = Wire.requestFrom((uint8_t)BQ27441_ADDR, (uint8_t)32, (uint8_t)true);
    for (int i = 0; i < 32; i++) block[i] = (i < n) ? Wire.read() : 0;

    // Patch Design Capacity (offset 10) and Design Energy (offset 12) in the block
    block[BQ27441_STATE_CAP_OFFSET]         = (cap    >> 8) & 0xFF;
    block[BQ27441_STATE_CAP_OFFSET    + 1]  =  cap          & 0xFF;
    block[BQ27441_STATE_ENERGY_OFFSET]      = (energy >> 8) & 0xFF;
    block[BQ27441_STATE_ENERGY_OFFSET + 1]  =  energy       & 0xFF;

    // Write the 4 modified bytes in one I2C transaction
    Wire.beginTransmission(BQ27441_ADDR);
    Wire.write(0x40 + BQ27441_STATE_CAP_OFFSET);
    Wire.write(block[BQ27441_STATE_CAP_OFFSET]);
    Wire.write(block[BQ27441_STATE_CAP_OFFSET    + 1]);
    Wire.write(block[BQ27441_STATE_ENERGY_OFFSET]);
    Wire.write(block[BQ27441_STATE_ENERGY_OFFSET + 1]);
    Wire.endTransmission();
    delay(1);

    // Recompute block checksum from the patched block: 255 - (sum mod 256)
    uint8_t csum = 0;
    for (int i = 0; i < 32; i++) csum += block[i];
    csum = 255 - (csum % 256);
    bq27441WriteByte(0x60, csum);  // BlockDataChecksum
    delay(150);  // wait for internal flash write to complete

    // Exit via SOFT_RESET so the Impedance Track algorithm resimulates
    // SoC from the new Design Capacity (same as SparkFun's exitConfig(resim=true))
    bq27441Control(0x0042);  // SOFT_RESET
    t = millis();
    while (millis() - t < 2000)
    {
        uint16_t flags = bq27441Read(BQ27441_CMD_FLAGS);
        if (flags != 0xFFFF && !(flags & 0x0010)) break;
        delay(10);
    }

    if (wasSealed) bq27441Seal();
    return true;  // wrote new values — chip will re-learn SoC over next charge cycle
}

// Try to reach the BQ27441 over I2C. Call once from setup().
// Returns true if the chip is present and readable.
bool batteryBegin()
{
    Wire.begin();
    Wire.beginTransmission(BQ27441_ADDR);
    if (Wire.endTransmission() != 0) { g_batteryFound = false; return false; }
    // A bare address probe can give a false ACK on first use of the SAMD51 Wire bus.
    // Confirm with an actual register read before declaring the chip present.
    g_batteryFound = (bq27441Read(BQ27441_CMD_SOC) != 0xFFFF);
    if (g_batteryFound)
        bq27441ConfigureCapacity();  // write 650 mAh to data flash if not already set
    return g_batteryFound;
}

// Returns true when the battery is charging (DSG flag clear = not discharging).
bool batteryCharging()
{
    if (!g_batteryFound) return false;
    uint16_t flags = bq27441Read(BQ27441_CMD_FLAGS);
    if (flags == 0xFFFF) return false;
    return !(flags & 0x01);  // bit 0 DSG: 0 = charging, 1 = discharging
}

// Returns battery state of charge (0–100), or -1 if unavailable.
int batteryLevel()
{
    if (!g_batteryFound) return -1;
    uint16_t soc = bq27441Read(BQ27441_CMD_SOC);
    if (soc == 0xFFFF) return -1;
    return (int)soc;
}

// ---- Battery overlay cache --------------------------------------------------
// I2C reads are time-gated to at most once per 30 s.  drawBatteryStatus()
// always repaints — it only skips the I2C read, not the draw.
// This means header clears never leave a blank battery area.

static int      g_batLevel  = -1;    // cached SoC 0–100, -1 = not read yet
static bool     g_batChg    = false; // cached charging flag
static uint32_t g_batLastMs = 0;     // millis() of last I2C read

// Refresh the cache from the BQ27441 (30-s gate).
// Returns true if the values changed — caller can queue a display update.
bool refreshBatteryCache()
{
    if (!g_batteryFound) return false;
    uint32_t now = millis();
    if (g_batLastMs != 0 && now - g_batLastMs < 30000) return false;  // 30-s gate
    g_batLastMs = now;
    int  lv  = batteryLevel();
    if (lv < 0) return false;
    bool chg = batteryCharging();
    bool changed = (lv != g_batLevel || chg != g_batChg);
    g_batLevel = lv;
    g_batChg   = chg;
    return changed;
}

// Cyberpunk battery indicator — top-right corner of any screen header.
//
// Header zone layout (320×30 px):
//
//   ├──accent──┤──── title (textSize 2) ────┤── tag (textSize 1) ──┤── battery ──┤
//    x=0..3      x=10..~150                   x=160..240              x=244..315
//
// Battery widget (y = 3..10):
//   [%text]  [■■■■□] ╕
//   244..278  281..312  313..315 (nub)
//
// Segment colours: charging=cyan  >75%=green  >50%=lime  ≤50%=amber  ≤25%=red
//
// Always repaints (I2C reads are cached); call freely after any header clear.
void drawBatteryStatus(uint16_t bgColor)
{
    if (!g_batteryFound) return;

    // Bootstrap: populate cache on very first call (before 30-s timer fires)
    if (g_batLevel < 0)
    {
        g_batLevel  = batteryLevel();
        g_batChg    = batteryCharging();
        g_batLastMs = millis();
        if (g_batLevel < 0) return;
    }

    // Time-gated I2C refresh (no-op within the 30-s window)
    refreshBatteryCache();

    int      level  = g_batLevel;
    bool     chg    = g_batChg;

    // ---- Palette ----
    uint16_t litCol =
        chg           ? tft.color565(  0, 220, 245)  // charging — cyan
      : level <= 25   ? tft.color565(220,  30,  30)  // critical — red
      : level <= 50   ? tft.color565(255, 130,   0)  // low      — amber
      : level <= 75   ? tft.color565(160, 220,   0)  // mid      — lime
      :                 tft.color565(  0, 220,  60); // good     — neon green

    uint16_t bdCol = chg
        ? tft.color565(0, 175, 210)
        : tft.color565(0,  55,  75);

    // ---- Battery body: outer 32×8 px at x = 281, y = 3 ----
    tft.drawRect(281, 3, 32, 8, bdCol);                       // border
    tft.fillRect(282, 4, 30, 6, tft.color565(0, 12, 20));     // dark interior

    // 4 segments × 6 px wide, 1 px gap, positions: 283, 290, 297, 304
    int litSegs = (level * 4 + 99) / 100;   // ceiling(level/25), capped 0–4
    if (litSegs > 4) litSegs = 4;
    for (int i = 0; i < litSegs; i++)
        tft.fillRect(283 + i * 7, 4, 6, 6, litCol);

    // ---- Battery nub: 3×4 px at x = 313, y = 5 ----
    tft.fillRect(313, 5, 3, 4, bdCol);

    // ---- Percentage text, right-aligned to x = 278, y = 4 ----
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", level);
    tft.setTextSize(1);
    int tw = tft.textWidth(buf);
    tft.fillRect(244, 3, 34, 8, bgColor);   // clear text+gap area with header bg
    tft.setTextColor(litCol);               // fg only — bg pre-cleared above
    tft.drawString(buf, 278 - tw, 4);
}

// Returns battery pack voltage in millivolts, or -1 if unavailable.
int batteryVoltage()
{
    if (!g_batteryFound) return -1;
    uint16_t v = bq27441Read(BQ27441_CMD_VOLTAGE);
    return (v == 0xFFFF) ? -1 : (int)v;
}

// Returns average current in mA (negative = discharging), or -32768 if unavailable.
int batteryCurrent()
{
    if (!g_batteryFound) return -32768;
    uint16_t raw = bq27441Read(BQ27441_CMD_CURRENT);
    if (raw == 0xFFFF) return -32768;
    return (int)(int16_t)raw;  // value is signed
}

// Returns remaining capacity in mAh, or -1 if unavailable.
int batteryRemaining()
{
    if (!g_batteryFound) return -1;
    uint16_t v = bq27441Read(BQ27441_CMD_REMAIN);
    return (v == 0xFFFF) ? -1 : (int)v;
}

// Returns full charge capacity in mAh, or -1 if unavailable.
int batteryFullCapacity()
{
    if (!g_batteryFound) return -1;
    uint16_t v = bq27441Read(BQ27441_CMD_FULL);
    return (v == 0xFFFF) ? -1 : (int)v;
}

// Returns average power in mW (negative = discharging), or -32768 if unavailable.
int batteryPower()
{
    if (!g_batteryFound) return -32768;
    uint16_t raw = bq27441Read(BQ27441_CMD_POWER);
    if (raw == 0xFFFF) return -32768;
    return (int)(int16_t)raw;
}

// Returns state of health (0–100 %), or -1 if unavailable.
int batteryHealth()
{
    if (!g_batteryFound) return -1;
    uint16_t v = bq27441Read(BQ27441_CMD_SOH);
    if (v == 0xFFFF) return -1;
    return (int)(v & 0xFF);  // low byte is the SoH percentage
}

// Returns true when the battery chassis is detected (chip responded at init).
bool batteryPresent()
{
    return g_batteryFound;
}
