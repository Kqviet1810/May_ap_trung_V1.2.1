#pragma once

#include "config.h"
#include <Arduino.h>
#include <HardwareSerial.h>
#include <Wire.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <esp32-hal-rgb-led.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <algorithm>

static volatile bool gMayapSerialDebugEnabled = SERIAL_DEBUG_DEFAULT_ON;

inline bool mayapSerialDebugEnabled() {
  return __atomic_load_n(&gMayapSerialDebugEnabled, __ATOMIC_ACQUIRE);
}

inline void mayapSetSerialDebugEnabled(bool enabled) {
  __atomic_store_n(&gMayapSerialDebugEnabled, enabled, __ATOMIC_RELEASE);
}

// Cong Serial duy nhat cua firmware. force=true chi dung de phan hoi lenh SERIAL.
inline void mayapSerialPrintf(bool force, const char *format, ...) {
#if MAYAP_DIAGNOSTIC_SERIAL
  if ((!force && !mayapSerialDebugEnabled()) || !format) return;
  char buffer[224];
  va_list args;
  va_start(args, format);
  const int length = vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  if (length <= 0) return;
  const size_t count = static_cast<size_t>(length) < sizeof(buffer)
      ? static_cast<size_t>(length) : sizeof(buffer) - 1U;
  if (Serial.availableForWrite() >= static_cast<int>(count)) {
    Serial.write(reinterpret_cast<const uint8_t *>(buffer), count);
  }
#else
  (void)force; (void)format;
#endif
}

// ============================================================================
// CAU NOI MANG (dinh nghia cho khai bao trong config.h)
// Dat o day de hmi.h chi phu thuoc khai bao, khong phu thuoc MAYAP_WebBridge.
// ============================================================================
// Hai con tro nay duoc ghi o cuoi setup(), tuc la SAU khi Control Task da bat
// dau goi mayapReadNetStatus(). Truy cap atomic de khong co cua so doc/ghi
// dong thoi, dung phong cach cua phan con lai trong kernel.
static MayapPortalStartFn gMayapPortalStartFn = nullptr;
static MayapNetStatusFn gMayapNetStatusFn = nullptr;

void mayapSetNetHooks(MayapPortalStartFn portalStart, MayapNetStatusFn status) {
  __atomic_store_n(&gMayapNetStatusFn, status, __ATOMIC_RELEASE);
  __atomic_store_n(&gMayapPortalStartFn, portalStart, __ATOMIC_RELEASE);
}
static bool gMayapCloudRestartPending = false;
void mayapSetCloudRestartPending(bool pending) {
  __atomic_store_n(&gMayapCloudRestartPending, pending, __ATOMIC_RELEASE);
}
bool mayapCloudRestartPending() {
  return __atomic_load_n(&gMayapCloudRestartPending, __ATOMIC_ACQUIRE);
}

bool mayapNetHooksInstalled() {
  return __atomic_load_n(&gMayapPortalStartFn, __ATOMIC_ACQUIRE) != nullptr;
}
bool mayapRequestWifiPortal() {
  MayapPortalStartFn fn = __atomic_load_n(&gMayapPortalStartFn, __ATOMIC_ACQUIRE);
  return fn ? fn() : false;
}
void mayapReadNetStatus(NetState &net, PortalState &portal) {
  MayapNetStatusFn fn = __atomic_load_n(&gMayapNetStatusFn, __ATOMIC_ACQUIRE);
  if (fn) { fn(net, portal); return; }
  net = NetState::Disabled;
  portal = PortalState::Idle;
}

namespace Mayap {

// ============================================================================
// TIEN ICH CHUNG
// ============================================================================
inline uint32_t elapsedMs(uint32_t now, uint32_t then) {
  return static_cast<uint32_t>(now - then);
}
inline bool timeReached(uint32_t now, uint32_t target) {
  return static_cast<int32_t>(now - target) >= 0;
}
inline float clampFloat(float value, float low, float high) {
  if (!isfinite(value)) return low;
  if (value < low) return low;
  if (value > high) return high;
  return value;
}
inline uint32_t mcCrc32(const uint8_t *data, size_t length) {
  uint32_t crc = 0xFFFFFFFFUL;
  while (length--) {
    crc ^= *data++;
    for (uint8_t i = 0; i < 8; ++i) {
      crc = (crc >> 1U) ^ (0xEDB88320UL & (0UL - (crc & 1UL)));
    }
  }
  return ~crc;
}

inline void setCompileDate(char out[11]) {
  static const char *const months[] = {
    "Jan","Feb","Mar","Apr","May","Jun",
    "Jul","Aug","Sep","Oct","Nov","Dec"
  };
  char monthText[4] = {__DATE__[0], __DATE__[1], __DATE__[2], '\0'};
  uint8_t month = 1;
  for (uint8_t i = 0; i < 12; ++i) {
    if (strncmp(monthText, months[i], 3) == 0) { month = i + 1U; break; }
  }
  const uint8_t day = static_cast<uint8_t>(
      (__DATE__[4] == ' ' ? 0 : __DATE__[4] - '0') * 10 + (__DATE__[5] - '0'));
  const uint16_t year = static_cast<uint16_t>(
      (__DATE__[7]-'0')*1000 + (__DATE__[8]-'0')*100 +
      (__DATE__[9]-'0')*10 + (__DATE__[10]-'0'));
  snprintf(out, 11, "%02u/%02u/%04u", day, month, year);
}

inline const char *resetReasonText(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON: return "POWERON";
    case ESP_RST_EXT: return "EXTERNAL";
    case ESP_RST_SW: return "SOFTWARE";
    case ESP_RST_PANIC: return "PANIC";
    case ESP_RST_INT_WDT: return "INT_WDT";
    case ESP_RST_TASK_WDT: return "TASK_WDT";
    case ESP_RST_WDT: return "OTHER_WDT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_SDIO: return "SDIO";
    default: return "UNKNOWN";
  }
}

inline bool resetReasonIsPowerInterruption(esp_reset_reason_t reason) {
  return reason == ESP_RST_POWERON || reason == ESP_RST_BROWNOUT ||
         reason == ESP_RST_UNKNOWN;
}

inline bool resetReasonIsAutomaticRecovery(esp_reset_reason_t reason) {
  return reason == ESP_RST_SW || reason == ESP_RST_EXT ||
         reason == ESP_RST_PANIC || reason == ESP_RST_INT_WDT ||
         reason == ESP_RST_TASK_WDT || reason == ESP_RST_WDT;
}

// ============================================================================
// KERNEL NHO: SCHEDULER DINH KY + HANG DOI TINH, KHONG CAP PHAT DONG
// ============================================================================
class PeriodicGate {
 public:
  explicit PeriodicGate(uint32_t periodMs = 0U) : periodMs_(periodMs) {}
  void setPeriod(uint32_t periodMs) { periodMs_ = periodMs; }
  void reset(uint32_t now = 0U) { lastAt_ = now; initialized_ = false; }
  bool due(uint32_t now, bool immediateFirst = false) {
    if (!initialized_) {
      initialized_ = true;
      lastAt_ = now;
      return immediateFirst;
    }
    if (periodMs_ == 0U) return false;
    const uint32_t elapsed = elapsedMs(now, lastAt_);
    if (elapsed < periodMs_) return false;
    // Cap nhat bang phep chia de thoi gian thuc thi luon huu han, khong lap bu.
    lastAt_ += (elapsed / periodMs_) * periodMs_;
    return true;
  }
 private:
  uint32_t periodMs_ = 0U;
  uint32_t lastAt_ = 0U;
  bool initialized_ = false;
};

template <typename T, uint8_t Capacity>
class FixedRing {
 public:
  static_assert(Capacity > 1U, "FixedRing capacity phai > 1");
  bool push(const T &value) {
    if (count_ >= Capacity) {
      // Giu su kien moi nhat: bo phan tu cu nhat va tang bo dem overflow.
      tail_ = static_cast<uint8_t>((tail_ + 1U) % Capacity);
      --count_;
      if (overflow_ < UINT32_MAX) ++overflow_;
    }
    data_[head_] = value;
    head_ = static_cast<uint8_t>((head_ + 1U) % Capacity);
    ++count_;
    return true;
  }
  bool pop(T &out) {
    if (!count_) return false;
    out = data_[tail_];
    tail_ = static_cast<uint8_t>((tail_ + 1U) % Capacity);
    --count_;
    return true;
  }
  void clear() { head_ = tail_ = count_ = 0U; overflow_ = 0U; }
  uint8_t size() const { return count_; }
  uint32_t overflowCount() const { return overflow_; }
 private:
  T data_[Capacity]{};
  uint8_t head_ = 0U;
  uint8_t tail_ = 0U;
  uint8_t count_ = 0U;
  uint32_t overflow_ = 0U;
};

// ============================================================================
// EVENT LOG RAM: NHAT KY NHE, KHONG GHI FLASH NOI DINH KY
// ============================================================================
enum class EventType : uint8_t {
  Boot = 1, InputChanged, OutputChanged, FaultRaised, FaultCleared, FaultAck,
  BatchStart, BatchStop, ModeChanged, ConfigSaved, SensorLost, SensorRestored,
  AutoTuneStart, AutoTuneEnd, StorageError, Recovery, Turning, System
};

enum class EventCode : uint16_t {
  None = 0,
  BootPowerOn = 1, BootExternal, BootSoftware, BootPanic, BootWdt, BootBrownout,
  BatchStart = 20, BatchStop, BatchResume,
  ResumePrompt = 23, ResumeAccepted, ResumeRejected,
  ModeAuto = 30, ModeManual,
  SensorOnline = 40, SensorOffline,
  ConfigSaved = 50, AutoTuneStarted, AutoTuneSuccess, AutoTuneFailed,
  TurnStartLeft = 60, TurnStartRight, TurnHomeLeft, TurnHomeRight,
  TurnCompleteLeft, TurnCompleteRight,
  CommandRejected = 80,
  NetStateChanged = 90, WifiPortalStarted, WifiPortalFailed,
  InputBase = 100,
  OutputBase = 200,
  FaultBase = 1000
};

struct EventEntry {
  uint32_t sequence = 0U;
  uint32_t atMs = 0U;
  uint32_t epoch = 0U;
  uint16_t code = 0U;
  int16_t value = 0;
  uint8_t type = 0U;
  uint8_t flags = 0U;
};

class EventLog {
 public:
  void begin(uint32_t now) { bootAt_ = now; }

  // Dong bo moc thoi gian DS3231 voi millis(). Khi RTC vua hop le, cac su kien
  // da phat sinh trong lan khoi dong hien tai duoc bo sung timestamp theo tuoi.
  void syncClock(uint32_t now, uint32_t epoch) {
    if (epoch == 0U) return;
    if (clockEpoch_ == 0U) {
      for (uint8_t offset = 0U; offset < count_; ++offset) {
        const uint8_t index = static_cast<uint8_t>(
            (head_ + EVENT_LOG_RAM_SIZE - 1U - offset) % EVENT_LOG_RAM_SIZE);
        EventEntry &entry = entries_[index];
        if (entry.epoch == 0U) {
          const uint32_t ageSec = elapsedMs(now, entry.atMs) / 1000UL;
          entry.epoch = epoch > ageSec ? epoch - ageSec : epoch;
        }
      }
    }
    clockEpoch_ = epoch;
    clockSyncedAtMs_ = now;
  }

  uint32_t estimatedEpoch(uint32_t now) const {
    if (clockEpoch_ == 0U) return 0U;
    return clockEpoch_ + elapsedMs(now, clockSyncedAtMs_) / 1000UL;
  }

  void push(uint32_t now, EventType type, uint16_t code,
            int16_t value = 0, uint8_t flags = 0U) {
    EventEntry &entry = entries_[head_];
    entry.sequence = ++sequence_;
    entry.atMs = now;
    entry.epoch = estimatedEpoch(now);
    entry.code = code;
    entry.value = value;
    entry.type = static_cast<uint8_t>(type);
    entry.flags = flags;
    head_ = static_cast<uint8_t>((head_ + 1U) % EVENT_LOG_RAM_SIZE);
    if (count_ < EVENT_LOG_RAM_SIZE) ++count_;
  }
  bool clear(uint32_t) { head_ = count_ = 0U; ++sequence_; return true; }
  uint32_t sequence() const { return sequence_; }
  void print(uint8_t maximum = 20U) const {
    const uint8_t n = std::min<uint8_t>(maximum, count_);
    mayapSerialPrintf(false, "--- EVENT LOG count=%u ---\n", n);
    for (uint8_t i = 0U; i < n; ++i) {
      const uint8_t index = static_cast<uint8_t>(
          (head_ + EVENT_LOG_RAM_SIZE - n + i) % EVENT_LOG_RAM_SIZE);
      const EventEntry &e = entries_[index];
      mayapSerialPrintf(false, "#%lu t=%lums epoch=%lu type=%u code=%u value=%d flags=%u\n",
          static_cast<unsigned long>(e.sequence),
          static_cast<unsigned long>(e.atMs),
          static_cast<unsigned long>(e.epoch),
          e.type, e.code, e.value, e.flags);
    }
  }

  void snapshotLastHour(uint32_t now, HmiEventSnapshot &out) const {
    out = HmiEventSnapshot{};
    out.sourceSequence = sequence_;
    uint8_t copied = 0U;
    uint8_t total = 0U;
    // Duyet tu moi den cu. HMI se hien su kien moi nhat truoc.
    for (uint8_t offset = 0U; offset < count_; ++offset) {
      const uint8_t index = static_cast<uint8_t>(
          (head_ + EVENT_LOG_RAM_SIZE - 1U - offset) % EVENT_LOG_RAM_SIZE);
      const EventEntry &e = entries_[index];
      // Phep tru uint32_t tren millis() an toan qua mot lan wrap; cua so chi
      // 1 gio nen nho hon rat nhieu so voi chu ky wrap xap xi 49 ngay.
      const uint32_t age = elapsedMs(now, e.atMs) / 1000UL;
      if (age > HMI_EVENT_WINDOW_SEC) continue;
      if (total < UINT8_MAX) ++total;
      if (copied >= HMI_EVENT_DISPLAY_CAPACITY) continue;
      HmiEventItem &dst = out.items[copied++];
      dst.sequence = e.sequence;
      dst.epoch = e.epoch;
      dst.ageSec = age;
      dst.code = e.code;
      dst.value = e.value;
      dst.type = e.type;
      dst.flags = e.flags;
    }
    out.count = copied;
    out.totalInWindow = total;
  }

  // Chi duoc goi tu Control Task. Tra cac su kien moi theo thu tu cu -> moi,
  // khong xoa log HMI. afterSequence duoc cap nhat boi nguoi goi.
  uint8_t copyAfter(uint32_t afterSequence, EventEntry *output,
                    uint8_t capacity) const {
    if (!output || capacity == 0U || count_ == 0U) return 0U;
    uint8_t copied = 0U;
    const uint8_t oldest = static_cast<uint8_t>(
        (head_ + EVENT_LOG_RAM_SIZE - count_) % EVENT_LOG_RAM_SIZE);
    for (uint8_t i = 0U; i < count_ && copied < capacity; ++i) {
      const uint8_t index = static_cast<uint8_t>((oldest + i) % EVENT_LOG_RAM_SIZE);
      const EventEntry &entry = entries_[index];
      if (static_cast<int32_t>(entry.sequence - afterSequence) <= 0) continue;
      output[copied++] = entry;
    }
    return copied;
  }
 private:
  EventEntry entries_[EVENT_LOG_RAM_SIZE]{};
  uint8_t head_ = 0U;
  uint8_t count_ = 0U;
  uint32_t sequence_ = 0U;
  uint32_t bootAt_ = 0U;
  uint32_t clockEpoch_ = 0U;
  uint32_t clockSyncedAtMs_ = 0U;
};

// ============================================================================
// FAULT MANAGER: MOT NOI DUY NHAT CHUYEN LOI -> ALARM/HEAT INHIBIT/LOG
// ============================================================================
// FaultSeverity / FaultCode / FaultDescriptor / faultDescriptor() da chuyen
// sang config.h de HMI dung chung dung mot bang. Xem "BANG LOI DUNG CHUNG".

struct FaultState {
  FaultCode code = FaultCode::None;
  bool condition = false;
  bool active = false;
  bool acknowledged = false;
  uint32_t firstAt = 0U;
  uint32_t lastAt = 0U;
  uint16_t occurrences = 0U;
  int16_t detail = 0;
};

class FaultManager {
 public:
  explicit FaultManager(EventLog *log = nullptr) : log_(log) {}
  void attachLog(EventLog *log) { log_ = log; }

  void set(FaultCode code, bool condition, uint32_t now, int16_t detail = 0) {
    if (code == FaultCode::None) return;
    FaultState &state = slot(code);
    const FaultDescriptor &desc = faultDescriptor(code);
    if (condition) {
      state.condition = true;
      state.detail = detail;
      state.lastAt = now;
      if (!state.active) {
        state.active = true;
        state.acknowledged = false;
        state.firstAt = now;
        if (state.occurrences < UINT16_MAX) ++state.occurrences;
        ++notificationSequence_;
        lastRaisedCode_ = code;
        lastRaisedSeverity_ = desc.severity;
        if (log_) log_->push(now, EventType::FaultRaised,
            static_cast<uint16_t>(EventCode::FaultBase) + static_cast<uint16_t>(code),
            detail, static_cast<uint8_t>(desc.severity));
        mayapSerialPrintf(false, "[FAULT] SET %u %s detail=%d\n",
                         static_cast<unsigned>(code), desc.text, detail);
      }
      return;
    }

    state.condition = false;
    if (!state.active) return;
    if (desc.latching && !state.acknowledged) return;
    clearState(state, now);
  }

  bool acknowledge(uint32_t now, uint32_t alarmMask = ALARM_KNOWN_MASK) {
    bool changed = false;
    for (uint8_t i = 0; i < count_; ++i) {
      FaultState &state = states_[i];
      if (!state.active) continue;
      const FaultDescriptor &desc = faultDescriptor(state.code);
      if (!(desc.alarmBit & alarmMask)) continue;
      state.acknowledged = true;
      changed = true;
      if (log_) log_->push(now, EventType::FaultAck,
          static_cast<uint16_t>(EventCode::FaultBase) + static_cast<uint16_t>(state.code),
          state.detail, static_cast<uint8_t>(desc.severity));
      if (!state.condition) clearState(state, now);
    }
    return changed;
  }

  bool active(FaultCode code) const {
    const FaultState *state = findConst(code);
    return state && state->active;
  }
  uint32_t alarmMask() const {
    uint32_t mask = AlarmNone;
    for (uint8_t i = 0; i < count_; ++i) {
      if (states_[i].active) mask |= faultDescriptor(states_[i].code).alarmBit;
    }
    return mask;
  }
  bool heatInhibited() const {
    for (uint8_t i = 0; i < count_; ++i) {
      if (states_[i].active && faultDescriptor(states_[i].code).inhibitsHeat) return true;
    }
    return false;
  }
  bool turningInhibited() const {
    for (uint8_t i = 0; i < count_; ++i) {
      if (states_[i].active && faultDescriptor(states_[i].code).inhibitsTurning) return true;
    }
    return false;
  }
  FaultCode primary() const {
    FaultCode best = FaultCode::None;
    uint8_t priority = 0U;
    for (uint8_t i = 0; i < count_; ++i) {
      if (!states_[i].active) continue;
      const uint8_t candidate = faultDescriptor(states_[i].code).displayPriority;
      if (best == FaultCode::None || candidate > priority) {
        best = states_[i].code;
        priority = candidate;
      }
    }
    return best;
  }
  uint8_t activeCount() const {
    uint8_t active = 0U;
    for (uint8_t i = 0; i < count_; ++i) if (states_[i].active) ++active;
    return active;
  }
  void print() const {
    mayapSerialPrintf(false, "--- FAULTS active=%u ---\n", activeCount());
    for (uint8_t i = 0; i < count_; ++i) {
      const FaultState &state = states_[i];
      if (!state.active) continue;
      const FaultDescriptor &desc = faultDescriptor(state.code);
      mayapSerialPrintf(false, "code=%u sev=%u cond=%u ack=%u count=%u detail=%d %s\n",
          static_cast<unsigned>(state.code), static_cast<unsigned>(desc.severity),
          state.condition, state.acknowledged, state.occurrences,
          state.detail, desc.text);
    }
  }

  uint32_t notificationSequence() const { return notificationSequence_; }
  FaultCode lastRaisedCode() const { return lastRaisedCode_; }
  FaultSeverity lastRaisedSeverity() const { return lastRaisedSeverity_; }

  static constexpr uint8_t faultCapacity() { return MAX_FAULTS; }

  uint8_t copyActiveForHmi(HmiFaultItem *out, uint8_t capacity) const {
    if (!out || !capacity) return 0U;
    bool used[MAX_FAULTS]{};
    uint8_t written = 0U;
    while (written < capacity) {
      int bestIndex = -1;
      uint8_t bestPriority = 0U;
      for (uint8_t i = 0U; i < count_; ++i) {
        if (used[i] || !states_[i].active) continue;
        const uint8_t candidate = faultDescriptor(states_[i].code).displayPriority;
        if (bestIndex < 0 || candidate > bestPriority) {
          bestIndex = i;
          bestPriority = candidate;
        }
      }
      if (bestIndex < 0) break;
      used[bestIndex] = true;
      const FaultState &state = states_[bestIndex];
      const FaultDescriptor &desc = faultDescriptor(state.code);
      HmiFaultItem &dst = out[written++];
      dst.code = static_cast<uint16_t>(state.code);
      dst.detail = state.detail;
      dst.severity = static_cast<uint8_t>(desc.severity);
      dst.flags = static_cast<uint8_t>((state.condition ? 0x01U : 0U) |
                                      (state.acknowledged ? 0x02U : 0U));
    }
    return written;
  }

 private:
  // Phai >= tong so ma loi trong bang FaultDescriptor cua config.h. Bang co 26
  // ma (them 104 SensorStuck, 401 HeaterRunaway, 402 HeaterNoResponse) nen 24
  // se lam hai ma cuoi dung chung mot slot va che lan nhau. De du 32.
  static constexpr uint8_t MAX_FAULTS = 32U;
  FaultState &slot(FaultCode code) {
    for (uint8_t i = 0; i < count_; ++i) if (states_[i].code == code) return states_[i];
    if (count_ < MAX_FAULTS) {
      states_[count_].code = code;
      return states_[count_++];
    }
    // Bang day: dung slot cuoi cho loi moi. Day la tinh huong bat thuong nhung van fail-safe.
    states_[MAX_FAULTS - 1U].code = code;
    return states_[MAX_FAULTS - 1U];
  }
  FaultState *find(FaultCode code) {
    for (uint8_t i = 0; i < count_; ++i) if (states_[i].code == code) return &states_[i];
    return nullptr;
  }
  const FaultState *findConst(FaultCode code) const {
    for (uint8_t i = 0; i < count_; ++i) if (states_[i].code == code) return &states_[i];
    return nullptr;
  }
  void clearState(FaultState &state, uint32_t now) {
    const FaultDescriptor &desc = faultDescriptor(state.code);
    if (log_) log_->push(now, EventType::FaultCleared,
        static_cast<uint16_t>(EventCode::FaultBase) + static_cast<uint16_t>(state.code),
        state.detail, static_cast<uint8_t>(desc.severity));
    mayapSerialPrintf(false, "[FAULT] CLEAR %u %s\n",
                     static_cast<unsigned>(state.code), desc.text);
    state.active = false;
    state.acknowledged = false;
    state.lastAt = now;
  }

  FaultState states_[MAX_FAULTS]{};
  uint8_t count_ = 0U;
  EventLog *log_ = nullptr;
  uint32_t notificationSequence_ = 0U;
  FaultCode lastRaisedCode_ = FaultCode::None;
  FaultSeverity lastRaisedSeverity_ = FaultSeverity::Info;
};

// ============================================================================
// POWER MANAGER: PHAN LOAI RESET, DEM BOOT/BROWNOUT/WDT VA QUYET DINH ACK
// ============================================================================
RTC_DATA_ATTR static uint32_t gMayapResetStormMagic = 0U;
RTC_DATA_ATTR static uint8_t gMayapConsecutiveAutoResets = 0U;
constexpr uint32_t RESET_STORM_MAGIC = 0x5253544DUL; // RSTM

class PowerManager {
 public:
  void begin(uint32_t now, EventLog &log) {
    bootAt_ = now;
    reason_ = esp_reset_reason();
    if (gMayapResetStormMagic != RESET_STORM_MAGIC) {
      gMayapResetStormMagic = RESET_STORM_MAGIC;
      gMayapConsecutiveAutoResets = 0U;
    }
    if (resetReasonIsAutomaticRecovery(reason_)) {
      if (gMayapConsecutiveAutoResets < UINT8_MAX) ++gMayapConsecutiveAutoResets;
    } else {
      gMayapConsecutiveAutoResets = 0U;
    }
    ackRequired_ = gMayapConsecutiveAutoResets >= RESET_STORM_LIMIT;
    log.push(now, EventType::Boot, bootEventCode(reason_),
             static_cast<int16_t>(reason_), ackRequired_ ? 1U : 0U);
  }
  void service(uint32_t now) {
    if (!ackRequired_ && gMayapConsecutiveAutoResets != 0U &&
        elapsedMs(now, bootAt_) >= RESET_STORM_STABLE_CLEAR_MS) {
      gMayapConsecutiveAutoResets = 0U;
    }
  }
  esp_reset_reason_t reason() const { return reason_; }
  bool ackRequired() const { return ackRequired_; }
  uint8_t resetStormCount() const { return gMayapConsecutiveAutoResets; }
  void acknowledge() {
    ackRequired_ = false;
    gMayapConsecutiveAutoResets = 0U;
  }
 private:
  static uint16_t bootEventCode(esp_reset_reason_t reason) {
    switch (reason) {
      case ESP_RST_POWERON: return static_cast<uint16_t>(EventCode::BootPowerOn);
      case ESP_RST_EXT: return static_cast<uint16_t>(EventCode::BootExternal);
      case ESP_RST_SW: return static_cast<uint16_t>(EventCode::BootSoftware);
      case ESP_RST_PANIC: return static_cast<uint16_t>(EventCode::BootPanic);
      case ESP_RST_INT_WDT:
      case ESP_RST_TASK_WDT:
      case ESP_RST_WDT: return static_cast<uint16_t>(EventCode::BootWdt);
      case ESP_RST_BROWNOUT: return static_cast<uint16_t>(EventCode::BootBrownout);
      default: return static_cast<uint16_t>(EventCode::BootSoftware);
    }
  }
  esp_reset_reason_t reason_ = ESP_RST_UNKNOWN;
  bool ackRequired_ = false;
  uint32_t bootAt_ = 0U;
};

// ============================================================================
// DS3231: DOC CO TIMEOUT, KIEM TRA OSF, KHONG DUNG WIFI/RTC
// ============================================================================
class RtcDs3231 {
 public:
  void begin(uint32_t now) {
    lastReadAt_ = now - RTC_READ_PERIOD_MS;
    update(now);
  }

  void update(uint32_t now) {
    if (elapsedMs(now, lastReadAt_) < RTC_READ_PERIOD_MS) return;
    lastReadAt_ = now;
    DateTime next{};
    bool osf = true;
    if (!readDateTime(next) || !readOsf(osf)) {
      if (failedReads_ < 255U) ++failedReads_;
      if (failedReads_ >= 3U) { online_ = false; valid_ = false; }
      return;
    }
    failedReads_ = 0U;
    online_ = true;
    osf_ = osf;
    const uint32_t nextEpoch = validDate(next) ? toEpoch(next) : 0U;
    if (nextEpoch != 0U && nextEpoch != epoch_) lastEpochChangeAt_ = now;
    const bool clockAdvancing = lastEpochChangeAt_ != 0U &&
        elapsedMs(now, lastEpochChangeAt_) <= RTC_STUCK_TIMEOUT_MS;
    valid_ = !osf && nextEpoch != 0U && clockAdvancing;
    if (!valid_) return;
    date_ = next;
    epoch_ = nextEpoch;
    snprintf(dateText_, sizeof(dateText_), "%02u/%02u/%04u",
             static_cast<unsigned>(next.day % 100U),
             static_cast<unsigned>(next.month % 100U),
             static_cast<unsigned>(next.year % 10000U));
  }

  bool set(uint16_t year, uint8_t month, uint8_t day,
           uint8_t hour, uint8_t minute, uint8_t second) {
    DateTime value{year, month, day, hour, minute, second};
    if (!validDate(value)) return false;
    uint8_t data[7] = {
      toBcd(second), toBcd(minute), toBcd(hour), 1U,
      toBcd(day), toBcd(month), toBcd(static_cast<uint8_t>(year - 2000U))
    };
    if (!writeRegisters(0x00U, data, sizeof(data))) return false;
    uint8_t status = 0U;
    if (!readRegisters(0x0FU, &status, 1U)) return false;
    status &= static_cast<uint8_t>(~0x80U); // clear OSF
    if (!writeRegisters(0x0FU, &status, 1U)) return false;
    failedReads_ = 0U;
    online_ = true;
    osf_ = false;
    valid_ = true;
    date_ = value;
    epoch_ = toEpoch(value);
    lastEpochChangeAt_ = millis();
    snprintf(dateText_, sizeof(dateText_), "%02u/%02u/%04u",
             static_cast<unsigned>(day % 100U),
             static_cast<unsigned>(month % 100U),
             static_cast<unsigned>(year % 10000U));
    return true;
  }

  bool online() const { return online_; }
  bool valid() const { return valid_; }
  bool oscillatorStopped() const { return osf_; }
  uint32_t epoch() const { return valid_ ? epoch_ : 0U; }
  const char *dateText() const { return valid_ ? dateText_ : "--/--/----"; }

 private:
  struct DateTime {
    uint16_t year = 2000U;
    uint8_t month = 1U;
    uint8_t day = 1U;
    uint8_t hour = 0U;
    uint8_t minute = 0U;
    uint8_t second = 0U;
  };

  static uint8_t fromBcd(uint8_t v) { return static_cast<uint8_t>((v >> 4U) * 10U + (v & 0x0FU)); }
  static uint8_t toBcd(uint8_t v) { return static_cast<uint8_t>(((v / 10U) << 4U) | (v % 10U)); }
  static bool leap(uint16_t y) { return (y % 4U == 0U && y % 100U != 0U) || (y % 400U == 0U); }
  static uint8_t daysInMonth(uint16_t y, uint8_t m) {
    static const uint8_t days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (m < 1U || m > 12U) return 0U;
    return static_cast<uint8_t>(days[m - 1U] + ((m == 2U && leap(y)) ? 1U : 0U));
  }
  static bool validDate(const DateTime &d) {
    return d.year >= RTC_VALID_YEAR_MIN && d.year <= RTC_VALID_YEAR_MAX &&
           d.month >= 1U && d.month <= 12U && d.day >= 1U &&
           d.day <= daysInMonth(d.year, d.month) && d.hour < 24U &&
           d.minute < 60U && d.second < 60U;
  }
  static uint32_t toEpoch(const DateTime &d) {
    uint32_t days = 0U;
    for (uint16_t y = 1970U; y < d.year; ++y) days += leap(y) ? 366U : 365U;
    for (uint8_t m = 1U; m < d.month; ++m) days += daysInMonth(d.year, m);
    days += static_cast<uint32_t>(d.day - 1U);
    return days * 86400UL + static_cast<uint32_t>(d.hour) * 3600UL +
           static_cast<uint32_t>(d.minute) * 60UL + d.second;
  }
  bool readDateTime(DateTime &out) const {
    uint8_t data[7]{};
    if (!readRegisters(0x00U, data, sizeof(data))) return false;
    out.second = fromBcd(data[0] & 0x7FU);
    out.minute = fromBcd(data[1] & 0x7FU);
    out.hour = fromBcd(data[2] & 0x3FU); // 24-hour mode expected
    out.day = fromBcd(data[4] & 0x3FU);
    out.month = fromBcd(data[5] & 0x1FU);
    out.year = static_cast<uint16_t>(2000U + fromBcd(data[6]));
    return true;
  }
  bool readOsf(bool &osf) const {
    uint8_t status = 0U;
    if (!readRegisters(0x0FU, &status, 1U)) return false;
    osf = (status & 0x80U) != 0U;
    return true;
  }
  static bool readRegisters(uint8_t reg, uint8_t *data, size_t length) {
    if (!data || !length || length > 32U || !mayapI2cLock(I2C_TIMEOUT_MS)) return false;
    Wire.beginTransmission(RTC_I2C_ADDRESS);
    Wire.write(reg);
    const uint8_t err = Wire.endTransmission(false);
    bool ok = err == 0U;
    if (ok) {
      const size_t got = Wire.requestFrom(RTC_I2C_ADDRESS,
                                          static_cast<uint8_t>(length), true);
      ok = got == length;
      if (ok) {
        for (size_t i = 0; i < length; ++i) {
          data[i] = static_cast<uint8_t>(Wire.read());
        }
      } else {
        while (Wire.available()) (void)Wire.read();
      }
    }
    mayapI2cUnlock();
    mayapI2cRecordResult(ok);
    return ok;
  }
  static bool writeRegisters(uint8_t reg, const uint8_t *data, size_t length) {
    if (!data || !length || length > 30U || !mayapI2cLock(I2C_TIMEOUT_MS)) return false;
    Wire.beginTransmission(RTC_I2C_ADDRESS);
    Wire.write(reg);
    const size_t written = Wire.write(data, length);
    const uint8_t err = Wire.endTransmission(true);
    mayapI2cUnlock();
    const bool ok = written == length && err == 0U;
    mayapI2cRecordResult(ok);
    return ok;
  }

  DateTime date_{};
  uint32_t epoch_ = 0U;
  uint32_t lastReadAt_ = 0U;
  uint32_t lastEpochChangeAt_ = 0U;
  uint8_t failedReads_ = 0U;
  bool online_ = false;
  bool valid_ = false;
  bool osf_ = true;
  char dateText_[11] = "--/--/----";
};

// ============================================================================
// SANITIZE CAU HINH - DONG BO RANG BUOC VOI HMI v3.3.1
// ============================================================================
// Giu ten cu de 9 diem goi khong phai sua. Toan bo gioi han da chuyen sang
// mayapSanitizeConfig() trong config.h - nguon duy nhat cho ca HMI va Web.
inline void sanitizeMachineConfig(MachineConfig &cfg) {
  mayapSanitizeConfig(cfg);
}

// ============================================================================
// LUU AT24C32 HAI KHE + CRC. MAT DIEN KHI GHI VAN CON BAN CU HOP LE.
// ============================================================================
#pragma pack(push, 1)
// Schema 3: dung de doc lai cau hinh cua firmware v3.3.1, khong ghi moi.
struct PackedMachineConfigV3 {
  float targetTemp;
  float tempHysteresis;
  float lowTempAlarm;
  float highTempAlarm;
  float emergencyTemp;
  uint8_t controlMode;
  float kp;
  float ki;
  float kd;
  uint16_t pidCycleSec;
  uint8_t maxHeaterPower;
  float lowHumidityAlarm;
  uint16_t humidityAlarmDelaySec;
  uint8_t circulationFanEnabled;
  float ventOnTemp;
  float ventOffTemp;
  uint8_t turningEnabled;
  uint16_t turnIntervalMin;
  uint16_t turnMaxRunSec;
  uint8_t nextDirection;
  uint8_t totalIncubationDays;
  uint8_t allowHeatWithoutBatch;
  uint16_t powerRestoreDelaySec;
  float tempOffset;
  float humidityOffset;
  uint16_t sensorTimeoutSec;
  uint8_t alarmEnabled;
};
struct ConfigRecordV3 {
  uint32_t magic;
  uint16_t schema;
  uint16_t size;
  uint32_t sequence;
  PackedMachineConfigV3 payload;
  uint32_t crc;
};

// Schema 4: them autoResumeAfterPower. Ghi hai khe A/B nhu cu.
struct PackedMachineConfigV4 {
  float targetTemp;
  float tempHysteresis;
  float lowTempAlarm;
  float highTempAlarm;
  float emergencyTemp;
  uint8_t controlMode;
  float kp;
  float ki;
  float kd;
  uint16_t pidCycleSec;
  uint8_t maxHeaterPower;
  float lowHumidityAlarm;
  uint16_t humidityAlarmDelaySec;
  uint8_t circulationFanEnabled;
  float ventOnTemp;
  float ventOffTemp;
  uint8_t turningEnabled;
  uint16_t turnIntervalMin;
  uint16_t turnMaxRunSec;
  uint8_t nextDirection;
  uint8_t totalIncubationDays;
  uint8_t autoResumeAfterPower;
  uint8_t allowHeatWithoutBatch;
  uint16_t powerRestoreDelaySec;
  float tempOffset;
  float humidityOffset;
  uint16_t sensorTimeoutSec;
  uint8_t alarmEnabled;
};
struct ConfigRecordV4 {
  uint32_t magic;
  uint16_t schema;
  uint16_t size;
  uint32_t sequence;
  PackedMachineConfigV4 payload;
  uint32_t crc;
};
// Schema 5: them cloudEnabled (ONLINE/OFFLINE chon tren HMI).
// Doc nguoc duoc V4 va V3; chi GHI theo V5.
struct PackedMachineConfigV5 {
  float targetTemp;
  float tempHysteresis;
  float lowTempAlarm;
  float highTempAlarm;
  float emergencyTemp;
  uint8_t controlMode;
  float kp;
  float ki;
  float kd;
  uint16_t pidCycleSec;
  uint8_t maxHeaterPower;
  float lowHumidityAlarm;
  uint16_t humidityAlarmDelaySec;
  uint8_t circulationFanEnabled;
  float ventOnTemp;
  float ventOffTemp;
  uint8_t turningEnabled;
  uint16_t turnIntervalMin;
  uint16_t turnMaxRunSec;
  uint8_t nextDirection;
  uint8_t totalIncubationDays;
  uint8_t autoResumeAfterPower;
  uint8_t allowHeatWithoutBatch;
  uint16_t powerRestoreDelaySec;
  float tempOffset;
  float humidityOffset;
  uint16_t sensorTimeoutSec;
  uint8_t alarmEnabled;
  uint8_t cloudEnabled;
};
struct ConfigRecordV5 {
  uint32_t magic;
  uint16_t schema;
  uint16_t size;
  uint32_t sequence;
  PackedMachineConfigV5 payload;
  uint32_t crc;
};
struct RecordHeader {
  uint32_t magic;
  uint16_t schema;
  uint16_t size;
  uint32_t sequence;
};
struct PackedBatchV1 {
  uint8_t wasRunning;
  uint8_t nextDirection;
  uint16_t turnCountToday;
  uint32_t turnCountBatch;
  uint32_t elapsedSec;
  uint32_t checkpointEpoch; // DS3231 epoch tai checkpoint, 0 neu RTC loi
};
struct BatchRecordV1 {
  uint32_t magic;
  uint16_t schema;
  uint16_t size;
  uint32_t sequence;
  PackedBatchV1 payload;
  uint32_t crc;
};
#pragma pack(pop)

constexpr uint32_t CONFIG_MAGIC = 0x4D415943UL; // MAYC
constexpr uint32_t BATCH_MAGIC  = 0x4D415942UL; // MAYB
constexpr uint16_t CONFIG_SCHEMA_V3 = 3;
constexpr uint16_t CONFIG_SCHEMA_V4 = 4;
constexpr uint16_t CONFIG_SCHEMA = 5;
constexpr uint16_t BATCH_SCHEMA = 2;

inline PackedMachineConfigV5 packConfig(const MachineConfig &c) {
  PackedMachineConfigV5 p{};
  p.targetTemp = c.targetTemp;
  p.tempHysteresis = c.tempHysteresis;
  p.lowTempAlarm = c.lowTempAlarm;
  p.highTempAlarm = c.highTempAlarm;
  p.emergencyTemp = c.emergencyTemp;
  p.controlMode = static_cast<uint8_t>(c.controlMode);
  p.kp = c.kp; p.ki = c.ki; p.kd = c.kd;
  p.pidCycleSec = c.pidCycleSec;
  p.maxHeaterPower = c.maxHeaterPower;
  p.lowHumidityAlarm = c.lowHumidityAlarm;
  p.humidityAlarmDelaySec = c.humidityAlarmDelaySec;
  p.circulationFanEnabled = c.circulationFanEnabled ? 1U : 0U;
  p.ventOnTemp = c.ventOnTemp;
  p.ventOffTemp = c.ventOffTemp;
  p.turningEnabled = c.turningEnabled ? 1U : 0U;
  p.turnIntervalMin = c.turnIntervalMin;
  p.turnMaxRunSec = c.turnMaxRunSec;
  p.nextDirection = static_cast<uint8_t>(c.nextDirection);
  p.totalIncubationDays = c.totalIncubationDays;
  p.autoResumeAfterPower = c.autoResumeAfterPower ? 1U : 0U;
  p.allowHeatWithoutBatch = c.allowHeatWithoutBatch ? 1U : 0U;
  p.powerRestoreDelaySec = c.powerRestoreDelaySec;
  p.tempOffset = c.tempOffset;
  p.humidityOffset = c.humidityOffset;
  p.sensorTimeoutSec = c.sensorTimeoutSec;
  p.alarmEnabled = c.alarmEnabled ? 1U : 0U;
  p.cloudEnabled = c.cloudEnabled ? 1U : 0U;
  return p;
}

inline MachineConfig unpackConfigV5(const PackedMachineConfigV5 &p) {
  MachineConfig c{};
  c.targetTemp = p.targetTemp;
  c.tempHysteresis = p.tempHysteresis;
  c.lowTempAlarm = p.lowTempAlarm;
  c.highTempAlarm = p.highTempAlarm;
  c.emergencyTemp = p.emergencyTemp;
  c.controlMode = static_cast<ControlMode>(p.controlMode);
  c.kp = p.kp; c.ki = p.ki; c.kd = p.kd;
  c.pidCycleSec = p.pidCycleSec;
  c.maxHeaterPower = p.maxHeaterPower;
  c.lowHumidityAlarm = p.lowHumidityAlarm;
  c.humidityAlarmDelaySec = p.humidityAlarmDelaySec;
  c.circulationFanEnabled = p.circulationFanEnabled != 0U;
  c.ventOnTemp = p.ventOnTemp;
  c.ventOffTemp = p.ventOffTemp;
  c.turningEnabled = p.turningEnabled != 0U;
  c.turnIntervalMin = p.turnIntervalMin;
  c.turnMaxRunSec = p.turnMaxRunSec;
  c.nextDirection = static_cast<TurnDirection>(p.nextDirection);
  c.totalIncubationDays = p.totalIncubationDays;
  c.autoResumeAfterPower = p.autoResumeAfterPower != 0U;
  c.allowHeatWithoutBatch = p.allowHeatWithoutBatch != 0U;
  c.powerRestoreDelaySec = p.powerRestoreDelaySec;
  c.tempOffset = p.tempOffset;
  c.humidityOffset = p.humidityOffset;
  c.sensorTimeoutSec = p.sensorTimeoutSec;
  c.alarmEnabled = p.alarmEnabled != 0U;
  c.cloudEnabled = p.cloudEnabled != 0U;
  sanitizeMachineConfig(c);
  return c;
}

inline MachineConfig unpackConfigV4(const PackedMachineConfigV4 &p) {
  MachineConfig c{};
  c.targetTemp = p.targetTemp;
  c.tempHysteresis = p.tempHysteresis;
  c.lowTempAlarm = p.lowTempAlarm;
  c.highTempAlarm = p.highTempAlarm;
  c.emergencyTemp = p.emergencyTemp;
  c.controlMode = static_cast<ControlMode>(p.controlMode);
  c.kp = p.kp; c.ki = p.ki; c.kd = p.kd;
  c.pidCycleSec = p.pidCycleSec;
  c.maxHeaterPower = p.maxHeaterPower;
  c.lowHumidityAlarm = p.lowHumidityAlarm;
  c.humidityAlarmDelaySec = p.humidityAlarmDelaySec;
  c.circulationFanEnabled = p.circulationFanEnabled != 0U;
  c.ventOnTemp = p.ventOnTemp;
  c.ventOffTemp = p.ventOffTemp;
  c.turningEnabled = p.turningEnabled != 0U;
  c.turnIntervalMin = p.turnIntervalMin;
  c.turnMaxRunSec = p.turnMaxRunSec;
  c.nextDirection = static_cast<TurnDirection>(p.nextDirection);
  c.totalIncubationDays = p.totalIncubationDays;
  c.autoResumeAfterPower = p.autoResumeAfterPower != 0U;
  c.allowHeatWithoutBatch = p.allowHeatWithoutBatch != 0U;
  c.powerRestoreDelaySec = p.powerRestoreDelaySec;
  c.tempOffset = p.tempOffset;
  c.humidityOffset = p.humidityOffset;
  c.sensorTimeoutSec = p.sensorTimeoutSec;
  c.alarmEnabled = p.alarmEnabled != 0U;
  c.cloudEnabled = true; // Ban cu chua co truong nay: giu mac dinh ONLINE.
  sanitizeMachineConfig(c);
  return c;
}

inline MachineConfig unpackConfigV3(const PackedMachineConfigV3 &p) {
  MachineConfig c{};
  c.targetTemp = p.targetTemp;
  c.tempHysteresis = p.tempHysteresis;
  c.lowTempAlarm = p.lowTempAlarm;
  c.highTempAlarm = p.highTempAlarm;
  c.emergencyTemp = p.emergencyTemp;
  c.controlMode = static_cast<ControlMode>(p.controlMode);
  c.kp = p.kp; c.ki = p.ki; c.kd = p.kd;
  c.pidCycleSec = p.pidCycleSec;
  c.maxHeaterPower = p.maxHeaterPower;
  c.lowHumidityAlarm = p.lowHumidityAlarm;
  c.humidityAlarmDelaySec = p.humidityAlarmDelaySec;
  c.circulationFanEnabled = p.circulationFanEnabled != 0U;
  c.ventOnTemp = p.ventOnTemp;
  c.ventOffTemp = p.ventOffTemp;
  c.turningEnabled = p.turningEnabled != 0U;
  c.turnIntervalMin = p.turnIntervalMin;
  c.turnMaxRunSec = p.turnMaxRunSec;
  c.nextDirection = static_cast<TurnDirection>(p.nextDirection);
  c.totalIncubationDays = p.totalIncubationDays;
  c.autoResumeAfterPower = false; // Giu dung chinh sach cua firmware cu.
  c.allowHeatWithoutBatch = p.allowHeatWithoutBatch != 0U;
  c.powerRestoreDelaySec = p.powerRestoreDelaySec;
  c.tempOffset = p.tempOffset;
  c.humidityOffset = p.humidityOffset;
  c.sensorTimeoutSec = p.sensorTimeoutSec;
  c.alarmEnabled = p.alarmEnabled != 0U;
  c.cloudEnabled = true;
  sanitizeMachineConfig(c);
  return c;
}

inline bool machineConfigEqual(const MachineConfig &a, const MachineConfig &b) {
  const PackedMachineConfigV5 pa = packConfig(a);
  const PackedMachineConfigV5 pb = packConfig(b);
  return memcmp(&pa, &pb, sizeof(pa)) == 0;
}

class ExternalEeprom24xx {
 public:
  bool begin() const { return probe(); }

  bool readBytes(uint16_t address, void *destination, size_t length) const {
    if (!destination || !rangeValid(address, length)) return false;
    if (!mayapI2cLock(I2C_TIMEOUT_MS)) return false;
    uint8_t *out = static_cast<uint8_t *>(destination);
    bool ok = true;
    while (length && ok) {
      const uint8_t chunk = static_cast<uint8_t>(std::min<size_t>(length, 32U));
      Wire.beginTransmission(EEPROM_I2C_ADDRESS);
      Wire.write(static_cast<uint8_t>(address >> 8U));
      Wire.write(static_cast<uint8_t>(address & 0xFFU));
      if (Wire.endTransmission(false) != 0U) { ok = false; break; }
      const size_t got = Wire.requestFrom(EEPROM_I2C_ADDRESS, chunk, true);
      if (got != chunk) {
        while (Wire.available()) (void)Wire.read();
        ok = false;
        break;
      }
      for (uint8_t i = 0U; i < chunk; ++i) out[i] = static_cast<uint8_t>(Wire.read());
      address = static_cast<uint16_t>(address + chunk);
      out += chunk;
      length -= chunk;
    }
    mayapI2cUnlock();
    mayapI2cRecordResult(ok);
    return ok;
  }

  bool writeBytes(uint16_t address, const void *source, size_t length) const {
    if (!source || !rangeValid(address, length)) return false;
    if (!mayapI2cLock(I2C_TIMEOUT_MS)) return false;
    const uint8_t *in = static_cast<const uint8_t *>(source);
    bool ok = true;
    while (length && ok) {
      const uint8_t pageRemain = static_cast<uint8_t>(EEPROM_PAGE_SIZE -
                                      (address % EEPROM_PAGE_SIZE));
      const uint8_t chunk = static_cast<uint8_t>(std::min<size_t>(length, pageRemain));
      Wire.beginTransmission(EEPROM_I2C_ADDRESS);
      Wire.write(static_cast<uint8_t>(address >> 8U));
      Wire.write(static_cast<uint8_t>(address & 0xFFU));
      const size_t written = Wire.write(in, chunk);
      const uint8_t err = Wire.endTransmission(true);
      if (written != chunk || err != 0U || !waitWriteCompleteLocked()) {
        ok = false;
        break;
      }
      address = static_cast<uint16_t>(address + chunk);
      in += chunk;
      length -= chunk;
    }
    mayapI2cUnlock();
    mayapI2cRecordResult(ok);
    return ok;
  }

 private:
  static bool rangeValid(uint16_t address, size_t length) {
    return length <= EEPROM_CAPACITY_BYTES &&
           address <= static_cast<uint16_t>(EEPROM_CAPACITY_BYTES - length);
  }
  static bool probeLocked() {
    Wire.beginTransmission(EEPROM_I2C_ADDRESS);
    return Wire.endTransmission(true) == 0U;
  }
  static bool probe() {
    if (!mayapI2cLock(I2C_TIMEOUT_MS)) return false;
    const bool ok = probeLocked();
    mayapI2cUnlock();
    mayapI2cRecordResult(ok);
    return ok;
  }
  static bool waitWriteCompleteLocked() {
    const uint32_t started = millis();
    do {
      if (probeLocked()) return true;
      vTaskDelay(pdMS_TO_TICKS(1));
    } while (elapsedMs(millis(), started) < EEPROM_WRITE_TIMEOUT_MS);
    return false;
  }
};

static_assert(sizeof(ConfigRecordV5) <= EEPROM_CONFIG_SLOT_BYTES,
              "Config record khong vua slot AT24C32");
static_assert(sizeof(ConfigRecordV3) <= EEPROM_CONFIG_SLOT_BYTES,
              "Config record cu khong vua slot AT24C32");
static_assert(sizeof(BatchRecordV1) <= EEPROM_BATCH_SLOT_BYTES,
              "Batch record khong vua slot AT24C32");

class PersistentStore {
 public:
  bool begin() {
    ready_ = !EXTERNAL_EEPROM_ENABLED || eeprom_.begin();
    return ready_ || !EXTERNAL_EEPROM_REQUIRED;
  }
  bool ready() const { return ready_; }

  bool loadConfig(MachineConfig &out) {
    if (!ready_) return false;
    ConfigSlot a{}, b{};
    const bool va = readConfigSlot(EEPROM_ADDR_CONFIG_A, a);
    const bool vb = readConfigSlot(EEPROM_ADDR_CONFIG_B, b);
    if (!va && !vb) return false;
    const ConfigSlot &best = (!vb || (va && newer(a.sequence, b.sequence))) ? a : b;
    out = best.config;
    return true;
  }

  bool saveConfig(const MachineConfig &input, MachineConfig &readback) {
    if (!ready_) return false;
    MachineConfig clean = input;
    sanitizeMachineConfig(clean);

    ConfigSlot a{}, b{};
    const bool va = readConfigSlot(EEPROM_ADDR_CONFIG_A, a);
    const bool vb = readConfigSlot(EEPROM_ADDR_CONFIG_B, b);
    const ConfigSlot *current = nullptr;
    if (va || vb) current = (!vb || (va && newer(a.sequence, b.sequence))) ? &a : &b;
    if (current && machineConfigEqual(current->config, clean)) {
      readback = clean;
      return true;
    }

    ConfigRecordV5 record{};
    record.magic = CONFIG_MAGIC;
    record.schema = CONFIG_SCHEMA;
    record.size = sizeof(record);
    record.sequence = current ? current->sequence + 1U : 1U;
    record.payload = packConfig(clean);
    record.crc = mcCrc32(reinterpret_cast<const uint8_t *>(&record),
                         offsetof(ConfigRecordV5, crc));

    const bool currentIsA = va && (!vb || newer(a.sequence, b.sequence));
    const uint16_t target = currentIsA ? EEPROM_ADDR_CONFIG_B : EEPROM_ADDR_CONFIG_A;
    if (!writeRecord(target, record)) return false;

    ConfigRecordV5 verify{};
    if (!readRecord(target, verify) || !validConfigV5(verify) ||
        verify.sequence != record.sequence) return false;
    readback = unpackConfigV5(verify.payload);
    return true;
  }

  bool loadBatch(PackedBatchV1 &out) {
    if (!ready_) return false;
    BatchRecordV1 a{}, b{};
    const bool va = readRecord(EEPROM_ADDR_BATCH_A, a) && validBatch(a);
    const bool vb = readRecord(EEPROM_ADDR_BATCH_B, b) && validBatch(b);
    if (!va && !vb) return false;
    const BatchRecordV1 &best = (!vb || (va && newer(a.sequence, b.sequence))) ? a : b;
    out = best.payload;
    return true;
  }

  bool saveBatch(const PackedBatchV1 &payload) {
    if (!ready_) return false;
    BatchRecordV1 a{}, b{};
    const bool va = readRecord(EEPROM_ADDR_BATCH_A, a) && validBatch(a);
    const bool vb = readRecord(EEPROM_ADDR_BATCH_B, b) && validBatch(b);
    const BatchRecordV1 *current = nullptr;
    if (va || vb) current = (!vb || (va && newer(a.sequence, b.sequence))) ? &a : &b;
    if (current && memcmp(&current->payload, &payload, sizeof(payload)) == 0) return true;

    BatchRecordV1 record{};
    record.magic = BATCH_MAGIC;
    record.schema = BATCH_SCHEMA;
    record.size = sizeof(record);
    record.sequence = current ? current->sequence + 1U : 1U;
    record.payload = payload;
    record.crc = mcCrc32(reinterpret_cast<const uint8_t *>(&record),
                         offsetof(BatchRecordV1, crc));
    const bool currentIsA = va && (!vb || newer(a.sequence, b.sequence));
    const uint16_t target = currentIsA ? EEPROM_ADDR_BATCH_B : EEPROM_ADDR_BATCH_A;
    if (!writeRecord(target, record)) return false;
    BatchRecordV1 verify{};
    return readRecord(target, verify) && validBatch(verify) &&
           verify.sequence == record.sequence;
  }

 private:
  struct ConfigSlot {
    uint32_t sequence = 0U;
    uint16_t schema = 0U;
    MachineConfig config{};
  };

  static bool newer(uint32_t a, uint32_t b) {
    return static_cast<int32_t>(a - b) > 0;
  }

  template <typename T>
  bool readRecord(uint16_t address, T &record) const {
    return eeprom_.readBytes(address, &record, sizeof(record));
  }

  template <typename T>
  bool writeRecord(uint16_t address, const T &record) const {
    return eeprom_.writeBytes(address, &record, sizeof(record));
  }

  bool readConfigSlot(uint16_t address, ConfigSlot &out) const {
    RecordHeader header{};
    if (!readRecord(address, header) || header.magic != CONFIG_MAGIC) return false;

    if (header.schema == CONFIG_SCHEMA && header.size == sizeof(ConfigRecordV5)) {
      ConfigRecordV5 record{};
      if (!readRecord(address, record) || !validConfigV5(record)) return false;
      out.sequence = record.sequence;
      out.schema = record.schema;
      out.config = unpackConfigV5(record.payload);
      return true;
    }

    // Nap nguoc firmware cu. Ban ghi cu KHONG bi ghi de ngay; lan luu tiep
    // theo se tu dong chuyen sang V5 o khe con lai (ghi luan phien A/B).
    if (header.schema == CONFIG_SCHEMA_V4 &&
        header.size == sizeof(ConfigRecordV4)) {
      ConfigRecordV4 record{};
      if (!readRecord(address, record) || !validConfigV4(record)) return false;
      out.sequence = record.sequence;
      out.schema = record.schema;
      out.config = unpackConfigV4(record.payload);
      return true;
    }

    if (header.schema == CONFIG_SCHEMA_V3 &&
        header.size == sizeof(ConfigRecordV3)) {
      ConfigRecordV3 record{};
      if (!readRecord(address, record) || !validConfigV3(record)) return false;
      out.sequence = record.sequence;
      out.schema = record.schema;
      out.config = unpackConfigV3(record.payload);
      return true;
    }
    return false;
  }

  static bool validConfigV5(const ConfigRecordV5 &r) {
    return r.magic == CONFIG_MAGIC && r.schema == CONFIG_SCHEMA &&
           r.size == sizeof(r) &&
           r.crc == mcCrc32(reinterpret_cast<const uint8_t *>(&r),
                            offsetof(ConfigRecordV5, crc));
  }

  static bool validConfigV4(const ConfigRecordV4 &r) {
    return r.magic == CONFIG_MAGIC && r.schema == CONFIG_SCHEMA_V4 &&
           r.size == sizeof(r) &&
           r.crc == mcCrc32(reinterpret_cast<const uint8_t *>(&r),
                            offsetof(ConfigRecordV4, crc));
  }

  static bool validConfigV3(const ConfigRecordV3 &r) {
    return r.magic == CONFIG_MAGIC && r.schema == CONFIG_SCHEMA_V3 &&
           r.size == sizeof(r) &&
           r.crc == mcCrc32(reinterpret_cast<const uint8_t *>(&r),
                            offsetof(ConfigRecordV3, crc));
  }

  static bool validBatch(const BatchRecordV1 &r) {
    return r.magic == BATCH_MAGIC && r.schema == BATCH_SCHEMA &&
           r.size == sizeof(r) &&
           r.crc == mcCrc32(reinterpret_cast<const uint8_t *>(&r),
                            offsetof(BatchRecordV1, crc));
  }

  ExternalEeprom24xx eeprom_{};
  bool ready_ = false;
};
// ============================================================================
// INPUT: CUNG MOT HOP DONG CHO GPIO THAT VA SERIAL MO PHONG
// ============================================================================
struct InputState {
  bool limitLeft = false;
  bool limitRight = false;
  bool autoMode = false;
  bool heaterEnable = false;
  bool circulationFan = false;
  bool light = false;
  bool turnLeft = false;
  bool turnRight = false;
};

enum class InputChannel : uint8_t {
  LimitLeft, LimitRight, Auto, Heater, Fan, Light, TurnLeft, TurnRight, Count
};

struct InputEvent {
  uint32_t timestamp = 0U;
  InputChannel channel = InputChannel::LimitLeft;
  bool active = false;
  bool rising = false;
};

class InputManager {
 public:
  void begin() {
    const uint32_t now = millis();
#if MAYAP_SERIAL_INPUT_SIM
    state_ = InputState{};
    for (uint8_t i = 0; i < channelCount(); ++i) {
      raw_[i] = stable_[i] = false;
      changedAt_[i] = now;
    }
#else
    for (uint8_t i = 0; i < channelCount(); ++i) {
      const InputChannel channel = static_cast<InputChannel>(i);
      pinMode(pinFor(channel), INPUT_PULLUP);
      const bool active = readActive(pinFor(channel));
      raw_[i] = stable_[i] = active;
      changedAt_[i] = now;
    }
    rebuildState();
#endif
    lastScanAt_ = now;
  }

  void update(uint32_t now) {
#if MAYAP_SERIAL_INPUT_SIM
    (void)now;
#else
    if (elapsedMs(now, lastScanAt_) < INPUT_SCAN_MS) return;
    lastScanAt_ = now;
    for (uint8_t i = 0; i < channelCount(); ++i) {
      const InputChannel channel = static_cast<InputChannel>(i);
      const bool sample = readActive(pinFor(channel));
      if (sample != raw_[i]) {
        raw_[i] = sample;
        changedAt_[i] = now;
      }
      const uint32_t debounce = (channel == InputChannel::LimitLeft ||
                                 channel == InputChannel::LimitRight)
                                  ? LIMIT_DEBOUNCE_MS : INPUT_DEBOUNCE_MS;
      if (stable_[i] != raw_[i] && elapsedMs(now, changedAt_[i]) >= debounce) {
        commitStable(channel, raw_[i], now);
      }
    }
#endif
  }

  const InputState &state() const { return state_; }
  bool popEvent(InputEvent &event) { return events_.pop(event); }
  uint32_t droppedEvents() const { return events_.overflowCount(); }

#if MAYAP_SERIAL_INPUT_SIM
  bool setSimulated(const char *name, bool value, uint32_t now) {
    InputChannel channel{};
    if (!channelFromName(name, channel)) return false;
    commitStable(channel, value, now);
    raw_[static_cast<uint8_t>(channel)] = value;
    changedAt_[static_cast<uint8_t>(channel)] = now;
    return true;
  }
  void resetSimulation(uint32_t now) {
    for (uint8_t i = 0; i < channelCount(); ++i) {
      commitStable(static_cast<InputChannel>(i), false, now);
      raw_[i] = false;
      changedAt_[i] = now;
    }
  }
#endif

  static const char *name(InputChannel channel) {
    switch (channel) {
      case InputChannel::LimitLeft: return "LIMIT_LEFT";
      case InputChannel::LimitRight: return "LIMIT_RIGHT";
      case InputChannel::Auto: return "AUTO";
      case InputChannel::Heater: return "HEATER";
      case InputChannel::Fan: return "FAN";
      case InputChannel::Light: return "LIGHT";
      case InputChannel::TurnLeft: return "LEFT";
      case InputChannel::TurnRight: return "RIGHT";
      default: return "UNKNOWN";
    }
  }

 private:
  static constexpr uint8_t channelCount() {
    return static_cast<uint8_t>(InputChannel::Count);
  }
  static uint8_t pinFor(InputChannel channel) {
    switch (channel) {
      case InputChannel::LimitLeft: return PIN_IN_LIMIT_LEFT;
      case InputChannel::LimitRight: return PIN_IN_LIMIT_RIGHT;
      case InputChannel::Auto: return PIN_IN_AUTO;
      case InputChannel::Heater: return PIN_IN_HEATER_ENABLE;
      case InputChannel::Fan: return PIN_IN_CIRC_FAN;
      case InputChannel::Light: return PIN_IN_LIGHT;
      case InputChannel::TurnLeft: return PIN_IN_TURN_LEFT;
      case InputChannel::TurnRight: return PIN_IN_TURN_RIGHT;
      default: return 0;
    }
  }
  static bool channelFromName(const char *text, InputChannel &channel) {
    if (!text) return false;
    for (uint8_t i = 0; i < channelCount(); ++i) {
      const InputChannel candidate = static_cast<InputChannel>(i);
      if (!strcmp(text, name(candidate))) { channel = candidate; return true; }
    }
    return false;
  }
  static bool readActive(uint8_t pin) {
    const bool level = digitalRead(pin) != LOW;
    return INPUT_ACTIVE_LOW ? !level : level;
  }
  void commitStable(InputChannel channel, bool active, uint32_t now) {
    const uint8_t index = static_cast<uint8_t>(channel);
    if (stable_[index] == active) return;
    stable_[index] = active;
    rebuildState();
    InputEvent event{};
    event.timestamp = now;
    event.channel = channel;
    event.active = active;
    event.rising = active;
    events_.push(event);
  }
  void rebuildState() {
    state_.limitLeft = stable_[0];
    state_.limitRight = stable_[1];
    state_.autoMode = stable_[2];
    state_.heaterEnable = stable_[3];
    state_.circulationFan = stable_[4];
    state_.light = stable_[5];
    state_.turnLeft = stable_[6];
    state_.turnRight = stable_[7];
  }

  InputState state_{};
  bool raw_[8]{};
  bool stable_[8]{};
  uint32_t changedAt_[8]{};
  uint32_t lastScanAt_ = 0U;
  FixedRing<InputEvent, INPUT_EVENT_QUEUE_SIZE> events_{};
};

// ============================================================================
// SHT RS485 INDUSTRIAL v2.2 - MAY TRANG THAI MODBUS KHONG BLOCKING
// ============================================================================
namespace SHT485Config {
constexpr uint8_t UART_PORT = SHT_UART_PORT;
constexpr uint8_t PIN_RX = PIN_RS485_RX;
constexpr uint8_t PIN_TX = PIN_RS485_TX;
constexpr uint8_t PIN_DE_RE = PIN_RS485_DE_RE;
constexpr uint32_t BAUD = 9600;
constexpr uint8_t SLAVE_ID = 1;
constexpr uint32_t FIRST_POLL_DELAY_MS = 500;
constexpr uint32_t POLL_PERIOD_MS = 2000;
constexpr uint32_t RESPONSE_TIMEOUT_MS = 200;
constexpr uint32_t RETRY_GAP_MS = 80;
constexpr uint8_t ATTEMPTS_PER_CYCLE = 2;
constexpr uint8_t OFFLINE_AFTER_FAILED_CYCLES = 2;
constexpr uint32_t DATA_STALE_MS = 6500;
constexpr uint32_t STARTUP_REPORT_MS = 5000;
constexpr uint32_t DE_SETUP_US = 300;
constexpr uint32_t TX_TAIL_GUARD_US = 1500;
constexpr uint32_t TX_ROOM_TIMEOUT_MS = 20;
constexpr uint8_t RX_BYTES_PER_UPDATE = 16;
constexpr uint8_t RX_PURGE_BYTES_PER_ATTEMPT = 16;
constexpr int16_t TEMP_MIN_X10 = -400;
constexpr int16_t TEMP_MAX_X10 = 600;
constexpr int16_t HUM_MIN_X10 = 0;
constexpr int16_t HUM_MAX_X10 = 1000;
constexpr uint8_t MEDIAN_WINDOW = 3;
constexpr int32_t IIR_NUMERATOR = 3;
constexpr int32_t IIR_DENOMINATOR = 8;
}

enum class SHT485Event : uint8_t {
  None = 0, PresentAtStartup, MissingAtStartup, Lost, Restored
};

class SHT485Industrial {
 public:
  SHT485Industrial() : serial_(SHT485Config::UART_PORT) {}

  void begin() {
    pinMode(SHT485Config::PIN_DE_RE, OUTPUT);
    digitalWrite(SHT485Config::PIN_DE_RE, LOW);
    serial_.begin(SHT485Config::BAUD, SERIAL_8N1,
                  SHT485Config::PIN_RX, SHT485Config::PIN_TX);
    purgeRxBounded(SHT485Config::RX_PURGE_BYTES_PER_ATTEMPT);
    const uint32_t now = millis();
    bootMs_ = now;
    nextPollMs_ = now + SHT485Config::FIRST_POLL_DELAY_MS;
    state_ = State::Idle;
  }

  void update() {
    const uint32_t nowMs = millis();
    const uint32_t nowUs = micros();
    updateFreshness(nowMs);
    updateStartupStatus(nowMs);
    switch (state_) {
      case State::Idle:
        if (timeReached(nowMs, nextPollMs_)) { attempt_ = 0; beginAttempt(); }
        break;
      case State::WaitTxRoom:
        if (serial_.availableForWrite() >= static_cast<int>(RequestSize)) {
          digitalWrite(SHT485Config::PIN_DE_RE, HIGH);
          deadlineUs_ = nowUs + SHT485Config::DE_SETUP_US;
          state_ = State::DeSetup;
        } else if (elapsedMs(nowMs, txRoomWaitStartedMs_) >=
                   SHT485Config::TX_ROOM_TIMEOUT_MS) {
          ++txErrors_; failAttempt(nowMs);
        }
        break;
      case State::DeSetup:
        if (static_cast<int32_t>(nowUs - deadlineUs_) >= 0) {
          const size_t written = serial_.write(request_, RequestSize);
          if (written == RequestSize) {
            deadlineUs_ = nowUs + txDurationUs(RequestSize);
            state_ = State::TxActive;
          } else { ++txErrors_; failAttempt(nowMs); }
        }
        break;
      case State::TxActive:
        if (static_cast<int32_t>(nowUs - deadlineUs_) >= 0) {
          digitalWrite(SHT485Config::PIN_DE_RE, LOW);
          responseStartedMs_ = nowMs;
          resetParser();
          state_ = State::WaitResponse;
        }
        break;
      case State::WaitResponse:
        consumeRxBounded();
        if (frameComplete_) {
          if (decodeFrame(nowMs)) completeCycleSuccess(nowMs);
          else failAttempt(nowMs);
        } else if (elapsedMs(nowMs, responseStartedMs_) >=
                   SHT485Config::RESPONSE_TIMEOUT_MS) {
          ++timeoutErrors_; failAttempt(nowMs);
        }
        break;
      case State::RetryGap:
        if (timeReached(nowMs, retryAtMs_)) beginAttempt();
        break;
    }
  }

  bool takeNewData() { const bool r = newData_; newData_ = false; return r; }
  bool popEvent(SHT485Event &event) {
    if (pendingEvents_ == 0U) { event = SHT485Event::None; return false; }
    if (takeEventBit(EventStartupPresent)) event = SHT485Event::PresentAtStartup;
    else if (takeEventBit(EventStartupMissing)) event = SHT485Event::MissingAtStartup;
    else if (takeEventBit(EventLost)) event = SHT485Event::Lost;
    else { (void)takeEventBit(EventRestored); event = SHT485Event::Restored; }
    return true;
  }
  bool online() const { return online_; }
  bool dataValid() const {
    return online_ && hasEverReceivedData_ &&
           elapsedMs(millis(), lastGoodFrameMs_) < SHT485Config::DATA_STALE_MS;
  }
  float temperatureC() const {
    return filterInitialized_ ? static_cast<float>(filteredTempQ8_) / 2560.0f : NAN;
  }
  float humidityRH() const {
    return filterInitialized_ ? static_cast<float>(filteredHumQ8_) / 2560.0f : NAN;
  }
  float rawTemperatureC() const {
    return hasEverReceivedData_ ? static_cast<float>(rawTempX10_) * 0.1f : NAN;
  }
  // Gia tri Modbus tho, chua loc va chua bu offset. Dung de phat hien cam bien
  // treo: bo loc median/IIR co the lam hai mau khac nhau thanh giong nhau, nen
  // bat buoc phai so sanh o muc tho.
  int16_t rawTempCount() const { return rawTempX10_; }
  int16_t rawHumidityCount() const { return rawHumX10_; }
  uint32_t dataAgeMs() const {
    return hasEverReceivedData_ ? elapsedMs(millis(), lastGoodFrameMs_) : UINT32_MAX;
  }
  uint32_t goodFrames() const { return goodFrames_; }
  uint32_t crcErrors() const { return crcErrors_; }
  uint32_t timeoutErrors() const { return timeoutErrors_; }
  uint32_t formatErrors() const { return formatErrors_; }
  uint32_t rangeErrors() const { return rangeErrors_; }
  uint32_t txErrors() const { return txErrors_; }
  uint32_t ignoredBytes() const { return ignoredBytes_; }

 private:
  enum class State : uint8_t { Idle, WaitTxRoom, DeSetup, TxActive, WaitResponse, RetryGap };
  static constexpr uint8_t RequestSize = 8;
  static constexpr uint8_t ResponseSize = 9;
  static constexpr uint8_t EventStartupPresent = 0x01;
  static constexpr uint8_t EventStartupMissing = 0x02;
  static constexpr uint8_t EventLost = 0x04;
  static constexpr uint8_t EventRestored = 0x08;

  HardwareSerial serial_;
  State state_ = State::Idle;
  const uint8_t request_[RequestSize] = {
    0x01, 0x03, 0x00, 0x00, 0x00, 0x02, 0xC4, 0x0B
  };
  uint8_t response_[ResponseSize]{};
  uint8_t responseLength_ = 0;
  bool frameComplete_ = false;
  uint8_t attempt_ = 0;
  uint8_t failedCycles_ = 0;
  uint32_t bootMs_ = 0;
  uint32_t nextPollMs_ = 0;
  uint32_t retryAtMs_ = 0;
  uint32_t responseStartedMs_ = 0;
  uint32_t txRoomWaitStartedMs_ = 0;
  uint32_t deadlineUs_ = 0;
  uint32_t lastGoodFrameMs_ = 0;
  bool online_ = false;
  bool hasEverReceivedData_ = false;
  bool newData_ = false;
  bool startupResolved_ = false;
  uint8_t pendingEvents_ = 0;
  int16_t rawTempX10_ = 0;
  int16_t rawHumX10_ = 0;
  int16_t tempWindow_[3]{};
  int16_t humWindow_[3]{};
  uint8_t windowCount_ = 0;
  uint8_t windowIndex_ = 0;
  int32_t filteredTempQ8_ = 0;
  int32_t filteredHumQ8_ = 0;
  bool filterInitialized_ = false;
  uint32_t goodFrames_ = 0;
  uint32_t crcErrors_ = 0;
  uint32_t timeoutErrors_ = 0;
  uint32_t formatErrors_ = 0;
  uint32_t rangeErrors_ = 0;
  uint32_t txErrors_ = 0;
  uint32_t ignoredBytes_ = 0;

  static uint32_t txDurationUs(uint8_t bytes) {
    const uint32_t bits = static_cast<uint32_t>(bytes) * 10UL;
    return (bits * 1000000UL + SHT485Config::BAUD - 1UL) /
           SHT485Config::BAUD + SHT485Config::TX_TAIL_GUARD_US;
  }
  void purgeRxBounded(uint8_t budget) {
    for (uint8_t i = 0; i < budget && serial_.available() > 0; ++i) {
      (void)serial_.read(); ++ignoredBytes_;
    }
  }
  void beginAttempt() {
    ++attempt_;
    purgeRxBounded(SHT485Config::RX_PURGE_BYTES_PER_ATTEMPT);
    resetParser();
    digitalWrite(SHT485Config::PIN_DE_RE, LOW);
    txRoomWaitStartedMs_ = millis();
    state_ = State::WaitTxRoom;
  }
  void resetParser() { responseLength_ = 0; frameComplete_ = false; }
  void consumeRxBounded() {
    for (uint8_t i = 0; i < SHT485Config::RX_BYTES_PER_UPDATE; ++i) {
      if (serial_.available() <= 0 || frameComplete_) break;
      const int incoming = serial_.read();
      if (incoming < 0) break;
      parseByte(static_cast<uint8_t>(incoming));
    }
  }
  void parseByte(uint8_t value) {
    if (responseLength_ == 0U) {
      if (value == SHT485Config::SLAVE_ID) response_[responseLength_++] = value;
      else ++ignoredBytes_;
      return;
    }
    if (responseLength_ == 1U) {
      if (value == 0x03U) response_[responseLength_++] = value;
      else if (value == SHT485Config::SLAVE_ID) response_[0] = value;
      else { ++ignoredBytes_; responseLength_ = 0; }
      return;
    }
    if (responseLength_ == 2U) {
      if (value == 0x04U) response_[responseLength_++] = value;
      else { ++formatErrors_; responseLength_ = 0; }
      return;
    }
    response_[responseLength_++] = value;
    if (responseLength_ == ResponseSize) frameComplete_ = true;
  }
  bool decodeFrame(uint32_t now) {
    if (responseLength_ != ResponseSize || response_[0] != SHT485Config::SLAVE_ID ||
        response_[1] != 0x03U || response_[2] != 0x04U) {
      ++formatErrors_; return false;
    }
    const uint16_t received = static_cast<uint16_t>(response_[7]) |
                              (static_cast<uint16_t>(response_[8]) << 8U);
    if (crc16(response_, 7) != received) { ++crcErrors_; return false; }
    const int16_t hum = static_cast<int16_t>((static_cast<uint16_t>(response_[3]) << 8U) |
                                             response_[4]);
    const int16_t temp = static_cast<int16_t>((static_cast<uint16_t>(response_[5]) << 8U) |
                                              response_[6]);
    if (temp < SHT485Config::TEMP_MIN_X10 || temp > SHT485Config::TEMP_MAX_X10 ||
        hum < SHT485Config::HUM_MIN_X10 || hum > SHT485Config::HUM_MAX_X10) {
      ++rangeErrors_; return false;
    }
    rawTempX10_ = temp; rawHumX10_ = hum;
    updateFilter(temp, hum);
    lastGoodFrameMs_ = now;
    hasEverReceivedData_ = true;
    newData_ = true;
    ++goodFrames_;
    return true;
  }
  void updateFilter(int16_t temp, int16_t hum) {
    tempWindow_[windowIndex_] = temp;
    humWindow_[windowIndex_] = hum;
    windowIndex_ = static_cast<uint8_t>((windowIndex_ + 1U) % 3U);
    if (windowCount_ < 3U) ++windowCount_;
    const int32_t targetTemp = medianTargetQ8(tempWindow_, windowCount_);
    const int32_t targetHum = medianTargetQ8(humWindow_, windowCount_);
    if (!filterInitialized_) {
      filteredTempQ8_ = targetTemp; filteredHumQ8_ = targetHum;
      filterInitialized_ = true; return;
    }
    filteredTempQ8_ += ((targetTemp - filteredTempQ8_) *
                        SHT485Config::IIR_NUMERATOR) / SHT485Config::IIR_DENOMINATOR;
    filteredHumQ8_ += ((targetHum - filteredHumQ8_) *
                       SHT485Config::IIR_NUMERATOR) / SHT485Config::IIR_DENOMINATOR;
  }
  static int32_t medianTargetQ8(const int16_t *v, uint8_t count) {
    if (count <= 1U) return static_cast<int32_t>(v[0]) * 256L;
    if (count == 2U) return (static_cast<int32_t>(v[0]) + v[1]) * 128L;
    const int16_t a = v[0], b = v[1], c = v[2];
    const int16_t m = (a > b) ? ((b > c) ? b : ((a > c) ? c : a))
                              : ((a > c) ? a : ((b > c) ? c : b));
    return static_cast<int32_t>(m) * 256L;
  }
  void completeCycleSuccess(uint32_t now) {
    const bool wasOnline = online_;
    online_ = true; failedCycles_ = 0;
    if (!startupResolved_) { startupResolved_ = true; setEvent(EventStartupPresent); }
    else if (!wasOnline) setEvent(EventRestored);
    nextPollMs_ = now + SHT485Config::POLL_PERIOD_MS;
    state_ = State::Idle;
  }
  void failAttempt(uint32_t now) {
    digitalWrite(SHT485Config::PIN_DE_RE, LOW);
    resetParser();
    if (attempt_ < SHT485Config::ATTEMPTS_PER_CYCLE) {
      retryAtMs_ = now + SHT485Config::RETRY_GAP_MS;
      state_ = State::RetryGap; return;
    }
    if (failedCycles_ < 255U) ++failedCycles_;
    if (online_ && failedCycles_ >= SHT485Config::OFFLINE_AFTER_FAILED_CYCLES) {
      online_ = false; setEvent(EventLost);
    }
    nextPollMs_ = now + SHT485Config::POLL_PERIOD_MS;
    state_ = State::Idle;
  }
  void updateFreshness(uint32_t now) {
    if (online_ && hasEverReceivedData_ &&
        elapsedMs(now, lastGoodFrameMs_) >= SHT485Config::DATA_STALE_MS) {
      online_ = false; setEvent(EventLost);
    }
  }
  void updateStartupStatus(uint32_t now) {
    if (!startupResolved_ && elapsedMs(now, bootMs_) >= SHT485Config::STARTUP_REPORT_MS) {
      startupResolved_ = true; online_ = false; setEvent(EventStartupMissing);
    }
  }
  void setEvent(uint8_t bit) { pendingEvents_ |= bit; }
  bool takeEventBit(uint8_t bit) {
    if ((pendingEvents_ & bit) == 0U) return false;
    pendingEvents_ &= static_cast<uint8_t>(~bit); return true;
  }
  static uint16_t crc16(const uint8_t *data, uint8_t length) {
    uint16_t crc = 0xFFFFU;
    for (uint8_t i = 0; i < length; ++i) {
      crc ^= data[i];
      for (uint8_t bit = 0; bit < 8U; ++bit) {
        crc = (crc & 1U) ? static_cast<uint16_t>((crc >> 1U) ^ 0xA001U)
                         : static_cast<uint16_t>(crc >> 1U);
      }
    }
    return crc;
  }
};

// ============================================================================
// PID THEO Nhip MAU CAM BIEN + ANTI-WINDUP + DAO HAM TREN PV
// ============================================================================
class ThermalController {
 public:
  void reset() {
    initialized_ = false;
    integral_ = 0.0f;
    lastInput_ = 0.0f;
    lastComputeAt_ = 0;
    output_ = 0.0f;
  }

  // Ap dung cau hinh moi ma giu nguyen cong suat hien tai. Cach nay tranh
  // nha contactor tong chi vi nguoi dung sua/lưu mot thong so tren HMI.
  void applyConfigBumpless(uint32_t now, float setpoint, float input,
                           const MachineConfig &cfg) {
    if (!initialized_ || !isfinite(input)) return;
    const float maxOut = static_cast<float>(cfg.maxHeaterPower);
    output_ = clampFloat(output_, 0.0f, maxOut);
    lastInput_ = input;
    lastComputeAt_ = now;
    if (cfg.controlMode == ControlMode::Pid) {
      const float error = setpoint - input;
      integral_ = clampFloat(output_ - cfg.kp * error, -maxOut, maxOut);
    } else {
      integral_ = 0.0f;
    }
  }

  float updateOnNewSample(uint32_t now, float setpoint, float input,
                          const MachineConfig &cfg, bool enabled) {
    if (!enabled || !isfinite(input)) { reset(); return 0.0f; }
    const float maxOut = static_cast<float>(cfg.maxHeaterPower);
    if (cfg.controlMode == ControlMode::OnOff) {
      const float half = cfg.tempHysteresis * 0.5f;
      if (!initialized_) { output_ = input < setpoint ? maxOut : 0.0f; initialized_ = true; }
      else if (input <= setpoint - half) output_ = maxOut;
      else if (input >= setpoint + half) output_ = 0.0f;
      lastInput_ = input; lastComputeAt_ = now;
      return output_;
    }

    if (!initialized_) {
      initialized_ = true;
      lastInput_ = input;
      lastComputeAt_ = now;
      integral_ = 0.0f;
      output_ = clampFloat(cfg.kp * (setpoint - input), 0.0f, maxOut);
      return output_;
    }

    float dt = static_cast<float>(elapsedMs(now, lastComputeAt_)) * 0.001f;
    dt = clampFloat(dt, 0.25f, 10.0f);
    lastComputeAt_ = now;
    const float error = setpoint - input;
    const float dInput = (input - lastInput_) / dt;
    lastInput_ = input;

    const float p = cfg.kp * error;
    const float d = -cfg.kd * dInput;
    const float candidateIntegral = clampFloat(
        integral_ + cfg.ki * error * dt, -maxOut, maxOut);
    const float unsaturated = p + candidateIntegral + d;
    // Tich phan co dieu kien: chi tich khi chua bao hoa hoac dang keo khoi bao hoa.
    if ((unsaturated >= 0.0f && unsaturated <= maxOut) ||
        (unsaturated > maxOut && error < 0.0f) ||
        (unsaturated < 0.0f && error > 0.0f)) {
      integral_ = candidateIntegral;
    }
    output_ = clampFloat(p + integral_ + d, 0.0f, maxOut);
    return output_;
  }

  float output() const { return output_; }

 private:
  bool initialized_ = false;
  float integral_ = 0.0f;
  float lastInput_ = 0.0f;
  uint32_t lastComputeAt_ = 0;
  float output_ = 0.0f;
};

class ConditionTimer {
 public:
  bool update(uint32_t now, bool condition, uint32_t requiredMs) {
    if (!condition) { active_ = false; since_ = 0; return false; }
    if (!active_) { active_ = true; since_ = now; }
    return requiredMs == 0U || elapsedMs(now, since_) >= requiredMs;
  }
  void reset() { active_ = false; since_ = 0; }
 private:
  bool active_ = false;
  uint32_t since_ = 0;
};

// ============================================================================
// OUTPUT ARBITER: CHI NOI DUY NHAT DUOC PHEP DIGITALWRITE CO CAU CHAP HANH
// ============================================================================
struct OutputRequest {
  bool heaterSsr = false;
  bool heatMaster = false;
  bool turnLeft = false;
  bool turnRight = false;
  bool ventFan = false;
  bool light = false;
  bool circulationFan = false;
  bool siren = false;
  bool pulseSpare = false;
  bool relaySpare = false;
  bool immediateMasterDrop = false;
  bool forceAllSafe = false;
};
struct OutputState {
  bool heaterSsr = false;
  bool heatMaster = false;
  bool turnLeft = false;
  bool turnRight = false;
  bool ventFan = false;
  bool light = false;
  bool circulationFan = false;
  bool siren = false;
  bool pulseSpare = false;
  bool relaySpare = false;
};

enum class OutputChannel : uint8_t {
  HeaterSsr = 0, HeatMaster, TurnLeft, TurnRight, VentFan, Light,
  CirculationFan, Siren, PulseSpare, RelaySpare, Count
};

struct OutputEvent {
  uint32_t timestamp = 0U;
  OutputChannel channel = OutputChannel::HeaterSsr;
  bool active = false;
};

inline const char *outputName(OutputChannel channel) {
  switch (channel) {
    case OutputChannel::HeaterSsr: return "HEATER_SSR";
    case OutputChannel::HeatMaster: return "HEAT_MASTER";
    case OutputChannel::TurnLeft: return "TURN_LEFT";
    case OutputChannel::TurnRight: return "TURN_RIGHT";
    case OutputChannel::VentFan: return "VENT_FAN";
    case OutputChannel::Light: return "LIGHT";
    case OutputChannel::CirculationFan: return "CIRC_FAN";
    case OutputChannel::Siren: return "SIREN";
    case OutputChannel::PulseSpare: return "PULSE_SPARE";
    case OutputChannel::RelaySpare: return "RELAY_SPARE";
    default: return "UNKNOWN";
  }
}

inline void writeLogical(uint8_t pin, bool on) {
  digitalWrite(pin, (on == OUTPUT_ACTIVE_HIGH) ? HIGH : LOW);
}

inline void mayapSafeOutputsEarly() {
  const uint8_t pins[] = {
    PIN_OUT_HEATER_SSR, PIN_OUT_PULSE_SPARE, PIN_OUT_TURN_RIGHT,
    PIN_OUT_TURN_LEFT, PIN_OUT_VENT_FAN, PIN_OUT_LIGHT,
    PIN_OUT_HEAT_MASTER, PIN_OUT_CIRC_FAN, PIN_OUT_SIREN,
    PIN_OUT_RELAY_SPARE
  };
  for (uint8_t pin : pins) {
    // Nap muc OFF vao output latch truoc khi chuyen sang OUTPUT de giam xung
    // kich relay trong giai do firmware vua bat dau chay.
    writeLogical(pin, false);
    pinMode(pin, OUTPUT);
    writeLogical(pin, false);
  }
}

class OutputArbiter {
 public:
  void begin() {
    mayapSafeOutputsEarly();
    state_ = OutputState{};
    masterOnAt_ = masterDropAt_ = turnInterlockUntil_ = 0U;
    transitionHourStartedAt_ = millis();
  }

  void update(uint32_t now, const OutputRequest &input) {
    const bool forceSafe = input.forceAllSafe || mayapSystemTripActive();
    OutputRequest request = input;
    if (forceSafe) {
      request = OutputRequest{};
      request.forceAllSafe = true;
      request.immediateMasterDrop = true;
    }

    if (elapsedMs(now, transitionHourStartedAt_) >= 3600000UL) {
      transitionHourStartedAt_ = now;
      transitionsThisHour_ = 0U;
      relayRateExceeded_ = false;
    }

    // Lien dong output doc lap voi state machine: OFF chieu cu truoc,
    // doi dead-time, sau do moi duoc phep ON chieu moi.
    updateTurnOutputs(now, request.turnLeft, request.turnRight, forceSafe);

    // Tai an toan: ON ngay; OFF ton trong thoi gian ON toi thieu de chong dap relay.
    setMinOn(PIN_OUT_VENT_FAN, OutputChannel::VentFan,
             request.ventFan, state_.ventFan, now, RELAY_FAN_MIN_ON_MS);
    setMinSwitch(PIN_OUT_LIGHT, OutputChannel::Light,
                 request.light, state_.light, now, RELAY_LIGHT_MIN_SWITCH_MS);
    setMinOn(PIN_OUT_CIRC_FAN, OutputChannel::CirculationFan,
             request.circulationFan, state_.circulationFan, now, RELAY_FAN_MIN_ON_MS);
    setImmediate(PIN_OUT_SIREN, OutputChannel::Siren,
                 request.siren, state_.siren, now);

    // Khi cat nhiet: SSR OFF truoc, contactor nha sau mot khoang ngan.
    if (!request.heatMaster || request.forceAllSafe) {
      setImmediate(PIN_OUT_HEATER_SSR, OutputChannel::HeaterSsr,
                   false, state_.heaterSsr, now);
      if (state_.heatMaster) {
        if (request.immediateMasterDrop || request.forceAllSafe) {
          setImmediate(PIN_OUT_HEAT_MASTER, OutputChannel::HeatMaster,
                       false, state_.heatMaster, now);
          masterDropAt_ = masterOnAt_ = 0U;
        } else {
          if (masterDropAt_ == 0U) masterDropAt_ = now + HEAT_MASTER_DROP_DELAY_MS;
          if (timeReached(now, masterDropAt_)) {
            setImmediate(PIN_OUT_HEAT_MASTER, OutputChannel::HeatMaster,
                         false, state_.heatMaster, now);
            masterDropAt_ = masterOnAt_ = 0U;
          }
        }
      }
    } else {
      masterDropAt_ = 0U;
      if (!state_.heatMaster) {
        setImmediate(PIN_OUT_HEAT_MASTER, OutputChannel::HeatMaster,
                     true, state_.heatMaster, now);
        masterOnAt_ = now;
      }
      const bool pickupDone = state_.heatMaster &&
          elapsedMs(now, masterOnAt_) >= HEAT_MASTER_PICKUP_MS;
      setImmediate(PIN_OUT_HEATER_SSR, OutputChannel::HeaterSsr,
                   request.heaterSsr && pickupDone, state_.heaterSsr, now);
    }

    setMinSwitch(PIN_OUT_PULSE_SPARE, OutputChannel::PulseSpare,
                 request.pulseSpare, state_.pulseSpare, now,
                 RELAY_GENERAL_MIN_SWITCH_MS);
    setMinSwitch(PIN_OUT_RELAY_SPARE, OutputChannel::RelaySpare,
                 request.relaySpare, state_.relaySpare, now,
                 RELAY_GENERAL_MIN_SWITCH_MS);
  }

  void forceSafe(uint32_t now) {
    OutputRequest request{};
    request.forceAllSafe = true;
    request.immediateMasterDrop = true;
    update(now, request);
  }

  const OutputState &state() const { return state_; }
  bool popEvent(OutputEvent &event) { return events_.pop(event); }
  bool conflictDetected() const { return outputConflict_; }
  bool relayRateExceeded() const { return relayRateExceeded_; }
  uint16_t transitionsThisHour() const { return transitionsThisHour_; }
  uint32_t droppedEvents() const { return events_.overflowCount(); }

 private:
  void updateTurnOutputs(uint32_t now, bool wantLeft, bool wantRight,
                         bool forceSafe) {
    outputConflict_ = wantLeft && wantRight;
    if (forceSafe || outputConflict_) {
      const bool wasMoving = state_.turnLeft || state_.turnRight;
      setImmediate(PIN_OUT_TURN_LEFT, OutputChannel::TurnLeft,
                   false, state_.turnLeft, now);
      setImmediate(PIN_OUT_TURN_RIGHT, OutputChannel::TurnRight,
                   false, state_.turnRight, now);
      if (wasMoving) turnInterlockUntil_ = now + TURN_DIRECTION_DEADTIME_MS;
      return;
    }

    if (!wantLeft && !wantRight) {
      const bool wasMoving = state_.turnLeft || state_.turnRight;
      setImmediate(PIN_OUT_TURN_LEFT, OutputChannel::TurnLeft,
                   false, state_.turnLeft, now);
      setImmediate(PIN_OUT_TURN_RIGHT, OutputChannel::TurnRight,
                   false, state_.turnRight, now);
      if (wasMoving) turnInterlockUntil_ = now + TURN_DIRECTION_DEADTIME_MS;
      return;
    }

    if (wantLeft) {
      if (state_.turnRight) {
        setImmediate(PIN_OUT_TURN_RIGHT, OutputChannel::TurnRight,
                     false, state_.turnRight, now);
        turnInterlockUntil_ = now + TURN_DIRECTION_DEADTIME_MS;
      }
      if (!state_.turnRight && timeReached(now, turnInterlockUntil_)) {
        setImmediate(PIN_OUT_TURN_LEFT, OutputChannel::TurnLeft,
                     true, state_.turnLeft, now);
      } else {
        setImmediate(PIN_OUT_TURN_LEFT, OutputChannel::TurnLeft,
                     false, state_.turnLeft, now);
      }
      return;
    }

    if (state_.turnLeft) {
      setImmediate(PIN_OUT_TURN_LEFT, OutputChannel::TurnLeft,
                   false, state_.turnLeft, now);
      turnInterlockUntil_ = now + TURN_DIRECTION_DEADTIME_MS;
    }
    if (!state_.turnLeft && timeReached(now, turnInterlockUntil_)) {
      setImmediate(PIN_OUT_TURN_RIGHT, OutputChannel::TurnRight,
                   true, state_.turnRight, now);
    } else {
      setImmediate(PIN_OUT_TURN_RIGHT, OutputChannel::TurnRight,
                   false, state_.turnRight, now);
    }
  }

  void recordTransition(OutputChannel channel, bool active, uint32_t now) {
    lastTransitionAt_[static_cast<uint8_t>(channel)] = now;
    if (transitionsThisHour_ < UINT16_MAX) ++transitionsThisHour_;
    if (transitionsThisHour_ > MAX_RELAY_TRANSITIONS_PER_HOUR)
      relayRateExceeded_ = true;
    OutputEvent event{};
    event.timestamp = now;
    event.channel = channel;
    event.active = active;
    events_.push(event);
  }

  void setImmediate(uint8_t pin, OutputChannel channel, bool requested,
                    bool &stored, uint32_t now) {
    if (requested == stored) return;
    stored = requested;
    writeLogical(pin, requested);
    recordTransition(channel, requested, now);
  }

  void setMinSwitch(uint8_t pin, OutputChannel channel, bool requested,
                    bool &stored, uint32_t now, uint32_t minimumMs) {
    if (requested == stored) return;
    const uint32_t last = lastTransitionAt_[static_cast<uint8_t>(channel)];
    if (last && elapsedMs(now, last) < minimumMs) return;
    setImmediate(pin, channel, requested, stored, now);
  }

  void setMinOn(uint8_t pin, OutputChannel channel, bool requested,
                bool &stored, uint32_t now, uint32_t minimumOnMs) {
    if (requested == stored) return;
    // Bat la uu tien an toan, khong bi tri hoan.
    if (requested) {
      setImmediate(pin, channel, true, stored, now);
      return;
    }
    const uint32_t last = lastTransitionAt_[static_cast<uint8_t>(channel)];
    if (last && elapsedMs(now, last) < minimumOnMs) return;
    setImmediate(pin, channel, false, stored, now);
  }

  OutputState state_{};
  uint32_t lastTransitionAt_[static_cast<uint8_t>(OutputChannel::Count)]{};
  FixedRing<OutputEvent, OUTPUT_EVENT_QUEUE_SIZE> events_{};
  uint32_t masterOnAt_ = 0U;
  uint32_t masterDropAt_ = 0U;
  uint32_t turnInterlockUntil_ = 0U;
  uint32_t transitionHourStartedAt_ = 0U;
  uint16_t transitionsThisHour_ = 0U;
  bool outputConflict_ = false;
  bool relayRateExceeded_ = false;
};

// ============================================================================
// LED TRANG THAI SK6812 - CHI CAP NHAT KHI MAU/PHASE THAY DOI
// ============================================================================
enum class LedCode : uint8_t {
  Off, Boot, ReadyAuto, ReadyManual, RunningAuto, RunningManual,
  AutoTune, TempWarning,
  SystemFault, SensorFault, TurnFault, Emergency
};

class StatusLed {
 public:
  void begin() {
    rgbLedWriteOrdered(PIN_STATUS_RGB, LED_COLOR_ORDER_GRB, 0, 0, 0);
    lastCode_ = LedCode::Off;
    lastVisible_ = false;
  }
  void update(uint32_t now, LedCode code) {
    bool visible = true;
    switch (code) {
      case LedCode::Boot: visible = ((now / 500U) & 1U) == 0U; break;
      case LedCode::TempWarning: visible = ((now / 400U) & 1U) == 0U; break;
      case LedCode::SystemFault: visible = ((now / 180U) & 1U) == 0U; break;
      case LedCode::SensorFault: visible = ((now / 250U) & 1U) == 0U; break;
      case LedCode::TurnFault: visible = ((now / 180U) % 6U) < 2U; break;
      case LedCode::Emergency: visible = ((now / 120U) & 1U) == 0U; break;
      default: break;
    }
    if (code == lastCode_ && visible == lastVisible_) return;
    lastCode_ = code; lastVisible_ = visible;
    uint8_t r = 0, g = 0, b = 0;
    if (visible) {
      switch (code) {
        case LedCode::Boot: r = 0; g = 0; b = RGB_BRIGHTNESS_NORMAL; break;
        case LedCode::ReadyAuto: r = 0; g = RGB_BRIGHTNESS_LOW; b = RGB_BRIGHTNESS_LOW; break;
        case LedCode::ReadyManual: r = RGB_BRIGHTNESS_NORMAL; g = RGB_BRIGHTNESS_NORMAL; b = 0; break;
        case LedCode::RunningAuto: r = 0; g = RGB_BRIGHTNESS_NORMAL; b = 0; break;
        case LedCode::RunningManual: r = RGB_BRIGHTNESS_NORMAL; g = RGB_BRIGHTNESS_LOW; b = 0; break;
        case LedCode::AutoTune: r = 0; g = RGB_BRIGHTNESS_LOW; b = RGB_BRIGHTNESS_NORMAL; break;
        case LedCode::TempWarning: r = RGB_BRIGHTNESS_ALARM; g = RGB_BRIGHTNESS_LOW; b = 0; break;
        case LedCode::SystemFault: r = RGB_BRIGHTNESS_ALARM; g = 0; b = RGB_BRIGHTNESS_LOW; break;
        case LedCode::SensorFault: r = RGB_BRIGHTNESS_NORMAL; g = 0; b = RGB_BRIGHTNESS_NORMAL; break;
        case LedCode::TurnFault: r = RGB_BRIGHTNESS_ALARM; g = 0; b = 0; break;
        case LedCode::Emergency: r = RGB_BRIGHTNESS_ALARM; g = 0; b = 0; break;
        default: break;
      }
    }
    rgbLedWriteOrdered(PIN_STATUS_RGB, LED_COLOR_ORDER_GRB, r, g, b);
  }
 private:
  LedCode lastCode_ = LedCode::Off;
  bool lastVisible_ = false;
};

// ============================================================================
// MAY TRANG THAI DAO TRUNG
// ============================================================================
enum class TrayPosition : uint8_t { Unknown, Left, Right };
enum class TurnPhase : uint8_t {
  Idle, DeadtimeLeft, DeadtimeRight, MovingLeft, MovingRight, Fault
};

// ============================================================================
// AUTO TUNE RELAY AN TOAN
// ============================================================================
class RelayAutoTune {
 public:
  void start(uint32_t now, float input) {
    state_ = AutoTuneState::Running;
    startedAt_ = now;
    phaseHeat_ = input < target_;
    phaseStartedAt_ = now;
    lastUpperCrossAt_ = 0;
    currentLow_ = input;
    currentHigh_ = input;
    capturedHigh_ = NAN;
    cycleCount_ = 0;
    progress_ = 1;
  }
  void configure(float target) { target_ = target; }
  void abort() { state_ = AutoTuneState::Failed; power_ = 0.0f; progress_ = 0; }

  bool update(uint32_t now, float input, const MachineConfig &cfg,
              MachineConfig &tunedOut) {
    if (state_ != AutoTuneState::Running || !isfinite(input)) return false;
    if (elapsedMs(now, startedAt_) >= AUTOTUNE_MAX_MS ||
        elapsedMs(now, phaseStartedAt_) >= AUTOTUNE_PHASE_MAX_MS) {
      abort();
      return false;
    }

    if (phaseHeat_) {
      if (input < currentLow_) currentLow_ = input;
      power_ = static_cast<float>(std::min<uint8_t>(AUTOTUNE_RELAY_POWER_PERCENT,
                                               cfg.maxHeaterPower));
      if (input >= target_ + AUTOTUNE_BAND_C) {
        if (isfinite(capturedHigh_) && lastUpperCrossAt_ != 0U) {
          const float amplitude = (capturedHigh_ - currentLow_) * 0.5f;
          const uint32_t period = elapsedMs(now, lastUpperCrossAt_);
          if (amplitude >= AUTOTUNE_MIN_AMPLITUDE_C &&
              period >= AUTOTUNE_MIN_PERIOD_MS) {
            amplitudes_[cycleCount_] = amplitude;
            periodsMs_[cycleCount_] = period;
            ++cycleCount_;
            progress_ = static_cast<uint8_t>(std::min<uint16_t>(
                95U, static_cast<uint16_t>(cycleCount_) * 90U /
                     AUTOTUNE_REQUIRED_CYCLES));
          }
        }
        lastUpperCrossAt_ = now;
        phaseHeat_ = false;
        phaseStartedAt_ = now;
        currentHigh_ = input;
        power_ = 0.0f;
      }
    } else {
      if (input > currentHigh_) currentHigh_ = input;
      power_ = 0.0f;
      if (input <= target_ - AUTOTUNE_BAND_C) {
        capturedHigh_ = currentHigh_;
        phaseHeat_ = true;
        phaseStartedAt_ = now;
        currentLow_ = input;
      }
    }

    if (cycleCount_ >= AUTOTUNE_REQUIRED_CYCLES) {
      float amplitude = 0.0f;
      float periodSec = 0.0f;
      for (uint8_t i = 0; i < AUTOTUNE_REQUIRED_CYCLES; ++i) {
        amplitude += amplitudes_[i];
        periodSec += static_cast<float>(periodsMs_[i]) * 0.001f;
      }
      amplitude /= AUTOTUNE_REQUIRED_CYCLES;
      periodSec /= AUTOTUNE_REQUIRED_CYCLES;
      const float relayAmplitude = static_cast<float>(
          std::min<uint8_t>(AUTOTUNE_RELAY_POWER_PERCENT, cfg.maxHeaterPower)) * 0.5f;
      const float ku = (4.0f * relayAmplitude) / (PI * amplitude);
      if (!isfinite(ku) || ku <= 0.0f || periodSec <= 0.0f) {
        abort(); return false;
      }
      tunedOut = cfg;
      // Tyreus-Luyben PID: it gay vuot lo hon Ziegler-Nichols, hop he nhiet cham.
      const float kp = ku / 2.2f;
      const float ti = 2.2f * periodSec;
      const float td = periodSec / 6.3f;
      tunedOut.kp = clampFloat(kp, 0.1f, 100.0f);
      tunedOut.ki = clampFloat(kp / ti, 0.0f, 20.0f);
      tunedOut.kd = clampFloat(kp * td, 0.0f, 200.0f);
      sanitizeMachineConfig(tunedOut);
      state_ = AutoTuneState::Success;
      power_ = 0.0f;
      progress_ = 100;
      return true;
    }
    return false;
  }

  AutoTuneState state() const { return state_; }
  uint8_t progress() const { return progress_; }
  float power() const { return power_; }
  bool running() const { return state_ == AutoTuneState::Running; }

 private:
  AutoTuneState state_ = AutoTuneState::Idle;
  float target_ = 37.5f;
  uint32_t startedAt_ = 0;
  uint32_t phaseStartedAt_ = 0;
  bool phaseHeat_ = false;
  uint32_t lastUpperCrossAt_ = 0;
  float currentLow_ = NAN;
  float currentHigh_ = NAN;
  float capturedHigh_ = NAN;
  float amplitudes_[AUTOTUNE_REQUIRED_CYCLES]{};
  uint32_t periodsMs_[AUTOTUNE_REQUIRED_CYCLES]{};
  uint8_t cycleCount_ = 0;
  uint8_t progress_ = 0;
  float power_ = 0.0f;
};

// ============================================================================
// SNAPSHOT + HANG DOI I2C TINH
// Sau boot, HMI/I2C Task la chu so huu duy nhat cua Wire. Control Task chi
// gui yeu cau va doc snapshot, khong bao gio cho LCD/RTC/EEPROM.
// ============================================================================
struct RtcSnapshot {
  uint32_t epoch = 0U;
  bool online = false;
  bool valid = false;
  bool oscillatorStopped = true;
  char dateText[11] = "--/--/----";
};

enum class I2cJobType : uint8_t { None = 0, SaveConfig, SaveBatch, SetRtc };

struct I2cJob {
  I2cJobType type = I2cJobType::None;
  uint32_t id = 0U;
  ConfigOrigin origin = ConfigOrigin::Hmi;
  uint32_t revision = 0U;
  char requestId[WEB_REQUEST_ID_CAPACITY]{};
  MachineConfig config{};
  PackedBatchV1 batch{};
  uint16_t year = 0U;
  uint8_t month = 0U;
  uint8_t day = 0U;
  uint8_t hour = 0U;
  uint8_t minute = 0U;
  uint8_t second = 0U;
};

struct I2cResult {
  I2cJobType type = I2cJobType::None;
  uint32_t id = 0U;
  bool ok = false;
  ConfigOrigin origin = ConfigOrigin::Hmi;
  uint32_t revision = 0U;
  char requestId[WEB_REQUEST_ID_CAPACITY]{};
  MachineConfig config{};
};

// ============================================================================
// BO DIEU KHIEN TONG
// ============================================================================
class MachineController {
 public:
  void begin() {
    bootAt_ = millis();
    eventLog_.begin(bootAt_);
    faults_.attachLog(&eventLog_);
    power_.begin(bootAt_, eventLog_);
    resetReason_ = power_.reason();
    abnormalResetLatched_ = power_.ackRequired();
    faults_.set(FaultCode::AbnormalReset, abnormalResetLatched_, bootAt_,
                static_cast<int16_t>(resetReason_));
    outputs_.begin();
    led_.begin();
    inputs_.begin();
    sensor_.begin();
    rtc_.begin(bootAt_);
    publishRtcSnapshot();
    consumeRtcSnapshot();
    sensorStartupGraceUntil_ = bootAt_ + SENSOR_STARTUP_GRACE_MS;

    const bool storeReady = store_.begin();
    MachineConfig loaded{};
    if (storeReady && store_.loadConfig(loaded)) {
      config_ = loaded;
      configLoaded_ = true;
    } else {
      config_ = MachineConfig{};
      sanitizeMachineConfig(config_);
      MachineConfig verify{};
      configLoaded_ = storeReady && store_.saveConfig(config_, verify);
      if (configLoaded_) config_ = verify;
    }
    storageFaultLatched_ = EXTERNAL_EEPROM_REQUIRED && (!storeReady || !configLoaded_);
    faults_.set(FaultCode::StorageUnavailable, storageFaultLatched_, bootAt_);

    if (DISPLAY_BUILD_DATE_WHEN_RTC_MISSING) setCompileDate(runtime_.dateText);
    else snprintf(runtime_.dateText, sizeof(runtime_.dateText), "--/--/----");
    hmiSetConfig(config_);

    PackedBatchV1 batch{};
    const bool hasBatchRecord = storeReady && store_.loadBatch(batch);
    if (hasBatchRecord && batch.wasRunning) {
      resumePending_ = true;
      const bool powerInterruption = resetReasonIsPowerInterruption(resetReason_);
      resumeConfirmationRequired_ = powerInterruption && !config_.autoResumeAfterPower;
      automaticResetRecovery_ = resetReasonIsAutomaticRecovery(resetReason_) ||
                                (powerInterruption && config_.autoResumeAfterPower);
      elapsedBeforeStartSec_ = batch.elapsedSec;
      savedElapsedAtCheckpoint_ = batch.elapsedSec;
      lastCheckpointEpoch_ = batch.checkpointEpoch;
      resumeClockAdjusted_ = batch.checkpointEpoch == 0U;
      turnCountToday_ = batch.turnCountToday;
      turnCountBatch_ = batch.turnCountBatch;
      config_.nextDirection = static_cast<TurnDirection>(batch.nextDirection <= 1U
          ? batch.nextDirection : 0U);
    }

    heatRestartNotBefore_ = bootAt_ +
        static_cast<uint32_t>(config_.powerRestoreDelaySec) * 1000UL;
    lastCheckpointAt_ = bootAt_;
    previousAutoMode_ = inputs_.state().autoMode;
    runtime_.alarmMask = (storageFaultLatched_ || abnormalResetLatched_)
        ? AlarmSystem : AlarmNone;
    runtime_.sensorStartupGrace = true;
    runtime_.resumeConfirmationRequired = resumeConfirmationRequired_;
    copyRuntimeToHmi(true);
    mirrorEventsToWeb();
    publishWebSnapshot(bootAt_);

    mayapSerialPrintf(false,
        "\n=== MAYAP OFFLINE v%s | HW=%s | HMI=%s/%s ===\n",
        MAYAP_FIRMWARE_VERSION, MAYAP_HARDWARE_REVISION,
        HMI_FIRMWARE_VERSION, HMI_HARDWARE_REVISION);
    mayapSerialPrintf(false, "[BOOT] reset=%s config=%s resume=%u input=%s\n",
                     resetReasonText(resetReason_),
                     configLoaded_ ? "EEPROM" : "DEFAULT",
                     resumePending_, MAYAP_SERIAL_INPUT_SIM ? "SERIAL_SIM" : "GPIO_REAL");
    if (resumeConfirmationRequired_) {
      eventLog_.push(bootAt_, EventType::Recovery,
                     static_cast<uint16_t>(EventCode::ResumePrompt));
      mayapSerialPrintf(false, "[BOOT] MAT DIEN GIUA ME - CHO XAC NHAN HMI\n");
    } else if (automaticResetRecovery_ && resumePending_) {
      mayapSerialPrintf(false, "[BOOT] TU PHUC HOI ME TU EEPROM SAU RESET/MAT DIEN\n");
    }
    if (mayapSerialDebugEnabled()) printSerialHelp();
  }

  // Goi duy nhat tu HMI/I2C Task. Moi Wire transaction sau boot nam o day.
  void serviceI2c(uint32_t now) {
    if (mayapSystemTripActive()) return;
    rtc_.update(now);
    publishRtcSnapshot();

    // Khong thuc thi lenh ghi neu chua con cho de tra ket qua. Nhu vay
    // khong co tinh huong EEPROM da doi nhung Control/HMI mat ACK.
    if (!i2cResultHasSpace()) return;
    I2cJob job{};
    if (!dequeueI2cJob(job)) return;

    I2cResult result{};
    result.type = job.type;
    result.id = job.id;
    result.origin = job.origin;
    result.revision = job.revision;
    snprintf(result.requestId, sizeof(result.requestId), "%s", job.requestId);
    switch (job.type) {
      case I2cJobType::SaveConfig:
        result.ok = store_.saveConfig(job.config, result.config);
        break;
      case I2cJobType::SaveBatch:
        result.ok = store_.saveBatch(job.batch);
        break;
      case I2cJobType::SetRtc:
        result.ok = rtc_.set(job.year, job.month, job.day,
                             job.hour, job.minute, job.second);
        publishRtcSnapshot();
        break;
      default:
        result.ok = false;
        break;
    }
    if (!enqueueI2cResult(result)) {
      mayapRequestSystemTrip(static_cast<uint16_t>(FaultCode::StorageQueueOverflow));
    }
  }

  void onI2cBusReset(uint32_t now) {
    rtc_.begin(now);
    publishRtcSnapshot();
    const bool ready = store_.begin();
    portENTER_CRITICAL(&i2cQueueMux_);
    i2cBusReprobePending_ = true;
    i2cBusReprobeOk_ = ready;
    portEXIT_CRITICAL(&i2cQueueMux_);
  }

  void update(uint32_t now) {
    consumeRtcSnapshot();
    processI2cResults(now);
    applyKernelHealth(now);
    power_.service(now);
    inputs_.update(now);
    processInputEvents(now);
    serviceSerial(now);
    sensor_.update();
    processSensor(now);
    updateSensorStuck(now);
    eventLog_.syncClock(now, rtcView_.epoch);
    faults_.set(FaultCode::RtcFailure, !rtcView_.valid, now,
                rtcView_.online ? 1 : 2);
    adjustResumeElapsedFromRtc(now);
    processInputModeTransition(now);
    processHmiTransactions(now);
    processWebTransactions(now);
    processResume(now);
    updateAlarms(now);
    clearHeaterLatchesIfSafe();
    updateAutoTune(now);
    updateTurning(now);
    updateHeatingAndOutputs(now);
    processOutputEvents(now);
    syncOutputFaults(now);
    updateBatchTime(now);
    updateLed(now);
    mirrorEventsToWeb();
    if (runtimeGate_.due(now, true)) {
      copyRuntimeToHmi(true);
      publishWebSnapshot(now);
    }
    if (checkpointGate_.due(now, false)) checkpointBatch(now, true);
    diagnosticGate_.setPeriod(diagnosticFast_ ? DIAGNOSTIC_FAST_STATUS_MS
                                              : DIAGNOSTIC_STATUS_MS);
    if (diagnosticGate_.due(now, false)) printStatus(now);
  }

  const MachineConfig &config() const { return config_; }
  const MachineRuntime &runtime() const { return runtime_; }
  const OutputState &outputs() const { return outputs_.state(); }

  // -------------------------- WEB STABLE PORT -------------------------------
  // Chi copy struct vao mailbox. Khong parse JSON, khong ghi EEPROM, khong doi IO.
  bool submitWebConfig(const WebConfigRequest &request) {
    bool ok = false;
    portENTER_CRITICAL(&webMux_);
    if (!webConfigPending_) {
      webConfigInbox_ = request;
      webConfigPending_ = true;
      ok = true;
    }
    portEXIT_CRITICAL(&webMux_);
    return ok;
  }

  bool submitWebCommand(const WebCommandRequest &request) {
    bool ok = false;
    portENTER_CRITICAL(&webMux_);
    if (webCommandCount_ < WEB_COMMAND_QUEUE_CAPACITY) {
      webCommandQueue_[webCommandTail_] = request;
      webCommandTail_ = static_cast<uint8_t>(
          (webCommandTail_ + 1U) % WEB_COMMAND_QUEUE_CAPACITY);
      ++webCommandCount_;
      ok = true;
    }
    portEXIT_CRITICAL(&webMux_);
    return ok;
  }

  bool takeWebConfigResult(WebConfigResult &out) {
    bool ok = false;
    portENTER_CRITICAL(&webMux_);
    if (webConfigResultCount_) {
      out = webConfigResults_[webConfigResultHead_];
      webConfigResultHead_ = static_cast<uint8_t>(
          (webConfigResultHead_ + 1U) % WEB_RESULT_QUEUE_CAPACITY);
      --webConfigResultCount_;
      ok = true;
    }
    portEXIT_CRITICAL(&webMux_);
    return ok;
  }

  bool takeWebCommandResult(WebCommandResult &out) {
    bool ok = false;
    portENTER_CRITICAL(&webMux_);
    if (webCommandResultCount_) {
      out = webCommandResults_[webCommandResultHead_];
      webCommandResultHead_ = static_cast<uint8_t>(
          (webCommandResultHead_ + 1U) % WEB_RESULT_QUEUE_CAPACITY);
      --webCommandResultCount_;
      ok = true;
    }
    portEXIT_CRITICAL(&webMux_);
    return ok;
  }

  bool copyWebSnapshot(WebMachineSnapshot &out) {
    bool ready = false;
    portENTER_CRITICAL(&webMux_);
    out = webSnapshot_;
    ready = webSnapshotReady_;
    portEXIT_CRITICAL(&webMux_);
    return ready;
  }

  bool takeWebEvent(WebEventRecord &out) {
    bool ok = false;
    portENTER_CRITICAL(&webMux_);
    if (webEventCount_) {
      out = webEvents_[webEventHead_];
      webEventHead_ = static_cast<uint8_t>(
          (webEventHead_ + 1U) % WEB_EVENT_QUEUE_CAPACITY);
      --webEventCount_;
      ok = true;
    }
    portEXIT_CRITICAL(&webMux_);
    return ok;
  }

  bool consumeWebConfigChanged() {
    portENTER_CRITICAL(&webMux_);
    const bool changed = webConfigReportDirty_;
    webConfigReportDirty_ = false;
    portEXIT_CRITICAL(&webMux_);
    return changed;
  }

  bool consumeWebStateChanged() {
    portENTER_CRITICAL(&webMux_);
    const bool changed = webStateDirty_;
    webStateDirty_ = false;
    portEXIT_CRITICAL(&webMux_);
    return changed;
  }

 private:
  enum class BatchPhase : uint8_t { Stopped, Prestart, Homing, Running };
  static constexpr uint8_t I2C_JOB_CAPACITY = 4U;
  static constexpr uint8_t I2C_RESULT_CAPACITY = 4U;

  void publishRtcSnapshot() {
    RtcSnapshot next{};
    next.epoch = rtc_.epoch();
    next.online = rtc_.online();
    next.valid = rtc_.valid();
    next.oscillatorStopped = rtc_.oscillatorStopped();
    snprintf(next.dateText, sizeof(next.dateText), "%s", rtc_.dateText());
    portENTER_CRITICAL(&i2cQueueMux_);
    rtcShared_ = next;
    portEXIT_CRITICAL(&i2cQueueMux_);
  }

  void consumeRtcSnapshot() {
    portENTER_CRITICAL(&i2cQueueMux_);
    rtcView_ = rtcShared_;
    portEXIT_CRITICAL(&i2cQueueMux_);
  }

  bool enqueueI2cJob(const I2cJob &job) {
    bool ok = false;
    portENTER_CRITICAL(&i2cQueueMux_);
    if (job.type == I2cJobType::SaveBatch) {
      // Checkpoint co the den lien tiep: chi can giu snapshot moi nhat.
      for (uint8_t i = 0U, index = i2cJobHead_; i < i2cJobCount_; ++i) {
        if (i2cJobs_[index].type == I2cJobType::SaveBatch) {
          i2cJobs_[index] = job;
          ok = true;
          break;
        }
        index = static_cast<uint8_t>((index + 1U) % I2C_JOB_CAPACITY);
      }
    }
    if (!ok && i2cJobCount_ < I2C_JOB_CAPACITY) {
      i2cJobs_[i2cJobTail_] = job;
      i2cJobTail_ = static_cast<uint8_t>((i2cJobTail_ + 1U) % I2C_JOB_CAPACITY);
      ++i2cJobCount_;
      ok = true;
    }
    portEXIT_CRITICAL(&i2cQueueMux_);
    return ok;
  }

  bool dequeueI2cJob(I2cJob &job) {
    bool ok = false;
    portENTER_CRITICAL(&i2cQueueMux_);
    if (i2cJobCount_) {
      job = i2cJobs_[i2cJobHead_];
      i2cJobHead_ = static_cast<uint8_t>((i2cJobHead_ + 1U) % I2C_JOB_CAPACITY);
      --i2cJobCount_;
      ok = true;
    }
    portEXIT_CRITICAL(&i2cQueueMux_);
    return ok;
  }

  bool i2cResultHasSpace() {
    bool hasSpace = false;
    portENTER_CRITICAL(&i2cQueueMux_);
    hasSpace = i2cResultCount_ < I2C_RESULT_CAPACITY;
    portEXIT_CRITICAL(&i2cQueueMux_);
    return hasSpace;
  }

  bool enqueueI2cResult(const I2cResult &result) {
    bool ok = false;
    portENTER_CRITICAL(&i2cQueueMux_);
    if (i2cResultCount_ < I2C_RESULT_CAPACITY) {
      i2cResults_[i2cResultTail_] = result;
      i2cResultTail_ = static_cast<uint8_t>((i2cResultTail_ + 1U) % I2C_RESULT_CAPACITY);
      ++i2cResultCount_;
      ok = true;
    }
    portEXIT_CRITICAL(&i2cQueueMux_);
    return ok;
  }

  bool dequeueI2cResult(I2cResult &result) {
    bool ok = false;
    portENTER_CRITICAL(&i2cQueueMux_);
    if (i2cResultCount_) {
      result = i2cResults_[i2cResultHead_];
      i2cResultHead_ = static_cast<uint8_t>((i2cResultHead_ + 1U) % I2C_RESULT_CAPACITY);
      --i2cResultCount_;
      ok = true;
    }
    portEXIT_CRITICAL(&i2cQueueMux_);
    return ok;
  }

  bool queueConfigSave(const MachineConfig &config, uint32_t transactionId,
                       ConfigOrigin origin = ConfigOrigin::Hmi,
                       uint32_t revision = 0U,
                       const char *requestId = nullptr) {
    I2cJob job{};
    job.type = I2cJobType::SaveConfig;
    job.id = transactionId;
    job.origin = origin;
    job.revision = revision;
    snprintf(job.requestId, sizeof(job.requestId), "%s", requestId ? requestId : "");
    job.config = config;
    return enqueueI2cJob(job);
  }

  bool queueBatchSave(const PackedBatchV1 &batch) {
    I2cJob job{};
    job.type = I2cJobType::SaveBatch;
    job.batch = batch;
    return enqueueI2cJob(job);
  }

  bool queueRtcSet(uint16_t year, uint8_t month, uint8_t day,
                   uint8_t hour, uint8_t minute, uint8_t second) {
    I2cJob job{};
    job.type = I2cJobType::SetRtc;
    job.year = year; job.month = month; job.day = day;
    job.hour = hour; job.minute = minute; job.second = second;
    return enqueueI2cJob(job);
  }

  void processI2cResults(uint32_t now) {
    bool reprobePending = false;
    bool reprobeOk = false;
    portENTER_CRITICAL(&i2cQueueMux_);
    if (i2cBusReprobePending_) {
      reprobePending = true;
      reprobeOk = i2cBusReprobeOk_;
      i2cBusReprobePending_ = false;
    }
    portEXIT_CRITICAL(&i2cQueueMux_);
    if (reprobePending) {
      if (reprobeOk) clearStorageDegraded(now);
      else latchStorageFault("I2C REPROBE");
    }

    I2cResult result{};
    uint8_t budget = I2C_RESULT_CAPACITY;
    while (budget-- && dequeueI2cResult(result)) {
      storageQueueOverflowLatched_ = false;
      switch (result.type) {
        case I2cJobType::SaveConfig: {
          const bool autoTuneSource = result.origin == ConfigOrigin::AutoTune;
          if (result.ok) {
            const MachineConfig previous = config_;
            config_ = result.config;
            hmiSetConfig(config_);
            pid_.applyConfigBumpless(now, config_.targetTemp, temperature_, config_);
            pidPower_ = pid_.output();
            if (batchRunning_ && previous.turnIntervalMin != config_.turnIntervalMin) {
              nextTurnAt_ = now + static_cast<uint32_t>(config_.turnIntervalMin) * 60000UL;
            }
            eventLog_.push(now,
                autoTuneSource ? EventType::AutoTuneEnd : EventType::ConfigSaved,
                static_cast<uint16_t>(autoTuneSource ? EventCode::AutoTuneSuccess
                                                     : EventCode::ConfigSaved),
                autoTuneSource
                    ? static_cast<int16_t>(lroundf(config_.kp * 10.0f)) : 0);
            clearStorageDegraded(now);
            portENTER_CRITICAL(&webMux_);
            webConfigReportDirty_ = true;
            portEXIT_CRITICAL(&webMux_);
          } else {
            latchStorageFault(autoTuneSource ? "AUTOTUNE SAVE" : "CONFIG SAVE");
          }

          if (result.origin == ConfigOrigin::Hmi && result.id != 0U) {
            hmiConfirmConfigSave(result.id, result.ok,
                                 result.ok ? &result.config : nullptr);
          } else if (result.origin == ConfigOrigin::Web) {
            WebConfigResult webResult{};
            webResult.transactionId = result.id;
            webResult.revision = result.revision;
            snprintf(webResult.requestId, sizeof(webResult.requestId), "%s",
                     result.requestId);
            webResult.ok = result.ok;
            if (result.ok) webResult.config = result.config;
            snprintf(webResult.message, sizeof(webResult.message), "%s",
                     result.ok ? "DA LUU VA DOC LAI EEPROM"
                               : "LOI LUU CAU HINH EEPROM");
            pushWebConfigResult(webResult);
          }
          break;
        }
        case I2cJobType::SaveBatch:
          if (result.ok) clearStorageDegraded(now);
          else latchStorageFault("BATCH SAVE");
          break;
        case I2cJobType::SetRtc:
          mayapSerialPrintf(false, "[RTC] SET=%s\n", result.ok ? "OK" : "FAIL");
          break;
        default:
          break;
      }
    }
  }

  bool takeWebConfigRequest(WebConfigRequest &out) {
    bool ok = false;
    portENTER_CRITICAL(&webMux_);
    if (webConfigPending_) {
      out = webConfigInbox_;
      webConfigPending_ = false;
      ok = true;
    }
    portEXIT_CRITICAL(&webMux_);
    return ok;
  }

  bool takeWebCommandRequest(WebCommandRequest &out) {
    bool ok = false;
    portENTER_CRITICAL(&webMux_);
    if (webCommandCount_) {
      out = webCommandQueue_[webCommandHead_];
      webCommandHead_ = static_cast<uint8_t>(
          (webCommandHead_ + 1U) % WEB_COMMAND_QUEUE_CAPACITY);
      --webCommandCount_;
      ok = true;
    }
    portEXIT_CRITICAL(&webMux_);
    return ok;
  }

  void pushWebConfigResult(const WebConfigResult &result) {
    portENTER_CRITICAL(&webMux_);
    if (webConfigResultCount_ >= WEB_RESULT_QUEUE_CAPACITY) {
      webConfigResultHead_ = static_cast<uint8_t>(
          (webConfigResultHead_ + 1U) % WEB_RESULT_QUEUE_CAPACITY);
      --webConfigResultCount_;
      ++webResultDropCount_;
    }
    webConfigResults_[webConfigResultTail_] = result;
    webConfigResultTail_ = static_cast<uint8_t>(
        (webConfigResultTail_ + 1U) % WEB_RESULT_QUEUE_CAPACITY);
    ++webConfigResultCount_;
    portEXIT_CRITICAL(&webMux_);
  }

  void pushWebCommandResult(const WebCommandResult &result) {
    portENTER_CRITICAL(&webMux_);
    if (webCommandResultCount_ >= WEB_RESULT_QUEUE_CAPACITY) {
      webCommandResultHead_ = static_cast<uint8_t>(
          (webCommandResultHead_ + 1U) % WEB_RESULT_QUEUE_CAPACITY);
      --webCommandResultCount_;
      ++webResultDropCount_;
    }
    webCommandResults_[webCommandResultTail_] = result;
    webCommandResultTail_ = static_cast<uint8_t>(
        (webCommandResultTail_ + 1U) % WEB_RESULT_QUEUE_CAPACITY);
    ++webCommandResultCount_;
    portEXIT_CRITICAL(&webMux_);
  }

  void pushWebEvent(const EventEntry &entry) {
    WebEventRecord record{};
    record.sequence = entry.sequence;
    record.epoch = entry.epoch;
    record.code = entry.code;
    record.value = entry.value;
    record.type = entry.type;
    record.flags = entry.flags;

    portENTER_CRITICAL(&webMux_);
    if (webEventCount_ >= WEB_EVENT_QUEUE_CAPACITY) {
      webEventHead_ = static_cast<uint8_t>(
          (webEventHead_ + 1U) % WEB_EVENT_QUEUE_CAPACITY);
      --webEventCount_;
      ++webEventDropCount_;
    }
    webEvents_[webEventTail_] = record;
    webEventTail_ = static_cast<uint8_t>(
        (webEventTail_ + 1U) % WEB_EVENT_QUEUE_CAPACITY);
    ++webEventCount_;
    portEXIT_CRITICAL(&webMux_);
  }

  void mirrorEventsToWeb() {
    EventEntry events[4]{};
    const uint8_t count = eventLog_.copyAfter(lastWebMirroredEventSequence_,
                                               events, 4U);
    for (uint8_t i = 0U; i < count; ++i) {
      pushWebEvent(events[i]);
      lastWebMirroredEventSequence_ = events[i].sequence;
    }
  }

  void publishWebSnapshot(uint32_t now) {
    WebMachineSnapshot next{};
    next.config = config_;
    next.runtime = runtime_;
    next.kernel = mayapKernelHealthSnapshot();

    const InputState &input = inputs_.state();
    next.autoMode = input.autoMode;
    next.manualMode = !input.autoMode;
    next.systemHealthy = !mayapSystemTripActive() && faults_.activeCount() == 0U;
    next.sensorHealthy = sensorUsable_;
    next.rtcHealthy = rtcView_.valid;
    next.storageHealthy = !storageFaultLatched_ && !storageDegraded_;
    next.heaterLocked = !sensorUsable_ || storageFaultLatched_ ||
                        storageDegraded_ || abnormalResetLatched_ ||
                        emergencyActive_ || mayapSystemTripActive();
    next.turningLocked = turnFaultLatched_ || mayapSystemTripActive();
    next.waitingPowerResume = resumeConfirmationRequired_;
    next.activeFaultCount = faults_.activeCount();
    next.highestFaultCode = static_cast<uint16_t>(faults_.primary());
    next.uptimeSec = elapsedMs(now, bootAt_) / 1000UL;

    portENTER_CRITICAL(&webMux_);
    const bool changed = !webSnapshotReady_ ||
        webSnapshot_.runtime.batchRunning != next.runtime.batchRunning ||
        webSnapshot_.runtime.sensorOnline != next.runtime.sensorOnline ||
        webSnapshot_.runtime.heaterOn != next.runtime.heaterOn ||
        webSnapshot_.runtime.circulationFanOn != next.runtime.circulationFanOn ||
        webSnapshot_.runtime.ventFanOn != next.runtime.ventFanOn ||
        webSnapshot_.runtime.turnState != next.runtime.turnState ||
        webSnapshot_.runtime.alarmMask != next.runtime.alarmMask ||
        webSnapshot_.runtime.autoTuneState != next.runtime.autoTuneState ||
        webSnapshot_.runtime.autoTuneProgress != next.runtime.autoTuneProgress ||
        webSnapshot_.runtime.stateCode != next.runtime.stateCode ||
        webSnapshot_.runtime.eventSequence != next.runtime.eventSequence ||
        webSnapshot_.waitingPowerResume != next.waitingPowerResume ||
        webSnapshot_.systemHealthy != next.systemHealthy;
    next.sequence = webSnapshot_.sequence + 1U;
    webSnapshot_ = next;
    webSnapshotReady_ = true;
    if (changed) webStateDirty_ = true;
    portEXIT_CRITICAL(&webMux_);
  }

  void applyKernelHealth(uint32_t now) {
    const KernelHealthSnapshot health = mayapKernelHealthSnapshot();
    faults_.set(FaultCode::ControlDeadline, health.deadlineWarning, now,
                static_cast<int16_t>(std::min<uint32_t>(health.lastControlCycleUs / 1000U,
                                                        INT16_MAX)));
    const UBaseType_t minimumStack = std::min(health.controlStackFree,
        std::min(health.hmiStackFree, health.supervisorStackFree));
    faults_.set(FaultCode::StackLow, health.stackLow, now,
                static_cast<int16_t>(std::min<UBaseType_t>(minimumStack,
                                                           INT16_MAX)));
    faults_.set(FaultCode::HeapLow, health.heapLow, now,
                static_cast<int16_t>(std::min<size_t>(health.freeInternalHeap / 1024U,
                                                      INT16_MAX)));
    faults_.set(FaultCode::HeapCorrupt, health.heapCorrupt, now);
    faults_.set(FaultCode::I2cBusFailure, health.i2cFailed, now,
                static_cast<int16_t>(health.i2cConsecutiveErrors));
    faults_.set(FaultCode::StorageQueueOverflow,
                storageQueueOverflowLatched_, now);
  }

  // ----------------------------- Event kernel --------------------------------
  void processInputEvents(uint32_t now) {
    InputEvent event{};
    uint8_t budget = INPUT_EVENT_QUEUE_SIZE;
    while (budget-- && inputs_.popEvent(event)) {
      const uint16_t code = static_cast<uint16_t>(EventCode::InputBase) +
                            static_cast<uint8_t>(event.channel);
      eventLog_.push(now, EventType::InputChanged, code,
                     event.active ? 1 : 0);
      mayapSerialPrintf(false, "[IN] %s=%s\n", InputManager::name(event.channel),
                       event.active ? "ON" : "OFF");
    }
  }

  void processOutputEvents(uint32_t now) {
    OutputEvent event{};
    uint8_t budget = OUTPUT_EVENT_QUEUE_SIZE;
    while (budget-- && outputs_.popEvent(event)) {
      const uint16_t code = static_cast<uint16_t>(EventCode::OutputBase) +
                            static_cast<uint8_t>(event.channel);
      // Khong dua xung SSR vao nhat ky HMI: PID co the doi moi vai giay va
      // se day mat cac lenh/loi quan trong. Serial van co the xem khi debug.
      if (event.channel != OutputChannel::HeaterSsr &&
          event.channel != OutputChannel::PulseSpare &&
          event.channel != OutputChannel::RelaySpare) {
        eventLog_.push(now, EventType::OutputChanged, code,
                       event.active ? 1 : 0);
      }
      mayapSerialPrintf(false, "[OUT] %s=%s\n", outputName(event.channel),
                       event.active ? "ON" : "OFF");
    }
  }

  void syncOutputFaults(uint32_t now) {
    faults_.set(FaultCode::OutputConflict, outputs_.conflictDetected(), now);
    faults_.set(FaultCode::RelayRateExceeded, outputs_.relayRateExceeded(), now,
                static_cast<int16_t>(std::min<uint16_t>(
                    outputs_.transitionsThisHour(), INT16_MAX)));
    runtime_.alarmMask = faults_.alarmMask();
  }


  // ----------------------------- Sensor --------------------------------------
  void processSensor(uint32_t now) {
    const bool wasUsable = sensorUsable_;
    const bool newData = sensor_.takeNewData();
    if (newData) {
      const float candidateTemp = sensor_.temperatureC() + config_.tempOffset;
      const float candidateHum = clampFloat(
          sensor_.humidityRH() + config_.humidityOffset, 0.0f, 100.0f);
      const float candidateRaw = sensor_.rawTemperatureC() + config_.tempOffset;
      const bool frameValid = sensor_.dataValid() && isfinite(candidateTemp) &&
                              isfinite(candidateHum) && isfinite(candidateRaw);

      bool acceptSample = frameValid;
      if (frameValid && isfinite(lastAcceptedTemperature_) &&
          candidateTemp < lastAcceptedTemperature_ - SENSOR_MAX_DOWN_STEP_C) {
        // Giam dot ngot co the lam PID tang nhiet sai. Yeu cau 3 mau moi on dinh
        // truoc khi chap nhan baseline moi; mau tang cao van vao bao ve ngay.
        acceptSample = false;
        if (!sensorSuspect_) {
          sensorSuspect_ = true;
          sensorPlausibilityGoodStreak_ = 1U;
        } else if (isfinite(lastSuspectCandidate_) &&
                   fabsf(candidateTemp - lastSuspectCandidate_) <= 0.30f) {
          if (sensorPlausibilityGoodStreak_ < 255U) {
            ++sensorPlausibilityGoodStreak_;
          }
        } else {
          sensorPlausibilityGoodStreak_ = 1U;
        }
        lastSuspectCandidate_ = candidateTemp;
        if (sensorPlausibilityGoodStreak_ >=
            SENSOR_PLAUSIBILITY_CLEAR_SAMPLES) {
          acceptSample = true;
          sensorSuspect_ = false;
          sensorPlausibilityGoodStreak_ = 0U;
        }
      } else if (frameValid) {
        if (sensorSuspect_) {
          if (sensorPlausibilityGoodStreak_ < 255U) {
            ++sensorPlausibilityGoodStreak_;
          }
          if (sensorPlausibilityGoodStreak_ >=
              SENSOR_PLAUSIBILITY_CLEAR_SAMPLES) {
            sensorSuspect_ = false;
            sensorPlausibilityGoodStreak_ = 0U;
          } else {
            acceptSample = false;
          }
        }
      }

      if (acceptSample) {
        temperature_ = candidateTemp;
        humidity_ = candidateHum;
        rawTemperature_ = candidateRaw;
        lastAcceptedTemperature_ = candidateTemp;
        lastSuspectCandidate_ = candidateTemp;
        if (goodSensorStreak_ < 255U) ++goodSensorStreak_;
        newSensorSample_ = true;
      } else {
        goodSensorStreak_ = 0U;
        newSensorSample_ = false;
      }
    }

    const uint32_t configuredTimeout = std::min<uint32_t>(15000UL,
        std::max<uint32_t>(5000UL,
          static_cast<uint32_t>(config_.sensorTimeoutSec) * 1000UL));
    const bool transportValid = sensor_.online() &&
                                sensor_.dataAgeMs() <= configuredTimeout &&
                                isfinite(temperature_) && isfinite(humidity_) &&
                                isfinite(rawTemperature_);
    safetySampleValid_ = transportValid;
    if (!transportValid) goodSensorStreak_ = 0;
    sensorUsable_ = transportValid && !sensorSuspect_ &&
                    goodSensorStreak_ >= SENSOR_RECOVERY_GOOD_SAMPLES;

    if (wasUsable != sensorUsable_) {
      pid_.reset();
      heatRestartNotBefore_ = now + HEAT_RESTART_LOCKOUT_MS;
      if (!sensorUsable_) postCoolUntil_ = now + POST_COOL_MS;
      eventLog_.push(now,
          sensorUsable_ ? EventType::SensorRestored : EventType::SensorLost,
          static_cast<uint16_t>(sensorUsable_ ? EventCode::SensorOnline
                                              : EventCode::SensorOffline),
          0, 0U);
      mayapSerialPrintf(false, "[SHT] CONTROL %s\n", sensorUsable_ ? "READY" : "BLOCKED");
    }

    SHT485Event event;
    while (sensor_.popEvent(event)) {
      switch (event) {
        case SHT485Event::PresentAtStartup: mayapSerialPrintf(false, "[SHT] CO CAM BIEN KHI KHOI DONG\n"); break;
        case SHT485Event::MissingAtStartup: mayapSerialPrintf(false, "[SHT] MAT CAM BIEN KHI KHOI DONG\n"); break;
        case SHT485Event::Lost: mayapSerialPrintf(false, "[SHT] MAT KET NOI\n"); break;
        case SHT485Event::Restored: mayapSerialPrintf(false, "[SHT] DA PHUC HOI\n"); break;
        default: break;
      }
    }
  }

  // ----------------------------- HMI/WEB/EEPROM ------------------------------
  bool executeMachineCommand(uint32_t now, const HmiCommand &command,
                             const char *&message) {
    if (command.type == HmiCommandType::None) {
      message = "LENH KHONG HOP LE";
      return false;
    }
    if (command.validForMs != 0U &&
        elapsedMs(now, command.createdAt) > command.validForMs) {
      message = "LENH DA HET HAN";
      return false;
    }

    bool ok = false;
    message = "LENH KHONG HOP LE";
    switch (command.type) {
      case HmiCommandType::BatchStart:
        ok = startBatch(now, message); break;
      case HmiCommandType::BatchStop:
        ok = stopBatch(now, message); break;
      case HmiCommandType::AlarmAck: {
        if (emergencyActive_) sirenMutedUntil_ = now + SIREN_TEMPORARY_MUTE_MS;
        const bool hadTurnFault = turnFaultLatched_;
        const bool turnCleared = hadTurnFault ? clearTurnFault() : false;
        const bool systemAck = (command.alarmMask & AlarmSystem) != 0U;
        const bool resetCleared = systemAck && abnormalResetLatched_;
        if (resetCleared) {
          abnormalResetLatched_ = false;
          power_.acknowledge();
          faults_.set(FaultCode::AbnormalReset, false, now);
          heatRestartNotBefore_ = now + HEAT_RESTART_LOCKOUT_MS;
          pid_.reset();
        }
        (void)faults_.acknowledge(now, command.alarmMask);
        const bool persistentSystemFault = systemAck && storageFaultLatched_;
        ok = !persistentSystemFault;
        message = persistentSystemFault ? "LOI BO NHO CHUA XOA"
                : emergencyActive_ ? "COI TAM DUNG 5 PHUT"
                : resetCleared ? "DA XAC NHAN RESET LOI"
                : turnCleared ? "DA XOA LOI DAO"
                : hadTurnFault ? "THA NUT/KT HANH TRINH"
                : "DA XAC NHAN";
        break;
      }
      case HmiCommandType::TurnLeft:
        ok = requestHmiManualTurn(now, TurnDirection::Left,
                                  command.actuatorLeaseMs, message); break;
      case HmiCommandType::TurnRight:
        ok = requestHmiManualTurn(now, TurnDirection::Right,
                                  command.actuatorLeaseMs, message); break;
      case HmiCommandType::TurnStop:
        hmiManualTurnUntil_ = 0;
        stopTurn(false);
        ok = true; message = "DA DUNG DAO"; break;
      case HmiCommandType::AutoTuneStart:
        ok = startAutoTune(now, message); break;
      case HmiCommandType::ResumeYes:
        if (resumePending_ && resumeConfirmationRequired_) {
          resumeConfirmationRequired_ = false;
          eventLog_.push(now, EventType::Recovery,
                         static_cast<uint16_t>(EventCode::ResumeAccepted));
          ok = true; message = "DA CHON TIEP TUC ME";
        } else {
          message = "KHONG CO ME CHO XAC NHAN";
        }
        break;
      case HmiCommandType::ResumeNo:
        if (resumePending_ && resumeConfirmationRequired_) {
          resumeConfirmationRequired_ = false;
          resumePending_ = false;
          elapsedBeforeStartSec_ = 0U;
          savedElapsedAtCheckpoint_ = 0U;
          lastCheckpointEpoch_ = 0U;
          resumeClockAdjusted_ = true;
          turnCountToday_ = 0U;
          turnCountBatch_ = 0U;
          (void)clearBatchRecord();
          eventLog_.push(now, EventType::Recovery,
                         static_cast<uint16_t>(EventCode::ResumeRejected));
          ok = true; message = "DA HUY ME CU";
        } else {
          message = "KHONG CO ME CHO XAC NHAN";
        }
        break;
      default:
        break;
    }
    return ok;
  }

  void processHmiTransactions(uint32_t now) {
    MachineConfig requested{};
    uint32_t transactionId = 0;
    if (hmiTakeSavedConfig(requested, transactionId)) {
      sanitizeMachineConfig(requested);
      const bool queued = queueConfigSave(requested, transactionId,
                                          ConfigOrigin::Hmi);
      if (!queued) {
        storageQueueOverflowLatched_ = true;
        hmiConfirmConfigSave(transactionId, false, nullptr);
      }
      mayapSerialPrintf(false,
          "[CFG] queue=%s SV=%.1f HIGH=%.1f EMG=%.1f turn=%umin\n",
          queued ? "OK" : "FULL", requested.targetTemp,
          requested.highTempAlarm, requested.emergencyTemp,
          requested.turnIntervalMin);
    }

    HmiCommand command{};
    uint8_t budget = 0U;
    while (budget++ < COMMAND_QUEUE_SIZE && hmiTakeCommand(command)) {
      const char *message = "LENH KHONG HOP LE";
      const bool ok = executeMachineCommand(now, command, message);
      if (!ok) {
        eventLog_.push(now, EventType::System,
                       static_cast<uint16_t>(EventCode::CommandRejected),
                       static_cast<int16_t>(command.type));
      }
      hmiConfirmCommand(command.id, ok, message);
    }
  }

  void processWebTransactions(uint32_t now) {
    WebConfigRequest configRequest{};
    if (takeWebConfigRequest(configRequest)) {
      WebConfigResult immediate{};
      immediate.transactionId = configRequest.transactionId;
      immediate.revision = configRequest.revision;
      snprintf(immediate.requestId, sizeof(immediate.requestId), "%s",
               configRequest.requestId);

      MachineConfig clean = configRequest.config;
      sanitizeMachineConfig(clean);
      if (configRequest.revision == 0U ||
          !machineConfigEqual(clean, configRequest.config)) {
        immediate.ok = false;
        snprintf(immediate.message, sizeof(immediate.message), "%s",
                 configRequest.revision == 0U
                     ? "REVISION KHONG HOP LE"
                     : "CAU HINH NGOAI GIOI HAN AN TOAN");
        pushWebConfigResult(immediate);
      } else {
        const bool queued = queueConfigSave(clean, configRequest.transactionId,
                                            ConfigOrigin::Web,
                                            configRequest.revision,
                                            configRequest.requestId);
        if (!queued) {
          storageQueueOverflowLatched_ = true;
          immediate.ok = false;
          snprintf(immediate.message, sizeof(immediate.message),
                   "HANG DOI EEPROM DANG BAN");
          pushWebConfigResult(immediate);
        }
      }
    }

    uint8_t budget = 2U;
    WebCommandRequest request{};
    while (budget-- && takeWebCommandRequest(request)) {
      const char *message = "LENH KHONG HOP LE";
      const bool ok = executeMachineCommand(now, request.command, message);
      if (!ok) {
        eventLog_.push(now, EventType::System,
                       static_cast<uint16_t>(EventCode::CommandRejected),
                       static_cast<int16_t>(request.command.type));
      }
      WebCommandResult result{};
      result.sequence = request.sequence;
      snprintf(result.requestId, sizeof(result.requestId), "%s",
               request.requestId);
      result.type = request.command.type;
      result.ok = ok;
      snprintf(result.message, sizeof(result.message), "%s", message);
      pushWebCommandResult(result);
    }
  }

  bool startBatch(uint32_t now, const char *&message) {
    const InputState &in = inputs_.state();
    if (batchRunning_) { message = "ME DANG CHAY"; return false; }
    if (storageFaultLatched_ || storageDegraded_) { message = "LOI BO NHO CAU HINH"; return false; }
    if (abnormalResetLatched_) { message = "HAY XAC NHAN RESET LOI"; return false; }
    if (resumePending_) { message = "DUNG ME CU TRUOC"; return false; }
    if (autotune_.running()) { message = "AUTO TUNE DANG CHAY"; return false; }
    if (!in.autoMode) { message = "HAY CHUYEN SANG AUTO"; return false; }
    if (!sensorUsable_) { message = "CAM BIEN CHUA SAN SANG"; return false; }
    if (!rtcView_.valid) { message = "RTC CHUA HOP LE"; return false; }
    if (in.limitLeft && in.limitRight) { message = "LOI 2 HANH TRINH"; return false; }
    if (turnFaultLatched_) { message = "DANG CO LOI DAO"; return false; }
    if (highTemperatureActive_ || emergencyActive_) { message = "NHIET DANG QUA CAO"; return false; }
    if (REQUIRE_HEATER_ENABLE_TO_START && !in.heaterEnable) {
      message = "HAY BAT CONG TAC NHIET"; return false;
    }

    batchRunning_ = true;
    batchPhase_ = BatchPhase::Prestart;
    batchStartedAt_ = now;
    phaseStartedAt_ = now;
    elapsedBeforeStartSec_ = 0;
    savedElapsedAtCheckpoint_ = 0U;
    lastCheckpointEpoch_ = 0U;
    resumeClockAdjusted_ = true;
    turnCountToday_ = 0;
    turnCountBatch_ = 0;
    nextTurnAt_ = 0;
    needHome_ = !(in.limitLeft ^ in.limitRight);
    if (in.limitLeft) trayPosition_ = TrayPosition::Left;
    else if (in.limitRight) trayPosition_ = TrayPosition::Right;
    else trayPosition_ = TrayPosition::Unknown;
    lowTempTimer_.reset();
    humidityTimer_.reset();
    heatRestartNotBefore_ = std::max<uint32_t>(
        heatRestartNotBefore_, now + FAN_PRESTART_MS);
    if (!saveBatchRecord()) {
      batchRunning_ = false;
      batchPhase_ = BatchPhase::Stopped;
      message = "LOI LUU TRANG THAI ME";
      return false;
    }
    message = "DA BAT DAU ME";
    eventLog_.push(now, EventType::BatchStart,
                   static_cast<uint16_t>(EventCode::BatchStart));
    mayapSerialPrintf(false, "[BATCH] START\n");
    return true;
  }

  bool stopBatch(uint32_t now, const char *&message) {
    if (!batchRunning_ && !resumePending_) { message = "KHONG CO ME DANG CHAY"; return false; }
    batchRunning_ = false;
    resumePending_ = false;
    batchPhase_ = BatchPhase::Stopped;
    stopTurn(false);
    nextTurnAt_ = 0;
    postCoolUntil_ = now + POST_COOL_MS;
    elapsedBeforeStartSec_ = 0;
    (void)clearBatchRecord();
    message = "DA DUNG ME";
    eventLog_.push(now, EventType::BatchStop,
                   static_cast<uint16_t>(EventCode::BatchStop));
    mayapSerialPrintf(false, "[BATCH] STOP\n");
    return true;
  }

  bool startAutoTune(uint32_t now, const char *&message) {
    const InputState &in = inputs_.state();
    if (batchRunning_ || resumePending_) { message = "DUNG ME TRUOC"; return false; }
    if (storageFaultLatched_ || storageDegraded_) { message = "LOI BO NHO CAU HINH"; return false; }
    if (abnormalResetLatched_) { message = "HAY XAC NHAN RESET LOI"; return false; }
    if (!in.autoMode) { message = "HAY CHUYEN SANG AUTO"; return false; }
    if (!in.heaterEnable) { message = "HAY BAT CONG TAC NHIET"; return false; }
    if (!sensorUsable_) { message = "CAM BIEN CHUA SAN SANG"; return false; }
    if (!rtcView_.valid) { message = "RTC CHUA HOP LE"; return false; }
    if (highTemperatureActive_ || emergencyActive_) { message = "NHIET DANG QUA CAO"; return false; }
    if (config_.targetTemp + AUTOTUNE_BAND_C >= config_.highTempAlarm) {
      message = "KHOANG NHIET KHONG DU"; return false;
    }
    autotune_.configure(config_.targetTemp);
    autotune_.start(now, temperature_);
    pid_.reset();
    postCoolUntil_ = 0;
    message = "AUTO TUNE DA BAT DAU";
    eventLog_.push(now, EventType::AutoTuneStart,
                   static_cast<uint16_t>(EventCode::AutoTuneStarted));
    mayapSerialPrintf(false, "[TUNE] START power=%u%% band=%.2fC\n",
                     AUTOTUNE_RELAY_POWER_PERCENT, AUTOTUNE_BAND_C);
    return true;
  }

  bool requestHmiManualTurn(uint32_t now, TurnDirection direction,
                            uint16_t leaseMs, const char *&message) {
    if (inputs_.state().autoMode) { message = "CHI DAO TAY O MANUAL"; return false; }
    if (turnFaultLatched_) { message = "DANG CO LOI DAO"; return false; }
    hmiManualDirection_ = direction;
    hmiManualTurnUntil_ = now + std::max<uint16_t>(leaseMs, 200U);
    message = direction == TurnDirection::Left ? "DANG DAO TRAI" : "DANG DAO PHAI";
    return true;
  }

  // Cong bu phan thoi gian mat dien bang moc RTC da ghi cung checkpoint.
  // Chi chay mot lan khi DS3231 hop le; neu RTC loi thi dung elapsedSec da luu.
  void adjustResumeElapsedFromRtc(uint32_t now) {
    if (resumeClockAdjusted_ || lastCheckpointEpoch_ == 0U ||
        !rtcView_.valid || (!resumePending_ && !batchRunning_)) return;
    const uint32_t currentEpoch = rtcView_.epoch;
    if (currentEpoch < lastCheckpointEpoch_) {
      resumeClockAdjusted_ = true;
      mayapSerialPrintf(false, "[TIME] BO QUA BU GIO: EPOCH LUI\n");
      return;
    }
    const uint32_t delta = currentEpoch - lastCheckpointEpoch_;
    if (delta > MAX_RTC_RECOVERY_GAP_SEC) {
      resumeClockAdjusted_ = true;
      mayapSerialPrintf(false, "[TIME] BO QUA BU GIO: GAP=%lus\n",
                        static_cast<unsigned long>(delta));
      return;
    }
    const uint64_t corrected = static_cast<uint64_t>(savedElapsedAtCheckpoint_) + delta;
    elapsedBeforeStartSec_ = corrected > UINT32_MAX
        ? UINT32_MAX : static_cast<uint32_t>(corrected);
    if (batchRunning_) batchStartedAt_ = now;
    resumeClockAdjusted_ = true;
    mayapSerialPrintf(false, "[TIME] DA BU THOI GIAN TU RTC: +%lus, elapsed=%lus\n",
                      static_cast<unsigned long>(delta),
                      static_cast<unsigned long>(elapsedBeforeStartSec_));
  }

  // ----------------------------- Resume --------------------------------------
  void processResume(uint32_t now) {
    if (!resumePending_ || resumeConfirmationRequired_) return;
    const InputState &in = inputs_.state();
    const bool ready = !storageFaultLatched_ && !abnormalResetLatched_ &&
                       in.autoMode && sensorUsable_ &&
                       !turnFaultLatched_ && !(in.limitLeft && in.limitRight) &&
                       !highTemperatureActive_ && !emergencyActive_ &&
                       elapsedMs(now, bootAt_) >=
                         static_cast<uint32_t>(config_.powerRestoreDelaySec) * 1000UL;
    if (!ready) return;
    resumePending_ = false;
    batchRunning_ = true;
    batchPhase_ = BatchPhase::Prestart;
    batchStartedAt_ = now;
    phaseStartedAt_ = now;
    nextTurnAt_ = 0;
    if (in.limitLeft) trayPosition_ = TrayPosition::Left;
    else if (in.limitRight) trayPosition_ = TrayPosition::Right;
    else trayPosition_ = TrayPosition::Unknown;
    needHome_ = trayPosition_ == TrayPosition::Unknown;
    heatRestartNotBefore_ = now + FAN_PRESTART_MS;
    eventLog_.push(now, EventType::BatchStart,
                   static_cast<uint16_t>(EventCode::BatchResume),
                   static_cast<int16_t>(std::min<uint32_t>(elapsedBeforeStartSec_ / 60UL,
                                                          INT16_MAX)),
                   1U);
    mayapSerialPrintf(false, "[BATCH] AUTO RESUME elapsed=%lus\n",
                     static_cast<unsigned long>(elapsedBeforeStartSec_));
  }

  // ----------------------------- Alarm ---------------------------------------
  void updateAlarms(uint32_t now) {
    const float safetyTemp = isfinite(rawTemperature_)
        ? (isfinite(temperature_) ? std::max(rawTemperature_, temperature_) : rawTemperature_)
        : temperature_;
    const bool validSafety = safetySampleValid_ && isfinite(safetyTemp);
    const bool wasHigh = highTemperatureActive_;
    const bool wasEmergency = emergencyActive_;

    // Cap 3 vao ngay bang mau hop le dau tien, sau do giu qua mat cam bien.
    if (validSafety && safetyTemp >= config_.emergencyTemp) {
      emergencyActive_ = true;
      emergencyClearTimer_.reset();
    } else if (emergencyActive_) {
      const bool safeToClear = validSafety &&
          safetyTemp <= config_.emergencyTemp - EMERGENCY_CLEAR_HYSTERESIS_C;
      if (emergencyClearTimer_.update(now, safeToClear,
                                     EMERGENCY_CLEAR_CONFIRM_MS)) {
        emergencyActive_ = false;
        emergencyClearTimer_.reset();
      }
    }

    // Cap 2 co xac nhan vao va xac nhan thoat rieng, khong chap chon contactor.
    if (!highTemperatureActive_) {
      const bool highTrip = validSafety && safetyTemp >= config_.highTempAlarm;
      if (highTripTimer_.update(now, highTrip, HIGH_TEMP_CONFIRM_MS)) {
        highTemperatureActive_ = true;
        highClearTimer_.reset();
      }
    } else if (!emergencyActive_) {
      const bool safeToClear = validSafety &&
          safetyTemp <= config_.highTempAlarm - HIGH_TEMP_CLEAR_HYSTERESIS_C;
      if (highClearTimer_.update(now, safeToClear,
                                HIGH_TEMP_CLEAR_CONFIRM_MS)) {
        highTemperatureActive_ = false;
        highTripTimer_.reset();
        highClearTimer_.reset();
      }
    }
    if (emergencyActive_) highTemperatureActive_ = true;

    if (autotune_.running()) {
      ventTemperatureActive_ = false;
    } else {
      if (!ventTemperatureActive_ && validSafety &&
          safetyTemp >= config_.ventOnTemp) ventTemperatureActive_ = true;
      if (ventTemperatureActive_ && validSafety &&
          safetyTemp <= config_.ventOffTemp) ventTemperatureActive_ = false;
      if (!validSafety) ventTemperatureActive_ = false;
    }

    if ((!wasHigh && highTemperatureActive_) ||
        (!wasEmergency && emergencyActive_)) {
      pid_.reset();
      postCoolUntil_ = now + POST_COOL_MS;
    }
    if ((wasHigh && !highTemperatureActive_) ||
        (wasEmergency && !emergencyActive_)) {
      pid_.reset();
      heatRestartNotBefore_ = now + HEAT_RESTART_LOCKOUT_MS;
    }
    if (wasEmergency && !emergencyActive_) sirenMutedUntil_ = 0;

    const bool lowEligible = batchRunning_ && sensorUsable_ &&
        elapsedBatchMs(now) >= LOW_TEMP_STARTUP_GRACE_MS;
    const bool lowCondition = lowEligible && temperature_ <= config_.lowTempAlarm;
    lowTemperatureActive_ = lowTempTimer_.update(now, lowCondition,
                                                 LOW_TEMP_CONFIRM_MS);
    if (lowTemperatureActive_ && temperature_ >= config_.lowTempAlarm + 0.2f) {
      lowTemperatureActive_ = false;
      lowTempTimer_.reset();
    }

    const bool humCondition = batchRunning_ && sensorUsable_ &&
                              humidity_ <= config_.lowHumidityAlarm;
    humidityLowActive_ = humidityTimer_.update(
        now, humCondition, static_cast<uint32_t>(config_.humidityAlarmDelaySec) * 1000UL);
    if (humidityLowActive_ && humidity_ >= config_.lowHumidityAlarm + 2.0f) {
      humidityLowActive_ = false;
      humidityTimer_.reset();
    }

    const bool sensorGrace = !timeReached(now, sensorStartupGraceUntil_) &&
                             !sensorUsable_;
    faults_.set(FaultCode::SensorLost, !sensorUsable_ && !sensorGrace, now,
                static_cast<int16_t>(std::min<uint32_t>(sensor_.dataAgeMs() / 100U,
                                                       INT16_MAX)));
    faults_.set(FaultCode::SensorInvalid,
                !sensorGrace && sensor_.online() && !safetySampleValid_, now);
    faults_.set(FaultCode::SensorSuspect, sensorSuspect_ && !sensorGrace, now,
                isfinite(lastSuspectCandidate_)
                    ? static_cast<int16_t>(lroundf(lastSuspectCandidate_ * 10.0f))
                    : 0);
    faults_.set(FaultCode::LowTemperature, lowTemperatureActive_, now,
                isfinite(temperature_) ? static_cast<int16_t>(lroundf(temperature_ * 10.0f)) : 0);
    faults_.set(FaultCode::HighTemperature, highTemperatureActive_, now,
                isfinite(safetyTemp) ? static_cast<int16_t>(lroundf(safetyTemp * 10.0f)) : 0);
    faults_.set(FaultCode::EmergencyTemperature, emergencyActive_, now,
                isfinite(safetyTemp) ? static_cast<int16_t>(lroundf(safetyTemp * 10.0f)) : 0);
    faults_.set(FaultCode::HumidityLow, humidityLowActive_, now,
                isfinite(humidity_) ? static_cast<int16_t>(lroundf(humidity_ * 10.0f)) : 0);
    faults_.set(FaultCode::StorageUnavailable, storageFaultLatched_, now);
    faults_.set(FaultCode::StorageDegraded, storageDegraded_, now);
    faults_.set(FaultCode::AbnormalReset, abnormalResetLatched_, now,
                static_cast<int16_t>(resetReason_));
    runtime_.alarmMask = faults_.alarmMask();
  }

  // ----------------------------- Auto Tune -----------------------------------
  void updateAutoTune(uint32_t now) {
    if (!autotune_.running()) return;
    const InputState &in = inputs_.state();
    if (!in.autoMode || !in.heaterEnable || !sensorUsable_ ||
        highTemperatureActive_ || emergencyActive_ || batchRunning_) {
      autotune_.abort();
      pid_.reset();
      postCoolUntil_ = now + POST_COOL_MS;
      heatRestartNotBefore_ = now + HEAT_RESTART_LOCKOUT_MS;
      eventLog_.push(now, EventType::AutoTuneEnd,
                     static_cast<uint16_t>(EventCode::AutoTuneFailed),
                     0, 1U);
      mayapSerialPrintf(false, "[TUNE] ABORT safety/mode\n");
      return;
    }
    if (!newSensorSample_) return;
    MachineConfig tuned{};
    if (autotune_.update(now, temperature_, config_, tuned)) {
      sanitizeMachineConfig(tuned);
      const bool queued = queueConfigSave(tuned, 0U, ConfigOrigin::AutoTune);
      if (!queued) {
        storageQueueOverflowLatched_ = true;
        autotune_.abort();
        eventLog_.push(now, EventType::AutoTuneEnd,
                       static_cast<uint16_t>(EventCode::AutoTuneFailed),
                       1, 1U);
        mayapSerialPrintf(false, "[TUNE] FAIL QUEUE\n");
      } else {
        mayapSerialPrintf(false,
                         "[TUNE] RESULT QUEUED Kp=%.3f Ki=%.3f Kd=%.3f\n",
                         tuned.kp, tuned.ki, tuned.kd);
      }
      postCoolUntil_ = now + POST_COOL_MS;
      heatRestartNotBefore_ = now + HEAT_RESTART_LOCKOUT_MS;
    }
  }

  // ----------------------------- AUTO/MANUAL ---------------------------------
  void processInputModeTransition(uint32_t now) {
    const bool autoMode = inputs_.state().autoMode;
    if (autoMode == previousAutoMode_) return;
    previousAutoMode_ = autoMode;
    stopTurn(false);
    hmiManualTurnUntil_ = 0;
    manualTurnRearmRequired_ = true;
    if (!autoMode && !inputs_.state().circulationFan &&
        (outputs_.state().heatMaster || outputs_.state().heaterSsr)) {
      postCoolUntil_ = now + POST_COOL_MS;
    }
    if (autoMode) {
      const InputState &in = inputs_.state();
      if (in.limitLeft) trayPosition_ = TrayPosition::Left;
      else if (in.limitRight) trayPosition_ = TrayPosition::Right;
      else { trayPosition_ = TrayPosition::Unknown; needHome_ = batchRunning_; }
      if (batchRunning_) nextTurnAt_ = now +
          static_cast<uint32_t>(config_.turnIntervalMin) * 60000UL;
    }
    eventLog_.push(now, EventType::ModeChanged,
        static_cast<uint16_t>(autoMode ? EventCode::ModeAuto : EventCode::ModeManual));
    mayapSerialPrintf(false, "[MODE] %s\n", autoMode ? "AUTO" : "MANUAL");
  }

  // ----------------------------- Turning -------------------------------------
  void updateTurning(uint32_t now) {
    const InputState &in = inputs_.state();
    if (in.limitLeft && in.limitRight) {
      latchTurnFault(FaultCode::TurnLimitConflict, "HAI HANH TRINH CUNG ON");
    }
    if (turnFaultLatched_) { stopTurn(false); turnPhase_ = TurnPhase::Fault; return; }
    if (faults_.turningInhibited()) {
      stopTurn(false);
      manualTurnRearmRequired_ = true;
      return;
    }
    if (highTemperatureActive_ || emergencyActive_) {
      stopTurn(false);
      manualTurnRearmRequired_ = true;
      return;
    }

    // Xac nhan den dich.
    if (turnPhase_ == TurnPhase::MovingLeft && in.limitLeft) {
      completeTurn(now, TrayPosition::Left);
    } else if (turnPhase_ == TurnPhase::MovingRight && in.limitRight) {
      completeTurn(now, TrayPosition::Right);
    }

    if (turnPhase_ == TurnPhase::MovingLeft || turnPhase_ == TurnPhase::MovingRight) {
      const bool originStillActive =
          (turnPhase_ == TurnPhase::MovingLeft && moveOrigin_ == TrayPosition::Right && in.limitRight) ||
          (turnPhase_ == TurnPhase::MovingRight && moveOrigin_ == TrayPosition::Left && in.limitLeft);
      if (originStillActive && elapsedMs(now, moveStartedAt_) >= TURN_LIMIT_RELEASE_TIMEOUT_MS) {
        latchTurnFault(FaultCode::TurnLimitStuck, "HANH TRINH KHONG NHA"); return;
      }
      if (elapsedMs(now, moveStartedAt_) >=
          static_cast<uint32_t>(config_.turnMaxRunSec) * 1000UL) {
        latchTurnFault(FaultCode::TurnTimeout, "DAO QUA THOI GIAN"); return;
      }
      if (in.autoMode && !sensorUsable_) {
        stopTurn(false); return;
      }
    }

    if (turnPhase_ == TurnPhase::DeadtimeLeft && timeReached(now, deadtimeUntil_)) {
      beginPhysicalMove(now, TurnDirection::Left);
    } else if (turnPhase_ == TurnPhase::DeadtimeRight && timeReached(now, deadtimeUntil_)) {
      beginPhysicalMove(now, TurnDirection::Right);
    }

    if (!in.autoMode) {
      updateManualTurning(now, in);
      return;
    }

    // AUTO: cong tac dao tay bi vo hieu hoa hoan toan.
    if (!batchRunning_ || !config_.turningEnabled || batchPhase_ == BatchPhase::Prestart) {
      if (turnPhase_ != TurnPhase::Idle) stopTurn(false);
      return;
    }

    if (batchPhase_ == BatchPhase::Homing || needHome_) {
      if (turnPhase_ == TurnPhase::Idle) {
        if (in.limitLeft) { trayPosition_ = TrayPosition::Left; finishHoming(now); }
        else if (in.limitRight) { trayPosition_ = TrayPosition::Right; finishHoming(now); }
        else requestTurn(now, HOME_TO_LEFT ? TurnDirection::Left : TurnDirection::Right,
                         true, false);
      }
      return;
    }

    if (nextTurnAt_ == 0U) {
      nextTurnAt_ = now + static_cast<uint32_t>(config_.turnIntervalMin) * 60000UL;
    }
    if (turnPhase_ == TurnPhase::Idle && timeReached(now, nextTurnAt_)) {
      if (trayPosition_ == TrayPosition::Left) {
        requestTurn(now, TurnDirection::Right, false, true);
      } else if (trayPosition_ == TrayPosition::Right) {
        requestTurn(now, TurnDirection::Left, false, true);
      } else {
        needHome_ = true;
      }
    }
  }

  void updateManualTurning(uint32_t now, const InputState &in) {
    bool left = in.turnLeft;
    bool right = in.turnRight;
    if (manualTurnRearmRequired_) {
      if (left || right) {
        hmiManualTurnUntil_ = 0;
        stopTurn(false);
        return;
      }
      manualTurnRearmRequired_ = false;
    }
    if (!left && !right && hmiManualTurnUntil_ && !timeReached(now, hmiManualTurnUntil_)) {
      left = hmiManualDirection_ == TurnDirection::Left;
      right = hmiManualDirection_ == TurnDirection::Right;
    }
    if (hmiManualTurnUntil_ && timeReached(now, hmiManualTurnUntil_)) hmiManualTurnUntil_ = 0;

    if (left && right) {
      if (manualConflictSince_ == 0U) manualConflictSince_ = now;
      stopTurn(false);
      if (elapsedMs(now, manualConflictSince_) >= TURN_INPUT_CONFLICT_MS) {
        latchTurnFault(FaultCode::TurnCommandConflict, "HAI LENH DAO CUNG ON");
      }
      return;
    }
    manualConflictSince_ = 0;

    if (!left && !right) {
      if (turnPhase_ != TurnPhase::Idle) stopTurn(false);
      return;
    }
    if (turnPhase_ == TurnPhase::Idle) {
      requestTurn(now, left ? TurnDirection::Left : TurnDirection::Right,
                  false, false);
    } else if ((left && (turnPhase_ == TurnPhase::MovingRight ||
                        turnPhase_ == TurnPhase::DeadtimeRight)) ||
               (right && (turnPhase_ == TurnPhase::MovingLeft ||
                          turnPhase_ == TurnPhase::DeadtimeLeft))) {
      stopTurn(false); // nha het truoc, lan update sau moi doi chieu
    }
  }

  void requestTurn(uint32_t now, TurnDirection direction, bool homing, bool countMove) {
    const InputState &in = inputs_.state();
    if (in.limitLeft && in.limitRight) { latchTurnFault(FaultCode::TurnLimitConflict, "LOI HANH TRINH"); return; }
    if ((direction == TurnDirection::Left && in.limitLeft) ||
        (direction == TurnDirection::Right && in.limitRight)) {
      trayPosition_ = direction == TurnDirection::Left
          ? TrayPosition::Left : TrayPosition::Right;
      if (homing) finishHoming(now);
      return;
    }
    stopTurn(false);
    moveDirection_ = direction;
    moveIsHoming_ = homing;
    moveCounts_ = countMove;
    moveOrigin_ = in.limitLeft ? TrayPosition::Left
                : in.limitRight ? TrayPosition::Right : TrayPosition::Unknown;
    deadtimeUntil_ = now + TURN_DIRECTION_DEADTIME_MS;
    turnPhase_ = direction == TurnDirection::Left
        ? TurnPhase::DeadtimeLeft : TurnPhase::DeadtimeRight;
  }

  void beginPhysicalMove(uint32_t now, TurnDirection direction) {
    const InputState &in = inputs_.state();
    if ((direction == TurnDirection::Left && in.limitLeft) ||
        (direction == TurnDirection::Right && in.limitRight)) {
      completeTurn(now, direction == TurnDirection::Left
                        ? TrayPosition::Left : TrayPosition::Right);
      return;
    }
    moveStartedAt_ = now;
    turnPhase_ = direction == TurnDirection::Left
        ? TurnPhase::MovingLeft : TurnPhase::MovingRight;
    eventLog_.push(now, EventType::Turning,
        static_cast<uint16_t>(moveIsHoming_
            ? (direction == TurnDirection::Left ? EventCode::TurnHomeLeft
                                                : EventCode::TurnHomeRight)
            : (direction == TurnDirection::Left ? EventCode::TurnStartLeft
                                                : EventCode::TurnStartRight)));
  }

  void completeTurn(uint32_t now, TrayPosition position) {
    stopTurn(false);
    trayPosition_ = position;
    if (moveIsHoming_) {
      finishHoming(now);
    } else if (moveCounts_) {
      eventLog_.push(now, EventType::Turning,
          static_cast<uint16_t>(position == TrayPosition::Left
              ? EventCode::TurnCompleteLeft : EventCode::TurnCompleteRight));
      ++turnCountBatch_;
      if (turnCountToday_ < UINT16_MAX) ++turnCountToday_;
      config_.nextDirection = position == TrayPosition::Left
          ? TurnDirection::Right : TurnDirection::Left;
      nextTurnAt_ = now + static_cast<uint32_t>(config_.turnIntervalMin) * 60000UL;
      (void)saveBatchRecord();
    }
    moveIsHoming_ = false;
    moveCounts_ = false;
  }

  void finishHoming(uint32_t now) {
    needHome_ = false;
    if (batchPhase_ == BatchPhase::Homing) batchPhase_ = BatchPhase::Running;
    nextTurnAt_ = now + static_cast<uint32_t>(config_.turnIntervalMin) * 60000UL;
    mayapSerialPrintf(false, "[TURN] HOME OK pos=%s\n",
                     trayPosition_ == TrayPosition::Left ? "LEFT" : "RIGHT");
  }

  void stopTurn(bool fault) {
    (void)fault;
    if (turnPhase_ != TurnPhase::Fault) turnPhase_ = TurnPhase::Idle;
    moveStartedAt_ = 0;
  }

  void latchTurnFault(FaultCode code, const char *reason) {
    if (!turnFaultLatched_) mayapSerialPrintf(false, "[TURN] FAULT: %s\n", reason ? reason : "UNKNOWN");
    turnFaultLatched_ = true;
    turnFaultCode_ = code;
    faults_.set(code, true, millis());
    turnPhase_ = TurnPhase::Fault;
    moveStartedAt_ = 0;
  }

  bool clearTurnFault() {
    const InputState &in = inputs_.state();
    if ((in.limitLeft && in.limitRight) || in.turnLeft || in.turnRight) return false;
    const uint32_t now = millis();
    if (turnFaultCode_ != FaultCode::None) {
      faults_.set(turnFaultCode_, false, now);
      faults_.acknowledge(now, AlarmTurning);
    }
    turnFaultCode_ = FaultCode::None;
    turnFaultLatched_ = false;
    turnPhase_ = TurnPhase::Idle;
    trayPosition_ = in.limitLeft ? TrayPosition::Left
                  : in.limitRight ? TrayPosition::Right : TrayPosition::Unknown;
    needHome_ = batchRunning_ && in.autoMode &&
                trayPosition_ == TrayPosition::Unknown;
    return true;
  }

  // ----------------------------- Heating/Output -------------------------------
  void updateHeatingAndOutputs(uint32_t now) {
    const InputState &in = inputs_.state();
    OutputRequest req{};
    req.light = in.light; // doc lap AUTO va me

    // Sau mat dien, trong luc dang cho nguoi dung chon TIEP TUC/HUY, tat toan
    // bo co cau, quat hut va nhiet. Den van doc lap de nguoi dung thao tac HMI.
    if (resumeConfirmationRequired_) {
      req.immediateMasterDrop = true;
      outputs_.update(now, req);
      pid_.reset();
      pidPower_ = 0.0f;
      runtime_.heaterPower = 0.0f;
      return;
    }

    bool baseCirculation = false;
    if (in.autoMode) {
      baseCirculation = config_.circulationFanEnabled &&
          (batchRunning_ || in.heaterEnable || autotune_.running());
    } else {
      baseCirculation = in.circulationFan;
    }
    // Khong cho quat tat cung luc voi bo gia nhiet: giu hau lam mat.
    if (!baseCirculation &&
        (outputs_.state().heatMaster || outputs_.state().heaterSsr)) {
      postCoolUntil_ = now + POST_COOL_MS;
    }
    const bool postCooling = !timeReached(now, postCoolUntil_);
    const bool sensorStartupGraceActive =
        !timeReached(now, sensorStartupGraceUntil_);
    // Khong bat quat hut chi vi cam bien chua khoi tao xong. Chi lam mat khi
    // loi cam bien da duoc xac nhan va may dang/da co nhiet can xa.
    const bool sensorFaultNeedsFan = !sensorUsable_ && !sensorStartupGraceActive &&
        (batchRunning_ || outputs_.state().heatMaster ||
         outputs_.state().heaterSsr || postCooling);
    const bool safetyForcesFan = ventTemperatureActive_ || highTemperatureActive_ ||
                                 emergencyActive_ || sensorFaultNeedsFan || postCooling;
    const bool circulation = baseCirculation || safetyForcesFan;
    req.circulationFan = circulation;

    if (circulation != previousFanCommand_) {
      previousFanCommand_ = circulation;
      fanOnSince_ = circulation ? now : 0;
    }
    const bool fanStable = circulation &&
                           elapsedMs(now, fanOnSince_) >= FAN_PRESTART_MS;

    req.ventFan = ventTemperatureActive_ || highTemperatureActive_ ||
                  emergencyActive_ || sensorFaultNeedsFan;

    const bool batchAllowsHeat = config_.allowHeatWithoutBatch || batchRunning_;
    const bool fanAllowsHeat = !MANUAL_FAN_CAN_DISABLE_HEATING || fanStable;
    // Mat EEPROM giua me: tiep tuc bang cau hinh RAM; cam me moi/nhiet ngoai me.
    const bool storageAllowsHeat = !storageFaultLatched_ &&
        (!storageDegraded_ || batchRunning_);
    const bool commonHeatPermit = !faults_.heatInhibited() && storageAllowsHeat &&
        !abnormalResetLatched_ && in.heaterEnable &&
        batchAllowsHeat && sensorUsable_ &&
        fanAllowsHeat && !highTemperatureActive_ && !emergencyActive_ &&
        timeReached(now, heatRestartNotBefore_);

    float commandedPower = 0.0f;
    if (autotune_.running()) {
      commandedPower = autotune_.power();
    } else if (commonHeatPermit && !ventTemperatureActive_) {
      if (newSensorSample_) {
        pidPower_ = pid_.updateOnNewSample(now, config_.targetTemp,
                                           temperature_, config_, true);
      }
      commandedPower = pidPower_;
    } else {
      pid_.reset();
      pidPower_ = 0.0f;
    }
    newSensorSample_ = false;

    const bool tunePermit = !faults_.heatInhibited() &&
                            !storageFaultLatched_ && !storageDegraded_ &&
                            !abnormalResetLatched_ && autotune_.running() &&
                            in.autoMode && in.heaterEnable &&
                            sensorUsable_ && fanStable &&
                            !highTemperatureActive_ && !emergencyActive_;
    const bool masterPermit = commonHeatPermit || tunePermit;
    req.heatMaster = masterPermit;
    req.heaterSsr = masterPermit && ssrWindowOn(now, commandedPower,
                                                config_.pidCycleSec);
    req.immediateMasterDrop = emergencyActive_ || !sensorUsable_ ||
                              faults_.heatInhibited() || storageFaultLatched_ ||
                              (storageDegraded_ && !batchRunning_) ||
                              abnormalResetLatched_;

    req.turnLeft = turnPhase_ == TurnPhase::MovingLeft;
    req.turnRight = turnPhase_ == TurnPhase::MovingRight;
    // Coi lon GPIO47 danh cho hai tinh huong doi hoi co mat nguoi ngay:
    // qua nhiet cap 3 va SSR dinh (nhiet tang du da cat lenh).
    req.siren = (emergencyActive_ || heaterRunawayLatched_) &&
                timeReached(now, sirenMutedUntil_);

    outputs_.update(now, req);
    runtime_.heaterPower = commandedPower;
    updateHeaterIntegrity(now, commandedPower, outputs_.state().heaterSsr,
                          masterPermit);
  }

  // --------------------------------------------------------------------------
  // BAO VE THANH NHIET
  // Hai che do hong nguy hiem nhat cua may ap cong nghiep ma phan cung hien tai
  // VAN phat hien duoc bang chinh cam bien nhiet + trang thai output:
  //   401 RUNAWAY      - SSR dinh: da tat het ma nhiet van tang.
  //   402 NO RESPONSE  - dut nhiet/hong contactor: day cong suat ma khong len.
  // Ca hai chi chay khi cam bien dung duoc, neu khong ta dang suy luan tu so
  // lieu rac. Auto Tune tu chu dong dong cat nen duoc mien tru.
  // --------------------------------------------------------------------------
  void updateHeaterIntegrity(uint32_t now, float commandedPower,
                             bool ssrOn, bool masterOn) {
    const bool measurable = sensorUsable_ && isfinite(temperature_) &&
                            !autotune_.running() &&
                            timeReached(now, sensorStartupGraceUntil_);
    if (!measurable) {
      runawayRefAt_ = 0U;
      stallRefAt_ = 0U;
      return;
    }

    // ---- 401: nhiet tang trong khi khong he cap nhiet ----
    // Bat ky xung SSR hoac contactor dong nao cung lam moc quan sat khong con
    // sach, nen phai dat lai cua so.
    if (ssrOn || masterOn || commandedPower > 0.0f) {
      runawayRefAt_ = 0U;
    } else {
      if (runawayRefAt_ == 0U) {
        runawayRefAt_ = now;
        runawayRefTemp_ = temperature_;
      }
      // Nhiet tut xuong la binh thuong khi tat: ha moc theo de khong tich luy
      // sai lech va bao nham sau nhieu gio.
      if (temperature_ < runawayRefTemp_) runawayRefTemp_ = temperature_;
      if (elapsedMs(now, runawayRefAt_) >= HEATER_RUNAWAY_WINDOW_MS) {
        const float rise = temperature_ - runawayRefTemp_;
        if (rise >= HEATER_RUNAWAY_RISE_C) {
          heaterRunawayLatched_ = true;
          heaterRunawayRiseX10_ = static_cast<int16_t>(
              std::min<long>(lroundf(rise * 10.0f), INT16_MAX));
          // Xa nhiet ngay: quat tuan hoan + quat hut duoc giu chay qua
          // postCool trong khi contactor da duoc inhibitsHeat cat.
          postCoolUntil_ = now + POST_COOL_MS;
        }
        // Truot cua so de lan sau van do tiep, khong ket o mot moc cu.
        runawayRefAt_ = now;
        runawayRefTemp_ = temperature_;
      }
    }

    // ---- 402: day cong suat ma nhiet khong len ----
    const bool demanding = masterOn && commandedPower >= HEATER_STALL_MIN_POWER &&
                           temperature_ < config_.targetTemp;
    if (!demanding) {
      stallRefAt_ = 0U;
      heaterStallLatched_ = false;
    } else {
      if (stallRefAt_ == 0U) {
        stallRefAt_ = now;
        stallRefTemp_ = temperature_;
      }
      // Neu co luc nao nhiet vuot moc du chi mot chut thi thanh nhiet co lam
      // viec: dat lai moc thay vi cong don.
      if (temperature_ > stallRefTemp_ + HEATER_STALL_RISE_C) {
        stallRefAt_ = now;
        stallRefTemp_ = temperature_;
        heaterStallLatched_ = false;
      } else if (elapsedMs(now, stallRefAt_) >= HEATER_STALL_WINDOW_MS) {
        heaterStallLatched_ = true;
        heaterStallMinutes_ = static_cast<int16_t>(std::min<uint32_t>(
            elapsedMs(now, stallRefAt_) / 60000UL, INT16_MAX));
      }
    }

    faults_.set(FaultCode::HeaterRunaway, heaterRunawayLatched_, now,
                heaterRunawayRiseX10_);
    faults_.set(FaultCode::HeaterNoResponse, heaterStallLatched_, now,
                heaterStallMinutes_);
  }

  // Cam bien treo: so sanh dem Modbus tho. Chi chay khi dang thuc su nhan
  // duoc frame moi, neu khong thi day la loi "mat cam bien" (101) chu khong
  // phai "dung yen" (104).
  void updateSensorStuck(uint32_t now) {
    // Dem frame Modbus hop le, KHONG dung newSensorSample_: bo loc
    // plausibility co the chan mau nhieu phut lien va se gay bao nham.
    const uint32_t frames = sensor_.goodFrames();
    const bool haveNewFrame = frames != sensorStuckFrameRef_;
    sensorStuckFrameRef_ = frames;
    if (!sensor_.online() || !sensor_.dataValid()) {
      sensorStuckSince_ = 0U;
      sensorStuckHasRef_ = false;
      faults_.set(FaultCode::SensorStuck, false, now);
      return;
    }
    if (haveNewFrame) {
      const int16_t t = sensor_.rawTempCount();
      const int16_t h = sensor_.rawHumidityCount();
      if (!sensorStuckHasRef_ || t != sensorStuckTempRef_ ||
          h != sensorStuckHumRef_) {
        sensorStuckHasRef_ = true;
        sensorStuckTempRef_ = t;
        sensorStuckHumRef_ = h;
        sensorStuckSince_ = now;
      }
    }
    const bool stuck = sensorStuckHasRef_ && sensorStuckSince_ != 0U &&
                       elapsedMs(now, sensorStuckSince_) >= SENSOR_STUCK_TIMEOUT_MS;
    faults_.set(FaultCode::SensorStuck, stuck, now,
                static_cast<int16_t>(std::min<uint32_t>(
                    elapsedMs(now, sensorStuckSince_) / 60000UL, INT16_MAX)));
  }

  // Nguoi van hanh xoa 401/402 bang cach ACK tren HMI. Dieu kien vat ly phai
  // het truoc, giong moi latching fault khac.
  void clearHeaterLatchesIfSafe() {
    if (heaterRunawayLatched_ && !faults_.active(FaultCode::HeaterRunaway)) {
      heaterRunawayLatched_ = false;
      heaterRunawayRiseX10_ = 0;
      runawayRefAt_ = 0U;
    }
    if (heaterStallLatched_ && !faults_.active(FaultCode::HeaterNoResponse)) {
      heaterStallLatched_ = false;
      heaterStallMinutes_ = 0;
      stallRefAt_ = 0U;
    }
  }

  bool ssrWindowOn(uint32_t now, float power, uint16_t cycleSec) {
    const uint32_t windowMs = std::max<uint32_t>(1000UL,
        static_cast<uint32_t>(cycleSec) * 1000UL);
    if (ssrWindowStartedAt_ == 0U) ssrWindowStartedAt_ = now;
    const uint32_t elapsed = elapsedMs(now, ssrWindowStartedAt_);
    if (elapsed >= windowMs) {
      ssrWindowStartedAt_ += (elapsed / windowMs) * windowMs;
    }
    const float clamped = clampFloat(power, 0.0f, 100.0f);
    uint32_t onMs = static_cast<uint32_t>(clamped * windowMs / 100.0f);
    if (onMs < SSR_MIN_ON_MS) onMs = 0;
    else if (windowMs - onMs < SSR_MIN_OFF_MS) onMs = windowMs;
    return elapsedMs(now, ssrWindowStartedAt_) < onMs;
  }

  // ----------------------------- Batch ---------------------------------------
  void updateBatchTime(uint32_t now) {
    if (batchRunning_ && batchPhase_ == BatchPhase::Prestart &&
        elapsedMs(now, phaseStartedAt_) >= FAN_PRESTART_MS) {
      if (needHome_ && inputs_.state().autoMode) {
        batchPhase_ = BatchPhase::Homing;
      } else {
        batchPhase_ = BatchPhase::Running;
        nextTurnAt_ = now + static_cast<uint32_t>(config_.turnIntervalMin) * 60000UL;
      }
    }

    if (!batchRunning_) return;
    const uint32_t totalSec = elapsedBatchSec(now);
    const uint32_t dayIndex = totalSec / 86400UL;
    runtime_.currentDay = static_cast<uint8_t>(std::min<uint32_t>(
        static_cast<uint32_t>(config_.totalIncubationDays), dayIndex + 1U));
    const uint32_t dayNumber = dayIndex + 1U;
    if (lastTurnCounterDay_ == 0U) lastTurnCounterDay_ = dayNumber;
    if (dayNumber != lastTurnCounterDay_) {
      lastTurnCounterDay_ = dayNumber;
      turnCountToday_ = 0;
      (void)saveBatchRecord();
    }
  }

  uint32_t elapsedBatchSec(uint32_t now) const {
    if (!batchRunning_) return elapsedBeforeStartSec_;
    return elapsedBeforeStartSec_ + elapsedMs(now, batchStartedAt_) / 1000UL;
  }
  uint32_t elapsedBatchMs(uint32_t now) const {
    const uint64_t ms = static_cast<uint64_t>(elapsedBeforeStartSec_) * 1000ULL +
                        (batchRunning_ ? elapsedMs(now, batchStartedAt_) : 0U);
    return ms > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(ms);
  }
  void checkpointBatch(uint32_t now, bool force) {
    if (!batchRunning_ && !resumePending_) return;
    if (!force && elapsedMs(now, lastCheckpointAt_) < BATCH_CHECKPOINT_MS) return;
    lastCheckpointAt_ = now;
    (void)saveBatchRecord();
  }
  bool saveBatchRecord() {
    PackedBatchV1 p{};
    p.wasRunning = (batchRunning_ || resumePending_) ? 1U : 0U;
    p.nextDirection = static_cast<uint8_t>(config_.nextDirection);
    p.turnCountToday = turnCountToday_;
    p.turnCountBatch = turnCountBatch_;
    p.elapsedSec = elapsedBatchSec(millis());
    p.checkpointEpoch = rtcView_.epoch;
    const bool queued = queueBatchSave(p);
    if (!queued) {
      storageQueueOverflowLatched_ = true;
      return false;
    }
    if (p.checkpointEpoch != 0U) {
      savedElapsedAtCheckpoint_ = p.elapsedSec;
      lastCheckpointEpoch_ = p.checkpointEpoch;
      resumeClockAdjusted_ = true;
    }
    return true;
  }
  bool clearBatchRecord() {
    PackedBatchV1 p{};
    const bool queued = queueBatchSave(p);
    if (!queued) {
      storageQueueOverflowLatched_ = true;
      return false;
    }
    savedElapsedAtCheckpoint_ = 0U;
    lastCheckpointEpoch_ = 0U;
    resumeClockAdjusted_ = true;
    return true;
  }
  void latchStorageFault(const char *where) {
    const uint32_t now = millis();
    const bool keepRunningFromRam = batchRunning_ || resumePending_;
    if (keepRunningFromRam) {
      if (!storageDegraded_) {
        mayapSerialPrintf(false, "[EEPROM] DEGRADED at %s - CONTINUE FROM RAM\n",
                          where ? where : "UNKNOWN");
        eventLog_.push(now, EventType::StorageError,
                       static_cast<uint16_t>(EventCode::FaultBase) +
                       static_cast<uint16_t>(FaultCode::StorageDegraded), 0, 1U);
      }
      storageDegraded_ = true;
      faults_.set(FaultCode::StorageDegraded, true, now);
      return;
    }

    if (!storageFaultLatched_) {
      mayapSerialPrintf(false, "[EEPROM] UNAVAILABLE at %s\n",
                        where ? where : "UNKNOWN");
      eventLog_.push(now, EventType::StorageError,
                     static_cast<uint16_t>(EventCode::FaultBase) +
                     static_cast<uint16_t>(FaultCode::StorageUnavailable), 0, 2U);
    }
    storageFaultLatched_ = true;
    faults_.set(FaultCode::StorageUnavailable, true, now);
    pid_.reset();
  }

  void clearStorageDegraded(uint32_t now) {
    if (!storageDegraded_) return;
    storageDegraded_ = false;
    faults_.set(FaultCode::StorageDegraded, false, now);
    mayapSerialPrintf(false, "[EEPROM] CONNECTION RESTORED\n");
  }

  // ----------------------------- Runtime/LED ---------------------------------
  void copyRuntimeToHmi(bool force) {
    const uint32_t now = millis();
    if (!force && elapsedMs(now, lastRuntimePushAt_) < RUNTIME_TO_HMI_MS) return;
    lastRuntimePushAt_ = now;

    runtime_.temperature = temperature_;
    runtime_.humidity = humidity_;
    runtime_.sensorOnline = sensorUsable_;
    runtime_.sensorStartupGrace = !timeReached(now, sensorStartupGraceUntil_) &&
                                  !sensorUsable_;
    runtime_.resumeConfirmationRequired = resumeConfirmationRequired_;
    runtime_.timeValid = rtcView_.valid;
    if (runtime_.timeValid) {
      snprintf(runtime_.dateText, sizeof(runtime_.dateText), "%s",
               rtcView_.dateText);
    }
    runtime_.batchRunning = batchRunning_ || resumePending_;
    if (resumePending_ && !batchRunning_) {
      const uint32_t dayIndex = elapsedBeforeStartSec_ / 86400UL;
      runtime_.currentDay = static_cast<uint8_t>(std::min<uint32_t>(
          static_cast<uint32_t>(config_.totalIncubationDays), dayIndex + 1U));
    } else if (!batchRunning_) {
      runtime_.currentDay = 0;
    }
    runtime_.turnCountToday = turnCountToday_;
    runtime_.turnCountBatch = turnCountBatch_;
    runtime_.heaterOn = outputs_.state().heaterSsr;
    runtime_.circulationFanOn = outputs_.state().circulationFan;
    runtime_.ventFanOn = outputs_.state().ventFan;
    runtime_.autoTuneState = autotune_.state();
    runtime_.autoTuneProgress = autotune_.progress();
    runtime_.primaryFaultCode = static_cast<uint16_t>(faults_.primary());
    runtime_.activeFaultCount = faults_.activeCount();
    runtime_.activeFaultDisplayCount = faults_.copyActiveForHmi(
        runtime_.activeFaults, HMI_FAULT_DISPLAY_CAPACITY);
    runtime_.faultNotificationSequence = faults_.notificationSequence();
    runtime_.lastRaisedFaultCode = static_cast<uint16_t>(faults_.lastRaisedCode());
    runtime_.lastRaisedFaultSeverity = static_cast<uint8_t>(faults_.lastRaisedSeverity());
    runtime_.eventSequence = eventLog_.sequence();
    runtime_.relayTransitionsHour = outputs_.transitionsThisHour();
    // Trang thai mang chi de hien thi/ghi log. Khong bao gio tham gia vao
    // quyet dinh dieu khien: may phai chay day du khi mat mang.
    if (config_.cloudEnabled) {
      mayapReadNetStatus(runtime_.netState, runtime_.portalState);
    } else {
      runtime_.netState = NetState::Disabled;
      runtime_.portalState = PortalState::Idle;
    }
    if (runtime_.netState != lastNetState_) {
      eventLog_.push(now, EventType::System,
                     static_cast<uint16_t>(EventCode::NetStateChanged),
                     static_cast<int16_t>(runtime_.netState));
      lastNetState_ = runtime_.netState;
    }
    if (runtime_.portalState != lastPortalState_) {
      if (runtime_.portalState == PortalState::Active) {
        eventLog_.push(now, EventType::System,
                       static_cast<uint16_t>(EventCode::WifiPortalStarted), 0);
      } else if (runtime_.portalState == PortalState::Failed) {
        eventLog_.push(now, EventType::System,
                       static_cast<uint16_t>(EventCode::WifiPortalFailed), 0);
      }
      lastPortalState_ = runtime_.portalState;
    }
    runtime_.alarmMask = faults_.alarmMask();

    if (turnFaultLatched_) runtime_.turnState = TurnState::Fault;
    else if (turnPhase_ == TurnPhase::MovingLeft) runtime_.turnState = TurnState::Left;
    else if (turnPhase_ == TurnPhase::MovingRight) runtime_.turnState = TurnState::Right;
    else if (batchRunning_ && inputs_.state().autoMode && config_.turningEnabled)
      runtime_.turnState = TurnState::Waiting;
    else runtime_.turnState = TurnState::Stopped;

    if (batchRunning_ && nextTurnAt_ && !timeReached(now, nextTurnAt_)) {
      runtime_.nextTurnMinutes = static_cast<uint16_t>(
          std::min<uint32_t>(65535UL, (nextTurnAt_ - now + 59999UL) / 60000UL));
    } else runtime_.nextTurnMinutes = 0;

    const InputState &in = inputs_.state();
    const char *state = "SAN SANG";
    MachineStateCode stateCode = MachineStateCode::Boot;
    if (emergencyActive_) {
      state = "QUA NHIET CAP 3"; stateCode = MachineStateCode::Emergency;
    } else if (storageFaultLatched_ || abnormalResetLatched_ ||
               faults_.active(FaultCode::OutputConflict)) {
      state = storageFaultLatched_ ? "LOI BO NHO" :
              abnormalResetLatched_ ? "CHO XN RESET LOI" : "LOI HE THONG";
      stateCode = MachineStateCode::SystemFault;
    } else if (runtime_.sensorStartupGrace) {
      state = "KHOI TAO CAM BIEN"; stateCode = MachineStateCode::Boot;
    } else if (!sensorUsable_) {
      state = "MAT CAM BIEN"; stateCode = MachineStateCode::SensorFault;
    } else if (turnFaultLatched_) {
      state = "LOI DAO"; stateCode = MachineStateCode::TurningFault;
    } else if (autotune_.running()) {
      state = "DANG AUTO TUNE"; stateCode = MachineStateCode::AutoTune;
    } else if (resumePending_) {
      state = in.autoMode ? "CHO PHUC HOI" : "HAY CHUYEN AUTO";
      stateCode = MachineStateCode::ResumeWait;
    } else if (batchRunning_ && batchPhase_ == BatchPhase::Homing) {
      state = "DANG TIM GOC"; stateCode = MachineStateCode::Homing;
    } else if (batchRunning_ && batchPhase_ != BatchPhase::Running) {
      state = "KHOI DONG ME"; stateCode = MachineStateCode::Prestart;
    } else if (batchRunning_) {
      state = in.autoMode ? "DANG AP AUTO" : "DANG AP MANUAL";
      stateCode = in.autoMode ? MachineStateCode::RunningAuto
                              : MachineStateCode::RunningManual;
    } else {
      state = in.autoMode ? "SAN SANG AUTO" : "SAN SANG MANUAL";
      stateCode = in.autoMode ? MachineStateCode::ReadyAuto
                              : MachineStateCode::ReadyManual;
    }
    runtime_.stateCode = stateCode;
    snprintf(runtime_.machineState, sizeof(runtime_.machineState), "%s", state);
    hmiSetRuntime(runtime_);
    if (lastHmiEventSequence_ != eventLog_.sequence() ||
        elapsedMs(now, lastHmiEventPushAt_) >= 30000UL) {
      HmiEventSnapshot snapshot{};
      eventLog_.snapshotLastHour(now, snapshot);
      hmiSetEventLog(snapshot);
      lastHmiEventSequence_ = eventLog_.sequence();
      lastHmiEventPushAt_ = now;
    }
  }

  void updateLed(uint32_t now) {
    LedCode code = LedCode::Boot;
    const InputState &in = inputs_.state();
    if (emergencyActive_) code = LedCode::Emergency;
    else if (storageFaultLatched_ || abnormalResetLatched_) code = LedCode::SystemFault;
    else if (!sensorUsable_ && !timeReached(now, sensorStartupGraceUntil_)) code = LedCode::Boot;
    else if (!sensorUsable_) code = LedCode::SensorFault;
    else if (turnFaultLatched_) code = LedCode::TurnFault;
    else if (highTemperatureActive_ || lowTemperatureActive_) code = LedCode::TempWarning;
    else if (autotune_.running()) code = LedCode::AutoTune;
    else if (batchRunning_) code = in.autoMode ? LedCode::RunningAuto : LedCode::RunningManual;
    else code = in.autoMode ? LedCode::ReadyAuto : LedCode::ReadyManual;
    led_.update(now, code);
  }

  // ----------------------------- Serial --------------------------------------
  static void upperAscii(char *text) {
    while (text && *text) {
      if (*text >= 'a' && *text <= 'z') *text = static_cast<char>(*text - 'a' + 'A');
      ++text;
    }
  }
  static bool parseOnOff(const char *text, bool &value) {
    if (!text) return false;
    if (!strcmp(text, "ON") || !strcmp(text, "1")) { value = true; return true; }
    if (!strcmp(text, "OFF") || !strcmp(text, "0")) { value = false; return true; }
    return false;
  }
  void serviceSerial(uint32_t now) {
#if MAYAP_SERIAL_INPUT_SIM || MAYAP_DIAGNOSTIC_SERIAL
    uint8_t budget = 32;
    while (budget-- && Serial.available() > 0) {
      const char c = static_cast<char>(Serial.read());
      if (c == '\r') continue;
      if (c == '\n') {
        serialLine_[serialLength_] = '\0';
        handleSerialLine(now, serialLine_);
        serialLength_ = 0;
      } else if (serialLength_ + 1U < sizeof(serialLine_)) {
        serialLine_[serialLength_++] = c;
      } else {
        serialLength_ = 0;
        mayapSerialPrintf(false, "[SER] DONG QUA DAI\n");
      }
    }
#else
    (void)now;
#endif
  }





  void handleSerialLine(uint32_t now, char *line) {
    if (!line || !*line) return;
    upperAscii(line);
    char *save = nullptr;
    char *cmd = strtok_r(line, " ", &save);
    char *arg1 = strtok_r(nullptr, " ", &save);
    char *arg2 = strtok_r(nullptr, " ", &save);
    char *arg3 = strtok_r(nullptr, " ", &save);

    if (cmd && !strcmp(cmd, "SERIAL")) {
      const bool enabled = !mayapSerialDebugEnabled();
      mayapSetSerialDebugEnabled(enabled);
      mayapSerialPrintf(true, "[SERIAL] %s\n", enabled ? "ON" : "OFF");
      if (enabled) {
        printSerialHelp();
        printStatus(now);
      }
      return;
    }
#if MAYAP_SERIAL_INPUT_SIM
    if (cmd && !strcmp(cmd, "IN") && arg1 && arg2) {
      bool value = false;
      const bool ok = parseOnOff(arg2, value) && inputs_.setSimulated(arg1, value, now);
      mayapSerialPrintf(false, "[SIM] %s %s = %s\n", arg1, arg2, ok ? "OK" : "FAIL");
      return;
    }
    if (cmd && !strcmp(cmd, "SIM") && arg1 && !strcmp(arg1, "RESET")) {
      inputs_.resetSimulation(now); mayapSerialPrintf(false, "[SIM] RESET\n"); return;
    }
#endif


    if (cmd && !strcmp(cmd, "TIME") && arg1 && !strcmp(arg1, "SET") && arg2 && arg3) {
      unsigned year = 0U, month = 0U, day = 0U, hour = 0U, minute = 0U, second = 0U;
      const bool parsed = sscanf(arg2, "%u-%u-%u", &year, &month, &day) == 3 &&
                          sscanf(arg3, "%u:%u:%u", &hour, &minute, &second) == 3;
      const bool ok = parsed && queueRtcSet(static_cast<uint16_t>(year),
          static_cast<uint8_t>(month), static_cast<uint8_t>(day),
          static_cast<uint8_t>(hour), static_cast<uint8_t>(minute),
          static_cast<uint8_t>(second));
      if (!ok && parsed) storageQueueOverflowLatched_ = true;
      mayapSerialPrintf(false, "[RTC] QUEUE=%s %s %s\n",
                        ok ? "OK" : "FAIL", arg2, arg3);
      return;
    }

    if (cmd && !strcmp(cmd, "BATCH") && arg1) {
      const char *message = nullptr;
      const bool ok = !strcmp(arg1, "START") ? startBatch(now, message)
                    : !strcmp(arg1, "STOP") ? stopBatch(now, message) : false;
      mayapSerialPrintf(false, "[SER] BATCH %s: %s - %s\n", arg1,
                       ok ? "OK" : "FAIL", message ? message : "SAI LENH");
    } else if (cmd && !strcmp(cmd, "ACK")) {
      if (emergencyActive_) {
        sirenMutedUntil_ = now + SIREN_TEMPORARY_MUTE_MS;
        mayapSerialPrintf(false, "[SER] SIREN MUTE 5 MIN\n");
      }
      if (abnormalResetLatched_) {
        abnormalResetLatched_ = false;
        power_.acknowledge();
        faults_.set(FaultCode::AbnormalReset, false, now);
        heatRestartNotBefore_ = now + HEAT_RESTART_LOCKOUT_MS;
        pid_.reset();
      }
      (void)faults_.acknowledge(now, ALARM_KNOWN_MASK);
      mayapSerialPrintf(false, "[SER] ACK DONE\n");
    } else if (cmd && !strcmp(cmd, "FAULT") && arg1 && !strcmp(arg1, "CLEAR")) {
      const bool turnOk = clearTurnFault();
      (void)faults_.acknowledge(now, ALARM_KNOWN_MASK);
      mayapSerialPrintf(false, "[SER] CLEAR TURN=%s\n", turnOk ? "OK" : "BLOCKED");
    } else if (cmd && !strcmp(cmd, "FAULT") && arg1 && !strcmp(arg1, "LIST")) {
      faults_.print();
    } else if (cmd && !strcmp(cmd, "LOG") && arg1 && !strcmp(arg1, "SHOW")) {
      eventLog_.print(arg2 ? static_cast<uint8_t>(constrain(atoi(arg2), 1, 64)) : 20U);
    } else if (cmd && !strcmp(cmd, "LOG") && arg1 && !strcmp(arg1, "CLEAR")) {
      mayapSerialPrintf(false, "[LOG] CLEAR=%s\n",
          eventLog_.clear(now) ? "OK" : "FAIL");
    } else if (cmd && !strcmp(cmd, "DIAG") && arg1) {
      bool value = false;
      if (parseOnOff(arg1, value)) diagnosticFast_ = value;
      mayapSerialPrintf(false, "[DIAG] FAST=%s\n", diagnosticFast_ ? "ON" : "OFF");
    } else if (cmd && !strcmp(cmd, "POWER")) {
      mayapSerialPrintf(false, "[POWER] reset=%s storm=%u ack=%u\n",
          resetReasonText(power_.reason()), power_.resetStormCount(), power_.ackRequired());
    } else if (cmd && !strcmp(cmd, "STATUS")) {
      printStatus(now);
    } else if (cmd && !strcmp(cmd, "HELP")) {
      printSerialHelp();
    } else {
      mayapSerialPrintf(false, "[SER] LENH SAI. GO HELP\n");
    }
  }

  void printSerialHelp() {
    mayapSerialPrintf(false, "\n--- MAYAP OFFLINE INDUSTRIAL v%s SERIAL ---\n", MAYAP_FIRMWARE_VERSION);
#if MAYAP_SERIAL_INPUT_SIM
    mayapSerialPrintf(false, "IN AUTO|HEATER|FAN|LIGHT|LEFT|RIGHT|LIMIT_LEFT|LIMIT_RIGHT ON|OFF\n");
    mayapSerialPrintf(false, "SIM RESET\n");
#endif
    mayapSerialPrintf(false, "TIME SET YYYY-MM-DD HH:MM:SS\n");
    mayapSerialPrintf(false, "BATCH START|STOP   ACK   FAULT LIST|CLEAR   STATUS   POWER\n");
    mayapSerialPrintf(false, "Nhap SERIAL de tat/bat toan bo debug. DIAG ON|OFF doi toc do STATUS.\n");
    mayapSerialPrintf(false, "LOG SHOW [N]|CLEAR   DIAG ON|OFF\n");
  }
  const char *heatBlockReason(uint32_t now) const {
    const InputState &in = inputs_.state();
    if (storageFaultLatched_) return "EEPROM_FAULT";
    if (storageDegraded_ && !batchRunning_) return "EEPROM_DEGRADED";
    if (abnormalResetLatched_) return "RESET_NOT_ACK";
    if (faults_.heatInhibited()) return "ACTIVE_FAULT";
    if (!in.heaterEnable) return "HEATER_SWITCH_OFF";
    if (!(config_.allowHeatWithoutBatch || batchRunning_)) return "BATCH_NOT_RUNNING";
    if (!sensorUsable_) return "SENSOR_NOT_READY";
    if (highTemperatureActive_) return "TEMP_HIGH";
    if (emergencyActive_) return "TEMP_EMERGENCY";
    if (!timeReached(now, heatRestartNotBefore_)) return "RESTART_DELAY";
    const bool baseFan = in.autoMode
        ? config_.circulationFanEnabled && (batchRunning_ || in.heaterEnable || autotune_.running())
        : in.circulationFan;
    const bool fanCommand = baseFan || ventTemperatureActive_ || highTemperatureActive_ ||
                            emergencyActive_ || !timeReached(now, postCoolUntil_);
    const bool fanStable = fanCommand && fanOnSince_ != 0U &&
                           elapsedMs(now, fanOnSince_) >= FAN_PRESTART_MS;
    if (MANUAL_FAN_CAN_DISABLE_HEATING && !fanStable) return "FAN_PRESTART";
    if (ventTemperatureActive_) return "VENT_LEVEL_1";
    return "READY";
  }

  uint32_t heatWaitRemainingMs(uint32_t now) const {
    if (!timeReached(now, heatRestartNotBefore_)) return heatRestartNotBefore_ - now;
    if (fanOnSince_ != 0U && elapsedMs(now, fanOnSince_) < FAN_PRESTART_MS) {
      return FAN_PRESTART_MS - elapsedMs(now, fanOnSince_);
    }
    return 0U;
  }

  void printStatus(uint32_t now) {
    const InputState &in = inputs_.state();
    mayapSerialPrintf(false, "[STATUS] switch=%s batch=%u resume=%u phase=%u sensor=%u storage=%u resetFault=%u T=%.2f raw=%.2f H=%.1f\n",
      in.autoMode ? "AUTO" : "MAN",
      batchRunning_, resumePending_, static_cast<unsigned>(batchPhase_),
      sensorUsable_, (storageFaultLatched_ || storageDegraded_), abnormalResetLatched_,
      temperature_, rawTemperature_, humidity_);
    mayapSerialPrintf(false, "[STATUS] IN heat=%u fan=%u light=%u L=%u R=%u limL=%u limR=%u\n",
      in.heaterEnable, in.circulationFan, in.light, in.turnLeft, in.turnRight,
      in.limitLeft, in.limitRight);
    mayapSerialPrintf(false, "[RTC] addr=0x%02X online=%u valid=%u osf=%u epoch=%lu date=%s\n",
      RTC_I2C_ADDRESS, rtcView_.online, rtcView_.valid,
      rtcView_.oscillatorStopped,
      static_cast<unsigned long>(rtcView_.epoch), rtcView_.dateText);
    mayapSerialPrintf(false, "[EEPROM] type=AT24C32 addr=0x%02X capacity=%u page=%u ready=%u\n",
      EEPROM_I2C_ADDRESS,
      static_cast<unsigned>(EEPROM_CAPACITY_BYTES),
      static_cast<unsigned>(EEPROM_PAGE_SIZE),
      !(storageFaultLatched_ || storageDegraded_));
    mayapSerialPrintf(false, "[HEAT] block=%s wait=%lums GPIO7=%d GPIO14=%d GPIO1=%d\n",
      heatBlockReason(now), static_cast<unsigned long>(heatWaitRemainingMs(now)),
      digitalRead(PIN_IN_HEATER_ENABLE), digitalRead(PIN_OUT_HEAT_MASTER),
      digitalRead(PIN_OUT_HEATER_SSR));
    mayapSerialPrintf(false, "[STATUS] OUT ssr=%u master=%u fan=%u vent=%u light=%u left=%u right=%u siren=%u spareP=%u spareR=%u PID=%.1f alarm=0x%08lX\n",
      outputs_.state().heaterSsr, outputs_.state().heatMaster,
      outputs_.state().circulationFan, outputs_.state().ventFan,
      outputs_.state().light, outputs_.state().turnLeft,
      outputs_.state().turnRight, outputs_.state().siren,
      outputs_.state().pulseSpare, outputs_.state().relaySpare,
      runtime_.heaterPower, static_cast<unsigned long>(runtime_.alarmMask));
    mayapSerialPrintf(false, "[KERNEL] fault=%u count=%u events=%lu relay/h=%u inDrop=%lu outDrop=%lu\n",
      static_cast<unsigned>(faults_.primary()), faults_.activeCount(),
      static_cast<unsigned long>(eventLog_.sequence()), outputs_.transitionsThisHour(),
      static_cast<unsigned long>(inputs_.droppedEvents()),
      static_cast<unsigned long>(outputs_.droppedEvents()));
    const KernelHealthSnapshot kh = mayapKernelHealthSnapshot();
    mayapSerialPrintf(false,
      "[KERNEL] cycle=%luus max=%luus miss=%lu stack=%lu/%lu/%lu heap=%u i2cErr=%u recover=%u trip=%u\n",
      static_cast<unsigned long>(kh.lastControlCycleUs),
      static_cast<unsigned long>(kh.maxControlCycleUs),
      static_cast<unsigned long>(kh.deadlineMissCount),
      static_cast<unsigned long>(kh.controlStackFree),
      static_cast<unsigned long>(kh.hmiStackFree),
      static_cast<unsigned long>(kh.supervisorStackFree),
      static_cast<unsigned>(kh.freeInternalHeap),
      static_cast<unsigned>(kh.i2cConsecutiveErrors),
      static_cast<unsigned>(kh.i2cRecoveryCount),
      static_cast<unsigned>(kh.hardTripReason));
    mayapSerialPrintf(false, "[SHT] good=%lu timeout=%lu crc=%lu fmt=%lu range=%lu tx=%lu ignored=%lu age=%lums heap=%u\n",
      static_cast<unsigned long>(sensor_.goodFrames()),
      static_cast<unsigned long>(sensor_.timeoutErrors()),
      static_cast<unsigned long>(sensor_.crcErrors()),
      static_cast<unsigned long>(sensor_.formatErrors()),
      static_cast<unsigned long>(sensor_.rangeErrors()),
      static_cast<unsigned long>(sensor_.txErrors()),
      static_cast<unsigned long>(sensor_.ignoredBytes()),
      static_cast<unsigned long>(sensor_.dataAgeMs()),
      ESP.getFreeHeap());
    (void)now;
  }
  // ----------------------------- Members -------------------------------------
  EventLog eventLog_{};
  FaultManager faults_{&eventLog_};
  PowerManager power_{};
  RtcDs3231 rtc_{};
  PersistentStore store_{};
  portMUX_TYPE i2cQueueMux_ = portMUX_INITIALIZER_UNLOCKED;
  RtcSnapshot rtcShared_{};
  RtcSnapshot rtcView_{};
  I2cJob i2cJobs_[I2C_JOB_CAPACITY]{};
  I2cResult i2cResults_[I2C_RESULT_CAPACITY]{};
  uint8_t i2cJobHead_ = 0U;
  uint8_t i2cJobTail_ = 0U;
  uint8_t i2cJobCount_ = 0U;
  uint8_t i2cResultHead_ = 0U;
  uint8_t i2cResultTail_ = 0U;
  uint8_t i2cResultCount_ = 0U;
  bool i2cBusReprobePending_ = false;
  bool i2cBusReprobeOk_ = false;

  // Mailbox web co dinh. MQTT/loopTask chi copy vao/ra cac vung nay.
  portMUX_TYPE webMux_ = portMUX_INITIALIZER_UNLOCKED;
  WebConfigRequest webConfigInbox_{};
  bool webConfigPending_ = false;

  WebCommandRequest webCommandQueue_[WEB_COMMAND_QUEUE_CAPACITY]{};
  uint8_t webCommandHead_ = 0U;
  uint8_t webCommandTail_ = 0U;
  uint8_t webCommandCount_ = 0U;

  WebConfigResult webConfigResults_[WEB_RESULT_QUEUE_CAPACITY]{};
  uint8_t webConfigResultHead_ = 0U;
  uint8_t webConfigResultTail_ = 0U;
  uint8_t webConfigResultCount_ = 0U;

  WebCommandResult webCommandResults_[WEB_RESULT_QUEUE_CAPACITY]{};
  uint8_t webCommandResultHead_ = 0U;
  uint8_t webCommandResultTail_ = 0U;
  uint8_t webCommandResultCount_ = 0U;

  WebEventRecord webEvents_[WEB_EVENT_QUEUE_CAPACITY]{};
  uint8_t webEventHead_ = 0U;
  uint8_t webEventTail_ = 0U;
  uint8_t webEventCount_ = 0U;
  uint32_t lastWebMirroredEventSequence_ = 0U;

  WebMachineSnapshot webSnapshot_{};
  bool webSnapshotReady_ = false;
  bool webConfigReportDirty_ = true;
  bool webStateDirty_ = true;
  uint32_t webResultDropCount_ = 0U;
  uint32_t webEventDropCount_ = 0U;

  InputManager inputs_{};
  SHT485Industrial sensor_{};
  ThermalController pid_{};
  RelayAutoTune autotune_{};
  OutputArbiter outputs_{};
  StatusLed led_{};

  MachineConfig config_{};
  MachineRuntime runtime_{};
  bool configLoaded_ = false;

  uint32_t bootAt_ = 0;
  esp_reset_reason_t resetReason_ = ESP_RST_UNKNOWN;
  bool abnormalResetLatched_ = false;
  bool sensorUsable_ = false;
  bool safetySampleValid_ = false;
  uint32_t sensorStartupGraceUntil_ = 0U;
  uint8_t goodSensorStreak_ = 0;
  bool newSensorSample_ = false;
  // --- Bao ve thanh nhiet + cam bien treo (401/402/104) ---
  uint32_t runawayRefAt_ = 0U;
  float runawayRefTemp_ = NAN;
  bool heaterRunawayLatched_ = false;
  int16_t heaterRunawayRiseX10_ = 0;
  uint32_t stallRefAt_ = 0U;
  float stallRefTemp_ = NAN;
  bool heaterStallLatched_ = false;
  int16_t heaterStallMinutes_ = 0;
  uint32_t sensorStuckSince_ = 0U;
  int16_t sensorStuckTempRef_ = 0;
  int16_t sensorStuckHumRef_ = 0;
  uint32_t sensorStuckFrameRef_ = 0U;
  NetState lastNetState_ = NetState::Disabled;
  PortalState lastPortalState_ = PortalState::Idle;
  bool sensorStuckHasRef_ = false;
  float temperature_ = NAN;
  float rawTemperature_ = NAN;
  float humidity_ = NAN;
  float lastAcceptedTemperature_ = NAN;
  float lastSuspectCandidate_ = NAN;
  bool sensorSuspect_ = false;
  uint8_t sensorPlausibilityGoodStreak_ = 0U;

  bool batchRunning_ = false;
  bool resumePending_ = false;
  bool resumeConfirmationRequired_ = false;
  bool automaticResetRecovery_ = false;
  BatchPhase batchPhase_ = BatchPhase::Stopped;
  uint32_t batchStartedAt_ = 0;
  uint32_t phaseStartedAt_ = 0;
  uint32_t elapsedBeforeStartSec_ = 0;
  uint32_t savedElapsedAtCheckpoint_ = 0U;
  uint32_t lastCheckpointEpoch_ = 0U;
  bool resumeClockAdjusted_ = true;
  uint32_t lastCheckpointAt_ = 0;
  uint32_t lastTurnCounterDay_ = 0;
  uint16_t turnCountToday_ = 0;
  uint32_t turnCountBatch_ = 0;

  TrayPosition trayPosition_ = TrayPosition::Unknown;
  TrayPosition moveOrigin_ = TrayPosition::Unknown;
  TurnPhase turnPhase_ = TurnPhase::Idle;
  TurnDirection moveDirection_ = TurnDirection::Left;
  bool moveIsHoming_ = false;
  bool moveCounts_ = false;
  bool needHome_ = false;
  bool turnFaultLatched_ = false;
  FaultCode turnFaultCode_ = FaultCode::None;
  uint32_t moveStartedAt_ = 0;
  uint32_t deadtimeUntil_ = 0;
  uint32_t nextTurnAt_ = 0;
  uint32_t manualConflictSince_ = 0;
  uint32_t hmiManualTurnUntil_ = 0;
  TurnDirection hmiManualDirection_ = TurnDirection::Left;
  bool previousAutoMode_ = false;
  bool manualTurnRearmRequired_ = true;

  ConditionTimer lowTempTimer_{};
  ConditionTimer highTripTimer_{};
  ConditionTimer highClearTimer_{};
  ConditionTimer emergencyClearTimer_{};
  ConditionTimer humidityTimer_{};
  bool ventTemperatureActive_ = false;
  bool lowTemperatureActive_ = false;
  bool highTemperatureActive_ = false;
  bool emergencyActive_ = false;
  bool humidityLowActive_ = false;
  uint32_t sirenMutedUntil_ = 0;
  uint32_t heatRestartNotBefore_ = 0;
  uint32_t postCoolUntil_ = 0;
  bool storageFaultLatched_ = false;
  bool storageDegraded_ = false;
  bool storageQueueOverflowLatched_ = false;

  float pidPower_ = 0.0f;
  uint32_t ssrWindowStartedAt_ = 0;
  bool previousFanCommand_ = false;
  uint32_t fanOnSince_ = 0;

  PeriodicGate runtimeGate_{RUNTIME_TO_HMI_MS};
  PeriodicGate checkpointGate_{BATCH_CHECKPOINT_MS};
  PeriodicGate diagnosticGate_{DIAGNOSTIC_STATUS_MS};
  uint32_t lastRuntimePushAt_ = 0;
  uint32_t lastHmiEventSequence_ = UINT32_MAX;
  uint32_t lastHmiEventPushAt_ = 0U;
  bool diagnosticFast_ = false;
  char serialLine_[128]{};
  uint8_t serialLength_ = 0;
};

// Neu them ma loi moi vao bang cua config.h ma quen noi rong FaultManager thi
// hai ma cuoi bang se dung chung mot slot va che lan nhau. Chan ngay tai bien
// dich thay vi de phat hien ngoai hien truong.
static_assert(FAULT_CODE_COUNT <= FaultManager::faultCapacity(),
              "Bang loi vuot so slot cua FaultManager - hay tang MAX_FAULTS");

// Mot doi tuong duy nhat, khong new/delete trong van hanh.
static MachineController Machine;

} // namespace Mayap
