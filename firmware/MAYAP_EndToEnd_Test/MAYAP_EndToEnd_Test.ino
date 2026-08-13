/*
  MAYAP End-to-End Test Firmware
  ESP32 -> HiveMQ Cloud (MQTT/TLS) -> PWA MAYAP
  ESP32 -> Cloudflare /api/alarm (HTTPS) -> Web Push

  Muc dich: firmware gia lap 1 may ap de test toan bo cloud/PWA truoc khi ghep logic may that.

  Thu vien Arduino Library Manager:
    - PubSubClient by Nick O'Leary
    - ArduinoJson by Benoit Blanchon

  Board: ESP32 Dev Module (hoac board ESP32 tuong duong)
*/

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <math.h>

// ============================================================================
// 1) CHI CAN DIEN CAC DONG NAY
// ============================================================================
static const char* WIFI_SSID     = "DIEN_WIFI_CUA_BAN";
static const char* WIFI_PASSWORD = "DIEN_MAT_KHAU_WIFI";

// HiveMQ Cloud: chi hostname, KHONG them mqtts://
// Vi du: xxxxxxxx.s1.eu.hivemq.cloud
static const char* HIVEMQ_HOST     = "DIEN_HIVEMQ_HOST";
static const uint16_t HIVEMQ_PORT  = 8883;
static const char* HIVEMQ_USERNAME = "DIEN_HIVEMQ_USERNAME";
static const char* HIVEMQ_PASSWORD = "DIEN_HIVEMQ_PASSWORD";

// Sau khi Cloudflare Pages deploy va Admin tao/gán may:
// Vi du: https://mayap-abc.pages.dev/api/alarm
static const char* CLOUD_ALARM_URL    = "DIEN_CLOUDFLARE_URL/api/alarm";
static const char* CLOUD_ALARM_SECRET = "DIEN_ALARM_SECRET_ADMIN_TRA_VE";

// = 1 de bring-up nhanh. CHI DUNG KHI TEST.
// Production phai = 0 va dien CA certificate phia duoi.
#define MAYAP_INSECURE_TLS_TEST 1

// Neu MAYAP_INSECURE_TLS_TEST = 0, dien root CA PEM tuong ung.
static const char HIVEMQ_ROOT_CA[] PROGMEM = R"EOF(
)EOF";
static const char CLOUDFLARE_ROOT_CA[] PROGMEM = R"EOF(
)EOF";

// De trong "" de tu sinh MAP-XXXXXXXXXXXX tu MAC ESP32.
// Neu muon co dinh, dung dung dang: MAP-A1B2C3D4E5F6
static const char* DEVICE_ID_OVERRIDE = "";

static const char* TOPIC_ROOT = "mayap/v1";
static const uint8_t TEST_BUTTON_PIN = 0; // nut BOOT: bam 1 lan de phat alarm test

WiFiClientSecure mqttTls;
PubSubClient mqtt(mqttTls);
String deviceId;
String baseTopic;
uint32_t bootId = 0;
uint32_t revision = 1;
uint32_t logSequence = 1;
uint32_t lastSnapshotMs = 0;
uint32_t lastPresenceMs = 0;
uint32_t lastReconnectAttemptMs = 0;
bool fastSession = false;
uint32_t fastSessionUntilMs = 0;
bool batchRunning = false;
bool resumeConfirmationRequired = false;
uint8_t autoTuneState = 0;
uint8_t autoTuneProgress = 0;
uint32_t autoTuneStartedMs = 0;
bool lastButton = HIGH;

struct MachineConfig {
  float targetTemp = 37.5;
  float tempHysteresis = 0.2;
  float lowTempAlarm = 36.5;
  float highTempAlarm = 38.2;
  float emergencyTemp = 39.0;
  float kp = 20.0;
  float ki = 0.5;
  float kd = 50.0;
  float lowHumidityAlarm = 40.0;
  float ventOnTemp = 38.0;
  float ventOffTemp = 37.6;
  float tempOffset = 0.0;
  float humidityOffset = 0.0;
  uint32_t pidCycleSec = 2;
  uint32_t humidityAlarmDelaySec = 60;
  uint32_t turnIntervalMin = 120;
  uint32_t turnMaxRunSec = 20;
  uint32_t powerRestoreDelaySec = 5;
  uint32_t sensorTimeoutSec = 10;
  float maxHeaterPower = 100.0;
  uint32_t totalIncubationDays = 21;
  bool circulationFanEnabled = true;
  bool turningEnabled = true;
  bool autoResumeAfterPower = true;
  bool allowHeatWithoutBatch = false;
  bool alarmEnabled = true;
  uint8_t controlMode = 0;
  uint8_t nextDirection = 0;
} cfg;

String topic(const char* suffix) { return baseTopic + "/" + suffix; }

String makeDeviceId() {
  if (strlen(DEVICE_ID_OVERRIDE) > 0) return String(DEVICE_ID_OVERRIDE);
  uint64_t mac = ESP.getEfuseMac();
  char buf[24];
  snprintf(buf, sizeof(buf), "MAP-%04X%08X", (uint16_t)(mac >> 32), (uint32_t)mac);
  return String(buf);
}

uint32_t epochNow() {
  time_t now = time(nullptr);
  return now > 1700000000 ? (uint32_t)now : 0;
}

void configureTls(WiFiClientSecure& client, const char* ca) {
#if MAYAP_INSECURE_TLS_TEST
  client.setInsecure();
#else
  if (ca && strlen(ca) > 100) client.setCACert(ca);
#endif
}

void fillConfig(JsonObject c) {
  c["targetTemp"] = cfg.targetTemp;
  c["tempHysteresis"] = cfg.tempHysteresis;
  c["lowTempAlarm"] = cfg.lowTempAlarm;
  c["highTempAlarm"] = cfg.highTempAlarm;
  c["emergencyTemp"] = cfg.emergencyTemp;
  c["kp"] = cfg.kp;
  c["ki"] = cfg.ki;
  c["kd"] = cfg.kd;
  c["lowHumidityAlarm"] = cfg.lowHumidityAlarm;
  c["ventOnTemp"] = cfg.ventOnTemp;
  c["ventOffTemp"] = cfg.ventOffTemp;
  c["tempOffset"] = cfg.tempOffset;
  c["humidityOffset"] = cfg.humidityOffset;
  c["pidCycleSec"] = cfg.pidCycleSec;
  c["humidityAlarmDelaySec"] = cfg.humidityAlarmDelaySec;
  c["turnIntervalMin"] = cfg.turnIntervalMin;
  c["turnMaxRunSec"] = cfg.turnMaxRunSec;
  c["powerRestoreDelaySec"] = cfg.powerRestoreDelaySec;
  c["sensorTimeoutSec"] = cfg.sensorTimeoutSec;
  c["maxHeaterPower"] = cfg.maxHeaterPower;
  c["totalIncubationDays"] = cfg.totalIncubationDays;
  c["circulationFanEnabled"] = cfg.circulationFanEnabled;
  c["turningEnabled"] = cfg.turningEnabled;
  c["autoResumeAfterPower"] = cfg.autoResumeAfterPower;
  c["allowHeatWithoutBatch"] = cfg.allowHeatWithoutBatch;
  c["alarmEnabled"] = cfg.alarmEnabled;
  c["controlMode"] = cfg.controlMode;
  c["nextDirection"] = cfg.nextDirection;
}

void applyConfig(JsonObjectConst c) {
  cfg.targetTemp = c["targetTemp"] | cfg.targetTemp;
  cfg.tempHysteresis = c["tempHysteresis"] | cfg.tempHysteresis;
  cfg.lowTempAlarm = c["lowTempAlarm"] | cfg.lowTempAlarm;
  cfg.highTempAlarm = c["highTempAlarm"] | cfg.highTempAlarm;
  cfg.emergencyTemp = c["emergencyTemp"] | cfg.emergencyTemp;
  cfg.kp = c["kp"] | cfg.kp;
  cfg.ki = c["ki"] | cfg.ki;
  cfg.kd = c["kd"] | cfg.kd;
  cfg.lowHumidityAlarm = c["lowHumidityAlarm"] | cfg.lowHumidityAlarm;
  cfg.ventOnTemp = c["ventOnTemp"] | cfg.ventOnTemp;
  cfg.ventOffTemp = c["ventOffTemp"] | cfg.ventOffTemp;
  cfg.tempOffset = c["tempOffset"] | cfg.tempOffset;
  cfg.humidityOffset = c["humidityOffset"] | cfg.humidityOffset;
  cfg.pidCycleSec = c["pidCycleSec"] | cfg.pidCycleSec;
  cfg.humidityAlarmDelaySec = c["humidityAlarmDelaySec"] | cfg.humidityAlarmDelaySec;
  cfg.turnIntervalMin = c["turnIntervalMin"] | cfg.turnIntervalMin;
  cfg.turnMaxRunSec = c["turnMaxRunSec"] | cfg.turnMaxRunSec;
  cfg.powerRestoreDelaySec = c["powerRestoreDelaySec"] | cfg.powerRestoreDelaySec;
  cfg.sensorTimeoutSec = c["sensorTimeoutSec"] | cfg.sensorTimeoutSec;
  cfg.maxHeaterPower = c["maxHeaterPower"] | cfg.maxHeaterPower;
  cfg.totalIncubationDays = c["totalIncubationDays"] | cfg.totalIncubationDays;
  cfg.circulationFanEnabled = c["circulationFanEnabled"] | cfg.circulationFanEnabled;
  cfg.turningEnabled = c["turningEnabled"] | cfg.turningEnabled;
  cfg.autoResumeAfterPower = c["autoResumeAfterPower"] | cfg.autoResumeAfterPower;
  cfg.allowHeatWithoutBatch = c["allowHeatWithoutBatch"] | cfg.allowHeatWithoutBatch;
  cfg.alarmEnabled = c["alarmEnabled"] | cfg.alarmEnabled;
  cfg.controlMode = c["controlMode"] | cfg.controlMode;
  cfg.nextDirection = c["nextDirection"] | cfg.nextDirection;
}

template <typename TDoc>
bool mqttPublishJson(const String& t, TDoc& doc, bool retained = false) {
  if (!mqtt.connected()) return false;
  String out;
  serializeJson(doc, out);
  return mqtt.publish(t.c_str(), out.c_str(), retained);
}

void publishPresence(bool online = true) {
  StaticJsonDocument<384> doc;
  doc["v"] = 1;
  doc["deviceId"] = deviceId;
  doc["bootId"] = bootId;
  doc["online"] = online;
  doc["ssid"] = online ? WiFi.SSID() : "";
  doc["rssi"] = online ? WiFi.RSSI() : 0;
  doc["ip"] = online ? WiFi.localIP().toString() : "";
  mqttPublishJson(topic("presence"), doc, true);
}

void publishConfigReported() {
  StaticJsonDocument<3072> doc;
  doc["v"] = 1;
  doc["deviceId"] = deviceId;
  doc["bootId"] = bootId;
  doc["revision"] = revision;
  JsonObject c = doc.createNestedObject("config");
  fillConfig(c);
  mqttPublishJson(topic("config/reported"), doc, true);
}

void publishAck(const char* requestId, const char* result, const char* message) {
  StaticJsonDocument<512> doc;
  doc["v"] = 1;
  doc["deviceId"] = deviceId;
  doc["bootId"] = bootId;
  doc["requestId"] = requestId ? requestId : "";
  doc["result"] = result;
  doc["message"] = message;
  mqttPublishJson(topic("ack"), doc, false);
}

void publishLog(uint16_t code, float value = 0) {
  StaticJsonDocument<384> doc;
  doc["v"] = 1;
  doc["deviceId"] = deviceId;
  doc["bootId"] = bootId;
  doc["sequence"] = logSequence++;
  doc["epoch"] = epochNow();
  doc["code"] = code;
  doc["value"] = value;
  mqttPublishJson(topic("log"), doc, false);
}

void updateAutoTune() {
  if (autoTuneState != 1) return;
  const uint32_t elapsed = millis() - autoTuneStartedMs;
  uint32_t progress = elapsed / 300;
  autoTuneProgress = (uint8_t)(progress > 100 ? 100 : progress);
  if (autoTuneProgress >= 100) {
    autoTuneProgress = 100;
    autoTuneState = 2;
    cfg.kp = 18.5;
    cfg.ki = 0.7;
    cfg.kd = 42.0;
    revision++;
    publishConfigReported();
    publishLog(52, 1);
  }
}

void publishSnapshot() {
  updateAutoTune();
  const float phase = (millis() % 60000UL) / 60000.0f * 6.2831853f;
  const float temperature = cfg.targetTemp - 0.15f + 0.22f * sinf(phase);
  const float humidity = 58.0f + 3.0f * sinf(phase * 0.43f);
  const bool heaterOn = batchRunning && temperature < cfg.targetTemp;
  const bool ventOn = temperature >= cfg.ventOnTemp;

  StaticJsonDocument<1536> doc;
  doc["v"] = 1;
  doc["deviceId"] = deviceId;
  doc["bootId"] = bootId;
  doc["revision"] = revision;
  JsonObject r = doc.createNestedObject("runtime");
  r["temperature"] = temperature + cfg.tempOffset;
  r["humidity"] = humidity + cfg.humidityOffset;
  r["machineState"] = batchRunning ? "Dang ap (simulator)" : "San sang (simulator)";
  r["batchRunning"] = batchRunning;
  r["heaterOn"] = heaterOn;
  r["heaterPower"] = heaterOn ? 55 : 0;
  r["circulationFanOn"] = cfg.circulationFanEnabled;
  r["ventFanOn"] = ventOn;
  r["turnState"] = 3;
  r["currentDay"] = batchRunning ? 1 : 0;
  r["nextTurnMinutes"] = cfg.turnIntervalMin;
  r["autoTuneState"] = autoTuneState;
  r["autoTuneProgress"] = autoTuneProgress;
  r["resumeConfirmationRequired"] = resumeConfirmationRequired;
  mqttPublishJson(topic("snapshot"), doc, false);
}

bool sendCloudAlarm(uint16_t code, float value, const char* message) {
  if (WiFi.status() != WL_CONNECTED) return false;
  if (String(CLOUD_ALARM_URL).startsWith("DIEN_") || String(CLOUD_ALARM_SECRET).startsWith("DIEN_")) {
    Serial.println("[PUSH] Chua dien CLOUD_ALARM_URL / CLOUD_ALARM_SECRET");
    return false;
  }

  WiFiClientSecure client;
  configureTls(client, CLOUDFLARE_ROOT_CA);
  HTTPClient https;
  if (!https.begin(client, CLOUD_ALARM_URL)) {
    Serial.println("[PUSH] https.begin failed");
    return false;
  }
  https.setTimeout(12000);
  https.addHeader("Content-Type", "application/json");
  https.addHeader("Authorization", String("Bearer ") + CLOUD_ALARM_SECRET);

  StaticJsonDocument<768> doc;
  doc["v"] = 1;
  doc["deviceId"] = deviceId;
  doc["bootId"] = bootId;
  doc["sequence"] = logSequence;
  doc["code"] = code;
  doc["value"] = value;
  doc["temperature"] = cfg.targetTemp + 0.3f;
  doc["humidity"] = 58.0f;
  doc["epoch"] = epochNow();
  doc["message"] = message;
  String body;
  serializeJson(doc, body);

  int status = https.POST(body);
  String response = https.getString();
  https.end();
  Serial.printf("[PUSH] HTTP %d -> %s\n", status, response.c_str());
  return status >= 200 && status < 300;
}

void handleConfigSet(JsonObjectConst root) {
  const char* requestId = root["requestId"] | "";
  uint32_t newRevision = root["revision"] | 0;
  JsonObjectConst c = root["config"].as<JsonObjectConst>();
  if (c.isNull()) {
    publishAck(requestId, "invalid", "CONFIG_INVALID");
    return;
  }
  if (newRevision && newRevision <= revision) {
    publishAck(requestId, "duplicate", "CONFIG_DUPLICATE");
    publishConfigReported();
    return;
  }
  applyConfig(c);
  revision = newRevision ? newRevision : revision + 1;
  publishAck(requestId, "applied", "CONFIG_APPLIED");
  publishConfigReported();
  publishLog(50, revision);
  publishSnapshot();
}

void handleCommand(JsonObjectConst root) {
  const char* requestId = root["requestId"] | "";
  const char* action = root["action"] | "";
  uint32_t requestedBoot = root["bootId"] | 0;
  if (requestedBoot && requestedBoot != bootId) {
    publishAck(requestId, "stale", "BOOT_ID_STALE");
    return;
  }

  if (!strcmp(action, "batch_start")) {
    batchRunning = true;
    publishAck(requestId, "applied", "BATCH_STARTED");
    publishLog(20, 1);
  } else if (!strcmp(action, "batch_stop")) {
    batchRunning = false;
    publishAck(requestId, "applied", "BATCH_STOPPED");
    publishLog(21, 0);
  } else if (!strcmp(action, "autotune_start")) {
    if (batchRunning) {
      publishAck(requestId, "rejected", "AUTOTUNE_BATCH_RUNNING");
    } else {
      autoTuneState = 1;
      autoTuneProgress = 0;
      autoTuneStartedMs = millis();
      publishAck(requestId, "applied", "AUTOTUNE_STARTED");
      publishLog(51, 1);
    }
  } else if (!strcmp(action, "resume_yes")) {
    resumeConfirmationRequired = false;
    batchRunning = true;
    publishAck(requestId, "applied", "RESUME_YES");
    publishLog(24, 1);
  } else if (!strcmp(action, "resume_no")) {
    resumeConfirmationRequired = false;
    batchRunning = false;
    publishAck(requestId, "applied", "RESUME_NO");
    publishLog(25, 0);
  } else {
    publishAck(requestId, "unsupported", "COMMAND_UNSUPPORTED");
  }
  publishSnapshot();
}

void mqttCallback(char* topicName, byte* payload, unsigned int length) {
  if (length > 4095) return;
  StaticJsonDocument<4096> doc;
  DeserializationError err = deserializeJson(doc, payload, length);
  if (err) return;
  const String t(topicName);
  JsonObjectConst root = doc.as<JsonObjectConst>();

  if (t == topic("session")) {
    bool active = root["active"] | false;
    uint32_t ttl = root["ttlMs"] | 15000;
    bool sync = root["sync"] | false;
    fastSession = active;
    fastSessionUntilMs = active ? millis() + ttl + 2000 : 0;
    if (sync) {
      publishPresence(true);
      publishConfigReported();
      publishSnapshot();
    }
  } else if (t == topic("config/set")) {
    handleConfigSet(root);
  } else if (t == topic("command")) {
    handleCommand(root);
  }
}

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("[WIFI] Connecting to %s", WIFI_SSID);
  uint32_t started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < 30000) {
    delay(400);
    Serial.print('.');
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[WIFI] OK IP=%s RSSI=%d\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
    configTime(7 * 3600, 0, "pool.ntp.org", "time.google.com");
  } else {
    Serial.println("[WIFI] FAILED");
  }
}

bool connectMqtt() {
  if (WiFi.status() != WL_CONNECTED) return false;
  if (String(HIVEMQ_HOST).startsWith("DIEN_")) {
    Serial.println("[MQTT] Chua dien HIVEMQ_HOST/credential");
    return false;
  }

  configureTls(mqttTls, HIVEMQ_ROOT_CA);
  mqtt.setServer(HIVEMQ_HOST, HIVEMQ_PORT);
  mqtt.setCallback(mqttCallback);
  mqtt.setBufferSize(4096);
  mqtt.setKeepAlive(60);
  mqtt.setSocketTimeout(10);

  String clientId = "mayap-esp32-" + deviceId.substring(4);
  String will = String("{\"v\":1,\"deviceId\":\"") + deviceId + "\",\"bootId\":" + bootId + ",\"online\":false}";
  bool ok = mqtt.connect(clientId.c_str(), HIVEMQ_USERNAME, HIVEMQ_PASSWORD,
                         topic("presence").c_str(), 1, true, will.c_str());
  if (!ok) {
    Serial.printf("[MQTT] Connect failed state=%d\n", mqtt.state());
    return false;
  }

  mqtt.subscribe(topic("session").c_str(), 1);
  mqtt.subscribe(topic("config/set").c_str(), 1);
  mqtt.subscribe(topic("command").c_str(), 1);
  publishPresence(true);
  publishConfigReported();
  publishSnapshot();
  publishLog(1, 1);
  Serial.printf("[MQTT] Connected to %s:%u\n", HIVEMQ_HOST, HIVEMQ_PORT);
  return true;
}

void printHelp() {
  Serial.println();
  Serial.println("===== MAYAP E2E TEST =====");
  Serial.printf("Device ID : %s\n", deviceId.c_str());
  Serial.printf("MQTT base : %s\n", baseTopic.c_str());
  Serial.println("A = Web Push alarm test");
  Serial.println("L = MQTT log test");
  Serial.println("B = toggle batch running");
  Serial.println("S = force sync");
  Serial.println("? = help");
  Serial.println("BOOT = MQTT log + Cloud Web Push test");
}

void handleSerial() {
  if (!Serial.available()) return;
  char c = toupper(Serial.read());
  if (c == 'A') sendCloudAlarm(41, 1, "TEST: Mat tin hieu cam bien");
  else if (c == 'L') publishLog(41, 1);
  else if (c == 'B') {
    batchRunning = !batchRunning;
    publishLog(batchRunning ? 20 : 21, batchRunning ? 1 : 0);
    publishSnapshot();
  } else if (c == 'S') {
    publishPresence(true);
    publishConfigReported();
    publishSnapshot();
  } else if (c == '?') printHelp();
}

void handleBootButton() {
  bool now = digitalRead(TEST_BUTTON_PIN);
  if (lastButton == HIGH && now == LOW) {
    Serial.println("[TEST] BOOT pressed -> MQTT log + Cloud alarm");
    publishLog(41, 1);
    sendCloudAlarm(41, 1, "TEST BOOT: Mat tin hieu cam bien");
  }
  lastButton = now;
}

void setup() {
  Serial.begin(115200);
  delay(500);
  pinMode(TEST_BUTTON_PIN, INPUT_PULLUP);
  randomSeed(esp_random());
  bootId = esp_random();
  if (bootId == 0) bootId = 1;
  deviceId = makeDeviceId();
  deviceId.toUpperCase();
  baseTopic = String(TOPIC_ROOT) + "/" + deviceId;
  printHelp();
  connectWiFi();
  connectMqtt();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) connectWiFi();
  if (!mqtt.connected()) {
    if (millis() - lastReconnectAttemptMs > 5000) {
      lastReconnectAttemptMs = millis();
      connectMqtt();
    }
  } else {
    mqtt.loop();
  }

  if (fastSession && (int32_t)(millis() - fastSessionUntilMs) > 0) fastSession = false;
  uint32_t snapshotInterval = fastSession ? 2000UL : 30000UL;
  if (mqtt.connected() && millis() - lastSnapshotMs >= snapshotInterval) {
    lastSnapshotMs = millis();
    publishSnapshot();
  }
  if (mqtt.connected() && millis() - lastPresenceMs >= 30000UL) {
    lastPresenceMs = millis();
    publishPresence(true);
  }
  handleSerial();
  handleBootButton();
  delay(5);
}
