#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>
#include "SSD1306Wire.h"

// ================= Heltec WiFi LoRa 32 V4 (ESP32-S3) onboard OLED =================
#define VEXT_CTRL 36   // external-peripheral power enable: ACTIVE LOW (drive LOW = ON)
#define OLED_RST  21
#define OLED_SDA  17
#define OLED_SCL  18
#define OLED_ADDR 0x3c
#define PRG_BTN   0    // onboard PRG/BOOT button (active LOW, has pull-up)

SSD1306Wire display(OLED_ADDR, OLED_SDA, OLED_SCL);
Preferences prefs;
WiFiManager wm;

// Default server URL. Overridden by whatever is saved via the WiFi config
// portal (persisted in NVS), so the box can be re-pointed without reflashing.
const char *DEFAULT_API_BASE = "https://esp-repo-ivet.vercel.app";
const char *DEFAULT_FALLBACK_PASSWORD = "12345678";
const int MAX_WIFI_CREDENTIALS = 8;
const unsigned long WIFI_CONNECT_TIMEOUT = 12000UL;
const unsigned long WIFI_RECOVERY_INTERVAL = 60000UL;

String apiBase;
String deviceId;             // this board's MAC (no colons) — unique per box
String currentBody = "";     // body of the note currently on screen
WiFiManagerParameter *serverParam = nullptr;

struct SavedWifi {
  String ssid;
  String password;
};

// Notes the PRG button fires off to the *other* box.
const char *RANDOM_MSGS[] = {
  "Thinking of you",
  "I love you",
  "Sending kisses",
  "Miss you lots",
  "You are awesome",
  "Hi from my box!",
};
const int RANDOM_MSGS_N = sizeof(RANDOM_MSGS) / sizeof(RANDOM_MSGS[0]);

// ---------- timing ----------
const unsigned long POLL_INTERVAL   = 15000UL;              // poll every 15 s
const unsigned long DISPLAY_TIMEOUT = 5UL * 60UL * 1000UL;  // hold a note 5 min

unsigned long lastPoll = 0;
unsigned long lastWifiRecovery = 0;
long          currentMsgId = 0;   // 0 = default screen
unsigned long displayStart = 0;

// =============================== OLED helpers ===============================
void vextOn() {
  pinMode(VEXT_CTRL, OUTPUT);
  digitalWrite(VEXT_CTRL, LOW);
  delay(50);
}

void oledReset() {
  pinMode(OLED_RST, OUTPUT);
  digitalWrite(OLED_RST, LOW);  delay(20);
  digitalWrite(OLED_RST, HIGH); delay(20);
}

// SSD1306 default font is ASCII-only; drop UTF-8 (emoji, degree signs, etc.).
String sanitizeAscii(const String &in) {
  String out;
  for (size_t i = 0; i < in.length(); i++) {
    uint8_t c = (uint8_t)in[i];
    if (c >= 0x20 && c < 0x7f) out += (char)c;
  }
  out.trim();
  return out;
}

String shortId() {
  return deviceId.length() > 4 ? deviceId.substring(deviceId.length() - 4) : deviceId;
}

void showStatus(const String &l1, const String &l2) {
  display.clear();
  display.setColor(WHITE);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.setFont(ArialMT_Plain_16);
  display.drawString(64, 10, l1);
  display.setFont(ArialMT_Plain_10);
  display.drawStringMaxWidth(64, 38, 128, l2);
  display.display();
}

void showDefault() {
  display.clear();
  display.setColor(WHITE);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.setFont(ArialMT_Plain_10);
  display.drawString(64, 2, "Box " + shortId());
  display.setFont(ArialMT_Plain_24);
  display.drawString(64, 16, "<3");
  display.setFont(ArialMT_Plain_10);
  display.drawString(64, 50, "waiting for love");
  display.display();
}

void showMessage(const String &body) {
  display.clear();
  display.setColor(WHITE);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  if (body.length() <= 18) {
    display.setFont(ArialMT_Plain_16);
    display.drawStringMaxWidth(64, 18, 128, body);
  } else {
    display.setFont(ArialMT_Plain_10);
    display.drawStringMaxWidth(64, 6, 128, body);
  }
  display.display();
}

// =============================== WiFi credential store ===============================
int loadWifiCredentials(SavedWifi creds[], int maxCreds) {
  String saved = prefs.getString("wifi_json", "");
  if (saved.length() == 0) return 0;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, saved);
  if (err || !doc.is<JsonArray>()) {
    Serial.printf("[wifi] bad saved credential list: %s\n", err.c_str());
    return 0;
  }

  int count = 0;
  for (JsonVariant item : doc.as<JsonArray>()) {
    if (count >= maxCreds) break;
    String ssid = item["ssid"] | "";
    String password = item["password"] | "";
    ssid.trim();
    if (ssid.length() == 0) continue;
    creds[count++] = { ssid, password };
  }
  return count;
}

bool saveWifiCredentials(SavedWifi creds[], int count) {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < count; i++) {
    if (creds[i].ssid.length() == 0) continue;
    JsonObject item = arr.add<JsonObject>();
    item["ssid"] = creds[i].ssid;
    item["password"] = creds[i].password;
  }

  String json;
  serializeJson(doc, json);
  bool ok = prefs.putString("wifi_json", json) > 0;
  Serial.printf("[wifi] saved %d credential(s), ok=%d\n", count, ok);
  return ok;
}

bool rememberWifiCredential(String ssid, String password) {
  ssid.trim();
  if (ssid.length() == 0) return false;

  SavedWifi creds[MAX_WIFI_CREDENTIALS];
  int count = loadWifiCredentials(creds, MAX_WIFI_CREDENTIALS);

  SavedWifi merged[MAX_WIFI_CREDENTIALS];
  int next = 0;
  merged[next++] = { ssid, password };

  for (int i = 0; i < count && next < MAX_WIFI_CREDENTIALS; i++) {
    if (creds[i].ssid == ssid) continue;
    merged[next++] = creds[i];
  }

  return saveWifiCredentials(merged, next);
}

void rememberCurrentWifiCredential() {
  if (WiFi.status() != WL_CONNECTED) return;
  String ssid = WiFi.SSID();
  String password = WiFi.psk();
  if (ssid.length() == 0 || password.length() == 0) return;
  rememberWifiCredential(ssid, password);
}

bool connectToWifi(const String &ssid, const String &password) {
  if (ssid.length() == 0) return false;
  Serial.printf("[wifi] trying %s\n", ssid.c_str());
  showStatus("Trying WiFi", ssid);

  WiFi.disconnect(false);
  delay(200);
  WiFi.begin(ssid.c_str(), password.c_str());

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT) {
    delay(250);
  }

  bool ok = WiFi.status() == WL_CONNECTED;
  Serial.printf("[wifi] %s -> %s\n", ssid.c_str(), ok ? "connected" : "failed");
  if (ok) {
    rememberWifiCredential(ssid, password);
  } else {
    WiFi.disconnect(false);
    delay(100);
  }
  return ok;
}

bool connectWithSavedCredentials() {
  SavedWifi creds[MAX_WIFI_CREDENTIALS];
  int count = loadWifiCredentials(creds, MAX_WIFI_CREDENTIALS);
  Serial.printf("[wifi] trying %d saved credential(s)\n", count);
  for (int i = 0; i < count; i++) {
    if (connectToWifi(creds[i].ssid, creds[i].password)) return true;
  }
  return false;
}

bool ssidAlreadyTried(const String tried[], int count, const String &ssid) {
  for (int i = 0; i < count; i++) {
    if (tried[i] == ssid) return true;
  }
  return false;
}

bool connectWithFallbackPassword() {
  Serial.printf("[wifi] scanning for fallback password %s\n", DEFAULT_FALLBACK_PASSWORD);
  showStatus("Scanning WiFi", "fallback password");

  int found = WiFi.scanNetworks();
  if (found <= 0) {
    Serial.println("[wifi] no networks found");
    WiFi.scanDelete();
    return false;
  }

  String tried[32];
  int triedCount = 0;
  for (int i = 0; i < found; i++) {
    String ssid = WiFi.SSID(i);
    ssid.trim();
    if (ssid.length() == 0 || ssidAlreadyTried(tried, triedCount, ssid)) continue;
    if (triedCount < 32) tried[triedCount++] = ssid;
    if (connectToWifi(ssid, DEFAULT_FALLBACK_PASSWORD)) {
      WiFi.scanDelete();
      return true;
    }
  }

  WiFi.scanDelete();
  return false;
}

// =============================== networking ===============================
bool httpGetJson(const String &url, JsonDocument &doc) {
  WiFiClientSecure client;
  client.setInsecure();  // skip cert validation (Vercel is valid; avoids bundling a CA)
  HTTPClient http;
  http.setUserAgent("LoveBox-ESP32");
  http.setTimeout(12000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(client, url)) return false;
  int code = http.GET();
  Serial.printf("[GET] %s -> %d\n", url.c_str(), code);
  bool ok = false;
  if (code == 200) {
    DeserializationError err = deserializeJson(doc, http.getString());
    ok = !err;
    if (err) Serial.printf("[json] %s\n", err.c_str());
  }
  http.end();
  return ok;
}

bool httpPostJson(const String &url, const String &json) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setUserAgent("LoveBox-ESP32");
  http.setTimeout(12000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(client, url)) return false;
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(json);
  Serial.printf("[POST] %s -> %d\n", url.c_str(), code);
  http.end();
  return code >= 200 && code < 300;
}

void ackMessage(long id) {
  // deviceId is plain hex, safe to embed directly.
  String json = "{\"device\":\"" + deviceId + "\",\"id\":" + String(id) + "}";
  httpPostJson(apiBase + "/api/ack", json);
}

void ackWifiCommand(long id, const String &status, const String &detail) {
  String json = "{\"device\":\"" + deviceId + "\",\"id\":" + String(id) +
                ",\"status\":\"" + status + "\",\"detail\":\"" + detail + "\"}";
  httpPostJson(apiBase + "/api/wifi/ack", json);
}

void pollWifiCommand() {
  JsonDocument doc;
  String url = apiBase + "/api/wifi?device=" + deviceId;
  if (!httpGetJson(url, doc)) return;
  if (doc["id"].isNull()) return;

  long id = doc["id"].as<long>();
  String ssid = doc["ssid"].as<String>();
  String password = doc["password"].as<String>();
  ssid.trim();

  if (id <= 0 || ssid.length() == 0) {
    ackWifiCommand(id, "failed", "bad command");
    return;
  }

  bool ok = rememberWifiCredential(ssid, password);
  Serial.printf("[wifi] remote credential #%ld for %s -> %s\n",
                id, ssid.c_str(), ok ? "saved" : "failed");
  ackWifiCommand(id, ok ? "saved" : "failed", ok ? "credential saved" : "save failed");
}

void sendRandom() {
  int idx = (int)(esp_random() % RANDOM_MSGS_N);
  String body = RANDOM_MSGS[idx];
  JsonDocument d;
  d["from"] = deviceId;
  d["body"] = body;
  String json;
  serializeJson(d, json);
  Serial.printf("[btn] sending: %s\n", body.c_str());
  bool ok = httpPostJson(apiBase + "/api/send", json);
  showStatus(ok ? "Sent!" : "Send failed", body);
  delay(1200);
}

void pollOnce() {
  JsonDocument doc;
  String url = apiBase + "/api/next?device=" + deviceId;
  if (!httpGetJson(url, doc)) return;
  if (doc["id"].isNull()) return;  // nothing new; keep showing whatever's up

  long id = doc["id"].as<long>();
  if (id != currentMsgId) {
    String body = sanitizeAscii(doc["body"].as<String>());
    if (body.length() == 0) body = "(empty note)";
    currentMsgId = id;
    currentBody = body;
    displayStart = millis();
    showMessage(body);
    Serial.printf("[msg] #%ld: %s\n", id, body.c_str());
    ackMessage(id);  // mark read so it never nags again (and survives reboot)
  }
}

// =============================== config portal ===============================
void saveServerFromParam() {
  if (!serverParam) return;
  String v = String(serverParam->getValue());
  v.trim();
  while (v.endsWith("/")) v.remove(v.length() - 1);
  if (v.length() > 0 && v != apiBase) {
    apiBase = v;
    prefs.putString("apiurl", apiBase);
    Serial.printf("[cfg] saved apiBase = %s\n", apiBase.c_str());
  }
}

void onSaveParams() { saveServerFromParam(); }

void onPortal(WiFiManager *m) {
  showStatus("Setup WiFi", "Join AP:\nLoveBox-Setup");
}

// =============================== button ===============================
void handleButton() {
  static int lastRaw = HIGH;
  static int stable = HIGH;
  static unsigned long tDeb = 0;
  int raw = digitalRead(PRG_BTN);
  if (raw != lastRaw) {
    tDeb = millis();
    lastRaw = raw;
  }
  if (millis() - tDeb > 50 && raw != stable) {
    stable = raw;
    if (stable == LOW) {  // pressed
      if (WiFi.status() == WL_CONNECTED) {
        sendRandom();
      } else {
        showStatus("No WiFi", "can't send");
        delay(1000);
      }
      if (currentMsgId != 0) showMessage(currentBody);
      else showDefault();
    }
  }
}

// =============================== setup / loop ===============================
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n[boot] LoveBox starting");

  pinMode(PRG_BTN, INPUT_PULLUP);

  vextOn();
  oledReset();
  display.init();
  display.flipScreenVertically();
  display.setContrast(255);

  WiFi.mode(WIFI_STA);
  deviceId = WiFi.macAddress();
  deviceId.replace(":", "");
  deviceId.toUpperCase();
  Serial.printf("[id] device = %s\n", deviceId.c_str());

  prefs.begin("lovebox", false);
  apiBase = prefs.getString("apiurl", DEFAULT_API_BASE);
  Serial.printf("[cfg] apiBase = %s\n", apiBase.c_str());

  showStatus("LoveBox", "starting...");

  // ---- WiFiManager: saved creds persist in NVS; portal to (re)configure ----
  wm.setConfigPortalTimeout(180);
  static WiFiManagerParameter sp("server", "Server URL (https://...)", apiBase.c_str(), 120);
  serverParam = &sp;
  wm.addParameter(&sp);
  wm.setSaveParamsCallback(onSaveParams);
  wm.setAPCallback(onPortal);

  bool forcePortal = (digitalRead(PRG_BTN) == LOW);  // hold PRG at boot to reconfigure
  bool connected;
  if (forcePortal) {
    Serial.println("[wifi] PRG held -> opening config portal");
    connected = wm.startConfigPortal("LoveBox-Setup");
  } else {
    connected = connectWithSavedCredentials();
    if (!connected) connected = connectWithFallbackPassword();
    if (!connected) connected = wm.autoConnect("LoveBox-Setup");
  }
  saveServerFromParam();

  if (connected || WiFi.status() == WL_CONNECTED) {
    WiFi.setAutoReconnect(true);
    WiFi.persistent(true);
    rememberCurrentWifiCredential();
    Serial.printf("[wifi] connected, IP %s\n", WiFi.localIP().toString().c_str());
    showStatus("Connected", WiFi.localIP().toString());
    delay(1200);
  } else {
    Serial.println("[wifi] NOT connected");
    showStatus("No WiFi", "hold PRG to setup");
    delay(1500);
  }

  showDefault();
  lastPoll = millis() - POLL_INTERVAL;  // poll immediately on first loop
}

void loop() {
  handleButton();

  unsigned long now = millis();
  if (now - lastPoll >= POLL_INTERVAL) {
    lastPoll = now;
    if (WiFi.status() == WL_CONNECTED) {
      pollWifiCommand();
      pollOnce();
    } else {
      Serial.println("[wifi] disconnected, reconnecting...");
      WiFi.reconnect();
      if (now - lastWifiRecovery >= WIFI_RECOVERY_INTERVAL) {
        lastWifiRecovery = now;
        if (!connectWithSavedCredentials()) {
          connectWithFallbackPassword();
        }
      }
    }
  }

  // Revert to the default screen after the note has been up for 5 minutes.
  if (currentMsgId != 0 && (millis() - displayStart > DISPLAY_TIMEOUT)) {
    currentMsgId = 0;
    currentBody = "";
    showDefault();
  }

  delay(20);
}
