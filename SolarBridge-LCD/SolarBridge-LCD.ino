/*
 * Solar Bridge LCD Display
 * ─────────────────────────
 * A tiny satellite display for the Solar Bridge inverter/BMS monitoring
 * system (github.com/manoranjan2050/Solar-Bridge-Flin-Fution-JKBMS).
 * Polls the same /api/state endpoint the web dashboard and Android app
 * use, and cycles solar / load / battery / grid readings across a 16x2
 * I2C LCD.
 *
 * Hardware: ESP8266 (NodeMCU / Wemos D1 Mini) + 16x2 I2C LCD (PCF8574
 * backpack, usually address 0x27 or 0x3F).
 *
 * First boot (or held-FLASH-button reset) opens a WiFi setup portal —
 * connect to it from a phone, no code changes needed. See README.md.
 *
 * Libraries (Library Manager): WiFiManager (tzapu), ArduinoJson (>=6.19),
 * LiquidCrystal_I2C (marcoschwartz/johnrickman fork), LittleFS (bundled
 * with the ESP8266 core).
 */

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>
#include <LiquidCrystal_I2C.h>
#include <LittleFS.h>
#include <Wire.h>
#include "lcd_icons.h"

// Optional, gitignored — lets you hardcode WiFi/server/token for a fast
// local flash without going through the captive portal each time. See
// secrets.h.example. Never commit a real secrets.h.
#if __has_include("secrets.h")
#include "secrets.h"
#endif

// ── I2C pins — this build is wired to D5/D6, not the ESP8266 defaults ───
// (D1/D2). Swap these two if the LCD doesn't respond at boot.
#define I2C_SDA_PIN D6
#define I2C_SCL_PIN D5

// ── Config persisted via WiFiManager's custom parameters ───────────────
#define CONFIG_PATH "/config.json"

#ifndef DEFAULT_SERVER_URL
#define DEFAULT_SERVER_URL "https://solar.manoranjan.dev"
#endif
#ifndef DEFAULT_API_TOKEN
#define DEFAULT_API_TOKEN ""
#endif
#ifndef DEFAULT_WIFI_SSID
#define DEFAULT_WIFI_SSID ""
#endif
#ifndef DEFAULT_WIFI_PASS
#define DEFAULT_WIFI_PASS ""
#endif

char serverUrl[96] = DEFAULT_SERVER_URL;
char apiToken[64] = DEFAULT_API_TOKEN;
char pollSecondsStr[4] = "5";
uint32_t pollIntervalMs = 5000;

// ── LCD (auto-detects 0x27 vs 0x3F, and SDA/SCL pin order) ──────────────
LiquidCrystal_I2C *lcd = nullptr;

// ── State polled from /api/state ────────────────────────────────────────
struct SolarState {
  bool valid = false;
  float pvPower = 0, pvToday = 0;
  float loadPower = 0, loadPercent = 0;
  float batterySoc = 0;
  float batteryCurrent = 0;
  float gridPower = 0;
  String deviceMode = "--";
  String faultStatus = "ok";
  String faultText = "";
};
SolarState state;

unsigned long lastPoll = 0;
unsigned long lastPageFlip = 0;
uint8_t page = 0;
const uint8_t PAGE_COUNT = 4;
const unsigned long PAGE_MS = 4000;

// ── Config load/save (LittleFS, so credentials survive re-flashing) ─────
void loadConfig() {
  if (!LittleFS.begin()) return;
  if (!LittleFS.exists(CONFIG_PATH)) return;
  File f = LittleFS.open(CONFIG_PATH, "r");
  if (!f) return;
  StaticJsonDocument<256> doc;
  if (deserializeJson(doc, f) == DeserializationError::Ok) {
    strlcpy(serverUrl, doc["server"] | serverUrl, sizeof(serverUrl));
    strlcpy(apiToken, doc["token"] | apiToken, sizeof(apiToken));
    strlcpy(pollSecondsStr, doc["poll"] | pollSecondsStr, sizeof(pollSecondsStr));
  }
  f.close();
}

void saveConfig() {
  StaticJsonDocument<256> doc;
  doc["server"] = serverUrl;
  doc["token"] = apiToken;
  doc["poll"] = pollSecondsStr;
  File f = LittleFS.open(CONFIG_PATH, "w");
  if (!f) return;
  serializeJson(doc, f);
  f.close();
}

// ── LCD helpers ──────────────────────────────────────────────────────────
// Tries the wired pin order first (I2C_SDA_PIN/I2C_SCL_PIN), then the
// swapped order, so a reversed SDA/SCL solder job still works.
bool i2cProbe(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

void lcdInit() {
  uint8_t addr = 0x27;
  bool found = false;

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  if (i2cProbe(0x27)) { addr = 0x27; found = true; }
  else if (i2cProbe(0x3F)) { addr = 0x3F; found = true; }

  if (!found) {
    // wired backwards? try the swapped pin order before giving up
    Wire.begin(I2C_SCL_PIN, I2C_SDA_PIN);
    if (i2cProbe(0x27)) { addr = 0x27; found = true; }
    else if (i2cProbe(0x3F)) { addr = 0x3F; found = true; }
  }

  Serial.printf("[LCD] %s at 0x%02X\n", found ? "found" : "NOT FOUND, defaulting to", addr);

  lcd = new LiquidCrystal_I2C(addr, 16, 2);
  lcd->init();
  lcd->backlight();
  lcdIconsInstall(*lcd);
}

void lcdLine(uint8_t row, const String &text) {
  String padded = text;
  while (padded.length() < 16) padded += ' ';
  if (padded.length() > 16) padded = padded.substring(0, 16);
  lcd->setCursor(0, row);
  lcd->print(padded);
}

// Icon glyph at column 0, then text padded to fill the remaining 15 columns.
void lcdIconLine(uint8_t row, LcdIcon icon, const String &text) {
  String padded = text;
  while (padded.length() < 15) padded += ' ';
  if (padded.length() > 15) padded = padded.substring(0, 15);
  lcd->setCursor(0, row);
  lcd->write((uint8_t)icon);
  lcd->print(padded);
}

void lcdMessage(const String &l1, const String &l2) {
  lcdLine(0, l1);
  lcdLine(1, l2);
}

// ── WiFiManager: extra fields for server/token/poll interval ────────────
void runWiFiPortal() {
  WiFiManager wm;
  WiFiManagerParameter p_server("server", "Dashboard URL (https://...)", serverUrl, sizeof(serverUrl) - 1);
  WiFiManagerParameter p_token("token", "Viewer API token", apiToken, sizeof(apiToken) - 1);
  WiFiManagerParameter p_poll("poll", "Poll interval (seconds)", pollSecondsStr, sizeof(pollSecondsStr) - 1);
  wm.addParameter(&p_server);
  wm.addParameter(&p_token);
  wm.addParameter(&p_poll);

  lcdMessage("Solar Bridge", "Setup: connect");
  lcd->setCursor(0, 1);
  lcd->print("to SolarBridge-AP");

  wm.setConfigPortalTimeout(180);
  bool ok = wm.autoConnect("SolarBridge-Setup");

  strlcpy(serverUrl, p_server.getValue(), sizeof(serverUrl));
  strlcpy(apiToken, p_token.getValue(), sizeof(apiToken));
  strlcpy(pollSecondsStr, p_poll.getValue(), sizeof(pollSecondsStr));
  saveConfig();

  if (!ok) {
    lcdMessage("WiFi setup", "timed out - retry");
    delay(3000);
    ESP.restart();
  }
}

// ── Fetch + parse /api/state (filtered, so memory use stays flat no ─────
// matter how many BMS cell fields the real payload has) ────────────────
bool fetchState() {
  if (WiFi.status() != WL_CONNECTED) return false;

  String url = String(serverUrl) + "/api/state";
  std::unique_ptr<BearSSL::WiFiClientSecure> https(new BearSSL::WiFiClientSecure);
  WiFiClient httpPlain;
  HTTPClient http;
  bool isHttps = url.startsWith("https://");

  if (isHttps) {
    https->setInsecure();  // see README: pin a fingerprint if you want stricter TLS
    if (!http.begin(*https, url)) return false;
  } else {
    if (!http.begin(httpPlain, url)) return false;
  }
  http.addHeader("Authorization", String("Bearer ") + apiToken);
  http.setTimeout(8000);

  int code = http.GET();
  Serial.printf("[API] GET %s -> %d\n", url.c_str(), code);
  if (code != HTTP_CODE_OK) {
    http.end();
    return false;
  }

  StaticJsonDocument<512> filter;
  filter["inverter_pv_power"] = true;
  filter["inverter_pv_energy_today"] = true;
  filter["inverter_ac_out_active_power"] = true;
  filter["inverter_load_percent"] = true;
  filter["bank_battery_soc"] = true;
  filter["inverter_battery_current"] = true;
  filter["inverter_grid_power"] = true;
  filter["inverter_device_mode"] = true;
  filter["inverter_fault_status"] = true;
  filter["inverter_fault_text"] = true;

  DynamicJsonDocument doc(768);
  DeserializationError err = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  if (err) {
    Serial.printf("[API] JSON parse failed: %s\n", err.c_str());
    return false;
  }

  state.pvPower = doc["inverter_pv_power"] | 0.0f;
  state.pvToday = doc["inverter_pv_energy_today"] | 0.0f;
  state.loadPower = doc["inverter_ac_out_active_power"] | 0.0f;
  state.loadPercent = doc["inverter_load_percent"] | 0.0f;
  state.batterySoc = doc["bank_battery_soc"] | 0.0f;
  state.batteryCurrent = doc["inverter_battery_current"] | 0.0f;
  state.gridPower = doc["inverter_grid_power"] | 0.0f;
  state.deviceMode = String((const char *)(doc["inverter_device_mode"] | "--"));
  state.faultStatus = String((const char *)(doc["inverter_fault_status"] | "ok"));
  state.faultText = String((const char *)(doc["inverter_fault_text"] | ""));
  state.valid = true;
  Serial.printf("[API] solar=%.0fW load=%.0fW soc=%.0f%% grid=%.0fW mode=%s\n",
                 state.pvPower, state.loadPower, state.batterySoc, state.gridPower, state.deviceMode.c_str());
  return true;
}

// ── Render the current rotation page ─────────────────────────────────────
String padNum(float v, uint8_t width, uint8_t decimals = 0) {
  char buf[16];
  dtostrf(v, width, decimals, buf);
  return String(buf);
}

void renderPage() {
  if (!state.valid) {
    lcdMessage("Solar Bridge", "Waiting for data");
    return;
  }

  // A fault/warning always takes over the display instead of rotating.
  if (state.faultStatus == "fault" || state.faultStatus == "warning") {
    String tag = state.faultStatus == "fault" ? " FAULT" : " WARNING";
    lcdIconLine(0, ICON_WARNING, tag);
    lcdLine(1, state.faultText);
    return;
  }

  switch (page) {
    case 0:
      lcdIconLine(0, ICON_SOLAR, " " + padNum(state.pvPower, 5) + "W");
      lcdLine(1, "Today  " + padNum(state.pvToday, 5, 1) + "kWh");
      break;
    case 1:
      lcdIconLine(0, ICON_LOAD, " " + padNum(state.loadPower, 5) + "W");
      lcdLine(1, "Load%     " + padNum(state.loadPercent, 3) + "%");
      break;
    case 2: {
      LcdIcon dirIcon = state.batteryCurrent >= 0 ? ICON_ARROW_UP : ICON_ARROW_DOWN;
      String dirLabel = state.batteryCurrent >= 0 ? "Chg " : "Dis ";
      lcdIconLine(0, ICON_BATTERY, "    " + padNum(state.batterySoc, 3) + "%");
      lcdIconLine(1, dirIcon, dirLabel + padNum(fabs(state.batteryCurrent), 4, 1) + "A");
      break;
    }
    case 3:
      lcdIconLine(0, ICON_GRID, " " + padNum(state.gridPower, 5) + "W");
      lcdLine(1, "Mode:" + state.deviceMode);
      break;
  }
}

// ── Setup / loop ──────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  lcdInit();
  loadConfig();
  pollIntervalMs = (uint32_t)atoi(pollSecondsStr) * 1000UL;
  if (pollIntervalMs < 2000) pollIntervalMs = 5000;

  WiFi.mode(WIFI_STA);
  if (strlen(DEFAULT_WIFI_SSID) > 0) {
    Serial.printf("[WiFi] connecting to '%s'...\n", DEFAULT_WIFI_SSID);
    WiFi.begin(DEFAULT_WIFI_SSID, DEFAULT_WIFI_PASS);
  } else {
    WiFi.begin();  // last WiFi creds saved by the SDK, if any
  }
  lcdMessage("Solar Bridge", "Connecting WiFi");
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(250);
    Serial.print('.');
  }
  Serial.printf("\n[WiFi] status=%d (3=connected) ip=%s\n", WiFi.status(), WiFi.localIP().toString().c_str());

  if (WiFi.status() != WL_CONNECTED || strlen(apiToken) == 0) {
    Serial.println("[WiFi] falling back to setup portal");
    runWiFiPortal();
  }

  fetchState();
  lastPoll = millis();
  lastPageFlip = millis();
  renderPage();
}

void loop() {
  unsigned long now = millis();
  bool dirty = false;

  if (now - lastPoll >= pollIntervalMs) {
    lastPoll = now;
    if (fetchState()) dirty = true;
  }

  if (now - lastPageFlip >= PAGE_MS) {
    lastPageFlip = now;
    page = (page + 1) % PAGE_COUNT;
    dirty = true;
  }

  if (dirty) renderPage();
  delay(200);
}
