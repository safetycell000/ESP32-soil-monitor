#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>

// センサー設定
const int SOIL_SENSOR_PIN = 9;  // GPIO9 (ADC1_CH8)
const int BUTTON_PIN = 2;       // GPIO2 (プルアップ内蔵)

// GitHub設定
const char* GITHUB_USER = "safetycell000";
const char* GITHUB_REPO = "ESP32-soil-monitor";
const String GITHUB_WEBHOOK_URL = "https://api.github.com/repos/" + String(GITHUB_USER) + "/" + String(GITHUB_REPO) + "/dispatches";

Preferences preferences;
WiFiClient wifiClient;
HTTPClient http;

void setup() {
  Serial.begin(115200);
  delay(2000);
  
  Serial.println("=== ESP32-S3 Soil Monitor Debug ===");
  
  // ADC設定 (SEN0193用: 0-3.0V出力)
  analogSetAttenuation(ADC_11db);  // 3.3V入力範囲
  analogReadResolution(12);        // 12bit resolution (0-4095)
  
  // ボタン設定
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  
  showMenu();
}

void loop() {
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    
    if (input == "1") {
      readSensor();
    } else if (input == "2") {
      calibrateSensor();
    } else if (input == "3") {
      testWiFiConnection();
    } else if (input == "4") {
      testGitHubWebhook();
    } else if (input == "5") {
      sendTestData();
    } else if (input == "6") {
      showStoredSettings();
    } else if (input == "7") {
      testNTPSync();
    } else if (input == "8") {
      testButtonOperations();
    } else if (input == "9") {
      testWakeTimeCalculation();
    } else if (input == "menu") {
      showMenu();
    } else {
      Serial.println("Invalid option. Type 'menu' to show options.");
    }
  }
  delay(100);
}

void showMenu() {
  Serial.println("\n" + String('=', 60));
  Serial.println("           ESP32-S3 Soil Monitor Debug Menu");
  Serial.println(String('=', 60));
  Serial.println("1. Read sensor   2. Calibrate    3. WiFi test     4. GitHub test");
  Serial.println("5. Send data     6. Settings     7. NTP sync      8. Button test");
  Serial.println("9. Wake calc     menu. Show menu");
  Serial.println(String('=', 60));
  Serial.print("Select option: ");
}

void readSensor() {
  Serial.println("\n=== Read Sensor Value ===");
  
  int rawValue = 0;
  const int numReadings = 10;
  
  Serial.println("Taking 10 readings...");
  for (int i = 0; i < numReadings; i++) {
    int reading = analogRead(SOIL_SENSOR_PIN);
    rawValue += reading;
    Serial.printf("Reading %d: %d\n", i + 1, reading);
    delay(100);
  }
  
  int avgRaw = rawValue / numReadings;
  Serial.printf("\nAverage raw value: %d\n", avgRaw);
  
  // 校正値を読み込んで湿度パーセント計算 (SEN0193: 逆相関)
  preferences.begin("sensor-config", true);
  int dryValue = preferences.getInt("dry_value", 2800);  // SEN0193空気中の典型値
  int wetValue = preferences.getInt("wet_value", 1300);  // SEN0193水中の典型値  
  preferences.end();
  
  float moisturePercent = map(avgRaw, dryValue, wetValue, 0, 100);
  moisturePercent = constrain(moisturePercent, 0, 100);
  
  Serial.printf("Moisture percent: %.1f%%\n", moisturePercent);
  Serial.printf("(Using calibration: Dry=%d, Wet=%d)\n", dryValue, wetValue);
  Serial.println(String('-', 40) + " Test Complete " + String('-', 40));
  
  showMenu();
}

void calibrateSensor() {
  Serial.println("\n=== SEN0193 Sensor Calibration ===");
  Serial.println("DFRobot Capacitive Soil Moisture Sensor calibration.");
  Serial.println("IMPORTANT: Do NOT submerge beyond the red line!");
  
  Serial.println("\n1. DRY CALIBRATION (Higher values)");
  Serial.println("Remove sensor from soil and expose to air.");
  Serial.println("Expected range: 2600-3000");
  Serial.print("Press Enter when ready...");
  
  while (!Serial.available()) {
    delay(100);
  }
  Serial.readStringUntil('\n'); // Clear input
  
  int dryValue = 0;
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
  Serial.print("Press Enter when ready...");
  
  while (!Serial.available()) {
    delay(100);
  }
  Serial.readStringUntil('\n'); // Clear input
  
  int wetValue = 0;
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
  Serial.println(String('-', 40) + " Calibration Complete " + String('-', 40));
  
  showMenu();
}

void testWiFiConnection() {
  Serial.println("\n=== Test WiFi Connection ===");
  
  // NVSからWiFi情報読み込み
  preferences.begin("wifi-config", true);
  String ssid = preferences.getString("ssid", "");
  String password = preferences.getString("password", "");
  preferences.end();
  
  if (ssid.length() == 0) {
    Serial.println("❌ No WiFi credentials found. Please run WiFi_Setup.ino first.");
    showMenu();
    return;
  }
  
  Serial.println("Saved SSID: " + ssid);
  Serial.println("Connecting...");
  
  WiFi.begin(ssid.c_str(), password.c_str());
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  Serial.println();
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("✅ WiFi connected successfully!");
    Serial.println("IP Address: " + WiFi.localIP().toString());
    Serial.println("Signal Strength: " + String(WiFi.RSSI()) + " dBm");
    
    // NTP時刻同期テスト
    Serial.println("Testing NTP time sync...");
    configTime(9 * 3600, 0, "pool.ntp.org");
    delay(2000);
    time_t now = time(nullptr);
    Serial.println("Current time: " + String(ctime(&now)));
    
  } else {
    Serial.println("❌ WiFi connection failed");
    Serial.println("Status: " + String(WiFi.status()));
  }
  Serial.println(String('-', 40) + " WiFi Test Complete " + String('-', 40));
  
  showMenu();
}

void testGitHubWebhook() {
  Serial.println("\n=== Test GitHub Webhook ===");
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("❌ WiFi not connected. Please test WiFi first (option 3).");
    showMenu();
    return;
  }
  
  // NVSからトークン読み込み
  preferences.begin("github-config", true);
  String token = preferences.getString("token", "");
  preferences.end();
  
  if (token.length() == 0) {
    Serial.println("❌ No GitHub token found. Please run WiFi_Setup.ino first.");
    showMenu();
    return;
  }
  
  Serial.println("Token: " + token.substring(0, 10) + "****** (testing webhook...)");
  
  // テスト用webhook送信
  http.begin(GITHUB_WEBHOOK_URL);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Accept", "application/vnd.github.v3+json");
  http.addHeader("Authorization", "token " + token);
  http.addHeader("User-Agent", "ESP32-SoilMonitor-Debug");
  
  StaticJsonDocument<300> doc;
  doc["event_type"] = "soil_data";
  
  JsonObject payload = doc.createNestedObject("client_payload");
  payload["raw_value"] = 9999;  // テストデータ
  payload["moisture_percent"] = 99.9;
  payload["timestamp"] = time(nullptr);
  payload["debug"] = true;
  
  String jsonString;
  serializeJson(doc, jsonString);
  
  Serial.printf("Sending test webhook: %s\n", jsonString.c_str());
  
  int httpResponseCode = http.POST(jsonString);
  
  if (httpResponseCode == 204) {
    Serial.println("✅ GitHub webhook sent successfully!");
    Serial.println("Check GitHub Actions tab in your repository to see if it triggered.");
  } else {
    Serial.printf("❌ HTTP Error: %d\n", httpResponseCode);
    String response = http.getString();
    Serial.printf("Response: %s\n", response.c_str());
  }
  
  http.end();
  Serial.println(String('-', 40) + " GitHub Test Complete " + String('-', 40));
  showMenu();
}

void sendTestData() {
  Serial.println("\n=== Send Test Data ===");
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("❌ WiFi not connected. Please test WiFi first (option 3).");
    showMenu();
    return;
  }
  
  // センサー読み取り
  int soilMoisture = readSoilMoisture();
  Serial.printf("Current sensor reading: %d\n", soilMoisture);
  
  // 実際のデータ送信
  if (sendDataToGitHub(soilMoisture)) {
    Serial.println("✅ Real data sent successfully!");
    Serial.println("Check your GitHub repository and dashboard!");
  } else {
    Serial.println("❌ Failed to send data");
  }
  Serial.println(String('-', 40) + " Send Test Complete " + String('-', 40));
  
  showMenu();
}

void showStoredSettings() {
  Serial.println("\n=== Stored Settings ===");
  
  // WiFi設定
  preferences.begin("wifi-config", true);
  String ssid = preferences.getString("ssid", "");
  preferences.end();
  
  Serial.println("WiFi SSID: " + (ssid.length() > 0 ? ssid : "Not set"));
  
  // GitHub設定
  preferences.begin("github-config", true);
  String token = preferences.getString("token", "");
  preferences.end();
  
  Serial.println("GitHub Token: " + (token.length() > 0 ? token.substring(0, 10) + "******" : "Not set"));
  
  // センサー校正
  preferences.begin("sensor-config", true);
  int dryValue = preferences.getInt("dry_value", 2800);
  int wetValue = preferences.getInt("wet_value", 1300);
  preferences.end();
  
  Serial.printf("Sensor Calibration: Dry=%d, Wet=%d\n", dryValue, wetValue);
  
  // システム情報
  Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());
  Serial.printf("WiFi status: %s\n", WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected");
  Serial.println(String('-', 40) + " Settings Complete " + String('-', 40));
  
  showMenu();
}

int readSoilMoisture() {
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
  
  // 現在時刻取得
  time_t now = time(nullptr);
  
  // 湿度パーセント計算
  preferences.begin("sensor-config", true);
  int dryValue = preferences.getInt("dry_value", 2800);
  int wetValue = preferences.getInt("wet_value", 1300);
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
  
  Serial.printf("Sending: %s\n", jsonString.c_str());
  
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

void testNTPSync() {
  Serial.println("\n=== Test NTP Time Sync ===");
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("❌ WiFi not connected. Please test WiFi first (option 3).");
    showMenu();
    return;
  }
  
  Serial.println("Testing NTP synchronization...");
  
  // 複数回テストして成功率を確認
  int successCount = 0;
  int totalTests = 5;
  
  for (int test = 1; test <= totalTests; test++) {
    Serial.printf("\nTest %d/%d: ", test, totalTests);
    
    // NTP同期実行
    configTime(9 * 3600, 0, "pool.ntp.org");
    delay(3000); // 3秒待機
    
    time_t now = time(nullptr);
    
    if (now > 1000000000) {  // 有効な時刻
      struct tm* timeinfo = localtime(&now);
      Serial.printf("✅ Success - %04d-%02d-%02d %02d:%02d:%02d JST\n", 
                   timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
                   timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
      successCount++;
    } else {
      Serial.printf("❌ Failed - Invalid timestamp: %ld\n", now);
    }
    
    delay(1000);
  }
  
  Serial.printf("\nNTP Sync Results: %d/%d successful (%.1f%%)\n", 
                successCount, totalTests, (float)successCount/totalTests * 100);
  
  if (successCount >= 4) {
    Serial.println("✅ NTP sync is working reliably");
  } else if (successCount >= 2) {
    Serial.println("⚠️ NTP sync is partially working - may need longer delay");
  } else {
    Serial.println("❌ NTP sync is failing - check internet connection");
  }
  Serial.println(String('-', 40) + " NTP Test Complete " + String('-', 40));
  
  showMenu();
}

void testButtonOperations() {
  Serial.println("\n=== Test Button Operations ===");
  Serial.println("GPIO2 Button Test");
  Serial.println("Press button for different durations:");
  Serial.println("- Short press (< 1 sec): Manual measurement simulation");
  Serial.println("- Long press (>= 1 sec): Calibration mode simulation");
  Serial.println("- Type 'stop' to exit test mode");
  
  while (true) {
    // シリアル入力チェック（終了条件）
    if (Serial.available()) {
      String input = Serial.readStringUntil('\n');
      input.trim();
      if (input == "stop") {
        Serial.println("Button test stopped.");
        Serial.println(String('-', 40) + " Button Test Complete " + String('-', 40));
        break;
      }
    }
    
    // ボタン押下チェック
    if (digitalRead(BUTTON_PIN) == LOW) {
      Serial.println("Button press detected! Measuring duration...");
      
      unsigned long pressStart = millis();
      while (digitalRead(BUTTON_PIN) == LOW) {
        delay(10);
      }
      unsigned long pressTime = millis() - pressStart;
      
      Serial.printf("Button pressed for %lu ms\n", pressTime);
      
      if (pressTime < 1000) {
        Serial.println("→ Short press detected: Would trigger manual measurement");
      } else {
        Serial.println("→ Long press detected: Would trigger calibration mode");
      }
      
      Serial.println("Ready for next button press...\n");
      delay(500); // デバウンス
    }
    
    delay(50);
  }
  
  showMenu();
}

void testWakeTimeCalculation() {
  Serial.println("\n=== Test Wake Time Calculation ===");
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("❌ WiFi not connected. Please test WiFi first (option 3).");
    showMenu();
    return;
  }
  
  // NTP時刻同期
  Serial.println("Syncing time...");
  configTime(9 * 3600, 0, "pool.ntp.org");
  delay(3000);
  time_t now = time(nullptr);
  
  if (now < 1000000000) {
    Serial.println("❌ NTP sync failed. Cannot test wake time calculation.");
    showMenu();
    return;
  }
  
  // 現在時刻表示
  struct tm* timeinfo = localtime(&now);
  Serial.printf("Current time: %02d:%02d:%02d\n", 
                timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
  
  // 次のウェイク時刻を計算
  int currentMinute = timeinfo->tm_min;
  int currentSecond = timeinfo->tm_sec;
  
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
  
  Serial.printf("Next target: :%02d:00\n", targetMinute % 60);
  Serial.printf("Sleep duration: %d seconds (%d minutes %d seconds)\n", 
                secondsToTarget, secondsToTarget / 60, secondsToTarget % 60);
  
  // 複数の時刻での計算例を表示
  Serial.println("\nCalculation examples for different times:");
  
  int testTimes[][2] = {{12, 5}, {12, 25}, {12, 35}, {12, 55}};
  
  for (int i = 0; i < 4; i++) {
    int testMin = testTimes[i][1];
    int testSec = testTimes[i][0];
    
    int testTarget;
    if (testMin < 30) {
      testTarget = 30;
    } else {
      testTarget = 60;
    }
    
    int testMinutesToTarget = testTarget - testMin;
    int testSecondsToTarget = (testMinutesToTarget * 60) - testSec;
    
    if (testMinutesToTarget == 60) {
      testSecondsToTarget = (60 - testMin) * 60 - testSec;
    }
    
    Serial.printf("  %02d:%02d:%02d → sleep %d sec → wake at :%02d:00\n",
                  testTimes[i][0], testMin, testSec,
                  testSecondsToTarget, testTarget % 60);
  }
  Serial.println(String('-', 40) + " Wake Calc Complete " + String('-', 40));
  
  showMenu();
}