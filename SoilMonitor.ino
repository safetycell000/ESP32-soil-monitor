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
      Serial.println("Wake-up: Power-on reset (initial boot)");
      break;
    default:
      Serial.printf("Wake-up: Other reason (%d)\n", wakeup_reason);
      break;
  }
  
  // ハードウェア初期化
  initializeHardware();
  
  // 初回起動時は即座にスリープ
  if (wakeup_reason == ESP_SLEEP_WAKEUP_UNDEFINED) {
    Serial.println("First boot detected - going to sleep immediately");
    performInitialSleep();
    return;
  }
  
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
  Serial.println("\n" + String('=', 60));
  Serial.println("           SEN0193 SENSOR CALIBRATION");
  Serial.println(String('=', 60));
  Serial.println("DFRobot Capacitive Soil Moisture Sensor calibration.");
  Serial.println("IMPORTANT: Do NOT submerge beyond the red line!");
  Serial.println("GPIO Pin: " + String(SOIL_SENSOR_PIN) + " (ADC1_CH8)");
  Serial.println("ADC Resolution: 12-bit (0-4095)");
  Serial.println("ADC Attenuation: 11dB (0-3.3V)");
  
  Serial.println("\n=== SMART CALIBRATION MODE ===");
  Serial.println("System will automatically detect DRY or WET state");
  Serial.println("Detection thresholds:");
  Serial.println("- DRY state:  >= 2000 (air exposure)");
  Serial.println("- WET state:  <= 1800 (water immersion)");
  Serial.println("- Unclear:    1801-1999 (soil/intermediate)");
  
  // 校正前の既存値を表示
  preferences.begin("sensor-config", true);
  int oldDryValue = preferences.getInt("dry_value", 2800);
  int oldWetValue = preferences.getInt("wet_value", 1300);
  preferences.end();
  
  Serial.println("\n📋 BEFORE CALIBRATION:");
  Serial.printf("Previous DRY value:  %d\n", oldDryValue);
  Serial.printf("Previous WET value:  %d\n", oldWetValue);
  Serial.printf("Previous range:      %d (span)\n", oldDryValue - oldWetValue);
  
  Serial.println("\n🔧 SENSOR STABILIZATION...");
  delay(2000); // 安定化待ち
  
  // 詳細測定（個別値も表示）
  Serial.println("\n📊 DETAILED MEASUREMENT (20 samples):");
  int readings[20];
  int totalValue = 0;
  int minVal = 4095, maxVal = 0;
  
  for (int i = 0; i < 20; i++) {
    readings[i] = analogRead(SOIL_SENSOR_PIN);
    totalValue += readings[i];
    if (readings[i] < minVal) minVal = readings[i];
    if (readings[i] > maxVal) maxVal = readings[i];
    
    Serial.printf("Sample %2d: %4d", i + 1, readings[i]);
    if ((i + 1) % 5 == 0) Serial.println(); // 5個ずつ改行
    else Serial.print("  ");
    
    delay(100);
  }
  
  int currentValue = totalValue / 20;
  int variance = maxVal - minVal;
  
  Serial.println("\n📈 MEASUREMENT STATISTICS:");
  Serial.printf("Average value:    %d\n", currentValue);
  Serial.printf("Minimum reading:  %d\n", minVal);
  Serial.printf("Maximum reading:  %d\n", maxVal);
  Serial.printf("Variance (max-min): %d\n", variance);
  Serial.printf("Stability: %s\n", variance < 50 ? "GOOD" : variance < 100 ? "FAIR" : "POOR");
  
  // 自動判定ロジック
  Serial.println("\n🤖 AUTOMATIC STATE DETECTION:");
  Serial.printf("Current reading: %d\n", currentValue);
  
  if (currentValue >= 2000) {
    // DRY状態として校正
    Serial.println("🌬️  DETECTED: DRY state (air exposure)");
    Serial.printf("Condition: %d >= 2000 ✅\n", currentValue);
    Serial.printf("Setting DRY calibration value: %d\n", currentValue);
    
    preferences.begin("sensor-config", false);
    preferences.putInt("dry_value", currentValue);
    preferences.end();
    
    Serial.println("💾 DRY calibration saved to NVS!");
    Serial.printf("Change: %d → %d (diff: %+d)\n", oldDryValue, currentValue, currentValue - oldDryValue);
    
  } else if (currentValue <= 1800) {
    // WET状態として校正
    Serial.println("💧 DETECTED: WET state (water immersion)");
    Serial.printf("Condition: %d <= 1800 ✅\n", currentValue);
    Serial.printf("Setting WET calibration value: %d\n", currentValue);
    
    preferences.begin("sensor-config", false);
    preferences.putInt("wet_value", currentValue);
    preferences.end();
    
    Serial.println("💾 WET calibration saved to NVS!");
    Serial.printf("Change: %d → %d (diff: %+d)\n", oldWetValue, currentValue, currentValue - oldWetValue);
    
  } else {
    // 中間値：判定不可
    Serial.println("❓ UNCLEAR STATE: Value in middle range");
    Serial.printf("Condition: 1800 < %d < 2000 ❌\n", currentValue);
    Serial.println("📋 CALIBRATION GUIDANCE:");
    Serial.println("  For DRY calibration:");
    Serial.println("  - Remove sensor from soil");
    Serial.println("  - Expose to air for 30+ seconds");
    Serial.println("  - Expected reading: >2000");
    Serial.println("  For WET calibration:");
    Serial.println("  - Submerge sensor tip in water");
    Serial.println("  - Up to red line only (not electronics!)");
    Serial.println("  - Expected reading: <1800");
    Serial.println("❌ No calibration performed this time.");
  }
  
  // 最終校正値を表示
  preferences.begin("sensor-config", true);
  int finalDryValue = preferences.getInt("dry_value", 2800);
  int finalWetValue = preferences.getInt("wet_value", 1300);
  preferences.end();
  
  Serial.println("\n" + String('-', 60));
  Serial.println("📊 FINAL CALIBRATION VALUES:");
  Serial.printf("DRY value (0%% moisture):   %d\n", finalDryValue);
  Serial.printf("WET value (100%% moisture): %d\n", finalWetValue);
  Serial.printf("Calibration range:          %d\n", finalDryValue - finalWetValue);
  Serial.printf("Range quality: %s\n", (finalDryValue - finalWetValue) > 1000 ? "EXCELLENT" : 
                                       (finalDryValue - finalWetValue) > 500 ? "GOOD" : "POOR");
  
  // バリデーション
  if (finalDryValue <= finalWetValue) {
    Serial.println("⚠️  CRITICAL WARNING: Invalid calibration range!");
    Serial.println("   DRY value must be higher than WET value");
    Serial.println("   Current: DRY=" + String(finalDryValue) + " <= WET=" + String(finalWetValue));
    Serial.println("   Please recalibrate properly!");
  } else {
    Serial.println("✅ Calibration range validation: PASSED");
  }
  
  // 現在値での湿度計算例
  if (finalDryValue > finalWetValue) {
    float currentMoisture = map(currentValue, finalDryValue, finalWetValue, 0, 100);
    currentMoisture = constrain(currentMoisture, 0, 100);
    Serial.printf("📱 Current moisture (with new calibration): %.1f%%\n", currentMoisture);
  }
  
  Serial.println("\n✅ CALIBRATION PROCESS COMPLETE!");
  Serial.println("Timestamp: " + String(millis()) + "ms since boot");
  Serial.println("Going to sleep for 5 minutes...");
  Serial.println("Will check schedule and resume normal operation on wake-up.");
  Serial.println(String('=', 60));
  
  // 校正完了後は5分スリープ（シンプル・高速）
  delay(2000); // 2秒でログ確認時間
  goToSleep(300); // 5分 = 300秒
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

void performInitialSleep() {
  Serial.println("=== Initial Boot Sleep Mode ===");
  Serial.println("System will sleep until next scheduled measurement time");
  
  // 初期設定チェック
  if (!checkInitialSetup()) {
    Serial.println("Initial setup required - sleeping for 1 hour");
    goToSleep(3600); // 1時間後に再起動
    return;
  }
  
  // WiFi接続して時刻取得
  if (connectToWiFi()) {
    int sleepSeconds = calculateNextWakeTime();
    Serial.printf("Calculated sleep time: %d seconds\n", sleepSeconds);
    goToSleep(sleepSeconds);
  } else {
    Serial.println("WiFi failed - using default 30min sleep");
    goToSleep(DEFAULT_SLEEP_SECONDS);
  }
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