#include <Arduino.h>
#include <ShiftRegister74HC595.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

const char* ssid = "";
const char* password = "";

// API
const char* url = "https://nordpool.didnt.work/api/lv/prices?vat=false&resolution=60";

// relay pins
#define DATA_PIN    7
#define CLOCK_PIN   5
#define LATCH_PIN   6
#define ENABLE_PIN  4
#define RELAY_COUNT 6
#define DELAY_MS 60000

ShiftRegister74HC595<RELAY_COUNT> sr(DATA_PIN, CLOCK_PIN, LATCH_PIN);

int lastUpdateDay = -1;

struct HourPrice {
  int hour;
  float price;
};

// qsort compare
int comparePrices(const void *a, const void *b) {
  HourPrice *h1 = (HourPrice *)a;
  HourPrice *h2 = (HourPrice *)b;

  if (h1->price < h2->price) return -1;
  if (h1->price > h2->price) return 1;
  return 0;
}

HourPrice hours[24];
int priceCount = 0;

void setupTime() {
  configTime(2 * 3600, 0, "pool.ntp.org", "time.nist.gov"); // UTC+2

  struct tm timeinfo;
  while (!getLocalTime(&timeinfo)) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nTime synced");
}

bool fetchPrices() {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.begin(client, url);

  int code = http.GET();
  if (code <= 0) {
    Serial.println("HTTP failed");
    return false;
  }

  String payload = http.getString();
  http.end();

  DynamicJsonDocument doc(8192);
  if (deserializeJson(doc, payload)) {
    Serial.println("JSON parse error");
    return false;
  }

  JsonArray arr = doc["prices"].as<JsonArray>();
  priceCount = 0;

  for (JsonObject p : arr) {
    if (priceCount >= 24) break;

    float eurPerMWh = p["value"];
    float eurPerKWh = eurPerMWh / 1000.0;

    hours[priceCount].hour = priceCount;     // hour index
    hours[priceCount].price = eurPerKWh;     // price
    priceCount++;
  }

  if (priceCount != 24) {
    Serial.println("Not enough prices!");
    return false;
  }

  qsort(hours, 24, sizeof(HourPrice), comparePrices);

  const char* apiDate = doc["date"];
  Serial.print("API date: ");
  Serial.println(apiDate);

  for (int i = 0; i < priceCount; i++) {
  Serial.print(hours[i].hour);
  Serial.print(" | ");
  Serial.println(hours[i].price, 4);
  }
  
  return true;
}

void dailyUpdateIfNeeded() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return;

  if (timeinfo.tm_mday != lastUpdateDay) {
    lastUpdateDay = timeinfo.tm_mday;

    Serial.println("Daily price update");

    if (fetchPrices()) {
      Serial.println("New prices loaded");
    }
  }
}

int getGroupForHour(int currentHour) {
  for (int i = 0; i < 24; i++) {
    if (hours[i].hour == currentHour) {
      return i / 4;
    }
  }
  return -1;
}

void controlRelay() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return;

  int currentHour = timeinfo.tm_hour;

  int group = getGroupForHour(currentHour);

  if (group < 0) return;

  Serial.print("Current hour: ");
  Serial.print(currentHour);
  Serial.print(" | Group: ");
  Serial.println(group);

  for(size_t i = 0; i<=5; i++){
    if(group == i){
      sr.set(i, HIGH);
    }
    else{
      sr.set(i, LOW);
    }
  }
}

void setup() {
  delay(1000);
  Serial.begin(115200);

  Serial.println("\n");

  WiFi.setSleep(false);
  WiFi.begin(ssid, password);
  Serial.print("Connecting WiFi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");

  setupTime();

  pinMode(ENABLE_PIN, OUTPUT);
  digitalWrite(ENABLE_PIN, LOW);
  sr.setAllLow();

  if (fetchPrices()) {
    Serial.println("Prices ready. Group created.");
  }
}

void loop() {
  dailyUpdateIfNeeded();
  controlRelay();
  delay(DELAY_MS);
}
