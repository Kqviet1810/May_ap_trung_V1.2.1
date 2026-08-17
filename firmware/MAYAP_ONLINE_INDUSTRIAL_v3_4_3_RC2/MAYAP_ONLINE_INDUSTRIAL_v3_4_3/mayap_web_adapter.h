#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <MAYAP_WebBridge.h>
#include <esp_system.h>
#include <time.h>
#include <math.h>
#include <stdarg.h>
#include <string.h>

#include "config.h"
#include "mayap_web_schema.h"

namespace Mayap {

class MayapWebAdapter {
 public:
  bool begin(mayap::WebBridge &bridge) {
    bridge_ = &bridge;
    bootId_ = esp_random();
    if (prefs_.begin("mayapweb", false)) {
      configRevision_ = prefs_.getUInt("cfgrev", 0U);
      prefsReady_ = true;
    }
    configReportRequested_ = true;
    snapshotRequested_ = true;
    nextSnapshotAt_ = 0U;
    return true;
  }

  mayap::BridgeCallbacks callbacks() {
    mayap::BridgeCallbacks cb{};
    cb.context = this;
    cb.onMessage = &MayapWebAdapter::onMessageThunk;
    cb.onConnectionChanged = &MayapWebAdapter::onConnectionThunk;
    cb.onSessionChanged = &MayapWebAdapter::onSessionThunk;
    return cb;
  }

  void loop() {
    if (!bridge_) return;

    if (bridge_->takeSyncRequest()) {
      configReportRequested_ = true;
      snapshotRequested_ = true;
      nextDiagnosticsAt_ = 0U;
    }
    if (Machine.consumeWebConfigChanged()) configReportRequested_ = true;
    if (Machine.consumeWebStateChanged()) snapshotRequested_ = true;

    serviceCoreResults();
    servicePeriodicSnapshot();
    serviceOneLog();

    // Moi loop chi publish toi da mot goi, uu tien config/ACK/log/snapshot.
    if (publishPendingConfig()) return;
    if (publishOneAck()) return;
    if (publishPendingLog()) return;
    (void)publishPendingSnapshot();
  }

 private:
  static constexpr size_t TX_CONFIG_CAPACITY = 2304U;
  static constexpr size_t TX_SNAPSHOT_CAPACITY = 3072U;
  static constexpr size_t TX_ACK_CAPACITY = 512U;
  static constexpr size_t TX_LOG_CAPACITY = 384U;
  static constexpr uint8_t ACK_QUEUE_CAPACITY = 4U;
  static constexpr uint32_t SNAPSHOT_ACTIVE_MS = 2000U;
  static constexpr uint32_t SNAPSHOT_IDLE_MS = 30000U;
  static constexpr uint32_t DIAGNOSTICS_INTERVAL_MS = 30000U;
  static constexpr uint32_t COMMAND_MAX_FUTURE_SEC = 30U;
  static constexpr uint32_t MIN_VALID_EPOCH = 1700000000UL;
  // Gioi han 8 loi de goi snapshot khong cham tran 3 KB.
  static constexpr uint8_t WEB_FAULT_REPORT_LIMIT = 8U;

  struct AckFrame {
    uint16_t length = 0U;
    char payload[TX_ACK_CAPACITY]{};
  };

  class Writer {
   public:
    Writer(char *buffer, size_t capacity) : buffer_(buffer), capacity_(capacity) {
      if (buffer_ && capacity_) buffer_[0] = '\0';
    }
    bool add(const char *format, ...)
        __attribute__((format(printf, 2, 3))) {
      if (!ok_ || !buffer_ || used_ >= capacity_) return false;
      va_list args;
      va_start(args, format);
      const int n = vsnprintf(buffer_ + used_, capacity_ - used_, format, args);
      va_end(args);
      if (n < 0 || static_cast<size_t>(n) >= capacity_ - used_) {
        ok_ = false;
        return false;
      }
      used_ += static_cast<size_t>(n);
      return true;
    }
    size_t size() const { return ok_ ? used_ : 0U; }
    bool ok() const { return ok_; }
   private:
    char *buffer_ = nullptr;
    size_t capacity_ = 0U;
    size_t used_ = 0U;
    bool ok_ = true;
  };

  struct CommandMap {
    const char *action;
    HmiCommandType type;
    uint16_t validForMs;
    uint16_t leaseMs;
    bool requireFresh;
  };

#define MAYAP_COMMAND_ROW(actionText, enumName, validMs, leaseMsValue, fresh) \
  {actionText, HmiCommandType::enumName, validMs, leaseMsValue, fresh},
  static constexpr CommandMap COMMANDS_[] = {
    MAYAP_WEB_COMMAND_TABLE(MAYAP_COMMAND_ROW)
  };
#undef MAYAP_COMMAND_ROW

  mayap::WebBridge *bridge_ = nullptr;
  Preferences prefs_{};
  bool prefsReady_ = false;
  uint32_t bootId_ = 0U;
  uint32_t configRevision_ = 0U;
  uint32_t lastCommandSequence_ = 0U;
  uint32_t nextTransactionId_ = 1U;
  uint32_t nextSnapshotAt_ = 0U;
  uint32_t nextDiagnosticsAt_ = 0U;
  bool configReportRequested_ = false;
  bool snapshotRequested_ = false;

  char configBuffer_[TX_CONFIG_CAPACITY]{};
  uint16_t configLength_ = 0U;
  char snapshotBuffer_[TX_SNAPSHOT_CAPACITY]{};
  uint16_t snapshotLength_ = 0U;
  char logBuffer_[TX_LOG_CAPACITY]{};
  uint16_t logLength_ = 0U;

  AckFrame ackQueue_[ACK_QUEUE_CAPACITY]{};
  uint8_t ackHead_ = 0U;
  uint8_t ackTail_ = 0U;
  uint8_t ackCount_ = 0U;

  static void onMessageThunk(void *context, mayap::InboundChannel channel,
                             const char *payload, size_t length, bool retained) {
    if (context) static_cast<MayapWebAdapter *>(context)->onMessage(
        channel, payload, length, retained);
  }

  static void onConnectionThunk(void *context, bool online) {
    if (context) static_cast<MayapWebAdapter *>(context)->onConnection(online);
  }

  static void onSessionThunk(void *context, bool active) {
    if (context) static_cast<MayapWebAdapter *>(context)->onSession(active);
  }

  void onConnection(bool online) {
    if (online) {
      configReportRequested_ = true;
      snapshotRequested_ = true;
      nextSnapshotAt_ = 0U;
      nextDiagnosticsAt_ = 0U;
    }
  }

  void onSession(bool active) {
    (void)active;
    snapshotRequested_ = true;
    nextSnapshotAt_ = 0U;
  }

  void onMessage(mayap::InboundChannel channel, const char *payload,
                 size_t length, bool retained) {
    if (!payload || !length || strnlen(payload, length) != length) return;
    if (!protocolCompatible(payload)) {
      queueImmediateAck("", 0U, channel == mayap::InboundChannel::ConfigSet
                          ? "config" : "command", "invalid",
                        "PHIEN BAN GIAO THUC KHONG TUONG THICH");
      return;
    }
    if (channel == mayap::InboundChannel::Command && retained) {
      queueImmediateAck("", 0U, "command", "rejected",
                        "RETAINED COMMAND BI CHAN");
      return;
    }
    if (channel == mayap::InboundChannel::ConfigSet) handleConfig(payload, length);
    else handleCommand(payload, length);
  }

  void handleConfig(const char *json, size_t length) {
    (void)length;
    uint32_t revision = 0U;
    char requestId[WEB_REQUEST_ID_CAPACITY]{};
    const char *configObject = findValue(json, "config");
    if (!configObject || *configObject != '{' ||
        !readU32(json, "revision", revision) || revision == 0U ||
        !readString(json, "requestId", requestId, sizeof(requestId))) {
      queueImmediateAck(requestId, 0U, "config", "invalid",
                        "THIEU REVISION/REQUEST ID");
      return;
    }
    if (revision <= configRevision_) {
      configReportRequested_ = true;
      queueImmediateAck(requestId, 0U, "config", "duplicate",
                        "CAU HINH DA DUOC XU LY");
      return;
    }

    MachineConfig candidate{};
#define READ_FLOAT_FIELD(name) \
    if (!readFloat(json, #name, candidate.name)) { \
      queueMissingConfigField(requestId, #name); return; \
    }
    MAYAP_WEB_CONFIG_FLOAT_FIELDS(READ_FLOAT_FIELD)
#undef READ_FLOAT_FIELD
#define READ_U16_FIELD(name) \
    { uint32_t value = 0U; if (!readU32(json, #name, value) || value > 65535U) { \
      queueMissingConfigField(requestId, #name); return; } \
      candidate.name = static_cast<uint16_t>(value); }
    MAYAP_WEB_CONFIG_U16_FIELDS(READ_U16_FIELD)
#undef READ_U16_FIELD
#define READ_U8_FIELD(name) \
    { uint32_t value = 0U; if (!readU32(json, #name, value) || value > 255U) { \
      queueMissingConfigField(requestId, #name); return; } \
      candidate.name = static_cast<uint8_t>(value); }
    MAYAP_WEB_CONFIG_U8_FIELDS(READ_U8_FIELD)
#undef READ_U8_FIELD
#define READ_BOOL_FIELD(name) \
    if (!readBool(json, #name, candidate.name)) { \
      queueMissingConfigField(requestId, #name); return; \
    }
    MAYAP_WEB_CONFIG_BOOL_FIELDS(READ_BOOL_FIELD)
#undef READ_BOOL_FIELD
#define READ_ENUM_FIELD(name, typeName, maxValue) \
    { uint32_t value = 0U; if (!readU32(json, #name, value) || value > maxValue) { \
      queueMissingConfigField(requestId, #name); return; } \
      candidate.name = static_cast<typeName>(value); }
    MAYAP_WEB_CONFIG_ENUM_FIELDS(READ_ENUM_FIELD)
#undef READ_ENUM_FIELD

    // Truong chi doc: lay tu cau hinh may dang chay, khong lay tu goi web.
    // Neu bo trong, sanitize se coi byte rac la gia tri hop le.
    {
      WebMachineSnapshot current{};
      if (Machine.copyWebSnapshot(current)) {
#define KEEP_READONLY_BOOL(name) candidate.name = current.config.name;
        MAYAP_WEB_CONFIG_READONLY_BOOL_FIELDS(KEEP_READONLY_BOOL)
#undef KEEP_READONLY_BOOL
      }
    }

    WebConfigRequest request{};
    request.transactionId = nextTransactionId_++;
    if (nextTransactionId_ == 0U) nextTransactionId_ = 1U;
    request.revision = revision;
    snprintf(request.requestId, sizeof(request.requestId), "%s", requestId);
    request.config = candidate;

    if (!Machine.submitWebConfig(request)) {
      queueImmediateAck(requestId, 0U, "config", "busy",
                        "MAY DANG XU LY CAU HINH KHAC");
    } else {
      queueImmediateAck(requestId, 0U, "config", "accepted",
                        "DA NHAN, DANG LUU EEPROM");
    }
  }

  void handleCommand(const char *json, size_t length) {
    (void)length;
    uint32_t sequence = 0U;
    char requestId[WEB_REQUEST_ID_CAPACITY]{};
    char action[32]{};
    if (!readU32(json, "sequence", sequence) || sequence == 0U ||
        !readString(json, "requestId", requestId, sizeof(requestId)) ||
        !readString(json, "action", action, sizeof(action))) {
      queueImmediateAck(requestId, sequence, "command", "invalid",
                        "THIEU SEQUENCE/REQUEST ID/ACTION");
      return;
    }
    if (sequence <= lastCommandSequence_) {
      queueImmediateAck(requestId, sequence, "command", "duplicate",
                        "LENH DA DUOC XU LY");
      return;
    }

    const CommandMap *map = findCommand(action);
    if (!map) {
      queueImmediateAck(requestId, sequence, "command", "unsupported",
                        "LENH CHUA DUOC HO TRO");
      return;
    }

    uint32_t commandBootId = 0U;
    if (!readU32(json, "bootId", commandBootId) || commandBootId != bootId_) {
      queueImmediateAck(requestId, sequence, "command", "stale",
                        "LENH KHONG THUOC LAN KHOI DONG HIEN TAI");
      return;
    }

    if (map->requireFresh) {
      uint32_t expiresAt = 0U;
      const time_t nowTime = time(nullptr);
      const uint32_t nowEpoch = nowTime > static_cast<time_t>(MIN_VALID_EPOCH)
          ? static_cast<uint32_t>(nowTime) : 0U;
      if (!readU32(json, "expiresAt", expiresAt) || nowEpoch == 0U ||
          expiresAt < nowEpoch || expiresAt > nowEpoch + COMMAND_MAX_FUTURE_SEC) {
        queueImmediateAck(requestId, sequence, "command", "expired",
                          "LENH HET HAN/THOI GIAN CHUA HOP LE");
        return;
      }
    }

    uint32_t validForMs = map->validForMs;
    uint32_t leaseMs = map->leaseMs;
    uint32_t alarmMask = AlarmNone;
    int32_t arg0 = 0;
    int32_t arg1 = 0;
    float value = 0.0f;
    (void)readU32(json, "validForMs", validForMs);
    (void)readU32(json, "leaseMs", leaseMs);
    (void)readU32(json, "alarmMask", alarmMask);
    (void)readI32(json, "arg0", arg0);
    (void)readI32(json, "arg1", arg1);
    (void)readFloat(json, "value", value);
    if (validForMs > 60000U) validForMs = 60000U;
    if (leaseMs > 5000U) leaseMs = 5000U;

    WebCommandRequest request{};
    request.sequence = sequence;
    snprintf(request.requestId, sizeof(request.requestId), "%s", requestId);
    request.command.id = sequence;
    request.command.type = map->type;
    request.command.createdAt = millis();
    request.command.validForMs = static_cast<uint16_t>(validForMs);
    request.command.actuatorLeaseMs = static_cast<uint16_t>(leaseMs);
    request.command.alarmMask = alarmMask & ALARM_KNOWN_MASK;
    request.command.arg0 = arg0;
    request.command.arg1 = arg1;
    request.command.value = value;

    if (!Machine.submitWebCommand(request)) {
      queueImmediateAck(requestId, sequence, "command", "busy",
                        "HANG DOI LENH DANG DAY");
      return;
    }
    lastCommandSequence_ = sequence;
  }

  void serviceCoreResults() {
    if (ackCount_ >= ACK_QUEUE_CAPACITY) return;

    WebConfigResult configResult{};
    if (Machine.takeWebConfigResult(configResult)) {
      if (configResult.ok) {
        configRevision_ = configResult.revision;
        if (prefsReady_) prefs_.putUInt("cfgrev", configRevision_);
        configReportRequested_ = true;
        snapshotRequested_ = true;
      }
      queueImmediateAck(configResult.requestId, 0U, "config",
                        configResult.ok ? "applied" : "rejected",
                        configResult.message);
      return;
    }

    WebCommandResult commandResult{};
    if (Machine.takeWebCommandResult(commandResult)) {
      queueImmediateAck(commandResult.requestId, commandResult.sequence,
                        "command", commandResult.ok ? "accepted" : "rejected",
                        commandResult.message);
      if (commandResult.ok) snapshotRequested_ = true;
    }
  }

  void servicePeriodicSnapshot() {
    const uint32_t now = millis();
    const uint32_t period = bridge_ && bridge_->sessionActive()
        ? SNAPSHOT_ACTIVE_MS : SNAPSHOT_IDLE_MS;
    if (snapshotRequested_ || nextSnapshotAt_ == 0U ||
        static_cast<int32_t>(now - nextSnapshotAt_) >= 0) {
      if (snapshotLength_ == 0U && buildSnapshot()) {
        snapshotRequested_ = false;
        nextSnapshotAt_ = now + period;
      }
    }
    if (configReportRequested_ && configLength_ == 0U) {
      if (buildConfigReport()) configReportRequested_ = false;
    }
  }

  void serviceOneLog() {
    if (logLength_ != 0U) return;
    WebEventRecord event{};
    if (!Machine.takeWebEvent(event)) return;
    Writer w(logBuffer_, sizeof(logBuffer_));
    w.add("{\"v\":%u,\"deviceId\":\"%s\",\"bootId\":%lu,\"sequence\":%lu,"
          "\"epoch\":%lu,\"code\":%u,\"type\":%u,\"value\":%d,\"flags\":%u}",
          MAYAP_WEB_PROTOCOL_VERSION, bridge_->deviceId(),
          static_cast<unsigned long>(bootId_),
          static_cast<unsigned long>(event.sequence),
          static_cast<unsigned long>(event.epoch), event.code, event.type,
          event.value, event.flags);
    logLength_ = static_cast<uint16_t>(w.size());
  }

  bool buildConfigReport() {
    WebMachineSnapshot snapshot{};
    if (!Machine.copyWebSnapshot(snapshot)) return false;
    Writer w(configBuffer_, sizeof(configBuffer_));
    w.add("{\"v\":%u,\"deviceId\":\"%s\",\"bootId\":%lu,\"revision\":%lu,\"config\":{",
          MAYAP_WEB_PROTOCOL_VERSION, bridge_->deviceId(), static_cast<unsigned long>(bootId_),
          static_cast<unsigned long>(configRevision_));
    bool first = true;
#define WRITE_FLOAT_FIELD(name) writeFloatField(w, first, #name, snapshot.config.name);
    MAYAP_WEB_CONFIG_FLOAT_FIELDS(WRITE_FLOAT_FIELD)
#undef WRITE_FLOAT_FIELD
#define WRITE_U16_FIELD(name) writeUnsignedField(w, first, #name, snapshot.config.name);
    MAYAP_WEB_CONFIG_U16_FIELDS(WRITE_U16_FIELD)
#undef WRITE_U16_FIELD
#define WRITE_U8_FIELD(name) writeUnsignedField(w, first, #name, snapshot.config.name);
    MAYAP_WEB_CONFIG_U8_FIELDS(WRITE_U8_FIELD)
#undef WRITE_U8_FIELD
#define WRITE_BOOL_FIELD(name) writeBoolField(w, first, #name, snapshot.config.name);
    MAYAP_WEB_CONFIG_BOOL_FIELDS(WRITE_BOOL_FIELD)
    MAYAP_WEB_CONFIG_READONLY_BOOL_FIELDS(WRITE_BOOL_FIELD)
#undef WRITE_BOOL_FIELD
#define WRITE_ENUM_FIELD(name, typeName, maxValue) \
    writeUnsignedField(w, first, #name, static_cast<uint8_t>(snapshot.config.name));
    MAYAP_WEB_CONFIG_ENUM_FIELDS(WRITE_ENUM_FIELD)
#undef WRITE_ENUM_FIELD
    w.add("}}");
    configLength_ = static_cast<uint16_t>(w.size());
    return configLength_ != 0U;
  }

  bool buildSnapshot() {
    WebMachineSnapshot s{};
    if (!Machine.copyWebSnapshot(s)) return false;
    char stateEsc[48]{};
    char dateEsc[32]{};
    escapeJson(s.runtime.machineState, stateEsc, sizeof(stateEsc));
    escapeJson(s.runtime.dateText, dateEsc, sizeof(dateEsc));
    const time_t nowEpoch = time(nullptr);
    const uint32_t epoch = nowEpoch > 1700000000 ? static_cast<uint32_t>(nowEpoch) : 0U;

    Writer w(snapshotBuffer_, sizeof(snapshotBuffer_));
    w.add("{\"v\":%u,\"deviceId\":\"%s\",\"bootId\":%lu,\"sequence\":%lu,\"epoch\":%lu,",
          MAYAP_WEB_PROTOCOL_VERSION, bridge_->deviceId(), static_cast<unsigned long>(bootId_),
          static_cast<unsigned long>(s.sequence), static_cast<unsigned long>(epoch));
    w.add("\"runtime\":{\"temperature\":%.2f,\"humidity\":%.2f,"
          "\"sensorOnline\":%s,\"batchRunning\":%s,\"currentDay\":%u,"
          "\"heaterPower\":%.1f,\"heaterOn\":%s,\"circulationFanOn\":%s,"
          "\"ventFanOn\":%s,\"turnState\":%u,\"nextTurnMinutes\":%u,"
          "\"turnCountToday\":%u,\"turnCountBatch\":%lu,\"alarmMask\":%lu,"
          "\"autoTuneState\":%u,\"autoTuneProgress\":%u,\"stateCode\":%u,"
          "\"primaryFaultCode\":%u,\"activeFaultCount\":%u,"
          "\"resumeConfirmationRequired\":%s,\"netState\":%u,"
          "\"portalState\":%u,\"dateText\":\"%s\","
          "\"machineState\":\"%s\"},",
          finiteOrZero(s.runtime.temperature), finiteOrZero(s.runtime.humidity),
          boolText(s.runtime.sensorOnline), boolText(s.runtime.batchRunning),
          s.runtime.currentDay, finiteOrZero(s.runtime.heaterPower),
          boolText(s.runtime.heaterOn), boolText(s.runtime.circulationFanOn),
          boolText(s.runtime.ventFanOn), static_cast<unsigned>(s.runtime.turnState),
          s.runtime.nextTurnMinutes, s.runtime.turnCountToday,
          static_cast<unsigned long>(s.runtime.turnCountBatch),
          static_cast<unsigned long>(s.runtime.alarmMask),
          static_cast<unsigned>(s.runtime.autoTuneState), s.runtime.autoTuneProgress,
          static_cast<unsigned>(s.runtime.stateCode), s.runtime.primaryFaultCode,
          s.runtime.activeFaultCount,
          boolText(s.runtime.resumeConfirmationRequired),
          static_cast<unsigned>(s.runtime.netState),
          static_cast<unsigned>(s.runtime.portalState), dateEsc, stateEsc);

    // Danh sach loi dang hoat dong. Truoc day web chi nhan primaryFaultCode va
    // activeFaultCount nen KHONG THE hien thi giong HMI. Nay ca hai giao dien
    // doc cung mot mang, cung thu tu uu tien do FaultManager sap xep.
    w.add("\"faults\":[");
    const uint8_t faultCount = s.runtime.activeFaultDisplayCount <
        WEB_FAULT_REPORT_LIMIT ? s.runtime.activeFaultDisplayCount
                               : WEB_FAULT_REPORT_LIMIT;
    for (uint8_t i = 0U; i < faultCount; ++i) {
      const HmiFaultItem &f = s.runtime.activeFaults[i];
      w.add("%s{\"code\":%u,\"severity\":%u,\"detail\":%d,\"flags\":%u}",
            i ? "," : "", f.code, f.severity, f.detail, f.flags);
    }
    w.add("],");
    w.add("\"status\":{\"autoMode\":%s,\"manualMode\":%s,"
          "\"systemHealthy\":%s,\"sensorHealthy\":%s,\"rtcHealthy\":%s,"
          "\"storageHealthy\":%s,\"heaterLocked\":%s,\"turningLocked\":%s,"
          "\"waitingPowerResume\":%s,\"activeFaultCount\":%u,"
          "\"highestFaultCode\":%u,\"uptimeSec\":%lu}",
          boolText(s.autoMode), boolText(s.manualMode), boolText(s.systemHealthy),
          boolText(s.sensorHealthy), boolText(s.rtcHealthy),
          boolText(s.storageHealthy), boolText(s.heaterLocked),
          boolText(s.turningLocked), boolText(s.waitingPowerResume),
          s.activeFaultCount, s.highestFaultCode,
          static_cast<unsigned long>(s.uptimeSec));

    const uint32_t nowMs = millis();
    const bool includeDiagnostics = nextDiagnosticsAt_ == 0U ||
        static_cast<int32_t>(nowMs - nextDiagnosticsAt_) >= 0;
    if (includeDiagnostics) {
      w.add(",\"diagnostics\":{\"freeHeap\":%u,\"maxControlCycleUs\":%lu,"
            "\"deadlineMissCount\":%lu,\"i2cRecoveryCount\":%u,"
            "\"i2cConsecutiveErrors\":%u,\"hardTripReason\":%u,"
            "\"controlStackFree\":%u,\"hmiStackFree\":%u,"
            "\"supervisorStackFree\":%u,\"rxDropped\":%lu,"
            "\"rxOversize\":%lu}",
            static_cast<unsigned>(s.kernel.freeInternalHeap),
            static_cast<unsigned long>(s.kernel.maxControlCycleUs),
            static_cast<unsigned long>(s.kernel.deadlineMissCount),
            s.kernel.i2cRecoveryCount, s.kernel.i2cConsecutiveErrors,
            s.kernel.hardTripReason,
            static_cast<unsigned>(s.kernel.controlStackFree),
            static_cast<unsigned>(s.kernel.hmiStackFree),
            static_cast<unsigned>(s.kernel.supervisorStackFree),
            static_cast<unsigned long>(bridge_->droppedRxPackets()),
            static_cast<unsigned long>(bridge_->rejectedOversizePackets()));
      nextDiagnosticsAt_ = nowMs + DIAGNOSTICS_INTERVAL_MS;
    }
    w.add("}");
    snapshotLength_ = static_cast<uint16_t>(w.size());
    return snapshotLength_ != 0U;
  }

  bool publishPendingConfig() {
    if (!configLength_ || !bridge_->mqttOnline()) return false;
    if (!bridge_->publish(mayap::OutboundChannel::ConfigReported,
                          configBuffer_, configLength_)) return false;
    configLength_ = 0U;
    return true;
  }

  bool publishOneAck() {
    if (!ackCount_ || !bridge_->mqttOnline()) return false;
    AckFrame &frame = ackQueue_[ackHead_];
    if (!bridge_->publish(mayap::OutboundChannel::Ack,
                          frame.payload, frame.length)) return false;
    frame = AckFrame{};
    ackHead_ = static_cast<uint8_t>((ackHead_ + 1U) % ACK_QUEUE_CAPACITY);
    --ackCount_;
    return true;
  }

  bool publishPendingLog() {
    if (!logLength_ || !bridge_->mqttOnline()) return false;
    if (!bridge_->publish(mayap::OutboundChannel::Log,
                          logBuffer_, logLength_)) return false;
    logLength_ = 0U;
    return true;
  }

  bool publishPendingSnapshot() {
    if (!snapshotLength_ || !bridge_->mqttOnline()) return false;
    if (!bridge_->publish(mayap::OutboundChannel::Snapshot,
                          snapshotBuffer_, snapshotLength_)) return false;
    snapshotLength_ = 0U;
    return true;
  }

  void queueMissingConfigField(const char *requestId, const char *field) {
    char message[96];
    snprintf(message, sizeof(message), "TRUONG %s THIEU/SAI KIEU", field ? field : "?");
    queueImmediateAck(requestId, 0U, "config", "invalid", message);
  }

  bool queueImmediateAck(const char *requestId, uint32_t sequence,
                         const char *kind, const char *result,
                         const char *message) {
    if (ackCount_ >= ACK_QUEUE_CAPACITY) return false;
    AckFrame &frame = ackQueue_[ackTail_];
    char idEsc[WEB_REQUEST_ID_CAPACITY * 2]{};
    char msgEsc[WEB_RESULT_MESSAGE_CAPACITY * 2]{};
    escapeJson(requestId ? requestId : "", idEsc, sizeof(idEsc));
    escapeJson(message ? message : "", msgEsc, sizeof(msgEsc));
    const int n = snprintf(frame.payload, sizeof(frame.payload),
        "{\"v\":%u,\"deviceId\":\"%s\",\"bootId\":%lu,\"requestId\":\"%s\","
        "\"sequence\":%lu,\"kind\":\"%s\",\"result\":\"%s\","
        "\"message\":\"%s\",\"revision\":%lu}",
        MAYAP_WEB_PROTOCOL_VERSION, bridge_ ? bridge_->deviceId() : "",
        static_cast<unsigned long>(bootId_), idEsc, static_cast<unsigned long>(sequence), kind ? kind : "",
        result ? result : "", msgEsc,
        static_cast<unsigned long>(configRevision_));
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(frame.payload)) {
      frame = AckFrame{};
      return false;
    }
    frame.length = static_cast<uint16_t>(n);
    ackTail_ = static_cast<uint8_t>((ackTail_ + 1U) % ACK_QUEUE_CAPACITY);
    ++ackCount_;
    return true;
  }

  const CommandMap *findCommand(const char *action) const {
    if (!action) return nullptr;
    for (const CommandMap &entry : COMMANDS_) {
      if (strcmp(action, entry.action) == 0) return &entry;
    }
    return nullptr;
  }

  static bool protocolCompatible(const char *json) {
    const char *value = findValue(json, "v");
    if (!value) return true;  // tuong thich payload v1 truoc khi khoa web.
    uint32_t version = 0U;
    return readU32(json, "v", version) &&
           version >= MAYAP_WEB_PROTOCOL_MIN_ACCEPTED &&
           version <= MAYAP_WEB_PROTOCOL_VERSION;
  }

  static bool isJsonDelimiter(char c) {
    return c == '\0' || c == ',' || c == '}' || c == ']' ||
           c == ' ' || c == '\t' || c == '\r' || c == '\n';
  }

  static const char *findValue(const char *json, const char *key) {
    if (!json || !key) return nullptr;
    char pattern[64];
    const int n = snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(pattern)) return nullptr;
    const char *p = strstr(json, pattern);
    if (!p) return nullptr;
    p += n;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
    if (*p++ != ':') return nullptr;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
    return p;
  }

  static bool readString(const char *json, const char *key,
                         char *out, size_t capacity) {
    if (!out || capacity == 0U) return false;
    out[0] = '\0';
    const char *p = findValue(json, key);
    if (!p || *p++ != '"') return false;
    size_t used = 0U;
    while (*p && *p != '"') {
      char c = *p++;
      if (c == '\\') {
        c = *p++;
        if (!c) return false;
        if (c == 'n') c = '\n';
        else if (c == 'r') c = '\r';
        else if (c == 't') c = '\t';
      }
      if (used + 1U >= capacity) return false;
      out[used++] = c;
    }
    if (*p != '"') return false;
    out[used] = '\0';
    return used > 0U;
  }

  static bool readBool(const char *json, const char *key, bool &out) {
    const char *p = findValue(json, key);
    if (!p) return false;
    if (strncmp(p, "true", 4) == 0 && isJsonDelimiter(p[4])) {
      out = true; return true;
    }
    if (strncmp(p, "false", 5) == 0 && isJsonDelimiter(p[5])) {
      out = false; return true;
    }
    return false;
  }

  static bool readU32(const char *json, const char *key, uint32_t &out) {
    const char *p = findValue(json, key);
    if (!p || *p < '0' || *p > '9') return false;
    uint64_t value = 0U;
    while (*p >= '0' && *p <= '9') {
      value = value * 10U + static_cast<uint8_t>(*p - '0');
      if (value > UINT32_MAX) return false;
      ++p;
    }
    if (!isJsonDelimiter(*p)) return false;
    out = static_cast<uint32_t>(value);
    return true;
  }

  static bool readI32(const char *json, const char *key, int32_t &out) {
    const char *p = findValue(json, key);
    if (!p) return false;
    char *end = nullptr;
    const long value = strtol(p, &end, 10);
    if (end == p || value < INT32_MIN || value > INT32_MAX ||
        !isJsonDelimiter(*end)) return false;
    out = static_cast<int32_t>(value);
    return true;
  }

  static bool readFloat(const char *json, const char *key, float &out) {
    const char *p = findValue(json, key);
    if (!p) return false;
    char *end = nullptr;
    const float value = strtof(p, &end);
    if (end == p || !isfinite(value) || !isJsonDelimiter(*end)) return false;
    out = value;
    return true;
  }

  static void escapeJson(const char *input, char *output, size_t capacity) {
    if (!output || capacity == 0U) return;
    if (!input) input = "";
    size_t used = 0U;
    while (*input && used + 1U < capacity) {
      const char c = *input++;
      const char *r = nullptr;
      switch (c) {
        case '\\': r = "\\\\"; break;
        case '"': r = "\\\""; break;
        case '\n': r = "\\n"; break;
        case '\r': r = "\\r"; break;
        case '\t': r = "\\t"; break;
        default: break;
      }
      if (r) {
        const size_t n = strlen(r);
        if (used + n >= capacity) break;
        memcpy(output + used, r, n);
        used += n;
      } else if (static_cast<uint8_t>(c) >= 0x20U) {
        output[used++] = c;
      }
    }
    output[used] = '\0';
  }

  static const char *boolText(bool value) { return value ? "true" : "false"; }
  static float finiteOrZero(float value) { return isfinite(value) ? value : 0.0f; }

  static void writeComma(Writer &w, bool &first) {
    if (!first) w.add(",");
    first = false;
  }
  static void writeFloatField(Writer &w, bool &first,
                              const char *name, float value) {
    writeComma(w, first);
    w.add("\"%s\":%.4f", name, finiteOrZero(value));
  }
  static void writeUnsignedField(Writer &w, bool &first,
                                 const char *name, uint32_t value) {
    writeComma(w, first);
    w.add("\"%s\":%lu", name, static_cast<unsigned long>(value));
  }
  static void writeBoolField(Writer &w, bool &first,
                             const char *name, bool value) {
    writeComma(w, first);
    w.add("\"%s\":%s", name, boolText(value));
  }
};

constexpr MayapWebAdapter::CommandMap MayapWebAdapter::COMMANDS_[];

}  // namespace Mayap
