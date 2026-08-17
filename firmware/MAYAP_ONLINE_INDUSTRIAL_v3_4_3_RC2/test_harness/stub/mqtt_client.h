#pragma once
// Stub PHAN CUNG/MANG: khong co broker tren host. Ghi lai moi publish/subscribe
// (topic, QoS, retain) va cho phep test bom event MQTT + goi tin xuong may.
#include <Arduino.h>
#include <esp_system.h>
#include <string>
#include <vector>

typedef void *esp_event_base_t;
typedef struct esp_mqtt_client *esp_mqtt_client_handle_t;

typedef enum {
  MQTT_EVENT_ANY = -1, MQTT_EVENT_ERROR = 0, MQTT_EVENT_CONNECTED,
  MQTT_EVENT_DISCONNECTED, MQTT_EVENT_SUBSCRIBED, MQTT_EVENT_UNSUBSCRIBED,
  MQTT_EVENT_PUBLISHED, MQTT_EVENT_DATA, MQTT_EVENT_BEFORE_CONNECT
} esp_mqtt_event_id_t;

typedef struct esp_mqtt_event_t {
  esp_mqtt_client_handle_t client;
  char *data; int data_len;
  char *topic; int topic_len;
  int total_data_len; int current_data_offset;
  int msg_id; int retain; int qos; bool dup;
  int session_present;
  void *error_handle;
} esp_mqtt_event_t;
typedef esp_mqtt_event_t *esp_mqtt_event_handle_t;

// Bo cuc struct theo ESP-IDF 5.x (nhanh ESP_IDF_VERSION_MAJOR >= 5 cua thu vien).
typedef struct {
  struct {
    struct { const char *uri; } address;
    struct { const char *certificate; bool skip_cert_common_name_check; } verification;
  } broker;
  struct {
    const char *client_id;
    const char *username;
    struct { const char *password; } authentication;
  } credentials;
  struct {
    int keepalive;
    bool disable_clean_session;
    struct { const char *topic; const char *msg; int msg_len; int qos; int retain; } last_will;
  } session;
  struct { int reconnect_timeout_ms; int timeout_ms; } network;
  struct { int stack_size; int priority; } task;
  struct { int size; int out_size; } buffer;
} esp_mqtt_client_config_t;

#define ESP_EVENT_ANY_ID -1

struct MqttRecord { std::string topic, payload; int qos, retain; };

struct MqttStubState {
  bool started = false, connected = false;
  std::vector<MqttRecord> published;
  std::vector<std::string> subscribed;
  void (*handler)(void *, esp_event_base_t, int32_t, void *) = nullptr;
  void *handlerArg = nullptr;
  esp_mqtt_client_handle_t client = nullptr;
  std::string lwtTopic, lwtMsg, clientId, uri, username;
  int lwtQos = 0, lwtRetain = 0, keepalive = 0;
  bool cleanSessionDisabled = false, skipCn = false;
  int bufferSize = 0;
  int outboxOverride = 0;
  std::string rootCa;
  void reset() { published.clear(); subscribed.clear(); }
  const MqttRecord *last(const char *needle) const {
    for (int i = (int)published.size() - 1; i >= 0; --i)
      if (published[i].topic.find(needle) != std::string::npos) return &published[i];
    return nullptr;
  }
  int countTopic(const char *needle) const {
    int n = 0; for (const auto &r : published)
      if (r.topic.find(needle) != std::string::npos) ++n;
    return n;
  }
};
extern MqttStubState g_mqtt;

inline esp_mqtt_client_handle_t esp_mqtt_client_init(const esp_mqtt_client_config_t *c) {
  static int dummy = 1;
  g_mqtt.client = reinterpret_cast<esp_mqtt_client_handle_t>(&dummy);
  if (c) {
    g_mqtt.uri = c->broker.address.uri ? c->broker.address.uri : "";
    g_mqtt.rootCa = c->broker.verification.certificate ? c->broker.verification.certificate : "";
    g_mqtt.skipCn = c->broker.verification.skip_cert_common_name_check;
    if (c->session.last_will.topic) g_mqtt.lwtTopic = c->session.last_will.topic;
    if (c->session.last_will.msg) g_mqtt.lwtMsg = c->session.last_will.msg;
    g_mqtt.lwtQos = c->session.last_will.qos;
    g_mqtt.lwtRetain = c->session.last_will.retain;
    g_mqtt.keepalive = c->session.keepalive;
    g_mqtt.cleanSessionDisabled = c->session.disable_clean_session;
    if (c->credentials.client_id) g_mqtt.clientId = c->credentials.client_id;
    if (c->credentials.username) g_mqtt.username = c->credentials.username;
    g_mqtt.bufferSize = c->buffer.size;
  }
  return g_mqtt.client;
}
inline esp_err_t esp_mqtt_client_register_event(esp_mqtt_client_handle_t, int32_t,
    void (*h)(void *, esp_event_base_t, int32_t, void *), void *arg) {
  g_mqtt.handler = h; g_mqtt.handlerArg = arg; return ESP_OK;
}
inline esp_err_t esp_mqtt_client_start(esp_mqtt_client_handle_t) { g_mqtt.started = true; return ESP_OK; }
inline esp_err_t esp_mqtt_client_stop(esp_mqtt_client_handle_t) { g_mqtt.started = false; g_mqtt.connected = false; return ESP_OK; }
inline esp_err_t esp_mqtt_client_destroy(esp_mqtt_client_handle_t) { g_mqtt.client = nullptr; return ESP_OK; }
inline int esp_mqtt_client_subscribe(esp_mqtt_client_handle_t, const char *t, int) {
  if (t) g_mqtt.subscribed.push_back(t);
  return 1;
}
inline int esp_mqtt_client_publish(esp_mqtt_client_handle_t, const char *t,
                                   const char *d, int len, int qos, int retain) {
  if (!g_mqtt.connected) return -1;
  MqttRecord r; r.topic = t ? t : ""; r.qos = qos; r.retain = retain;
  if (d) r.payload.assign(d, len > 0 ? (size_t)len : strlen(d));
  g_mqtt.published.push_back(r);
  return (int)g_mqtt.published.size();
}
inline int esp_mqtt_client_enqueue(esp_mqtt_client_handle_t c, const char *t,
                                   const char *d, int len, int qos, int retain, bool) {
  return esp_mqtt_client_publish(c, t, d, len, qos, retain);
}
inline int esp_mqtt_client_get_outbox_size(esp_mqtt_client_handle_t) {
  size_t total = 0;
  for (const auto &r : g_mqtt.published) total += r.payload.size();
  return (int)(g_mqtt.outboxOverride >= 0 ? g_mqtt.outboxOverride : 0);
}
#define MQTT_EVENT_ANY_BASE nullptr
