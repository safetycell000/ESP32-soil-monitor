#include <Preferences.h>

Preferences preferences;

void setup() {
  Serial.begin(115200);
  delay(2000);
  
  Serial.println("=== ESP32-S3 WiFi Setup ===");
  Serial.println("Enter WiFi credentials:");
  
  String ssid = "";
  String password = "";
  
  // SSID入力
  Serial.print("WiFi SSID: ");
  while (ssid.length() == 0) {
    if (Serial.available()) {
      ssid = Serial.readStringUntil('\n');
      ssid.trim();
    }
    delay(100);
  }
  Serial.println(ssid);
  
  // パスワード入力
  Serial.print("WiFi Password: ");
  while (password.length() == 0) {
    if (Serial.available()) {
      password = Serial.readStringUntil('\n');
      password.trim();
    }
    delay(100);
  }
  Serial.println("****** (hidden)");
  
  // NVSに保存
  preferences.begin("wifi-config", false);
  preferences.putString("ssid", ssid);
  preferences.putString("password", password);
  preferences.end();
  
  Serial.println("\n=== WiFi credentials saved to NVS ===");
  Serial.println("Setup complete! You can now upload the main program.");
  
  // 保存確認
  preferences.begin("wifi-config", true);
  String savedSSID = preferences.getString("ssid", "");
  preferences.end();
  
  if (savedSSID == ssid) {
    Serial.println("✓ Verification successful");
  } else {
    Serial.println("✗ Verification failed");
  }
}

void loop() {
  // 何もしない
}