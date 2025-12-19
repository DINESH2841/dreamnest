#include <Wire.h>
#include <SPI.h>
#include <MFRC522.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <LiquidCrystal_I2C.h>

// ---------------- CONFIG ----------------
const char* WIFI_SSID     = "Nikhil";
const char* WIFI_PASSWORD = "12345678";

// Your Cloudflare Worker URL (from earlier)
const String WORKER_URL = "https://smart-iot-attendance-manager.sevennidinesh.workers.dev/?uid=";

// ---------------- HARDWARE PINS ----------------
// MFRC522 wiring (recommended):
// SDA(SS) -> D4
// SCK     -> D5
// MOSI    -> D7
// MISO    -> D6
// RST     -> D3
// 3.3V    -> 3.3V
// GND     -> GND
#define RST_PIN D3
#define SS_PIN  D8

MFRC522 mfrc522(SS_PIN, RST_PIN);
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ---------------- STATE ----------------
unsigned long lastReconnectAttempt = 0;
const unsigned long WIFI_RECONNECT_INTERVAL = 7000; // try reconnect every 7s if disconnected

String lastUID = "";
unsigned long lastUIDtime = 0;
const unsigned long DEBOUNCE_MS = 2000; // ignore same UID within 2s

// ---------------- HELPERS ----------------
void connectWiFiOnce(unsigned long timeoutMs = 10000) {
  if (WiFi.status() == WL_CONNECTED) return;

  WiFi.mode(WIFI_STA);
  WiFi.setSleepMode(WIFI_NONE_SLEEP); // reduce disconnects
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long start = millis();
  Serial.print("Connecting WiFi");
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    delay(150); Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi connected: " + WiFi.localIP().toString());
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("WiFi OK");
  } else {
    Serial.println("WiFi failed");
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("WiFi Fail");
  }
}

// safe small LCD update without clearing whole screen
void lcdStatus(const char *line1, const char *line2 = "") {
  lcd.setCursor(0,0);
  lcd.print("                "); // 16 spaces to blank
  lcd.setCursor(0,0);
  lcd.print(line1);
  lcd.setCursor(0,1);
  lcd.print("                ");
  lcd.setCursor(0,1);
  lcd.print(line2);
}

// read UID as hex string with leading zeros (no spaces)
String readUID() {
  String uid = "";
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    if (mfrc522.uid.uidByte[i] < 0x10) uid += "0";
    uid += String(mfrc522.uid.uidByte[i], HEX);
  }
  uid.toUpperCase();
  return uid;
}

// ---------------- SEND ----------------
void sendToWorker(const String &uid) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("sendToWorker: WiFi not connected");
    lcdStatus("WiFi Lost", uid.c_str());
    return;
  }

  // Use secure client to talk to Cloudflare Worker (trusted certs)
  WiFiClientSecure client;
  client.setInsecure(); // Cloudflare certs acceptable; saves pain on ESP

  HTTPClient https;
  String url = WORKER_URL + uid;

  Serial.println("Request URL:");
  Serial.println(url);

  if (!https.begin(client, url)) {
    Serial.println("HTTP begin failed");
    lcdStatus("Begin Err", "");
    return;
  }

  https.setTimeout(4000); // reasonable timeout for internet variability
  int code = https.GET();
  Serial.println("HTTP Code: " + String(code));

  if (code == HTTP_CODE_OK) {
    String resp = https.getString();
    Serial.println("Response: " + resp);

    // show short response on LCD
    String show = resp;
    if (show.length() > 16) show = show.substring(0,16);
    lcdStatus(show.c_str(), uid.c_str());

  } else {
    Serial.println("HTTP ERROR: " + String(code));
    lcdStatus("ERR", String(code).c_str());
  }

  https.end();
}

// ---------------- SETUP ----------------
void setup() {
  Serial.begin(115200);
  delay(50);
  Wire.begin(D2, D1);
  lcd.init();
  lcd.backlight();
  lcd.print("Booting...");

  // connect WiFi once (non-blocking approach later)
  connectWiFiOnce(5000);

  SPI.begin();
  mfrc522.PCD_Init();
  Serial.println("RFID ready");

  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Scan Now");
}

// ---------------- MAIN LOOP ----------------
void loop() {
  // keep WiFi alive: reconnect if disconnected, but don't spin too often
  if (WiFi.status() != WL_CONNECTED) {
    if (millis() - lastReconnectAttempt > WIFI_RECONNECT_INTERVAL) {
      lastReconnectAttempt = millis();
      Serial.println("WiFi down - trying reconnect...");
      connectWiFiOnce(7000);
    }
  }

  // check for new card
  if (!mfrc522.PICC_IsNewCardPresent()) return;
  if (!mfrc522.PICC_ReadCardSerial()) return;

  String uid = readUID();
  unsigned long now = millis();

  Serial.println("UID: " + uid);

  // debounce duplicates
  if (uid == lastUID && (now - lastUIDtime) < DEBOUNCE_MS) {
    Serial.println("Ignored duplicate scan");
    // still halt to prevent card double-read
    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
    return;
  }

  lastUID = uid;
  lastUIDtime = now;

  // quick UI
  lcdStatus("Sending...", uid.c_str());

  // send (blocking network call but short timeout)
  sendToWorker(uid);

  // halt card so it won't re-read immediately
  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();

  // small cooldown so LCD + sheet have time to show results
  delay(150);
  lcdStatus("Scan Now", "");
}
