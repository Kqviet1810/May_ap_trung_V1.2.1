// KIEM CHUNG TICH HOP VOI THU VIEN THAT MAYAP_WebBridge v2.0.1
// Chay tren host: thu vien that + stub chi cho tang phan cung (Wi-Fi radio,
// TCP server, DNS, NVS, MQTT socket).
// Nap CHINH .ino de test dung doan tich hop that (hook mang, cau hinh
// BridgeOptions, cong tac ONLINE/OFFLINE), khong test mot ban sao.
#define main arduino_main_unused
#include "MAYAP_ONLINE_INDUSTRIAL_v3_4_3.ino"
#undef main
#include <mqtt_client.h>
#include <string>
using namespace Mayap;

// Dung dung doi tuong ma .ino tao ra.
#define Bridge WebBridge
#define Adapter WebAdapter
static int gFail = 0, gPass = 0;
#define CHECK(c, m) do { if (c) ++gPass; else { ++gFail; printf("  FAIL: %s\n", m); } } while (0)
static void section(const char *n) { printf("\n[%s]\n", n); }

static void pump(int n = 20) {
  for (int i = 0; i < n; ++i) {
    g_fakeMillis += 250;
    Machine.update(g_fakeMillis);
    Machine.serviceI2c(g_fakeMillis);
    Bridge.loop();
    Adapter.loop();
  }
}
static void mqttFire(esp_mqtt_event_id_t id, const char *topic = nullptr,
                     const char *data = nullptr, bool retained = false) {
  esp_mqtt_event_t ev{};
  ev.client = g_mqtt.client; ev.msg_id = 1; ev.retain = retained ? 1 : 0;
  if (topic) { ev.topic = (char *)topic; ev.topic_len = (int)strlen(topic); }
  if (data) {
    ev.data = (char *)data; ev.data_len = (int)strlen(data);
    ev.total_data_len = ev.data_len; ev.current_data_offset = 0;
  }
  if (g_mqtt.handler) g_mqtt.handler(g_mqtt.handlerArg, nullptr, (int32_t)id, &ev);
}
static void mqttConnect() { g_mqtt.connected = true; mqttFire(MQTT_EVENT_CONNECTED); pump(8); }
static void mqttDrop() { g_mqtt.connected = false; mqttFire(MQTT_EVENT_DISCONNECTED); pump(8); }
static std::string T(const char *suffix) {
  return std::string("mayap/v1/") + Bridge.deviceId() + suffix;
}
static bool subscribed(const char *suffix) {
  const std::string t = T(suffix);
  for (const auto &s : g_mqtt.subscribed) if (s == t) return true;
  return false;
}
static double jnum(const std::string &j, const char *k) {
  char p[64]; snprintf(p, sizeof(p), "\"%s\":", k);
  const size_t i = j.find(p);
  return i == std::string::npos ? NAN : atof(j.c_str() + i + strlen(p));
}

static std::string fullConfig(uint32_t rev, int v) {
  char b[2048];
  snprintf(b, sizeof(b),
    "{\"v\":%d,\"requestId\":\"r%lu\",\"revision\":%lu,\"config\":{"
    "\"targetTemp\":37.5,\"tempHysteresis\":0.2,\"lowTempAlarm\":36.5,"
    "\"highTempAlarm\":38.2,\"emergencyTemp\":39.0,\"kp\":18.0,\"ki\":0.8,\"kd\":45.0,"
    "\"lowHumidityAlarm\":%d.0,\"ventOnTemp\":38.0,\"ventOffTemp\":37.7,"
    "\"tempOffset\":0.0,\"humidityOffset\":0.0,\"pidCycleSec\":10,"
    "\"humidityAlarmDelaySec\":60,\"turnIntervalMin\":120,\"turnMaxRunSec\":300,"
    "\"powerRestoreDelaySec\":30,\"sensorTimeoutSec\":10,\"maxHeaterPower\":100,"
    "\"totalIncubationDays\":21,\"circulationFanEnabled\":true,\"turningEnabled\":true,"
    "\"autoResumeAfterPower\":true,\"allowHeatWithoutBatch\":false,\"alarmEnabled\":true,"
    "\"controlMode\":1,\"nextDirection\":0}}",
    v, (unsigned long)rev, (unsigned long)rev, 40 + (int)(rev % 10));
  return b;
}

int main() {
  printf("===== TICH HOP THU VIEN THAT MAYAP_WebBridge v%s =====\n",
         MAYAP_WEBBRIDGE_VERSION);
  setup();   // dung setup() that cua firmware
  CHECK(webBridgeStarted, "setup() khoi dong duoc WebBridge that");
  CHECK(mayapNetHooksInstalled(), "setup() da dang ky hook mang cho HMI");
  pump(40);
  mqttConnect();
  pump(20);

  section("1. TOPIC VA SUBSCRIBE");
  printf("  deviceId = %s\n", Bridge.deviceId());
  CHECK(subscribed("/config/set"), "Da subscribe config/set");
  CHECK(subscribed("/command"), "Da subscribe command");
  CHECK(subscribed("/session"), "Da subscribe session");
  CHECK(g_mqtt.lwtTopic == T("/presence"), "LWT dung topic presence");
  CHECK(g_mqtt.lwtQos == 1 && g_mqtt.lwtRetain == 1, "LWT QoS1 + retain");
  CHECK(g_mqtt.lwtMsg.find("\"online\":false") != std::string::npos,
        "LWT bao offline");

  section("2. QoS / RETAIN TUNG KENH");
  pump(200);
  const MqttRecord *snap = g_mqtt.last("/snapshot");
  const MqttRecord *cfg = g_mqtt.last("/config/reported");
  const MqttRecord *pres = g_mqtt.last("/presence");
  CHECK(snap != nullptr, "Da phat snapshot");
  CHECK(cfg != nullptr, "Da phat config/reported");
  if (snap) CHECK(snap->qos == 0 && snap->retain == 0, "snapshot QoS0, khong retain");
  if (cfg) CHECK(cfg->qos == 1 && cfg->retain == 1, "config/reported QoS1 + retain");
  if (pres) CHECK(pres->qos == 1 && pres->retain == 1, "presence QoS1 + retain");

  section("3. CONFIG SET QUA DUONG MQTT THAT");
  const std::string cs = T("/config/set");
  {
    const std::string j = fullConfig(1000, 1);   // website v1
    mqttFire(MQTT_EVENT_DATA, cs.c_str(), j.c_str(), true); // config/set LA retained
    pump(120);
  }
  const MqttRecord *ack = g_mqtt.last("/ack");
  CHECK(ack != nullptr, "Co ack tren topic /ack");
  if (ack) {
    CHECK(ack->qos == 1, "ack QoS1");
    CHECK(ack->payload.find("\"applied\"") != std::string::npos,
          "Website v1 (28 truong, retained) -> applied");
  }
  CHECK(fabs(Machine.config().lowHumidityAlarm - 40.0f) < 0.01f,
        "Firmware da ap dung gia tri tu web v1");

  section("4. RETAINED CONFIG PHAT LAI KHI RECONNECT");
  const int ackBefore = g_mqtt.countTopic("/ack");
  {
    const std::string j = fullConfig(1000, 1);
    mqttFire(MQTT_EVENT_DATA, cs.c_str(), j.c_str(), true);  // broker gui lai
    pump(60);
  }
  const MqttRecord *dup = g_mqtt.last("/ack");
  CHECK(dup && dup->payload.find("\"duplicate\"") != std::string::npos,
        "Config retained phat lai -> duplicate, khong ghi lai EEPROM");
  CHECK(g_mqtt.countTopic("/ack") > ackBefore, "Van tra loi web, khong im lang");

  section("5. LENH RETAINED BI CHAN");
  {
    char j[320];
    snprintf(j, sizeof(j), "{\"v\":1,\"requestId\":\"c1\",\"sequence\":1,"
             "\"action\":\"batch_start\",\"bootId\":%lu,\"expiresAt\":4102444800}",
             (unsigned long)jnum(g_mqtt.last("/snapshot")->payload, "bootId"));
    mqttFire(MQTT_EVENT_DATA, T("/command").c_str(), j, true);
    pump(40);
  }
  CHECK(g_mqtt.last("/ack")->payload.find("RETAINED") != std::string::npos,
        "Lenh retained bi chan (chong phat lai khi reconnect)");

  section("6. LENH THAT QUA MQTT");
  {
    const unsigned long boot =
        (unsigned long)jnum(g_mqtt.last("/snapshot")->payload, "bootId");
    const char *acts[] = {"batch_start","turn_left","turn_stop","alarm_ack",
                          "batch_stop","resume_no"};
    uint32_t seq = 10;
    for (const char *a : acts) {
      char j[320];
      snprintf(j, sizeof(j), "{\"v\":1,\"requestId\":\"c%lu\",\"sequence\":%lu,"
               "\"action\":\"%s\",\"bootId\":%lu,\"expiresAt\":4102444800}",
               (unsigned long)seq, (unsigned long)seq, a, boot);
      ++seq;
      mqttFire(MQTT_EVENT_DATA, T("/command").c_str(), j, false);
      pump(30);
      char m[96];
      snprintf(m, sizeof(m), "Lenh '%s' qua MQTT that duoc nhan dien", a);
      CHECK(g_mqtt.last("/ack")->payload.find("unsupported") == std::string::npos, m);
    }
  }

  section("7. MAT MQTT ROI KET NOI LAI");
  mqttDrop();
  pump(20);
  CHECK(!Bridge.mqttOnline(), "Bridge bao mat MQTT");
  {
    NetState n; PortalState p;
    mayapReadNetStatus(n, p);
    CHECK(n != NetState::Online, "netState khong con Online khi mat broker");
  }
  const int before = g_mqtt.countTopic("/config/reported");
  mqttConnect();
  pump(150);
  CHECK(g_mqtt.countTopic("/config/reported") > before,
        "Reconnect -> tu dong phat lai config/reported va snapshot");
  CHECK(Bridge.mqttOnline() && Bridge.state() == mayap::ConnectionState::Online,
        "Bridge tro lai Online");
  {
    NetState n; PortalState p;
    mayapReadNetStatus(n, p);
    CHECK(n == NetState::Online, "netState tro lai Online sau reconnect");
  }

  section("8. MAT WI-FI");
  WiFi.forcedStatus = WL_DISCONNECTED;
  g_mqtt.connected = false;
  pump(60);
  {
    NetState n; PortalState p;
    mayapReadNetStatus(n, p);
    CHECK(n == NetState::NoWifi || n == NetState::Starting,
          "Mat Wi-Fi -> HMI bao chua co Wi-Fi / dang khoi dong");
  }
  {
    WebMachineSnapshot s{}; Machine.copyWebSnapshot(s);
    CHECK(s.runtime.alarmMask == (s.runtime.alarmMask & ALARM_KNOWN_MASK),
          "Mat mang khong tao alarm bit la");
    CHECK((s.runtime.alarmMask & AlarmSystem) == 0U ||
          s.runtime.primaryFaultCode != 0U,
          "Mat mang KHONG tu sinh bao dong he thong");
  }
  WiFi.forcedStatus = WL_CONNECTED;
  mqttConnect();
  pump(40);

  section("9. REQUESTID DAI NHU WEBSITE THAT (UUID)");
  {
    // app.js that (requestId()) tao id dang "<prefix>-<32 hex tu
    // crypto.randomUUID().replace(/-/g,'')" = 36 ky tu, vd:
    // "cfg-56406166ab4b4243993b157df2acebe8". WEB_REQUEST_ID_CAPACITY = 40
    // (config.h) nen readString() trong mayap_web_adapter.h chi giu duoc toi
    // da 39 ky tu; mot chuoi requestId dai hon (vd giu nguyen dau gach ngang
    // UUID: "cfg-" + uuid 36 ky tu = 40 ky tu) se lam readString() tra ve
    // false va CA GOI CONFIG/COMMAND BI TU CHOI "invalid". Test nay khoa
    // dung do dai website that dang phat sinh, tranh tai dien loi cu.
    const char *cfgReqId = "cfg-56406166ab4b4243993b157df2acebe8";  // 36 ky tu
    const char *cmdReqId = "cmd-a1b2c3d4e5f64789a0b1c2d3e4f5a6b7";  // 36 ky tu
    CHECK(strlen(cfgReqId) < 40 && strlen(cmdReqId) < 40,
          "requestId website that (36 ky tu) nam duoi WEB_REQUEST_ID_CAPACITY");

    uint32_t rev = 1900;
    char cfgJson[2048];
    snprintf(cfgJson, sizeof(cfgJson),
      "{\"v\":1,\"requestId\":\"%s\",\"revision\":%lu,\"config\":{"
      "\"targetTemp\":37.5,\"tempHysteresis\":0.2,\"lowTempAlarm\":36.5,"
      "\"highTempAlarm\":38.2,\"emergencyTemp\":39.0,\"kp\":18.0,\"ki\":0.8,\"kd\":45.0,"
      "\"lowHumidityAlarm\":45.0,\"ventOnTemp\":38.0,\"ventOffTemp\":37.7,"
      "\"tempOffset\":0.0,\"humidityOffset\":0.0,\"pidCycleSec\":10,"
      "\"humidityAlarmDelaySec\":60,\"turnIntervalMin\":120,\"turnMaxRunSec\":300,"
      "\"powerRestoreDelaySec\":30,\"sensorTimeoutSec\":10,\"maxHeaterPower\":100,"
      "\"totalIncubationDays\":21,\"circulationFanEnabled\":true,\"turningEnabled\":true,"
      "\"autoResumeAfterPower\":true,\"allowHeatWithoutBatch\":false,\"alarmEnabled\":true,"
      "\"controlMode\":1,\"nextDirection\":0}}",
      cfgReqId, (unsigned long)rev);
    mqttFire(MQTT_EVENT_DATA, cs.c_str(), cfgJson, false);
    pump(120);
    const MqttRecord *cfgAck = g_mqtt.last("/ack");
    CHECK(cfgAck && cfgAck->payload.find("\"applied\"") != std::string::npos,
          "Config voi requestId dai nhu website that -> applied (khong bi 'invalid')");
    CHECK(cfgAck && cfgAck->payload.find(cfgReqId) != std::string::npos,
          "Ack tra dung nguyen requestId website that gui len");

    const unsigned long boot =
        (unsigned long)jnum(g_mqtt.last("/snapshot")->payload, "bootId");
    char cmdJson[320];
    snprintf(cmdJson, sizeof(cmdJson),
      "{\"v\":1,\"requestId\":\"%s\",\"sequence\":500,"
      "\"action\":\"alarm_ack\",\"bootId\":%lu,\"expiresAt\":4102444800}",
      cmdReqId, boot);
    mqttFire(MQTT_EVENT_DATA, T("/command").c_str(), cmdJson, false);
    pump(40);
    const MqttRecord *cmdAck = g_mqtt.last("/ack");
    CHECK(cmdAck && cmdAck->payload.find("\"invalid\"") == std::string::npos &&
          cmdAck->payload.find("THIEU") == std::string::npos,
          "Command voi requestId dai nhu website that duoc nhan dien (khong 'invalid')");
    CHECK(cmdAck && cmdAck->payload.find(cmdReqId) != std::string::npos,
          "Ack lenh tra dung nguyen requestId website that gui len");
  }

  section("10. TUONG THICH PHIEN BAN + TU CHOI GOI SAI");
  {
    uint32_t rev = 2000;
    struct { int v; const char *name; } cases[] = {
      {1, "website v1"}, {2, "website v2"}
    };
    for (auto &c : cases) {
      const std::string j = fullConfig(++rev, c.v);
      mqttFire(MQTT_EVENT_DATA, cs.c_str(), j.c_str(), false);
      pump(120);
      char m[96]; snprintf(m, sizeof(m), "%s luu duoc cau hinh", c.name);
      CHECK(g_mqtt.last("/ack")->payload.find("\"applied\"") != std::string::npos, m);
    }
    // Phien ban tuong lai phai bi tu choi ro rang.
    std::string j9 = fullConfig(++rev, 2);
    j9.replace(j9.find("\"v\":2"), 5, "\"v\":9");
    mqttFire(MQTT_EVENT_DATA, cs.c_str(), j9.c_str(), false);
    pump(40);
    CHECK(g_mqtt.last("/ack")->payload.find("\"invalid\"") != std::string::npos,
          "Phien ban tuong lai (v9) bi tu choi");

    // Thieu truong -> tu choi ca goi, khong luu mot phan.
    char partial[256];
    snprintf(partial, sizeof(partial),
             "{\"v\":1,\"requestId\":\"p\",\"revision\":%lu,"
             "\"config\":{\"targetTemp\":37.5}}", (unsigned long)(++rev));
    mqttFire(MQTT_EVENT_DATA, cs.c_str(), partial, false);
    pump(40);
    CHECK(g_mqtt.last("/ack")->payload.find("\"invalid\"") != std::string::npos,
          "Thieu truong -> invalid, khong luu mot phan");

    // Ngoai gioi han an toan -> tu choi ca goi, khong clamp am tham.
    std::string bad = fullConfig(++rev, 1);
    bad.replace(bad.find("\"targetTemp\":37.5"), strlen("\"targetTemp\":37.5"),
                "\"targetTemp\":99.0");
    const float keepSv = Machine.config().targetTemp;
    mqttFire(MQTT_EVENT_DATA, cs.c_str(), bad.c_str(), false);
    pump(80);
    CHECK(g_mqtt.last("/ack")->payload.find("NGOAI GIOI HAN") != std::string::npos,
          "SV ngoai dai -> tu choi ca goi");
    CHECK(fabs(Machine.config().targetTemp - keepSv) < 0.001f,
          "Cau hinh dang chay khong bi goi loi lam hong");

    // cloudEnabled la chi doc voi web.
    const bool cloudBefore = Machine.config().cloudEnabled;
    std::string wr = fullConfig(++rev, 2);
    wr.insert(wr.rfind("}}"), ",\"cloudEnabled\":false");
    mqttFire(MQTT_EVENT_DATA, cs.c_str(), wr.c_str(), false);
    pump(120);
    CHECK(Machine.config().cloudEnabled == cloudBefore,
          "Web KHONG tat duoc ONLINE tu xa");
  }

  section("11. ECHO KHOP CAU HINH DANG CHAY");
  {
    pump(60);
    const std::string e = g_mqtt.last("/config/reported")->payload;
    const MachineConfig live = Machine.config();
    int bad = 0;
#define CMP_F(name) if (fabs(jnum(e, #name) - (double)live.name) > 0.02) \
    { ++bad; printf("    lech %s\n", #name); }
    MAYAP_WEB_CONFIG_FLOAT_FIELDS(CMP_F)
#undef CMP_F
#define CMP_U(name) if ((long)jnum(e, #name) != (long)live.name) \
    { ++bad; printf("    lech %s\n", #name); }
    MAYAP_WEB_CONFIG_U16_FIELDS(CMP_U)
    MAYAP_WEB_CONFIG_U8_FIELDS(CMP_U)
#undef CMP_U
#define CMP_E(name, t, mx) if ((long)jnum(e, #name) != (long)live.name) \
    { ++bad; printf("    lech %s\n", #name); }
    MAYAP_WEB_CONFIG_ENUM_FIELDS(CMP_E)
#undef CMP_E
    CHECK(bad == 0, "TAT CA truong echo khop MachineConfig dang chay");
    CHECK(e.find("\"cloudEnabled\"") != std::string::npos,
          "Echo van bao cloudEnabled cho web (chi doc)");
  }

  section("12. SNAPSHOT KHOP RUNTIME");
  {
    pump(60);
    const std::string sn = g_mqtt.last("/snapshot")->payload;
    WebMachineSnapshot live{}; Machine.copyWebSnapshot(live);
    const char *need[] = {"temperature","humidity","heaterPower","stateCode",
                          "turnState","nextTurnMinutes","alarmMask",
                          "primaryFaultCode","activeFaultCount","netState",
                          "portalState","faults","machineState"};
    for (const char *k : need) {
      char pat[48]; snprintf(pat, sizeof(pat), "\"%s\"", k);
      char m[80]; snprintf(m, sizeof(m), "Snapshot co truong %s", k);
      CHECK(sn.find(pat) != std::string::npos, m);
    }
    CHECK((long)jnum(sn, "stateCode") == (long)live.runtime.stateCode,
          "stateCode khop runtime");
    CHECK((long)jnum(sn, "netState") == (long)live.runtime.netState,
          "netState khop runtime");
    CHECK(fabs(jnum(sn, "heaterPower") - live.runtime.heaterPower) < 0.11,
          "heaterPower khop runtime");
    CHECK(sn.size() < 2304, "Snapshot nho hon RX_PAYLOAD_MAX cua thu vien");
  }

  section("13. MAPPING ENUM");
  CHECK((int)mayap::ConnectionState::Online == 4, "ConnectionState::Online = 4");
  CHECK((int)mayap::OutboundChannel::ConfigReported == 0 &&
        (int)mayap::OutboundChannel::Ack == 1 &&
        (int)mayap::OutboundChannel::Snapshot == 2 &&
        (int)mayap::OutboundChannel::Log == 3,
        "OutboundChannel khop thu tu thu vien");
  CHECK((int)mayap::InboundChannel::ConfigSet == 0 &&
        (int)mayap::InboundChannel::Command == 1,
        "InboundChannel khop thu tu thu vien");

  section("14. CAPTIVE PORTAL TU HMI (chay cuoi: pha huy)");
  CHECK(!Bridge.portalOpen(), "Truoc khi bam: portal dong");
  const bool opened = mayapRequestWifiPortal();
  pump(20);
  CHECK(opened, "Hook HMI goi duoc clearNetworkAndOpenPortal()");
  CHECK(Bridge.portalOpen(), "Thu vien bao portal DANG MO");
  {
    NetState n; PortalState p;
    mayapReadNetStatus(n, p);
    CHECK(p == PortalState::Active, "HMI thay portalState = Active");
  }
  CHECK(Preferences::store().count("mayapnet:ssid") == 0 ||
        Preferences::store()["mayapnet:ssid"].empty(),
        "Wi-Fi da luu bi xoa (dung nhu thao tac BOOT)");


  printf("\n============================================================\n");
  printf("PASS = %d   FAIL = %d   ->  %s\n", gPass, gFail,
         gFail ? "CO LOI" : "TAT CA DAT");
  return gFail ? 1 : 0;
}
