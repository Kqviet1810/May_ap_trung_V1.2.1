#pragma once
#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

// Tích hợp mẫu production-friendly: URL, device secret và CA certificate phải nằm ngoài source public.
class MayapCloudAlarm {
public:
  MayapCloudAlarm(const String& endpoint, const String& deviceId, const String& alarmSecret, const char* rootCa)
    : _endpoint(endpoint), _deviceId(deviceId), _alarmSecret(alarmSecret), _rootCa(rootCa) {}

  bool send(int code, float value, float temperature, float humidity, const String& message = "") {
    if (WiFi.status() != WL_CONNECTED || !_rootCa || !_rootCa[0]) return false;
    WiFiClientSecure client;
    client.setCACert(_rootCa);
    HTTPClient https;
    if (!https.begin(client, _endpoint)) return false;
    https.addHeader("Content-Type", "application/json");
    https.addHeader("Authorization", "Bearer " + _alarmSecret);
    String body = "{\"deviceId\":\"" + escape(_deviceId) + "\",\"code\":" + String(code) +
                  ",\"value\":" + String(value, 2) +
                  ",\"temperature\":" + String(temperature, 2) +
                  ",\"humidity\":" + String(humidity, 1) +
                  ",\"message\":\"" + escape(message) + "\"}";
    const int status = https.POST(body);
    https.end();
    return status >= 200 && status < 300;
  }

private:
  String _endpoint;
  String _deviceId;
  String _alarmSecret;
  const char* _rootCa;

  static String escape(const String& input) {
    String out;
    out.reserve(input.length() + 8);
    for (size_t i = 0; i < input.length(); ++i) {
      const char c = input[i];
      if (c == '\\' || c == '"') out += '\\';
      if (c == '\n') { out += "\\n"; continue; }
      if (c == '\r') continue;
      out += c;
    }
    return out;
  }
};

/*
Ví dụ:
MayapCloudAlarm cloudAlarm(
  "https://mayap.pages.dev/api/alarm",
  "MAP-A1B2C3D4E5F6",
  "mayap_<secret-duoc-admin-cap>",
  MAYAP_CLOUD_ROOT_CA
);

Khi firmware phát sinh alarm quan trọng:
cloudAlarm.send(41, 1, temperature, humidity, "Mất tín hiệu cảm biến");

Giữ MQTT hiện tại song song. HTTPS này chỉ dùng cho alarm cần Push khi PWA đã đóng.
Không dùng client.setInsecure() trong firmware thương mại.
*/
