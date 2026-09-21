/*
 * lcd_icons.h — a small set of 5x8 custom characters for HD44780-compatible
 * character LCDs (16x2, 20x4, etc.), sized for energy-monitoring displays:
 * solar panel, battery, load/house, grid/plug, charge/discharge arrows,
 * warning, and WiFi.
 *
 * Unlike the classic "build one big icon from a 2x2 block of custom
 * characters" technique (see README credit), each icon here is a single
 * CGRAM slot — the HD44780 only has 8 of those total, so this trades a
 * bigger icon for being able to show several *different* icons on screen
 * at once, which matters more on a 16x2 display showing several readings.
 *
 * Usage:
 *   #include "lcd_icons.h"
 *   lcdIconsInstall(lcd);              // once, in setup()
 *   lcd.write(ICON_BATTERY);           // anywhere after that
 */

#pragma once

// CGRAM slot numbers — pass these to lcd.write()
enum LcdIcon : uint8_t {
  ICON_SOLAR = 0,
  ICON_BATTERY = 1,
  ICON_LOAD = 2,
  ICON_GRID = 3,
  ICON_ARROW_UP = 4,
  ICON_ARROW_DOWN = 5,
  ICON_WARNING = 6,
  ICON_WIFI = 7,
};

// Solar panel — grid of cells
static byte ICON_BITMAP_SOLAR[8] = {
  0b11111,
  0b10101,
  0b11111,
  0b10101,
  0b11111,
  0b10101,
  0b11111,
  0b00000,
};

// Battery — narrow terminal nub distinguishes it from a plain rounded box
static byte ICON_BITMAP_BATTERY[8] = {
  0b00100,
  0b01110,
  0b11111,
  0b10001,
  0b10001,
  0b10001,
  0b10001,
  0b11111,
};

// Load — house
static byte ICON_BITMAP_LOAD[8] = {
  0b00100,
  0b01110,
  0b11111,
  0b10101,
  0b10101,
  0b10101,
  0b11111,
  0b00000,
};

// Grid — plug
static byte ICON_BITMAP_GRID[8] = {
  0b01010,
  0b01010,
  0b11111,
  0b10001,
  0b10001,
  0b10001,
  0b01110,
  0b00100,
};

// Charging (current flowing in)
static byte ICON_BITMAP_ARROW_UP[8] = {
  0b00100,
  0b01110,
  0b10101,
  0b00100,
  0b00100,
  0b00100,
  0b00100,
  0b00000,
};

// Discharging (current flowing out)
static byte ICON_BITMAP_ARROW_DOWN[8] = {
  0b00000,
  0b00100,
  0b00100,
  0b00100,
  0b00100,
  0b10101,
  0b01110,
  0b00100,
};

// Fault / warning — classic exclamation mark
static byte ICON_BITMAP_WARNING[8] = {
  0b00100,
  0b00100,
  0b00100,
  0b00100,
  0b00100,
  0b00000,
  0b00100,
  0b00000,
};

// WiFi / connectivity
static byte ICON_BITMAP_WIFI[8] = {
  0b00000,
  0b01110,
  0b10001,
  0b00000,
  0b00100,
  0b01110,
  0b00100,
  0b00000,
};

// Registers all 8 icons into CGRAM. Call once in setup(), after lcd.init()/
// lcd.begin(). Works with LiquidCrystal and LiquidCrystal_I2C — anything
// with a createChar(uint8_t, byte[8]) method.
template <typename LcdT>
void lcdIconsInstall(LcdT &lcd) {
  lcd.createChar(ICON_SOLAR, ICON_BITMAP_SOLAR);
  lcd.createChar(ICON_BATTERY, ICON_BITMAP_BATTERY);
  lcd.createChar(ICON_LOAD, ICON_BITMAP_LOAD);
  lcd.createChar(ICON_GRID, ICON_BITMAP_GRID);
  lcd.createChar(ICON_ARROW_UP, ICON_BITMAP_ARROW_UP);
  lcd.createChar(ICON_ARROW_DOWN, ICON_BITMAP_ARROW_DOWN);
  lcd.createChar(ICON_WARNING, ICON_BITMAP_WARNING);
  lcd.createChar(ICON_WIFI, ICON_BITMAP_WIFI);
}
