#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "SSD1306Wire.h"

// ---------- Heltec WiFi LoRa 32 V4 (ESP32-S3) onboard OLED ----------
// SSD1306 128x64 over I2C @ 0x3C.
#define VEXT_CTRL 36   // external-peripheral power enable: ACTIVE LOW (drive LOW = ON)
#define OLED_RST  21   // OLED reset
#define OLED_SDA  17
#define OLED_SCL  18
#define OLED_ADDR 0x3c

SSD1306Wire display(OLED_ADDR, OLED_SDA, OLED_SCL);

// ---------- WiFi ----------
const char *WIFI_SSID = "A1_1230";
const char *WIFI_PASS = "485754435D9F7E9D";

// ---------- App state ----------
const char *TITLE = "ivan is awesome";
String weatherText = "weather: ...";

unsigned long lastWeather = 0;
const unsigned long WEATHER_INTERVAL = 10UL * 60UL * 1000UL; // refresh every 10 min

// Drive Vext low to power the OLED (and other peripherals).
void vextOn() {
  pinMode(VEXT_CTRL, OUTPUT);
  digitalWrite(VEXT_CTRL, LOW);
  delay(50);
}

void oledReset() {
  pinMode(OLED_RST, OUTPUT);
  digitalWrite(OLED_RST, LOW);
  delay(20);
  digitalWrite(OLED_RST, HIGH);
  delay(20);
}

// Keep only printable ASCII so the SSD1306 font never sees UTF-8
// junk (wttr.in returns a degree symbol etc.).
String sanitizeAscii(const String &in) {
  String out;
  for (size_t i = 0; i < in.length(); i++) {
    uint8_t c = (uint8_t)in[i];
    if (c >= 0x20 && c < 0x7f) out += (char)c;
  }
  out.trim();
  return out;
}

void render() {
  display.clear();
  display.setColor(WHITE);
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.setFont(ArialMT_Plain_16);
  display.drawString(0, 0, TITLE);
  display.drawHorizontalLine(0, 20, 128);
  display.setFont(ArialMT_Plain_10);
  display.drawStringMaxWidth(0, 24, 128, weatherText);
  display.display();
}

bool fetchWeather() {
  if (WiFi.status() != WL_CONNECTED) return false;

  WiFiClientSecure client;
  client.setInsecure(); // wttr.in over TLS, skip cert pinning
  HTTPClient http;
  http.setUserAgent("curl/8.4.0"); // ask wttr.in for the plain-text (curl) view
  http.setTimeout(12000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  // %l = location (from public IP), %C = condition text, %t = temperature.
  // '+' renders as a space; non-ASCII is stripped on the device.
  const char *url = "https://wttr.in/?format=%l:+%C+%t";
  if (!http.begin(client, url)) {
    Serial.println("[weather] http.begin failed");
    return false;
  }

  int code = http.GET();
  Serial.printf("[weather] HTTP %d\n", code);
  bool ok = false;
  if (code == HTTP_CODE_OK) {
    String clean = sanitizeAscii(http.getString());
    if (clean.length() > 0) {
      weatherText = clean;
      Serial.printf("[weather] %s\n", weatherText.c_str());
      ok = true;
    }
  }
  http.end();
  return ok;
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n[boot] Heltec WiFi LoRa 32 V4 starting");

  // Power + reset the panel BEFORE talking to it.
  vextOn();
  oledReset();

  display.init();
  display.flipScreenVertically(); // Heltec panel is mounted rotated
  display.setContrast(255);
  weatherText = "connecting wifi...";
  render();
  Serial.println("[oled] init done, title shown");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("[wifi] connecting to %s", WIFI_SSID);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 30000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[wifi] connected, IP %s\n", WiFi.localIP().toString().c_str());
    weatherText = "wifi ok, fetching...";
    render();
    if (!fetchWeather()) weatherText = "weather fetch failed";
    lastWeather = millis();
  } else {
    Serial.println("[wifi] FAILED to connect");
    weatherText = "wifi connect failed";
  }
  render();
}

void loop() {
  if (WiFi.status() == WL_CONNECTED && (millis() - lastWeather > WEATHER_INTERVAL)) {
    if (fetchWeather()) {
      render();
      lastWeather = millis();
    } else {
      // retry sooner on failure
      lastWeather = millis() - WEATHER_INTERVAL + 60000UL;
    }
  }
  delay(200);
}
