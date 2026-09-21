# Solar Bridge LCD Display

A tiny satellite display for [Solar Bridge](https://github.com/manoranjan2050/Solar-Bridge-Flin-Fution-JKBMS) —
an ESP8266 + 16x2 I2C LCD that polls the same `/api/state` endpoint the web dashboard and
[Android app](https://github.com/manoranjan2050/SolarBridgeApp) use, and rotates through live solar,
load, battery and grid readings. No screen to unlock, no app to open — just glance at the wall.

## What it shows

Four pages, rotating every 4 seconds:

| Page | Line 1 | Line 2 |
|---|---|---|
| Solar | `Solar    1024W` | `Today   10.4kWh` |
| Load | `Load      920W` | `Load%       24%` |
| Battery | `Battery     76%` | `Chg      6.2A` (or `Dis`) |
| Grid | `Grid       760W` | `Mode:Line` |

If the inverter reports a fault or warning, the display locks onto a `! FAULT !` / `! WARNING !`
screen with the message instead of rotating — you'll notice it across the room.

## Hardware

| Part | Notes |
|---|---|
| ESP8266 dev board | NodeMCU or Wemos D1 Mini (this sketch uses the default I2C pins) |
| 16x2 LCD + PCF8574 I2C backpack | Address `0x27` or `0x3F` — auto-detected at boot |

### Wiring

| LCD backpack | ESP8266 (NodeMCU / D1 Mini) |
|---|---|
| GND | GND |
| VCC | 5V (VU / VIN) |
| SDA | D2 |
| SCL | D1 |

## Setup

1. **Get a read-only viewer token** from your Solar Bridge dashboard: **System** page →
   **Demo / Viewer Access** card. Use the viewer token here, not your admin token — this device
   only ever needs to *read* state, and a viewer token can't change a setting even if the board
   or its WiFi credentials were ever lost or cloned.
2. Install the libraries below via the Arduino IDE's Library Manager, then flash
   `SolarBridge-LCD.ino` to the board.
3. On first boot (or if it can't reconnect to WiFi), the board opens its own access point named
   **`SolarBridge-Setup`**. Connect to it from a phone, and a setup page opens automatically
   (or go to `192.168.4.1`). Fill in:
   - Your WiFi network + password
   - **Dashboard URL** — `https://solar.yourdomain.com` (Cloudflare Tunnel) or
     `http://solar.local:8080` (LAN only)
   - **Viewer API token** — from step 1
   - **Poll interval** — 5 seconds by default; raise it if you'd rather not hit your Pi as often
4. Save — the board reboots, connects, and starts showing live data within a few seconds.

Re-run setup anytime by holding the board's FLASH button at boot, or just re-flash — WiFi and API
settings are stored in flash (LittleFS) and survive a normal reset.

## Libraries (Arduino Library Manager)

- **WiFiManager** by tzapu
- **ArduinoJson** (>= 6.19) by Benoit Blanchon
- **LiquidCrystal I2C** (the `marcoschwartz`/`johnrickman` fork — search "LiquidCrystal I2C")
- `LittleFS` and `ESP8266HTTPClient` ship with the ESP8266 Arduino core

## A note on TLS

The sketch uses `setInsecure()` for HTTPS (no certificate pinning) — simplest to set up and fine
for a read-only viewer token, but it does mean the board won't detect a MITM'd connection. If your
dashboard is only reachable on your LAN, use the plain `http://solar.local:8080` address instead
and skip TLS entirely.

## Related

- [Solar-Bridge-Flin-Fution-JKBMS](https://github.com/manoranjan2050/Solar-Bridge-Flin-Fution-JKBMS) —
  the Raspberry Pi bridge + web dashboard this pairs with.
- [SolarBridgeApp](https://github.com/manoranjan2050/SolarBridgeApp) — the Android companion app.
- Built by [ElectroIoT](https://electroiot.in)
