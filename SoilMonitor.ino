#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "esp_sleep.h"

// センサー設定
const int SOIL_SENSOR_PIN = 9;  // GPIO9 (ADC1_CH8)

// Deep Sleep設定 (30分 = 1800秒)
const int SLEEP_TIME_SECONDS = 1800;

// GitHub設定
const char* GITHUB_USER = "safetycell000";
const char* GITHUB_REPO = "ESP32-soil-monitor";
const String GITHUB_WEBHOOK_URL = "https://api.github.com/repos/" + String(GITHUB_USER) + "/" + String(GITHUB_REPO) + "/dispatches";

Preferences preferences;
WiFiClient wifiClient;
HTTPClient http;

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("=== ESP32-S3 Soil Monitor Starting ===");
  
  // ADC設定 (SEN0193用: 0-3.0V出力)
  analogSetAttenuation(ADC_11db);  // 3.3V入力範囲
  analogReadResolution(12);        // 12bit resolution (0-4095)
  
  // WiFi接続
  if (!connectToWiFi()) {
    Serial.println("WiFi connection failed, going to sleep...");
    goToSleep();
    return;
  }
  
  // 土壌湿度測定
  int soilMoisture = readSoilMoisture();
  Serial.printf("Soil Moisture: %d\n", soilMoisture);
  
  // GitHubにデータ送信
  if (sendDataToGitHub(soilMoisture)) {
    Serial.println("Data sent successfully!");
  } else {
    Serial.println("Failed to send data");
  }
  
  // Deep Sleepに移行
  Serial.printf("Going to sleep for %d minutes...\n", SLEEP_TIME_SECONDS / 60);
  goToSleep();
}

void loop() {
  // Deep Sleepから復帰時はsetup()から再実行されるため、loop()は使用しない
}

bool connectToWiFi() {
  // NVSからWiFi情報読み取り
  preferences.begin("wifi-config", true);
  String ssid = preferences.getString("ssid", "");
  String password = preferences.getString("password", "");
  preferences.end();
  
  if (ssid.length() == 0 || password.length() == 0) {
    Serial.println("WiFi credentials not found in NVS");
    Serial.println("Please run WiFi_Setup.ino first");
    return false;
  }
  
  Serial.printf("Connecting to WiFi: %s\n", ssid.c_str());
  WiFi.begin(ssid.c_str(), password.c_str());
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  Serial.println();
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("Connected! IP: %s\n", WiFi.localIP().toString().c_str());
    return true;
  } else {
    Serial.println("WiFi connection timeout");
    return false;
  }
}

int readSoilMoisture() {
  // 複数回読み取りの平均値を計算（ノイズ除去）
  int totalValue = 0;
  const int numReadings = 10;
  
  for (int i = 0; i < numReadings; i++) {
    totalValue += analogRead(SOIL_SENSOR_PIN);
    delay(100);
  }
  
  return totalValue / numReadings;
}

bool sendDataToGitHub(int soilMoisture) {
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }
  
  // NVSからGitHubトークン取得
  preferences.begin("github-config", true);
  String token = preferences.getString("token", "");
  preferences.end();
  
  if (token.length() == 0) {
    Serial.println("GitHub token not found. Please run WiFi_Setup.ino first.");
    return false;
  }
  
  // 現在時刻取得 (WiFi接続後にNTPで同期)
  configTime(9 * 3600, 0, "pool.ntp.org");
  delay(1000); // NTP同期待ち
  time_t now = time(nullptr);
  
  // 湿度パーセント計算 (SEN0193: 逆相関)
  preferences.begin("sensor-config", true);
  int dryValue = preferences.getInt("dry_value", 2800);  // SEN0193空気中の典型値
  int wetValue = preferences.getInt("wet_value", 1300);  // SEN0193水中の典型値
  preferences.end();
  
  float moisturePercent = map(soilMoisture, dryValue, wetValue, 0, 100);
  moisturePercent = constrain(moisturePercent, 0, 100);
  
  // GitHub Actions webhook用のJSONペイロード作成
  http.begin(GITHUB_WEBHOOK_URL);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Accept", "application/vnd.github.v3+json");
  http.addHeader("Authorization", "token " + token);
  http.addHeader("User-Agent", "ESP32-SoilMonitor");
  
  // 超シンプルなJSONペイロード
  StaticJsonDocument<300> doc;
  doc["event_type"] = "soil_data";
  
  JsonObject payload = doc.createNestedObject("client_payload");
  payload["raw_value"] = soilMoisture;
  payload["moisture_percent"] = round(moisturePercent * 10) / 10.0;
  payload["timestamp"] = now;
  
  String jsonString;
  serializeJson(doc, jsonString);
  
  Serial.printf("Sending webhook: %s\n", jsonString.c_str());
  
  int httpResponseCode = http.POST(jsonString);
  
  if (httpResponseCode == 204) {
    Serial.println("GitHub webhook triggered successfully!");
    http.end();
    return true;
  } else {
    Serial.printf("HTTP Error: %d\n", httpResponseCode);
    String response = http.getString();
    Serial.printf("Response: %s\n", response.c_str());
    http.end();
    return false;
  }
}

void goToSleep() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  
  // Deep Sleep設定
  esp_sleep_enable_timer_wakeup(SLEEP_TIME_SECONDS * 1000000);  // マイクロ秒単位
  
  Serial.println("Entering Deep Sleep...");
  Serial.flush();
  
  esp_deep_sleep_start();
}