#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <SPI.h>
#include <MFRC522.h>
#include <DFRobotDFPlayerMini.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <ArduinoJson.h>  // ✅ Include ArduinoJson

const char* defaultSSID     = "Vivo";
const char* defaultPassword = "12345678";
const char* attendAPI       = "Yours attendence api";
const char* credAPI         = "Yours get-config api";

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Preferences preferences;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 19800, 60000);

#define SS_PIN   5
#define RST_PIN  4
MFRC522 mfrc522(SS_PIN, RST_PIN);

HardwareSerial mySerial(2);
DFRobotDFPlayerMini mp3;
const uint8_t DF_RX = 25;
const uint8_t DF_TX = 26;

const uint8_t BUTTON_PIN = 12;
unsigned long lastScan   = 0;
bool           showStatus = false;
String         rfidStatus;

void showOLEDStatus(const char* line1, const char* line2 = "", uint16_t delayMs = 0) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(line1);
  if (line2[0]) {
    display.setCursor(0, 12);
    display.println(line2);
  }
  display.display();
  if (delayMs) delay(delayMs);
}

void clearWiFiAndRestart() {
  Serial.println("\nButton pressed - clearing saved WiFi creds");
  showOLEDStatus("Clearing WiFi", "Please wait...");
  preferences.begin("wifiCreds", false);
  preferences.clear();
  preferences.end();
  delay(1000);
  ESP.restart();
}

void connectToWiFi() {
  preferences.begin("wifiCreds", false);
  String storedSSID = preferences.getString("ssid", "");
  String storedPASS = preferences.getString("password", "");
  preferences.end();

  const char* useSSID = defaultSSID;
  const char* usePASS = defaultPassword;
  bool hasStored = storedSSID.length();

  if (hasStored) {
    useSSID = storedSSID.c_str();
    usePASS = storedPASS.c_str();
    Serial.printf("Using stored WiFi: %s / %s\n", useSSID, usePASS);
  } else {
    Serial.printf("Using default WiFi: %s / %s\n", useSSID, usePASS);
  }

  showOLEDStatus("Connecting to", useSSID);
  WiFi.begin(useSSID, usePASS);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() != WL_CONNECTED && hasStored) {
    Serial.println("\nStored WiFi failed. Trying default.");
    showOLEDStatus("Retry default WiFi");
    WiFi.begin(defaultSSID, defaultPassword);
    start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
      delay(500);
      Serial.print(".");
    }
    preferences.begin("wifiCreds", false);
    preferences.clear();
    preferences.end();
    Serial.println("Cleared bad stored credentials.");
  }

  if (WiFi.status() == WL_CONNECTED) {
    showOLEDStatus("WiFi Connected", WiFi.SSID().c_str());
    Serial.printf("Connected to %s\n", WiFi.SSID().c_str());
      mp3.play(2);  // 🔊 Play WiFi connected message
      delay(2000);  // Allow time for sound to play
    if (!hasStored) {
      HTTPClient http;
      http.begin(credAPI);
      String json = String("{\"uid\":\"") + "test" + "\"}";
      int code = http.POST(json);
      Serial.printf("Fetching WiFi creds from API, response: %d\n", code);
      if (code == 200) {
        String payload = http.getString();
        StaticJsonDocument<512> doc;
        auto err = deserializeJson(doc, payload);
        if (!err && doc["success"]) {
          JsonObject data = doc["data"];
          // Pull out every field, dynamic lengths guaranteed
          const char* newSSID      = data["ssid"];
          const char* newPASS      = data["password"];
          const char* newORG       = data["organisation"];
          const char* newPRODUCT   = data["product"];
          const char* newCLASS     = data["class"];
          const char* newSECTION   = data["section"];
          const char* newBATCH     = data["batch"];
        
          // Persist them all
          preferences.begin("wifiCreds", false);
            preferences.putString("ssid",      newSSID);
            preferences.putString("password",  newPASS);
            preferences.putString("org",       newORG);
            preferences.putString("product",   newPRODUCT);
            preferences.putString("class",     newCLASS);
            preferences.putString("section",   newSECTION);
            preferences.putString("batch",     newBATCH);
          preferences.end();
                  Serial.printf("New WiFi credentials saved: %s / %s\n", newSSID, newPASS);
        }

        showOLEDStatus("New WiFi Saved", "Rebooting...");
        delay(2000);
        ESP.restart();
      } else {
        Serial.println("Failed to fetch new WiFi credentials.");
        showOLEDStatus("Cred-API failed", "");
      }
      http.end();
    }
  } else {
      mp3.play(5);  // 🔊 Play "WiFi Failed" message (e.g., 0008.mp3)
    showOLEDStatus("WiFi Failed", "Restarting...");
    Serial.println("WiFi connect failed. Halting.");
    delay(2000);
    ESP.restart();
  }
}

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 init failed"));
    while (true) delay(1000);
  }
  showOLEDStatus("Booting...");

  // RFID Init
  SPI.begin();
  mfrc522.PCD_Init();

  // DFPlayer Init (before WiFi)
  mySerial.begin(9600, SERIAL_8N1, DF_RX, DF_TX);
  delay(100);
  if (!mp3.begin(mySerial)) {
    Serial.println("DFPlayer not found!");
  } else {
    mp3.volume(25);
    delay(2000);
    mp3.play(1);  // 🔊 Play a sound before WiFi
    Serial.println("DFPlayer initialized");
  }

  // Now connect to WiFi
  connectToWiFi();

  // NTP Time Init
  timeClient.begin();
  timeClient.update();
}

void loop() {
  static unsigned long lastCheck = 0;
  timeClient.update();

  if (digitalRead(BUTTON_PIN) == LOW) {
    clearWiFiAndRestart();
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected. Attempting reconnect...");
    showOLEDStatus("WiFi Lost", "Reconnecting...");
      mp3.play(4);  // 🔊 Play WiFi lost message
      delay(2000);  // Give time for the audio to play
    connectToWiFi();
  }

  if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
    char uidStr[12];
    sprintf(uidStr, "%02X:%02X:%02X:%02X",
            mfrc522.uid.uidByte[0], mfrc522.uid.uidByte[1],
            mfrc522.uid.uidByte[2], mfrc522.uid.uidByte[3]);
            mp3.play(3);  // 🔊 Play "Card Reading" sound
  delay(1000);  // Optional: Give some time for the sound
    showOLEDStatus("Scanned UID", uidStr, 500);
    Serial.printf("Card scanned: %s\n", uidStr);

    HTTPClient http;
    http.begin(attendAPI);
    http.addHeader("Content-Type", "application/json");
    preferences.begin("wifiCreds", false);
    String org     = preferences.getString("org",     "");
    String product = preferences.getString("product", "");
    String cls     = preferences.getString("class",   "");
    String sect    = preferences.getString("section", "");
    String batch   = preferences.getString("batch",   "");
    preferences.end();
    StaticJsonDocument<256> payloadDoc;
    payloadDoc["uid"]      = uidStr;
    payloadDoc["org"]      = org;
    payloadDoc["product"]  = product;
    payloadDoc["class"]    = cls;
    payloadDoc["section"]  = sect;
    payloadDoc["batch"]    = batch;
    String payload;
    serializeJson(payloadDoc, payload);
    int code = http.POST(payload);

    if (code > 0) {
      String resp = http.getString();
      Serial.printf("Server Response: %s\n", resp.c_str());

      StaticJsonDocument<256> doc;
      DeserializationError error = deserializeJson(doc, resp);

      if (!error) {
        bool success = doc["success"];
        if (success) {
          rfidStatus = "Access\nGranted";
          mp3.play(6);
        } else {
          rfidStatus = "Invalid\nCard";
          mp3.play(7);
        }
      } else {
        Serial.println("Failed to parse JSON");
        rfidStatus = "Parse\nError";
      }
    } else {
      Serial.println("Error connecting to server");
      rfidStatus = "Server\nError";
    }

    http.end();
    lastScan = millis();
    showStatus = true;
    mfrc522.PICC_HaltA();
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  if (showStatus && millis() - lastScan < 3000) {
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("RFID Status:");
    display.setTextSize(2);
    display.setCursor(0, 20);
    display.println(rfidStatus);
  } else {
    showStatus = false;
    String t = timeClient.getFormattedTime();
    time_t raw = timeClient.getEpochTime();
    struct tm* ti = localtime(&raw);
    char d[20];
    strftime(d, sizeof(d), "%d-%m-%Y", ti);
    display.setTextSize(2);
    display.setCursor(0, 0);
    display.println(t);
    display.setTextSize(1);
    display.setCursor(0, 35);
    display.println("Date:");
    display.setCursor(0, 45);
    display.println(d);
    display.setCursor(0, 55);
    display.println("Waiting for RFID...");
  }
  display.display();
  delay(500);
}