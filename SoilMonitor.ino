#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "esp_sleep.h"

// ハードウェア設定
const int SOIL_SENSOR_PIN = 9;  // GPIO9 (ADC1_CH8)
const int BUTTON_PIN = 2;       // GPIO2 (プルアップ内蔵)

// システム設定
const int DEFAULT_SLEEP_SECONDS = 1800;  // 30分 (NTP失敗時のデフォルト)
const int WIFI_MAX_ATTEMPTS = 3;         // WiFi接続試行回数
const int BUTTON_SHORT_PRESS_MS = 1000;  // 短押し/長押しの境界 (1秒)

// GitHub設定
const char* GITHUB_USER = "safetycell000";
const char* GITHUB_REPO = "ESP32-soil-monitor";
const String GITHUB_WEBHOOK_URL = "https://api.github.com/repos/" + String(GITHUB_USER) + "/" + String(GITHUB_REPO) + "/dispatches";

// グローバル変数
Preferences preferences;
WiFiClient wifiClient;
HTTPClient http;
volatile bool buttonPressed = false;
unsigned long buttonPressTime = 0;

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n=== ESP32-S3 Soil Monitor v2.0 ===");
  
  // ウェイクアップ理由をログ出力
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  switch(wakeup_reason) {
    case ESP_SLEEP_WAKEUP_EXT0:
      Serial.println("Wake-up: Button press (GPIO2)");
      break;
    case ESP_SLEEP_WAKEUP_TIMER:
      Serial.println("Wake-up: Timer (scheduled)");
      break;
    case ESP_SLEEP_WAKEUP_UNDEFINED:
      Serial.println("Wake-up: Power-on reset");
      break;
    default:
      Serial.printf("Wake-up: Other reason (%d)\n", wakeup_reason);
      break;
  }
  
  // ハードウェア初期化
  initializeHardware();
  
  // ボタン押下チェック
  if (checkButtonPress()) {
    return; // ボタン処理完了後は関数終了
  }
  
  // 通常の計測・送信処理
  performNormalOperation();
}

void loop() {
  // Deep Sleepから復帰時はsetup()から再実行されるため、loop()は使用しない
}

void initializeHardware() {
  // ADC設定 (SEN0193用: 0-3.0V出力)
  analogSetAttenuation(ADC_11db);  // 3.3V入力範囲
  analogReadResolution(12);        // 12bit resolution (0-4095)
  
  // ボタン設定
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  
  Serial.println("Hardware initialized.");
}

bool checkButtonPress() {
  // ボタン押下チェック（起動後100ms以内）
  unsigned long startTime = millis();
  while (millis() - startTime < 100) {
    if (digitalRead(BUTTON_PIN) == LOW) {
      Serial.println("Button press detected!");
      return handleButtonOperation();
    }
    delay(10);
  }
  return false;
}

bool handleButtonOperation() {
  // ボタン押下時間測定
  unsigned long pressStart = millis();
  while (digitalRead(BUTTON_PIN) == LOW) {
    delay(10);
  }
  unsigned long pressTime = millis() - pressStart;
  
  Serial.printf("Button pressed for %lu ms\n", pressTime);
  
  if (pressTime < BUTTON_SHORT_PRESS_MS) {
    // 短押し: 臨時計測
    Serial.println("=== Manual Measurement ===");
    performManualMeasurement();
  } else {
    // 長押し: 校正モード
    Serial.println("=== Calibration Mode ===");
    performCalibration();
  }
  
  return true;
}

void performManualMeasurement() {
  // WiFi接続
  if (!connectToWiFi()) {
    Serial.println("WiFi connection failed. Manual measurement aborted.");
    goToSleep(DEFAULT_SLEEP_SECONDS);
    return;
  }
  
  // センサー読み取り
  int soilMoisture = readSoilMoisture();
  Serial.printf("Manual measurement: %d\n", soilMoisture);
  
  // GitHubにデータ送信
  if (sendDataToGitHub(soilMoisture)) {
    Serial.println("Manual data sent successfully!");
  } else {
    Serial.println("Failed to send manual data");
  }
  
  // 次の定期実行時刻まで計算してスリープ
  int sleepSeconds = calculateNextWakeTime();
  goToSleep(sleepSeconds);
}

void performCalibration() {
  Serial.println("\n=== SEN0193 Sensor Calibration ===");
  Serial.println("DFRobot Capacitive Soil Moisture Sensor calibration.");
  Serial.println("IMPORTANT: Do NOT submerge beyond the red line!");
  
  Serial.println("\n1. DRY CALIBRATION (Higher values)");
  Serial.println("Remove sensor from soil and expose to air.");
  Serial.println("Expected range: 2600-3000");
  Serial.println("Press button when ready...");
  
  waitForButtonPress();
  
  int dryValue = 0;
  Serial.print("Measuring dry value");
  for (int i = 0; i < 20; i++) {
    dryValue += analogRead(SOIL_SENSOR_PIN);
    Serial.print(".");
    delay(100);
  }
  dryValue /= 20;
  
  Serial.printf("\nDry value: %d\n", dryValue);
  
  Serial.println("\n2. WET CALIBRATION (Lower values)");
  Serial.println("Submerge sensor tip in water up to RED LINE (not the electronics!)");
  Serial.println("Expected range: 1200-1500");
  Serial.println("Press button when ready...");
  
  waitForButtonPress();
  
  int wetValue = 0;
  Serial.print("Measuring wet value");
  for (int i = 0; i < 20; i++) {
    wetValue += analogRead(SOIL_SENSOR_PIN);
    Serial.print(".");
    delay(100);
  }
  wetValue /= 20;
  
  Serial.printf("\nWet value: %d\n", wetValue);
  
  // NVSに保存
  preferences.begin("sensor-config", false);
  preferences.putInt("dry_value", dryValue);
  preferences.putInt("wet_value", wetValue);
  preferences.end();
  
  Serial.println("\n✓ Calibration saved to NVS");
  Serial.printf("Range: %d (0%%) to %d (100%%)\n", dryValue, wetValue);
  Serial.println("Calibration complete. Entering normal operation mode...");
  
  // 校正完了後は通常動作に移行
  delay(2000);
  performNormalOperation();
}

void waitForButtonPress() {
  while (digitalRead(BUTTON_PIN) == HIGH) {
    delay(100);
  }
  // ボタンが離されるまで待機
  while (digitalRead(BUTTON_PIN) == LOW) {
    delay(100);
  }
  delay(200); // チャタリング対策
}

void performNormalOperation() {
  Serial.println("=== Normal Operation Mode ===");
  
  // 初回設定チェック
  if (!checkInitialSetup()) {
    return;
  }
  
  // WiFi接続
  if (!connectToWiFi()) {
    Serial.println("WiFi connection failed, going to sleep...");
    goToSleep(DEFAULT_SLEEP_SECONDS);
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
  
  // 次の定期実行時刻まで計算してスリープ
  int sleepSeconds = calculateNextWakeTime();
  goToSleep(sleepSeconds);
}

bool checkInitialSetup() {
  // WiFi設定チェック
  preferences.begin("wifi-config", true);
  String ssid = preferences.getString("ssid", "");
  String password = preferences.getString("password", "");
  preferences.end();
  
  // GitHub設定チェック
  preferences.begin("github-config", true);
  String token = preferences.getString("token", "");
  preferences.end();
  
  if (ssid.length() == 0 || password.length() == 0 || token.length() == 0) {
    Serial.println("\n=== Initial Setup Required ===");
    Serial.println("Please configure WiFi and GitHub settings:");
    performInitialSetup();
    return false;
  }
  
  return true;
}

void performInitialSetup() {
  Serial.println("\n--- WiFi Configuration ---");
  Serial.print("Enter WiFi SSID: ");
  String ssid = readSerialInput();
  
  Serial.print("Enter WiFi Password: ");
  String password = readSerialInput();
  
  Serial.println("\n--- GitHub Configuration ---");
  Serial.print("Enter GitHub Personal Access Token: ");
  String token = readSerialInput();
  
  // NVSに保存
  preferences.begin("wifi-config", false);
  preferences.putString("ssid", ssid);
  preferences.putString("password", password);
  preferences.end();
  
  preferences.begin("github-config", false);
  preferences.putString("token", token);
  preferences.end();
  
  Serial.println("\n✓ Configuration saved!");
  Serial.println("Restarting system...");
  delay(2000);
  ESP.restart();
}

String readSerialInput() {
  String input = "";
  while (input.length() == 0) {
    if (Serial.available()) {
      input = Serial.readStringUntil('\n');
      input.trim();
    }
    delay(100);
  }
  return input;
}

bool connectToWiFi() {
  // NVSからWiFi情報読み取り
  preferences.begin("wifi-config", true);
  String ssid = preferences.getString("ssid", "");
  String password = preferences.getString("password", "");
  preferences.end();
  
  if (ssid.length() == 0 || password.length() == 0) {
    Serial.println("WiFi credentials not found in NVS");
    return false;
  }
  
  Serial.printf("Connecting to WiFi: %s\n", ssid.c_str());
  
  // WiFi接続試行 (最大3回)
  for (int attempt = 1; attempt <= WIFI_MAX_ATTEMPTS; attempt++) {
    Serial.printf("Attempt %d/%d: ", attempt, WIFI_MAX_ATTEMPTS);
    
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
      Serial.printf("Attempt %d failed (Status: %d)\n", attempt, WiFi.status());
      WiFi.disconnect();
      delay(1000);
    }
  }
  
  Serial.println("WiFi connection failed after 3 attempts");
  return false;
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
    Serial.println("GitHub token not found.");
    return false;
  }
  
  // NTP時刻同期
  configTime(9 * 3600, 0, "pool.ntp.org");
  delay(5000); // NTP同期待ち (5秒)
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

int calculateNextWakeTime() {
  // NTP時刻同期
  configTime(9 * 3600, 0, "pool.ntp.org");
  delay(5000);
  time_t now = time(nullptr);
  
  // NTP同期失敗チェック
  if (now < 1000000000) {  // 無効な時刻
    Serial.println("NTP sync failed, using default 30min sleep");
    return DEFAULT_SLEEP_SECONDS;
  }
  
  // 現在時刻を取得
  struct tm* timeinfo = localtime(&now);
  int currentMinute = timeinfo->tm_min;
  int currentSecond = timeinfo->tm_sec;
  
  // 次の00分または30分までの時間を計算
  int targetMinute;
  if (currentMinute < 30) {
    targetMinute = 30;
  } else {
    targetMinute = 60; // 次の時間の00分
  }
  
  int minutesToTarget = targetMinute - currentMinute;
  int secondsToTarget = (minutesToTarget * 60) - currentSecond;
  
  // 60分を超える場合（次の時間の00分）
  if (minutesToTarget == 60) {
    secondsToTarget = (60 - currentMinute) * 60 - currentSecond;
  }
  
  Serial.printf("Current time: %02d:%02d:%02d\n", timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
  Serial.printf("Next wake in %d seconds (at :%02d:00)\n", secondsToTarget, targetMinute % 60);
  
  return secondsToTarget;
}

void goToSleep(int sleepSeconds) {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  
  // ボタン状態の詳細ログ
  int buttonState = digitalRead(BUTTON_PIN);
  Serial.printf("GPIO2 button state before sleep: %s (%d)\n", 
                buttonState == HIGH ? "HIGH" : "LOW", buttonState);
  
  // ボタンによるウェイクアップを有効化
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_2, 0); // GPIO2, LOW (ボタン押下)
  Serial.println("Button wake-up enabled");
  
  // タイマーによるウェイクアップを設定
  esp_sleep_enable_timer_wakeup(sleepSeconds * 1000000ULL);  // マイクロ秒単位
  
  Serial.printf("Entering Deep Sleep for %d seconds...\n", sleepSeconds);
  Serial.flush();
  
  esp_deep_sleep_start();
}