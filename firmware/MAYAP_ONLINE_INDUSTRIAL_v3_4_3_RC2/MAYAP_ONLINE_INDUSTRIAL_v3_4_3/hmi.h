#pragma once

/*
  MAYAP HMI ST7567S 128x64 + rotary + buzzer - v3.4.0
  Phan cung: LCD 0x3F SDA8/SCL9, rotary 38/39/40, buzzer GPIO41.
  File nay chi dung trong firmware tong; khong chua setup/loop demo, Wi-Fi,
  ket noi mang, luu flash noi hay dieu khien GPIO chap hanh.
*/

#include "config.h"
#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <math.h>
#include <stddef.h>
#include <string.h>
#include <type_traits>
#include <ctype.h>
#include <stdlib.h>
#if MAYAP_HMI_ENCODER_INTERRUPT
#include <driver/gpio.h>
#endif

#if LCD_PROFILE == 1
U8G2_ST7567_ENH_DG128064I_F_HW_I2C lcd(U8G2_R0, U8X8_PIN_NONE);
#elif LCD_PROFILE == 2
U8G2_ST7567_JLX12864_F_HW_I2C lcd(U8G2_R0, U8X8_PIN_NONE);
#else
#error "LCD_PROFILE khong hop le"
#endif

// Coi active chi dieu khien ON/OFF qua transistor/MOSFET.
// "Nhe/to" duoc tao bang do dai va nhip keu, khong PWM relay.
struct BuzzerPattern {
  uint16_t onMs;
  uint16_t offMs;
  uint8_t pulses;
};

struct BuzzerState {
  uint32_t acknowledgedAlarmMask = 0;
  uint32_t acknowledgedAt[8] = {};
  uint32_t soundingAlarmBit = AlarmNone;
  uint32_t deadline = 0;
  bool outputOn = false;
  bool transientActive = false;
  bool transientOn = false;
  bool transientMandatory = false;
  bool resumePromptActive = false;
  bool turnContinuousActive = false;
  uint8_t transientPulsesLeft = 0;
  BuzzerPattern transientPattern{0, 0, 0};
};

BuzzerState buzzer;

void buzzerBegin();
void buzzerUpdate(uint32_t now);
void buzzerPlayCue(BuzzerCue cue);
void buzzerAcknowledge(uint32_t alarmMask);
BuzzerPattern buzzerCuePattern(BuzzerCue cue);
BuzzerPattern alarmPattern(uint32_t bit);

static_assert(std::is_standard_layout<MachineConfig>::value, "MachineConfig phai la standard-layout");
static_assert(std::is_trivially_copyable<MachineConfig>::value,
              "MachineConfig phai copy duoc trong mailbox");
static_assert(std::is_trivially_copyable<MachineRuntime>::value,
              "MachineRuntime phai copy duoc trong mailbox");
static_assert(std::is_trivially_copyable<HmiEventSnapshot>::value,
              "HmiEventSnapshot phai copy duoc trong mailbox");

// CRC32 duoc dung cho ban ghi cau hinh cua che do demo.
uint32_t crc32(const uint8_t *data, size_t length) {
  uint32_t crc = 0xFFFFFFFFUL;
  while (length--) {
    crc ^= *data++;
    for (uint8_t i = 0; i < 8; ++i)
      crc = (crc >> 1) ^ (0xEDB88320UL & (0UL - (crc & 1UL)));
  }
  return ~crc;
}


enum class ButtonEvent : uint8_t { None, ShortPress, LongPress };
struct RotaryState {
  uint8_t previousAB = 0;
  int16_t accumulator = 0;
  int8_t step = 0;
  bool lastRawButton = HIGH;
  bool stableButton = HIGH;
  uint32_t rawChangedAt = 0;
  uint32_t pressedAt = 0;
  bool longPressReported = false;
  ButtonEvent button = ButtonEvent::None;
};
RotaryState rotary;

// Dat bang giai ma trong DRAM de ISR rotary khong phu thuoc cache flash.
DRAM_ATTR static const int8_t QUADRATURE_TABLE[16] = {
   0, -1,  1,  0,  1,  0,  0, -1,
  -1,  0,  0,  1,  0,  1, -1,  0
};

uint8_t readAB() {
#if MAYAP_HMI_ENCODER_INTERRUPT
  return static_cast<uint8_t>(
      (gpio_get_level(static_cast<gpio_num_t>(PIN_ENCODER_CLK)) << 1) |
       gpio_get_level(static_cast<gpio_num_t>(PIN_ENCODER_DT)));
#else
  return static_cast<uint8_t>(
      (digitalRead(PIN_ENCODER_CLK) << 1) | digitalRead(PIN_ENCODER_DT));
#endif
}

#if MAYAP_HMI_ENCODER_INTERRUPT
portMUX_TYPE rotaryIsrMux = portMUX_INITIALIZER_UNLOCKED;
volatile int16_t rotaryPendingTransitions = 0;
volatile uint8_t rotaryIsrPreviousAB = 0;

void IRAM_ATTR rotaryEncoderIsr() {
  const uint8_t ab = static_cast<uint8_t>(
      (gpio_get_level(static_cast<gpio_num_t>(PIN_ENCODER_CLK)) << 1) |
       gpio_get_level(static_cast<gpio_num_t>(PIN_ENCODER_DT)));

  portENTER_CRITICAL_ISR(&rotaryIsrMux);
  const uint8_t previous = rotaryIsrPreviousAB;
  rotaryIsrPreviousAB = ab;
  const int8_t delta = QUADRATURE_TABLE[(previous << 2) | ab];
  if (delta) {
    int16_t pending = static_cast<int16_t>(rotaryPendingTransitions + delta);
    // Gioi han backlog khi day encoder bi nhieu lien tuc.
    if (pending > 64) pending = 64;
    if (pending < -64) pending = -64;
    rotaryPendingTransitions = pending;
  }
  portEXIT_CRITICAL_ISR(&rotaryIsrMux);
}
#endif

void beginRotary() {
  pinMode(PIN_ENCODER_CLK, INPUT_PULLUP);
  pinMode(PIN_ENCODER_DT, INPUT_PULLUP);
  pinMode(PIN_ENCODER_SW, INPUT_PULLUP);
  rotary.previousAB = readAB();
#if MAYAP_HMI_ENCODER_INTERRUPT
  rotaryIsrPreviousAB = rotary.previousAB;
  attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_CLK), rotaryEncoderIsr, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_DT), rotaryEncoderIsr, CHANGE);
#endif
  rotary.lastRawButton = digitalRead(PIN_ENCODER_SW);
  rotary.stableButton = rotary.lastRawButton;
}

void updateRotary(uint32_t now) {
  rotary.step = 0;
  rotary.button = ButtonEvent::None;

  int16_t transitionDelta = 0;
#if MAYAP_HMI_ENCODER_INTERRUPT
  portENTER_CRITICAL(&rotaryIsrMux);
  transitionDelta = rotaryPendingTransitions;
  rotaryPendingTransitions = 0;
  portEXIT_CRITICAL(&rotaryIsrMux);
#else
  const uint8_t ab = readAB();
  if (ab != rotary.previousAB) {
    transitionDelta = QUADRATURE_TABLE[(rotary.previousAB << 2) | ab];
    rotary.previousAB = ab;
  }
#endif

  rotary.accumulator = static_cast<int16_t>(rotary.accumulator + transitionDelta);
  int16_t detents = rotary.accumulator / ENCODER_STEPS_PER_DETENT;
  if (detents) {
    int16_t emitted = detents;
    if (emitted > ENCODER_MAX_STEPS_PER_UPDATE) {
      emitted = ENCODER_MAX_STEPS_PER_UPDATE;
    } else if (emitted < -static_cast<int16_t>(ENCODER_MAX_STEPS_PER_UPDATE)) {
      emitted = -static_cast<int16_t>(ENCODER_MAX_STEPS_PER_UPDATE);
    }
    rotary.accumulator = static_cast<int16_t>(
        rotary.accumulator - emitted * ENCODER_STEPS_PER_DETENT);
    rotary.step = static_cast<int8_t>(REVERSE_ENCODER ? -emitted : emitted);
  }

  const bool raw = digitalRead(PIN_ENCODER_SW);
  if (raw != rotary.lastRawButton) {
    rotary.lastRawButton = raw;
    rotary.rawChangedAt = now;
  }
  if (raw != rotary.stableButton && now - rotary.rawChangedAt >= BUTTON_DEBOUNCE_MS) {
    rotary.stableButton = raw;
    if (raw == LOW) {
      rotary.pressedAt = now;
      rotary.longPressReported = false;
    } else if (!rotary.longPressReported) {
      rotary.button = ButtonEvent::ShortPress;
    }
  }

  // Phat LongPress ngay khi du thoi gian, khong doi nguoi dung nha nut.
  if (rotary.stableButton == LOW && !rotary.longPressReported &&
      now - rotary.pressedAt >= BUTTON_LONG_PRESS_MS) {
    rotary.longPressReported = true;
    rotary.button = ButtonEvent::LongPress;
  }
}

// ============================================================
// 4. BANG CAI DAT - THEM THONG SO KHONG CAN THEM MAN HINH MOI
// ============================================================
// Enum8: byte enum (vi du TurnDirection). Tach rieng khoi Bool de khong
// phai reinterpret_cast<bool*> len mot byte enum.
enum class SettingType : uint8_t { Bool, U8, U16, Float, Enum8 };

struct SettingItem {
  const char *label;
  SettingType type;
  uint16_t offset;
  float minimum;
  float maximum;
  float step;
  uint8_t decimals;
  const char *unit;
  const char *const *options;
  uint8_t optionCount;
};

// Khai bao prototype thu cong de tranh loi Arduino .ino auto-prototype
// voi kieu SettingItem duoc khai bao sau cac ham dau tien trong sketch.
float readSetting(const MachineConfig &cfg, const SettingItem &item);
void writeSetting(MachineConfig &cfg, const SettingItem &item, float value);
void normalizeConfig(MachineConfig &cfg);
void sanitizeConfig(MachineConfig &cfg);
void settingLimits(const MachineConfig &cfg, const SettingItem &item, float &minimum, float &maximum);
void formatSettingValue(const SettingItem &item, float value, char *out, size_t size);

const char *const OPT_OFF_ON[] = {"TAT", "BAT"};
const char *const OPT_DIRECTION[] = {"TRAI", "PHAI"};
const char *const OPT_NET_MODE[] = {"OFFLINE", "ONLINE"};

#define ITEM_FLOAT(lbl, member, mn, mx, st, dec, unitText) \
  {lbl, SettingType::Float, offsetof(MachineConfig, member), mn, mx, st, dec, unitText, nullptr, 0}
#define ITEM_U8(lbl, member, mn, mx, st, unitText) \
  {lbl, SettingType::U8, offsetof(MachineConfig, member), mn, mx, st, 0, unitText, nullptr, 0}
#define ITEM_U16(lbl, member, mn, mx, st, unitText) \
  {lbl, SettingType::U16, offsetof(MachineConfig, member), mn, mx, st, 0, unitText, nullptr, 0}
#define ITEM_BOOL_OPTIONS(lbl, member, opts) \
  {lbl, SettingType::Bool, offsetof(MachineConfig, member), 0, 1, 1, 0, "", opts, 2}
#define ITEM_BOOL(lbl, member) ITEM_BOOL_OPTIONS(lbl, member, OPT_OFF_ON)
#define ITEM_ENUM8(lbl, member, opts, count) \
  {lbl, SettingType::Enum8, offsetof(MachineConfig, member), 0, (count) - 1, 1, 0, "", opts, count}

// ---------------------------------------------------------------------------
// BANG THONG SO
// Gioi han lay TRUC TIEP tu cac hang CFG_* trong config.h - cung bang so ma
// mayapSanitizeConfig() dung. Khong con kha nang HMI cho phep mot gia tri roi
// firmware lang le clamp sang gia tri khac.
// ---------------------------------------------------------------------------
const SettingItem SETTINGS[] = {
  // --- Nhom 0: CAI DAT ME (thong so truc tiep cua me dang ap) ---
  ITEM_FLOAT("Nhiet do ap", targetTemp, TARGET_TEMP_MIN_C,
             TARGET_TEMP_MAX_C, 0.1f, 1, "C"),                                  // 0
  ITEM_FLOAT("Bao het nuoc", lowHumidityAlarm,
             CFG_HUM_ALARM_MIN, CFG_HUM_ALARM_MAX, 1.0f, 0, "%"),               // 1
  ITEM_U8("So ngay ap", totalIncubationDays,
          CFG_DAYS_MIN, CFG_DAYS_MAX, 1, "ng"),                                 // 2

  // --- Nhom 1: NHIET & BAO VE ---
  ITEM_FLOAT("Bao thap", lowTempAlarm, CFG_LOW_ALARM_MIN_C,
             TARGET_TEMP_MAX_C - LOW_ALARM_GAP_C, 0.1f, 1, "C"),                // 3
  ITEM_FLOAT("Bao cao", highTempAlarm,
             TARGET_TEMP_MIN_C + HIGH_ALARM_GAP_C,
             HIGH_ALARM_MAX_C, 0.1f, 1, "C"),                                   // 4
  ITEM_FLOAT("Ngat khan cap", emergencyTemp,
             TARGET_TEMP_MIN_C + HIGH_ALARM_GAP_C + EMERGENCY_ABOVE_HIGH_C,
             EMERGENCY_MAX_C, 0.1f, 1, "C"),                                    // 5
  ITEM_FLOAT("Bat thong gio", ventOnTemp, TARGET_TEMP_MIN_C,
             HIGH_ALARM_MAX_C, 0.1f, 1, "C"),                                   // 6
  ITEM_FLOAT("Tat thong gio", ventOffTemp, TARGET_TEMP_MIN_C,
             HIGH_ALARM_MAX_C, 0.1f, 1, "C"),                                   // 7
  ITEM_FLOAT("Vung chet PID", tempHysteresis,
             CFG_HYSTERESIS_MIN, CFG_HYSTERESIS_MAX, 0.1f, 1, "C"),             // 8
  ITEM_U8("Cong suat toi da", maxHeaterPower,
          CFG_MAX_POWER_MIN, CFG_MAX_POWER_MAX, 5, "%"),                        // 9

  // --- Nhom 2: DAO TRUNG (toan bo cai dat dao) ---
  ITEM_BOOL("Tu dong dao", turningEnabled),                                     // 10
  ITEM_U16("Chu ky dao", turnIntervalMin,
           CFG_TURN_INTERVAL_MIN_M, CFG_TURN_INTERVAL_MAX_M, 1, "ph"),          // 11
  ITEM_U16("Tre loi dao", turnMaxRunSec,
           CFG_TURN_RUN_MIN_S, CFG_TURN_RUN_MAX_S, 5, "s"),                     // 12
  ITEM_ENUM8("Huong dao ke", nextDirection, OPT_DIRECTION, 2),                  // 13

  // --- Nhom 3: QUAT & GIA NHIET ---
  ITEM_BOOL("Quat tuan hoan", circulationFanEnabled),                           // 14
  ITEM_BOOL("Nhiet ngoai me", allowHeatWithoutBatch),                           // 15

  // --- Nhom 4: CAM BIEN ---
  ITEM_FLOAT("Bu nhiet do", tempOffset,
             CFG_TEMP_OFFSET_MIN, CFG_TEMP_OFFSET_MAX, 0.1f, 1, "C"),           // 16
  ITEM_FLOAT("Bu do am", humidityOffset,
             CFG_HUM_OFFSET_MIN, CFG_HUM_OFFSET_MAX, 1.0f, 0, "%"),             // 17
  ITEM_U16("Timeout cam bien", sensorTimeoutSec,
           CFG_SENSOR_TIMEOUT_MIN_S, CFG_SENSOR_TIMEOUT_MAX_S, 1, "s"),         // 18
  ITEM_U16("Tre bao do am", humidityAlarmDelaySec,
           CFG_HUM_DELAY_MIN_S, CFG_HUM_DELAY_MAX_S, 10, "s"),                  // 19

  // --- Nhom 5: NGUON & PHUC HOI ---
  ITEM_BOOL("Tu tiep tuc me", autoResumeAfterPower),                            // 20
  ITEM_U16("Tre cap nhiet", powerRestoreDelaySec,
           CFG_POWER_DELAY_MIN_S, CFG_POWER_DELAY_MAX_S, 5, "s"),               // 21

  // --- Nhom 6: KET NOI ---
  ITEM_BOOL_OPTIONS("Che do ket noi", cloudEnabled, OPT_NET_MODE)               // 22
};

constexpr uint8_t SETTING_COUNT = sizeof(SETTINGS) / sizeof(SETTINGS[0]);
static_assert(SETTING_COUNT == 23, "Bang SETTINGS phai co 23 thong so");

const uint8_t GROUP_SETTING_INDEXES[] = {
  0,1,2,                    // CAI DAT ME
  3,4,5,6,7,8,9,            // NHIET & BAO VE
  10,11,12,13,              // DAO TRUNG
  14,15,                    // QUAT & GIA NHIET
  16,17,18,19,              // CAM BIEN
  20,21,                    // NGUON & PHUC HOI
  22                        // KET NOI
};

// Dong phu khong phai thong so (thong ke, hanh dong) cua tung nhom.
enum class GroupExtra : uint8_t { None = 0, TurnStats, WifiPortal };

struct SettingGroup {
  const char *label;
  uint8_t first;
  uint8_t count;
  GroupExtra extra;
};

const SettingGroup GROUPS[] = {
  {"CAI DAT ME",      0,  3, GroupExtra::None},        // 0
  {"NHIET & BAO VE",  3,  7, GroupExtra::None},        // 1
  {"DAO TRUNG",      10,  4, GroupExtra::TurnStats},   // 2
  {"QUAT & NHIET",   14,  2, GroupExtra::None},        // 3
  {"CAM BIEN",       16,  4, GroupExtra::None},        // 4
  {"NGUON/PHUC HOI", 20,  2, GroupExtra::None},        // 5
  {"KET NOI",        22,  1, GroupExtra::WifiPortal}   // 6
};
constexpr uint8_t GROUP_COUNT = sizeof(GROUPS) / sizeof(GROUPS[0]);
static_assert(GROUP_COUNT == 7, "Bang GROUPS phai co 7 nhom");
static_assert(sizeof(GROUP_SETTING_INDEXES) / sizeof(GROUP_SETTING_INDEXES[0]) == SETTING_COUNT,
              "Sai so luong tham chieu setting trong GROUP_SETTING_INDEXES");

// Nhom 0 la CAI DAT ME (mo thang tu MENU CHINH).
// Nhom 1..6 nam trong CAI DAT CHUNG.
constexpr uint8_t GROUP_BATCH = 0;
constexpr uint8_t GENERAL_GROUP_FIRST = 1;
constexpr uint8_t GENERAL_GROUP_COUNT = GROUP_COUNT - GENERAL_GROUP_FIRST;
constexpr uint8_t GROUP_NETWORK = 6;

float readSetting(const MachineConfig &cfg, const SettingItem &item) {
  const uint8_t *base = reinterpret_cast<const uint8_t *>(&cfg) + item.offset;
  switch (item.type) {
    case SettingType::Bool: return *reinterpret_cast<const bool *>(base) ? 1.0f : 0.0f;
    case SettingType::Enum8: return *reinterpret_cast<const uint8_t *>(base);
    case SettingType::U8: return *reinterpret_cast<const uint8_t *>(base);
    case SettingType::U16: return *reinterpret_cast<const uint16_t *>(base);
    case SettingType::Float: return *reinterpret_cast<const float *>(base);
  }
  return 0;
}

void writeSetting(MachineConfig &cfg, const SettingItem &item, float value) {
  value = constrain(value, item.minimum, item.maximum);
  uint8_t *base = reinterpret_cast<uint8_t *>(&cfg) + item.offset;
  switch (item.type) {
    case SettingType::Bool: *reinterpret_cast<bool *>(base) = value >= 0.5f; break;
    case SettingType::Enum8:
      *reinterpret_cast<uint8_t *>(base) = static_cast<uint8_t>(lroundf(value));
      break;
    case SettingType::U8: *reinterpret_cast<uint8_t *>(base) = static_cast<uint8_t>(lroundf(value)); break;
    case SettingType::U16: *reinterpret_cast<uint16_t *>(base) = static_cast<uint16_t>(lroundf(value)); break;
    case SettingType::Float: *reinterpret_cast<float *>(base) = value; break;
  }
}

// Ca hai ham duoi day tu day chi la vo boc mong cua mayapSanitizeConfig()
// trong config.h. Truoc v3.4.3, HMI co bang gioi han rieng (vi du
// sensorTimeoutSec 2..120 s) khac firmware (5..30 s) nen mot gia tri hop le
// tren HMI lai bi firmware clamp, gay lech hien thi cho toi khi nhan echo.
void normalizeConfig(MachineConfig &cfg) { mayapSanitizeConfig(cfg); }
void sanitizeConfig(MachineConfig &cfg) { mayapSanitizeConfig(cfg); }

// Gioi han dong cho tung o dang chinh. Dung dung cac hang cua config.h nen
// khong the lech voi mayapSanitizeConfig().
void settingLimits(const MachineConfig &cfg, const SettingItem &item,
                   float &minimum, float &maximum) {
  minimum = item.minimum; maximum = item.maximum;
  const uint16_t offset = item.offset;
  if (offset == offsetof(MachineConfig, lowTempAlarm)) {
    maximum = fminf(maximum, cfg.targetTemp - LOW_ALARM_GAP_C);
  } else if (offset == offsetof(MachineConfig, highTempAlarm)) {
    minimum = fmaxf(minimum, cfg.targetTemp + HIGH_ALARM_GAP_C);
    maximum = fminf(maximum, cfg.emergencyTemp - EMERGENCY_ABOVE_HIGH_C);
  } else if (offset == offsetof(MachineConfig, emergencyTemp)) {
    minimum = fmaxf(minimum, cfg.highTempAlarm + EMERGENCY_ABOVE_HIGH_C);
  } else if (offset == offsetof(MachineConfig, ventOnTemp)) {
    minimum = fmaxf(minimum, cfg.targetTemp + VENT_ON_ABOVE_SV_C);
    minimum = fmaxf(minimum, cfg.ventOffTemp + VENT_HYSTERESIS_C);
    maximum = fminf(maximum, cfg.highTempAlarm);
  } else if (offset == offsetof(MachineConfig, ventOffTemp)) {
    minimum = fmaxf(minimum, cfg.targetTemp + VENT_OFF_ABOVE_SV_C);
    maximum = fminf(maximum, cfg.ventOnTemp - VENT_HYSTERESIS_C);
  }
  if (minimum > maximum) minimum = maximum;
}

void formatSettingValue(const SettingItem &item, float value, char *out, size_t size) {
  if ((item.type == SettingType::Bool || item.type == SettingType::Enum8) &&
      item.options && item.optionCount > 0) {
    const uint8_t index = static_cast<uint8_t>(constrain(lroundf(value), 0L,
                                  static_cast<long>(item.optionCount - 1U)));
    snprintf(out, size, "%s", item.options[index]);
  } else if (item.decimals == 0) {
    snprintf(out, size, "%ld%s", lroundf(value), item.unit);
  } else {
    snprintf(out, size, "%.*f%s", item.decimals, value, item.unit);
  }
}

// ============================================================
// 5. TRANG, MENU, HANG DOI LENH
// ============================================================
enum class View : uint8_t {
  Home, MainMenu, GeneralMenu, SettingList, EditSetting, TurnStats, AutoTune,
  EventLog, Confirm, Alarm, WifiPortal
};

enum class ConfirmAction : uint8_t { None, BatchToggle, AutoTuneStart, ResumeBatch,
                                    WifiPortal };

// Prototype thu cong: Arduino IDE tu sinh prototype cho ham trong .ino.
// Neu ham dung enum/struct tuy chinh, prototype tu dong co the bi chen
// truoc noi khai bao kieu va gay loi "View was not declared".
void openBatchConfirm(View returnView);
void openResumeConfirm();
void openWifiPortalView();
void openAlarmView(View returnView);
bool probeLcdUnlocked();

MachineConfig currentConfig;
MachineRuntime currentRuntime;
HmiEventSnapshot currentEventLog;

bool lcdReady = false;
bool dirty = true;
bool hasRenderedView = false;
View lastRenderedView = View::Home;

struct ConfigSaveTransaction {
  bool active = false;
  bool readyForHost = false;
  uint32_t id = 0;
  uint32_t startedAt = 0;
  MachineConfig rollback;
  MachineConfig candidate;
};
ConfigSaveTransaction configSave;
uint32_t nextConfigTransactionId = 1;

View view = View::Home;
uint8_t homePage = 0;
uint8_t mainIndex = 0;
uint8_t listIndex = 0;
uint8_t listTop = 0;
uint8_t selectedGroup = 0;
float editValue = 0;
uint8_t editSettingIndex = 0;
ConfirmAction confirmAction = ConfirmAction::None;
View confirmReturnView = View::Home;
bool confirmYes = true;
bool resumeDecisionSubmitted = false;
View alarmReturnView = View::Home;
uint8_t alarmIndex = 0;
uint8_t eventLogIndex = 0;
uint32_t alarmPresentedMask = 0;

uint32_t lastDrawAt = 0;
uint32_t lastHomeDrawAt = 0;
uint32_t lastAlarmDrawAt = 0;
uint32_t lastInteractionAt = 0;
uint32_t lastLcdRetryAt = 0;
uint32_t lastLcdHealthCheckAt = 0;
uint32_t lastLcdFaultLogAt = 0;
char toastText[64] = "";
char toastLines[3][26] = {};
uint8_t toastLineCount = 0;
bool toastCompact = false;
bool toastError = false;
uint32_t toastUntil = 0;
uint32_t lastCommandPollAt = 0;

HmiCommand commandQueue[COMMAND_QUEUE_SIZE];
uint8_t commandHead = 0, commandTail = 0, commandCount = 0;
uint8_t commandOutstandingCount = 0;
uint32_t nextCommandId = 1;

struct ActiveCommandSlot {
  bool used = false;
  uint32_t takenAt = 0;
  HmiCommand command;
};
ActiveCommandSlot activeCommands[COMMAND_QUEUE_SIZE];

struct CommandAck {
  uint32_t commandId = 0;
  bool ok = false;
  char message[64] = "";
};
CommandAck commandAckQueue[COMMAND_ACK_QUEUE_SIZE];
uint8_t commandAckHead = 0, commandAckTail = 0, commandAckCount = 0;

struct ConfigAckInbox {
  bool pending = false;
  uint32_t transactionId = 0;
  bool ok = false;
  bool hasStoredConfig = false;
  MachineConfig storedConfig;
};

portMUX_TYPE hmiApiMux = portMUX_INITIALIZER_UNLOCKED;
MachineRuntime runtimeInbox;
MachineConfig configInbox;
HmiEventSnapshot eventLogInbox;
bool runtimeInboxPending = false;
bool configInboxPending = false;
bool eventLogInboxPending = false;
bool apiWorkPending = false;
char dateInbox[11] = "--/--/----";
bool dateInboxPending = false;
BuzzerCue cueInbox = BuzzerCue::None;
bool cueInboxPending = false;
uint32_t expiredAlarmAckMask = AlarmNone;
ConfigAckInbox configAckInbox;

bool apiHasPendingWork() {
  return __atomic_load_n(&apiWorkPending, __ATOMIC_ACQUIRE);
}

void markApiWorkPending() {
  __atomic_store_n(&apiWorkPending, true, __ATOMIC_RELEASE);
}

void setApiWorkPending(bool pending) {
  __atomic_store_n(&apiWorkPending, pending, __ATOMIC_RELEASE);
}

// Prototype thu cong de Arduino .ino preprocessor khong chen khai bao
// processConfigAck truoc dinh nghia ConfigAckInbox.
void processConfigAck(const ConfigAckInbox &ack);

HmiI2cLockFn i2cLockCallback = nullptr;
HmiI2cUnlockFn i2cUnlockCallback = nullptr;

// MENU CHINH dung 4 muc theo yeu cau van hanh. Thoat bang NHAN GIU, giong
// moi man hinh khac, nen khong ton mot dong cho nut THOAT.
constexpr uint8_t MAIN_BATCH = 0;
constexpr uint8_t MAIN_GENERAL = 1;
constexpr uint8_t MAIN_LOG = 2;
constexpr uint8_t MAIN_PID = 3;
constexpr uint8_t MAIN_COUNT = 4;

const char *mainItemLabel(uint8_t index) {
  switch (index) {
    case MAIN_BATCH: return "CAI DAT ME";
    case MAIN_GENERAL: return "CAI DAT CHUNG";
    case MAIN_LOG: return "NHAT KY";
    default: return "TU CHINH PID";
  }
}

GroupExtra groupExtra(uint8_t group) {
  if (group >= GROUP_COUNT) return GroupExtra::None;
  // Dong "Cau hinh Wi-Fi" chi co nghia khi may dang o che do ONLINE.
  if (GROUPS[group].extra == GroupExtra::WifiPortal &&
      !currentConfig.cloudEnabled) return GroupExtra::None;
  return GROUPS[group].extra;
}

uint8_t groupExtraRows(uint8_t group) {
  return groupExtra(group) == GroupExtra::None ? 0U : 1U;
}

// Moi nhom = cac thong so + (toi da) mot dong phu + dong Thoat.
uint8_t settingListItemCount(uint8_t group) {
  return static_cast<uint8_t>(GROUPS[group].count + groupExtraRows(group) + 1U);
}

uint8_t settingListExitIndex(uint8_t group) {
  return static_cast<uint8_t>(GROUPS[group].count + groupExtraRows(group));
}

const char *groupExtraLabel(uint8_t group) {
  switch (groupExtra(group)) {
    case GroupExtra::TurnStats: return "So lan dao";
    case GroupExtra::WifiPortal: return "Cau hinh Wi-Fi";
    default: return "";
  }
}

bool timeReached(uint32_t now, uint32_t deadline) {
  return static_cast<int32_t>(now - deadline) >= 0;
}

void prepareToastLayout() {
  memset(toastLines, 0, sizeof(toastLines));
  toastLineCount = 0;
  const size_t length = strnlen(toastText, sizeof(toastText));
  size_t position = 0;
  while (position < length && toastLineCount < 3U) {
    while (position < length && toastText[position] == ' ') ++position;
    size_t take = min(static_cast<size_t>(25U), length - position);
    if (position + take < length) {
      size_t wordBreak = take;
      while (wordBreak > 10U && toastText[position + wordBreak] != ' ') {
        --wordBreak;
      }
      if (wordBreak > 10U) take = wordBreak;
    }
    memcpy(toastLines[toastLineCount], toastText + position, take);
    toastLines[toastLineCount][take] = '\0';
    position += take;
    ++toastLineCount;
  }
  toastCompact = toastLineCount > 1U || length > 20U;
}

void showToast(const char *text, bool error = false, uint32_t duration = 0) {
  snprintf(toastText, sizeof(toastText), "%s", text ? text : "");
  prepareToastLayout();
  toastError = error;
  toastUntil = millis() +
      (duration ? duration : (error ? TOAST_ERROR_MS : TOAST_INFO_MS));
  dirty = true;
}

bool commandTypesConflict(HmiCommandType a, HmiCommandType b) {
  if (a == b) return true;
  const bool aBatch = a == HmiCommandType::BatchStart || a == HmiCommandType::BatchStop;
  const bool bBatch = b == HmiCommandType::BatchStart || b == HmiCommandType::BatchStop;
  if (aBatch && bBatch) return true;
  const bool aTune = a == HmiCommandType::AutoTuneStart;
  const bool bTune = b == HmiCommandType::AutoTuneStart;
  if ((aTune && bBatch) || (bTune && aBatch)) return true;
  const bool aResume = a == HmiCommandType::ResumeYes || a == HmiCommandType::ResumeNo;
  const bool bResume = b == HmiCommandType::ResumeYes || b == HmiCommandType::ResumeNo;
  if (aResume && bResume) return true;
  const bool aDirection = a == HmiCommandType::TurnLeft || a == HmiCommandType::TurnRight;
  const bool bDirection = b == HmiCommandType::TurnLeft || b == HmiCommandType::TurnRight;
  return aDirection && bDirection;
}

bool commandConflictLocked(HmiCommandType type) {
  for (uint8_t i = 0, pos = commandHead; i < commandCount;
       ++i, pos = static_cast<uint8_t>((pos + 1U) % COMMAND_QUEUE_SIZE)) {
    if (commandTypesConflict(type, commandQueue[pos].type)) return true;
  }
  for (uint8_t i = 0; i < COMMAND_QUEUE_SIZE; ++i) {
    if (activeCommands[i].used &&
        commandTypesConflict(type, activeCommands[i].command.type)) return true;
  }
  return false;
}

bool queueCommand(HmiCommandType type,
                  uint16_t validForMs = COMMAND_DEFAULT_VALID_MS,
                  uint16_t actuatorLeaseMs = 0,
                  uint32_t alarmMask = AlarmNone,
                  uint32_t *commandId = nullptr) {
  bool full = false;
  bool duplicate = false;
  uint32_t id = 0;
  portENTER_CRITICAL(&hmiApiMux);
  full = commandOutstandingCount >= COMMAND_QUEUE_SIZE;
  duplicate = !full && commandConflictLocked(type);
  if (!full && !duplicate) {
    id = nextCommandId++;
    if (id == 0) id = nextCommandId++;
    commandQueue[commandTail] = {
      id, type, static_cast<uint32_t>(millis()), validForMs,
      actuatorLeaseMs, alarmMask
    };
    commandTail = static_cast<uint8_t>((commandTail + 1U) % COMMAND_QUEUE_SIZE);
    ++commandCount;
    ++commandOutstandingCount;
  }
  portEXIT_CRITICAL(&hmiApiMux);

  if (full || duplicate) {
    showToast(full ? "HE THONG DANG BAN" : "LENH DANG XU LY", true);
    buzzerPlayCue(BuzzerCue::Error);
    return false;
  }
  if (commandId) *commandId = id;
  return true;
}

void setListSelection(int value, uint8_t count) {
  if (count == 0) return;
  listIndex = static_cast<uint8_t>(constrain(value, 0, count - 1));
  if (listIndex < listTop) listTop = listIndex;
  if (listIndex >= listTop + 4) listTop = listIndex - 3;
  dirty = true;
}

void alignMainMenuWindow() {
  listTop = mainIndex >= 3 ? static_cast<uint8_t>(mainIndex - 3) : 0;
}

// Quay ve dung mot cap. Nhom 0 thuoc MENU CHINH, nhom 1..6 thuoc CAI DAT CHUNG.
void returnToGroupOwner() {
  if (selectedGroup == GROUP_BATCH) {
    view = View::MainMenu;
    mainIndex = MAIN_BATCH;
    alignMainMenuWindow();
  } else {
    view = View::GeneralMenu;
    setListSelection(selectedGroup - GENERAL_GROUP_FIRST, GENERAL_GROUP_COUNT);
  }
}

void goBack() {
  switch (view) {
    case View::Home: break;
    case View::MainMenu: view = View::Home; break;
    case View::GeneralMenu:
      view = View::MainMenu;
      mainIndex = MAIN_GENERAL;
      alignMainMenuWindow();
      break;
    case View::SettingList:
      returnToGroupOwner();
      break;
    case View::EditSetting:
      view = View::SettingList;
      break;
    case View::TurnStats:
    case View::WifiPortal:
      // Ca hai deu la dong phu cua mot nhom cai dat: tra ve dung dong do.
      view = View::SettingList;
      setListSelection(GROUPS[selectedGroup].count,
                       settingListItemCount(selectedGroup));
      break;
    case View::AutoTune:
      view = View::MainMenu;
      mainIndex = MAIN_PID;
      alignMainMenuWindow();
      break;
    case View::EventLog:
      view = View::MainMenu;
      mainIndex = MAIN_LOG;
      alignMainMenuWindow();
      break;
    case View::Confirm: view = confirmReturnView; break;
    case View::Alarm: view = alarmReturnView; break;
  }
  dirty = true;
}

void openGroup(uint8_t group) {
  if (configSave.active) {
    showToast("DANG CHO XAC NHAN LUU", true);
    return;
  }
  if (currentRuntime.autoTuneState == AutoTuneState::Running) {
    showToast("AUTO TUNE DANG CHAY", true);
    return;
  }
  selectedGroup = group;
  listIndex = listTop = 0;
  view = View::SettingList;
  dirty = true;
}

void openSetting() {
  const SettingGroup &group = GROUPS[selectedGroup];
  editSettingIndex = GROUP_SETTING_INDEXES[group.first + listIndex];
  if (configSave.active) {
    showToast("DANG LUU - VUI LONG CHO", true);
    return;
  }
  const SettingItem &item = SETTINGS[editSettingIndex];
  float minimum, maximum;
  settingLimits(currentConfig, item, minimum, maximum);
  editValue = constrain(readSetting(currentConfig, item), minimum, maximum);
  view = View::EditSetting;
  dirty = true;
}

void openBatchConfirm(View returnView) {
  // Dang chay me thi luon cho phep mo xac nhan DUNG ME, ke ca EEPROM dang
  // hoan tat giao dich luu. Chi chan thao tac BAT DAU me moi.
  if (configSave.active && !currentRuntime.batchRunning) {
    showToast("CHO LUU XONG TRUOC KHI CHAY ME", true);
    return;
  }
  if (currentRuntime.autoTuneState == AutoTuneState::Running) {
    showToast("HAY CHO AUTO TUNE XONG", true);
    return;
  }
  confirmAction = ConfirmAction::BatchToggle;
  confirmReturnView = returnView;
  // Bat dau: mac dinh DONG Y de thao tac nhanh.
  // Dung me: mac dinh HUY de tranh dung nham chu trinh dang chay.
  confirmYes = !currentRuntime.batchRunning;
  view = View::Confirm;
  dirty = true;
}


void openResumeConfirm() {
  confirmAction = ConfirmAction::ResumeBatch;
  confirmReturnView = View::Home;
  confirmYes = true;
  view = View::Confirm;
  dirty = true;
}

// ---------------------------------------------------------------------------
// KET NOI / CAU HINH WI-FI
// Nut nay kich hoat DUNG co che captive portal ma thao tac giu nut BOOT dang
// dung. HMI khong biet gi ve MAYAP_WebBridge: no goi hook do .ino dang ky, nen
// ban OFFLINE (MAYAP_WEB_ENABLED=0) van bien dich va bao "KHONG CO MANG".
// ---------------------------------------------------------------------------
const char *netStateText(NetState state) {
  switch (state) {
    case NetState::Starting: return "DANG KHOI DONG";
    case NetState::NoWifi:   return "CHUA CO WI-FI";
    case NetState::WifiOnly: return "CHO MAY CHU";
    case NetState::Online:   return "DA KET NOI";
    default:                 return "DA TAT (OFFLINE)";
  }
}

const char *portalStateText(PortalState state) {
  switch (state) {
    case PortalState::Starting: return "DANG MO DIEM PHAT...";
    case PortalState::Active:   return "AP DA MO: MAYAP-XXXX";
    case PortalState::Failed:   return "MO AP THAT BAI";
    default:                    return "";
  }
}

void openWifiPortalView() {
  view = View::WifiPortal;
  dirty = true;
}

void requestWifiPortal() {
  if (!currentConfig.cloudEnabled) {
    showToast("HAY BAT ONLINE TRUOC", true);
    buzzerPlayCue(BuzzerCue::Error);
    return;
  }
  if (mayapCloudRestartPending()) {
    showToast("HAY KHOI DONG LAI MAY TRUOC", true);
    buzzerPlayCue(BuzzerCue::Error);
    return;
  }
  if (!mayapNetHooksInstalled() || currentRuntime.netState == NetState::Disabled) {
    showToast("CHUA BAT DUOC RADIO", true);
    buzzerPlayCue(BuzzerCue::Error);
    return;
  }
  if (currentRuntime.portalState == PortalState::Active ||
      currentRuntime.portalState == PortalState::Starting) {
    showToast("AP DANG MO SAN");
    return;
  }
  // clearNetworkAndOpenPortal() cua thu vien XOA Wi-Fi da luu roi moi mo AP
  // (dung nhu thao tac BOOT). Bam nham se lam may mat mang cho toi khi cau
  // hinh lai, nen phai hoi xac nhan.
  confirmAction = ConfirmAction::WifiPortal;
  confirmReturnView = View::WifiPortal;
  confirmYes = false;
  view = View::Confirm;
  dirty = true;
}

void executeWifiPortal() {
  if (mayapRequestWifiPortal()) {
    showToast("DANG MO AP CAU HINH");
    buzzerPlayCue(BuzzerCue::Ok);
  } else {
    showToast("KHONG MO DUOC AP", true);
    buzzerPlayCue(BuzzerCue::Error);
  }
  view = View::WifiPortal;
  dirty = true;
}

void openAutoTuneConfirm() {
  if (configSave.active) {
    showToast("CHO LUU XONG TRUOC", true);
    return;
  }
  if (currentRuntime.autoTuneState == AutoTuneState::Running) {
    showToast("AUTO TUNE DANG CHAY");
    return;
  }
  if (currentRuntime.batchRunning) {
    showToast("HAY DUNG ME TRUOC AUTO TUNE", true);
    return;
  }
  if (!currentRuntime.sensorOnline ||
      (currentRuntime.alarmMask & (AlarmSensor | AlarmEmergency | AlarmSystem))) {
    showToast("CAM BIEN/AN TOAN CHUA SAN SANG", true);
    return;
  }
  confirmAction = ConfirmAction::AutoTuneStart;
  confirmReturnView = View::AutoTune;
  confirmYes = false;
  view = View::Confirm;
  dirty = true;
}

constexpr uint32_t ALARM_PRIORITY[] = {
  AlarmEmergency, AlarmSystem, AlarmSensor, AlarmTurning, AlarmTempHigh,
  AlarmTempLow, AlarmHumidityLow
};
constexpr uint8_t ALARM_PRIORITY_COUNT = sizeof(ALARM_PRIORITY) / sizeof(ALARM_PRIORITY[0]);

// ============================================================
// 3A. BO DIEU KHIEN COI KHONG BLOCKING
// ============================================================
void buzzerWrite(bool on) {
  buzzer.outputOn = on;
  digitalWrite(PIN_BUZZER, (on == BUZZER_ACTIVE_HIGH) ? HIGH : LOW);
}

void buzzerBegin() {
  // Dat muc OFF truoc khi OUTPUT de tranh mot xung keu luc khoi dong.
  digitalWrite(PIN_BUZZER, BUZZER_ACTIVE_HIGH ? LOW : HIGH);
  pinMode(PIN_BUZZER, OUTPUT);
  buzzerWrite(false);
}

BuzzerPattern buzzerCuePattern(BuzzerCue cue) {
  switch (cue) {
    case BuzzerCue::Key:   return {22, 0, 1};      // Tieng nhan phim rat ngan
    case BuzzerCue::Save:  return {35, 0, 1};      // Bip rat ngan, khong gay on
    case BuzzerCue::Ok:    return {55, 70, 2};     // Hai bip ngan
    case BuzzerCue::Error: return {110, 100, 3};   // Ba bip de phan biet loi
    default:               return {0, 0, 0};
  }
}

BuzzerPattern alarmPattern(uint32_t bit) {
  switch (bit) {
    case AlarmEmergency:   return {900, 100, 0};   // Gan lien tuc, uu tien cao nhat
    case AlarmSystem:      return {600, 200, 0};
    case AlarmSensor:      return {450, 300, 0};
    case AlarmTurning:     return {400, 400, 0};
    case AlarmTempHigh:    return {350, 500, 0};
    case AlarmTempLow:     return {220, 900, 0};
    case AlarmHumidityLow: return {120, 2880, 0};  // Het nuoc: nhac nho thua
    default:               return {0, 0, 0};
  }
}

uint32_t highestUnacknowledgedAlarm() {
  const uint32_t pending = currentRuntime.alarmMask & ~buzzer.acknowledgedAlarmMask;
  for (uint8_t i = 0; i < ALARM_PRIORITY_COUNT; ++i) {
    if (pending & ALARM_PRIORITY[i]) return ALARM_PRIORITY[i];
  }
  return AlarmNone;
}

void buzzerStopTransient() {
  buzzer.transientActive = false;
  buzzer.transientOn = false;
  buzzer.transientMandatory = false;
  buzzer.transientPulsesLeft = 0;
}

void buzzerPlayCue(BuzzerCue cue) {
  // Bao dong va yeu cau phuc hoi co uu tien cao hon tieng UI.
  if (highestUnacknowledgedAlarm() != AlarmNone ||
      currentRuntime.resumeConfirmationRequired ||
      currentRuntime.turnState == TurnState::Left ||
      currentRuntime.turnState == TurnState::Right) return;
  const BuzzerPattern pattern = buzzerCuePattern(cue);
  if (!pattern.pulses) return;
  const bool mandatory = cue == BuzzerCue::Error;
  if (!currentConfig.alarmEnabled && !mandatory) return;
  buzzer.transientPattern = pattern;
  buzzer.transientPulsesLeft = pattern.pulses;
  buzzer.transientActive = true;
  buzzer.transientOn = true;
  buzzer.transientMandatory = mandatory;
  buzzer.deadline = millis() + pattern.onMs;
  buzzerWrite(true);
}

void buzzerAcknowledge(uint32_t alarmMask) {
  const uint32_t now = millis();
  alarmMask &= ALARM_KNOWN_MASK;
  buzzer.acknowledgedAlarmMask |= alarmMask;
  for (uint8_t i = 0; i < ALARM_PRIORITY_COUNT; ++i) {
    if (alarmMask & ALARM_PRIORITY[i]) buzzer.acknowledgedAt[i] = now;
  }
  buzzer.soundingAlarmBit = AlarmNone;
  buzzerStopTransient();
  buzzerWrite(false);
}

void buzzerUnacknowledge(uint32_t alarmMask) {
  buzzer.acknowledgedAlarmMask &= ~alarmMask;
  for (uint8_t i = 0; i < ALARM_PRIORITY_COUNT; ++i) {
    if (alarmMask & ALARM_PRIORITY[i]) buzzer.acknowledgedAt[i] = 0;
  }
  buzzer.soundingAlarmBit = AlarmNone;
}

uint32_t alarmRepeatMs(uint32_t bit) {
  if (bit == AlarmEmergency) return EMERGENCY_RESOUND_MS;
  if (bit == AlarmSystem || bit == AlarmSensor || bit == AlarmTurning || bit == AlarmTempHigh) {
    return CRITICAL_RESOUND_MS;
  }
  return 0;
}

void buzzerUpdate(uint32_t now) {
  // Bit da het loi tu dong mat ACK. Loi tai xuat hien se keu lai.
  buzzer.acknowledgedAlarmMask &= currentRuntime.alarmMask;
  for (uint8_t i = 0; i < ALARM_PRIORITY_COUNT; ++i) {
    const uint32_t bit = ALARM_PRIORITY[i];
    if (!(buzzer.acknowledgedAlarmMask & bit)) {
      buzzer.acknowledgedAt[i] = 0;
      continue;
    }
    const uint32_t repeatMs = alarmRepeatMs(bit);
    if (repeatMs && now - buzzer.acknowledgedAt[i] >= repeatMs) {
      buzzer.acknowledgedAlarmMask &= ~bit;
      buzzer.acknowledgedAt[i] = 0;
    }
  }

  const uint32_t alarmBit = highestUnacknowledgedAlarm();
  // Moi loi/canh bao moi deu phat am it nhat den khi nguoi dung ACK.
  // alarmEnabled chi tat cac bip giao dien thong thuong, khong che giau loi.
  if (alarmBit != AlarmNone) {
    buzzer.resumePromptActive = false;
    buzzerStopTransient();
    const BuzzerPattern pattern = alarmPattern(alarmBit);
    if (buzzer.soundingAlarmBit != alarmBit) {
      buzzer.soundingAlarmBit = alarmBit;
      buzzerWrite(true);
      buzzer.deadline = now + pattern.onMs;
      return;
    }
    if (static_cast<int32_t>(now - buzzer.deadline) < 0) return;
    if (buzzer.outputOn) {
      buzzerWrite(false);
      buzzer.deadline = now + pattern.offMs;
    } else {
      buzzerWrite(true);
      buzzer.deadline = now + pattern.onMs;
    }
    return;
  }

  buzzer.soundingAlarmBit = AlarmNone;

  // Mat dien giua me la yeu cau bat buoc nguoi dung ra quyet dinh.
  // Coi nhac lap lai den khi chon TIEP TUC hoac HUY ME.
  if (currentRuntime.resumeConfirmationRequired) {
    buzzerStopTransient();
    if (!buzzer.resumePromptActive) {
      buzzer.resumePromptActive = true;
      buzzerWrite(true);
      buzzer.deadline = now + RESUME_PROMPT_ON_MS;
      return;
    }
    if (static_cast<int32_t>(now - buzzer.deadline) < 0) return;
    if (buzzer.outputOn) {
      buzzerWrite(false);
      buzzer.deadline = now + RESUME_PROMPT_OFF_MS;
    } else {
      buzzerWrite(true);
      buzzer.deadline = now + RESUME_PROMPT_ON_MS;
    }
    return;
  }
  buzzer.resumePromptActive = false;

  // Coi GPIO41 keu lien tuc trong suot thoi gian motor dao dang thuc su chay.
  // Bao dong loi va nhac phuc hoi mat dien van co uu tien cao hon.
  const bool turningNow = currentRuntime.turnState == TurnState::Left ||
                          currentRuntime.turnState == TurnState::Right;
  if (turningNow) {
    buzzerStopTransient();
    buzzer.turnContinuousActive = true;
    if (!buzzer.outputOn) buzzerWrite(true);
    return;
  }
  if (buzzer.turnContinuousActive) {
    buzzer.turnContinuousActive = false;
    buzzerWrite(false);
  }

  if (!buzzer.transientActive ||
      (!currentConfig.alarmEnabled && !buzzer.transientMandatory)) {
    buzzerStopTransient();
    buzzerWrite(false);
    return;
  }

  if (static_cast<int32_t>(now - buzzer.deadline) < 0) return;
  if (buzzer.transientOn) {
    buzzerWrite(false);
    buzzer.transientOn = false;
    if (buzzer.transientPulsesLeft > 0) --buzzer.transientPulsesLeft;
    if (!buzzer.transientPulsesLeft) {
      buzzerStopTransient();
      return;
    }
    buzzer.deadline = now + buzzer.transientPattern.offMs;
  } else {
    buzzerWrite(true);
    buzzer.transientOn = true;
    buzzer.deadline = now + buzzer.transientPattern.onMs;
  }
}

uint8_t activeAlarmCount(uint32_t mask) {
  uint8_t count = 0;
  for (uint8_t i = 0; i < ALARM_PRIORITY_COUNT; ++i)
    if (mask & ALARM_PRIORITY[i]) ++count;
  return count;
}

uint32_t alarmBitAt(uint32_t mask, uint8_t index) {
  uint8_t found = 0;
  for (uint8_t i = 0; i < ALARM_PRIORITY_COUNT; ++i) {
    if (!(mask & ALARM_PRIORITY[i])) continue;
    if (found++ == index) return ALARM_PRIORITY[i];
  }
  return AlarmNone;
}

void openAlarmView(View returnView) {
  if (!currentRuntime.activeFaultDisplayCount) return;
  alarmReturnView = returnView == View::Alarm ? View::Home : returnView;
  alarmIndex = 0;
  view = View::Alarm;
  dirty = true;
}

bool startConfigSave(const MachineConfig &candidate) {
  if (configSave.active) {
    showToast("DANG CHO XAC NHAN LUU", true);
    return false;
  }
  uint32_t id = nextConfigTransactionId++;
  if (id == 0) id = nextConfigTransactionId++;
  portENTER_CRITICAL(&hmiApiMux);
  configSave.active = true;
  configSave.readyForHost = true;
  configSave.id = id;
  configSave.startedAt = millis();
  configSave.rollback = currentConfig;
  configSave.candidate = candidate;
  portEXIT_CRITICAL(&hmiApiMux);
  return true;
}

void commitSetting() {
  MachineConfig candidate = currentConfig;
  const SettingItem &item = SETTINGS[editSettingIndex];
  float minimum, maximum;
  settingLimits(currentConfig, item, minimum, maximum);
  const float oldTarget = candidate.targetTemp;
  writeSetting(candidate, item, constrain(editValue, minimum, maximum));
  if (item.offset == offsetof(MachineConfig, targetTemp)) {
    // Doi SV keo theo ca chuoi nguong. Dung ham dung chung trong config.h de
    // thao tac nay cho ra ket qua giong het khi thuc hien tu Web.
    mayapShiftThresholdsWithTarget(candidate, oldTarget);
  }
  sanitizeConfig(candidate);

  const float oldValue = readSetting(currentConfig, item);
  const float newValue = readSetting(candidate, item);
  view = View::SettingList;
  if (fabsf(oldValue - newValue) < 0.0001f) {
    showToast("GIA TRI KHONG DOI");
    return;
  }
  if (!startConfigSave(candidate)) return;
  currentConfig = candidate;
  showToast("DANG LUU...");
}

void exitSettingGroup() {
  returnToGroupOwner();
  dirty = true;
}

bool requestAlarmAcknowledge() {
  const uint32_t snapshot = currentRuntime.alarmMask & ALARM_KNOWN_MASK;
  if (!snapshot) {
    showToast("KHONG CO CANH BAO");
    return false;
  }
  uint32_t commandId = 0;
  if (!queueCommand(HmiCommandType::AlarmAck, COMMAND_DEFAULT_VALID_MS,
                    0, snapshot, &commandId)) return false;
  buzzerAcknowledge(snapshot);
  showToast("DA TAT COI - LOI VAN CON");
  return true;
}

void selectMainItem() {
  switch (mainIndex) {
    case MAIN_BATCH: openGroup(GROUP_BATCH); return;
    case MAIN_GENERAL:
      view = View::GeneralMenu;
      setListSelection(0, GENERAL_GROUP_COUNT);
      break;
    case MAIN_LOG:
      eventLogIndex = 0U;
      view = View::EventLog;
      break;
    default:
      view = View::AutoTune;
      break;
  }
  dirty = true;
}

void handleInput() {
  if (rotary.button == ButtonEvent::ShortPress ||
      rotary.button == ButtonEvent::LongPress) {
    buzzerPlayCue(BuzzerCue::Key);
  }

  if (rotary.button == ButtonEvent::LongPress) {
    if (view == View::Confirm && confirmAction == ConfirmAction::ResumeBatch) {
      return;
    }
    if (view == View::Home) {
      // Nut BAT DAU ME da duoc bo khoi man hinh chinh. Thao tac bat/dung me
      // van con nguyen, chuyen sang NHAN GIU: mot thao tac co chu dich, dung
      // thong le thiet bi cong nghiep cho lenh anh huong toi ca me ap.
      openBatchConfirm(View::Home);
      return;
    }
    if (view == View::SettingList) {
      exitSettingGroup();
      return;
    }
    goBack();
    return;
  }

  switch (view) {
    case View::Home:
      if (rotary.step) {
        int p = static_cast<int>(homePage) + rotary.step;
        if (p < 0) p = 1;
        if (p > 1) p = 0;
        homePage = static_cast<uint8_t>(p);
        dirty = true;
      }
      if (rotary.button == ButtonEvent::ShortPress) {
        if (currentRuntime.activeFaultDisplayCount) {
          openAlarmView(View::Home);
        } else {
          view = View::MainMenu;
          mainIndex = listTop = 0;
          dirty = true;
        }
      }
      break;

    case View::GeneralMenu:
      if (rotary.step) {
        setListSelection(static_cast<int>(listIndex) + rotary.step,
                         GENERAL_GROUP_COUNT);
      }
      if (rotary.button == ButtonEvent::ShortPress) {
        openGroup(static_cast<uint8_t>(GENERAL_GROUP_FIRST + listIndex));
      }
      break;

    case View::WifiPortal:
      if (rotary.button == ButtonEvent::ShortPress) requestWifiPortal();
      break;

    case View::MainMenu:
      if (rotary.step) {
        mainIndex = static_cast<uint8_t>(constrain(static_cast<int>(mainIndex) + rotary.step, 0, MAIN_COUNT - 1));
        if (mainIndex < listTop) listTop = mainIndex;
        if (mainIndex >= listTop + 4) listTop = mainIndex - 3;
        dirty = true;
      }
      if (rotary.button == ButtonEvent::ShortPress) selectMainItem();
      break;

    case View::SettingList: {
      const uint8_t itemCount = settingListItemCount(selectedGroup);
      // So dong co the giam ngay duoi chan nguoi dung: bat/tat ONLINE lam an
      // dong "Cau hinh Wi-Fi". Kep con tro truoc khi doc bat cu thu gi.
      if (listIndex >= itemCount) setListSelection(itemCount - 1, itemCount);
      if (rotary.step) {
        setListSelection(static_cast<int>(listIndex) + rotary.step, itemCount);
      }
      if (rotary.button == ButtonEvent::ShortPress) {
        const SettingGroup &group = GROUPS[selectedGroup];
        if (listIndex < group.count) {
          openSetting();
        } else if (listIndex == group.count &&
                   groupExtra(selectedGroup) != GroupExtra::None) {
          if (groupExtra(selectedGroup) == GroupExtra::TurnStats) {
            view = View::TurnStats;
            dirty = true;
          } else {
            openWifiPortalView();
          }
        } else {
          exitSettingGroup();
        }
      }
      break;
    }

    case View::EditSetting: {
      const SettingItem &item = SETTINGS[editSettingIndex];
      if (rotary.step) {
        float minimum, maximum;
        settingLimits(currentConfig, item, minimum, maximum);
        editValue = constrain(editValue + rotary.step * item.step, minimum, maximum);
        dirty = true;
      }
      if (rotary.button == ButtonEvent::ShortPress) commitSetting();
      break;
    }

    case View::TurnStats:
      if (rotary.button == ButtonEvent::ShortPress) goBack();
      break;

    case View::AutoTune:
      if (rotary.button == ButtonEvent::ShortPress) openAutoTuneConfirm();
      break;

    case View::EventLog: {
      const uint8_t count = currentEventLog.count;
      if (count && rotary.step) {
        int next = static_cast<int>(eventLogIndex) + rotary.step;
        while (next < 0) next += count;
        while (next >= count) next -= count;
        eventLogIndex = static_cast<uint8_t>(next);
        dirty = true;
      }
      if (rotary.button == ButtonEvent::ShortPress) goBack();
      break;
    }

    case View::Confirm:
      if (rotary.step) { confirmYes = !confirmYes; dirty = true; }
      if (rotary.button == ButtonEvent::ShortPress) {
        if (confirmYes) {
          if (confirmAction == ConfirmAction::BatchToggle) {
            if (queueCommand(currentRuntime.batchRunning ?
                             HmiCommandType::BatchStop : HmiCommandType::BatchStart)) {
              showToast("DANG KIEM TRA DIEU KIEN");
            }
            view = View::Home;
            homePage = 0;
          } else if (confirmAction == ConfirmAction::AutoTuneStart) {
            if (queueCommand(HmiCommandType::AutoTuneStart,
                             COMMAND_AUTOTUNE_VALID_MS)) {
              showToast("DANG KHOI DONG AUTO TUNE");
            }
            view = View::AutoTune;
          } else if (confirmAction == ConfirmAction::ResumeBatch) {
            if (queueCommand(HmiCommandType::ResumeYes)) {
              resumeDecisionSubmitted = true;
              showToast("DANG PHUC HOI ME CU");
            }
            view = View::Home;
          } else if (confirmAction == ConfirmAction::WifiPortal) {
            executeWifiPortal();
          }
        } else {
          if (confirmAction == ConfirmAction::ResumeBatch) {
            if (queueCommand(HmiCommandType::ResumeNo)) {
              resumeDecisionSubmitted = true;
              showToast("DA HUY ME CU");
            }
            view = View::Home;
          } else {
            view = confirmReturnView;
          }
        }
        confirmAction = ConfirmAction::None;
        dirty = true;
      }
      break;

    case View::Alarm: {
      const uint8_t count = currentRuntime.activeFaultDisplayCount;
      if (!count) {
        view = alarmReturnView;
        dirty = true;
        break;
      }
      if (rotary.step) {
        int next = static_cast<int>(alarmIndex) + rotary.step;
        while (next < 0) next += count;
        while (next >= count) next -= count;
        alarmIndex = static_cast<uint8_t>(next);
        dirty = true;
      }
      if (rotary.button == ButtonEvent::ShortPress) {
        if (requestAlarmAcknowledge()) view = alarmReturnView;
      }
      break;
    }
  }
}

// ============================================================
// 6. VE GIAO DIEN
// ============================================================
void drawHeader(const char *title) {
  lcd.setDrawColor(1);
  lcd.setFont(u8g2_font_5x8_tf);
  char shortTitle[14];
  snprintf(shortTitle, sizeof(shortTitle), "%.13s", title ? title : "");
  lcd.drawStr(0, 8, shortTitle);
  const char *date = currentRuntime.dateText[0] ? currentRuntime.dateText : "--/--/----";
  const int16_t dateX = max(0, 127 - static_cast<int16_t>(lcd.getStrWidth(date)));
  if (currentRuntime.alarmMask) {
    lcd.drawBox(67, 0, 8, 8);
    lcd.setDrawColor(0);
    lcd.drawStr(69, 8, "!");
    lcd.setDrawColor(1);
  }
  lcd.drawStr(dateX, 8, date);
  lcd.drawHLine(0, 10, 128);
}

// Thanh tac vu o ba trang Home duoc thu gon con 9 pixel de uu tien noi dung chinh.
void drawActionBar(const char *label) {
  lcd.setDrawColor(1);
  lcd.drawBox(0, 55, 128, 9);
  lcd.setDrawColor(0);
  lcd.setFont(u8g2_font_5x8_tf);
  const int16_t x = max(0, (128 - static_cast<int16_t>(lcd.getStrWidth(label))) / 2);
  lcd.drawStr(x, 63, label);
  lcd.setDrawColor(1);
}

// Tra cuu truc tiep tu bang loi dung chung trong config.h. Khong con ban chep
// tay nen them ma loi moi la HMI hien dung ngay, khong can sua o day.
uint32_t alarmBitForFaultCode(uint16_t code) { return faultAlarmBit(code); }

const char *faultTitle(uint16_t code) {
  return faultDescriptor(static_cast<FaultCode>(code)).title;
}
const char *faultActionText(uint16_t code) {
  return faultDescriptor(static_cast<FaultCode>(code)).action;
}
const char *severityText(uint8_t severity) {
  switch (static_cast<FaultSeverity>(severity)) {
    case FaultSeverity::Emergency: return "KHAN CAP";
    case FaultSeverity::Stop:      return "NGHIEM TRONG";
    case FaultSeverity::Warning:   return "CANH BAO";
    default:                       return "THONG TIN";
  }
}

// Chi con cac ma loi CO GIA TRI DONG can hien. Moi ma khac lay nguyen nhan
// tinh tu bang loi dung chung, nen ma loi moi khong bao gio ra "CHI TIET 0".
void faultDetail(const HmiFaultItem &fault, char *out, size_t size) {
  switch (fault.code) {
    case 103: snprintf(out, size, "GIAM DOT NGOT %.1fC", fault.detail / 10.0f); return;
    case 104: snprintf(out, size, "KHONG DOI %u PHUT",
                       static_cast<unsigned>(fault.detail)); return;
    case 110: snprintf(out, size, "PV %.1f < %.1fC", currentRuntime.temperature,
                       currentConfig.lowTempAlarm); return;
    case 111: snprintf(out, size, "PV %.1f > %.1fC", currentRuntime.temperature,
                       currentConfig.highTempAlarm); return;
    case 112: snprintf(out, size, "PV %.1f > %.1fC", currentRuntime.temperature,
                       currentConfig.emergencyTemp); return;
    case 120: snprintf(out, size, "AM %.0f%% < %.0f%%", currentRuntime.humidity,
                       currentConfig.lowHumidityAlarm); return;
    case 305: snprintf(out, size, "%d LAN DONG CAT/GIO", fault.detail); return;
    case 307: snprintf(out, size, "CHU KY %d MS", fault.detail); return;
    case 308: snprintf(out, size, "STACK CON %d BYTE", fault.detail); return;
    case 309: snprintf(out, size, "HEAP CON %d KB", fault.detail); return;
    case 311: snprintf(out, size, "I2C LOI LIEN TIEP %d", fault.detail); return;
    case 401: snprintf(out, size, "TANG %.1fC KHI DA TAT",
                       fault.detail / 10.0f); return;
    case 402: snprintf(out, size, "%d PHUT CHI TANG <0.3C", fault.detail); return;
    default: break;
  }
  snprintf(out, size, "%s",
           faultDescriptor(static_cast<FaultCode>(fault.code)).cause);
}

// ---------------------------------------------------------------------------
// MAN HINH CANH BAO
// Nguoi van hanh phai doc duoc DAY DU bon thong tin ngay tren mot man hinh:
//   MA LOI  -  MUC NGHIEM TRONG  -  NGUYEN NHAN  -  HANH DONG CAN LAM
// Ban cu chi in mot dong footer "E112 1/3 NHAN:TAT COI" nen mat hoan toan
// muc nghiem trong va huong xu ly. Thanh action bar bi bo di de lay cho cho
// hai dong hanh dong; goi y phim duoc dua len header.
// ---------------------------------------------------------------------------
uint8_t wrapText(const char *text, char out[2][26], uint8_t maxChars) {
  memset(out, 0, 2 * 26);
  if (!text || !text[0]) return 0;
  const size_t length = strnlen(text, 64);
  if (maxChars > 25U) maxChars = 25U;
  uint8_t lines = 0;
  size_t position = 0;
  while (position < length && lines < 2U) {
    while (position < length && text[position] == ' ') ++position;
    if (position >= length) break;
    size_t take = min(static_cast<size_t>(maxChars), length - position);
    if (position + take < length) {
      // Cat theo tu, khong cat giua chu.
      size_t wordBreak = take;
      while (wordBreak > 6U && text[position + wordBreak] != ' ') --wordBreak;
      if (wordBreak > 6U) take = wordBreak;
    }
    memcpy(out[lines], text + position, take);
    out[lines][take] = '\0';
    position += take;
    ++lines;
  }
  return lines;
}

void drawSeverityBadge(uint8_t severity, int16_t baselineY) {
  const char *label = severityText(severity);
  lcd.setFont(u8g2_font_5x8_tf);
  const int16_t width = static_cast<int16_t>(lcd.getStrWidth(label) + 6);
  const int16_t x = static_cast<int16_t>(max(0, 128 - width));
  const int16_t top = static_cast<int16_t>(baselineY - 8);
  // Muc Warning ve khung rong, Stop/Emergency ve khoi dac de nhin la thay ngay.
  if (static_cast<FaultSeverity>(severity) >= FaultSeverity::Stop) {
    lcd.drawBox(x, top, width, 10);
    lcd.setDrawColor(0);
    lcd.drawStr(x + 3, baselineY, label);
    lcd.setDrawColor(1);
  } else {
    lcd.drawFrame(x, top, width, 10);
    lcd.drawStr(x + 3, baselineY, label);
  }
}

void drawAlarm() {
  const uint8_t count = currentRuntime.activeFaultDisplayCount;
  if (!count) {
    drawHeader("CANH BAO");
    lcd.setFont(u8g2_font_6x12_tf);
    lcd.drawStr(22, 36, "KHONG CO CANH BAO");
    return;
  }
  if (alarmIndex >= count) alarmIndex = 0;
  const HmiFaultItem &fault = currentRuntime.activeFaults[alarmIndex];

  // --- Header rieng: so thu tu loi + goi y phim (thay cho ngay thang) ---
  char header[24];
  lcd.setDrawColor(1);
  lcd.setFont(u8g2_font_5x8_tf);
  snprintf(header, sizeof(header), "CANH BAO %u/%u", alarmIndex + 1U, count);
  lcd.drawStr(0, 8, header);
  const char *hint = "NHAN=TAT COI";
  const int16_t hintX = max(0, 127 - static_cast<int16_t>(lcd.getStrWidth(hint)));
  lcd.drawStr(hintX, 8, hint);
  lcd.drawHLine(0, 10, 128);

  // --- Ma loi + muc nghiem trong ---
  char code[10];
  snprintf(code, sizeof(code), "E%03u", fault.code);
  lcd.setFont(u8g2_font_helvB12_tf);
  lcd.drawStr(0, 24, code);
  drawSeverityBadge(fault.severity, 23);

  // --- Tieu de loi ---
  lcd.setFont(u8g2_font_6x12_tf);
  char title[24];
  snprintf(title, sizeof(title), "%.21s", faultTitle(fault.code));
  lcd.drawStr(0, 36, title);

  // --- Nguyen nhan (co gia tri thuc te neu co) ---
  lcd.setFont(u8g2_font_5x8_tf);
  char detail[30];
  faultDetail(fault, detail, sizeof(detail));
  lcd.drawStr(0, 45, detail);

  // --- Hanh dong can lam, toi da hai dong ---
  char action[2][26];
  const uint8_t lines = wrapText(faultActionText(fault.code), action, 24U);
  if (lines) lcd.drawStr(0, 54, ">");
  for (uint8_t i = 0; i < lines; ++i) {
    lcd.drawStr(6, static_cast<int16_t>(54 + i * 9), action[i]);
  }
  // Loi da ACK nhung dieu kien con ton tai: bao ro de khong tuong da xu ly xong.
  if ((fault.flags & 0x02U) && (fault.flags & 0x01U)) {
    lcd.setFont(u8g2_font_5x8_tf);
    lcd.drawStr(38, 24, "AK");
  }
}

// ---------------------------------------------------------------------------
// TRANG CHINH
// Bo cuc moi:
//   - Thanh duoi = TRANG THAI MAY (SAN SANG / DANG AP ...), lay thang tu
//     runtime.machineState nen khong the lech voi firmware. Vi tri nay truoc
//     day la nut BAT DAU ME; lenh do chuyen sang NHAN GIU.
//   - Dong giua = THOI GIAN DAO TIEP THEO (cho o cu cua trang thai).
//   - NGAY AP chuyen xuong duoi, di kem cong suat SSR de nhin la biet may
//     dang cap bao nhieu nhiet.
// ---------------------------------------------------------------------------
void formatNextTurn(char *out, size_t size) {
  if (!currentConfig.turningEnabled) {
    snprintf(out, size, "DAO TRUNG: DA TAT");
    return;
  }
  if (currentRuntime.turnState == TurnState::Left ||
      currentRuntime.turnState == TurnState::Right) {
    snprintf(out, size, "DANG DAO SANG %s",
             currentRuntime.turnState == TurnState::Left ? "TRAI" : "PHAI");
    return;
  }
  if (currentRuntime.turnState == TurnState::Fault) {
    snprintf(out, size, "DAO TRUNG: DANG LOI");
    return;
  }
  if (!currentRuntime.batchRunning) {
    snprintf(out, size, "DAO SAU: CHUA CO ME");
    return;
  }
  // nextTurnMinutes = 0 truoc day vua nghia "khong co lich" vua nghia "den
  // gio dao", nguoi van hanh khong the phan biet. Nay tach ro hai truong hop.
  if (currentRuntime.nextTurnMinutes == 0U) {
    snprintf(out, size, "DAO SAU: SAP DAO");
    return;
  }
  const uint16_t minutes = currentRuntime.nextTurnMinutes;
  if (minutes < 60U) {
    snprintf(out, size, "DAO SAU: %u PHUT", minutes);
  } else {
    snprintf(out, size, "DAO SAU: %uG%02u", minutes / 60U, minutes % 60U);
  }
}

// Thanh duoi: loi luon uu tien hon trang thai binh thuong.
void drawHomeStatusBar() {
  char text[40];
  if (currentRuntime.activeFaultCount) {
    snprintf(text, sizeof(text), "E%03u %.20s",
             currentRuntime.primaryFaultCode,
             faultTitle(currentRuntime.primaryFaultCode));
    drawActionBar(text);
    return;
  }
  drawActionBar(currentRuntime.machineState);
}

void drawHomeMain() {
  char text[40];
  drawHeader("MAY AP");

  // Nhiet do do duoc: so lon nhat man hinh.
  lcd.setFont(u8g2_font_logisoso20_tn);
  snprintf(text, sizeof(text), currentRuntime.sensorOnline ? "%.1f" : "--.-",
           currentRuntime.temperature);
  lcd.drawStr(0, 33, text);

  lcd.setFont(u8g2_font_6x12_tf);
  snprintf(text, sizeof(text), "SV %.1fC", currentConfig.targetTemp);
  lcd.drawStr(74, 22, text);
  snprintf(text, sizeof(text),
           currentRuntime.sensorOnline ? "AM %.0f%%" : "AM --%%",
           currentRuntime.humidity);
  lcd.drawStr(74, 34, text);

  lcd.setFont(u8g2_font_5x8_tf);
  formatNextTurn(text, sizeof(text));
  lcd.drawStr(0, 44, text);

  if (currentRuntime.batchRunning || currentRuntime.currentDay) {
    snprintf(text, sizeof(text), "NGAY %u/%u", currentRuntime.currentDay,
             currentConfig.totalIncubationDays);
  } else {
    snprintf(text, sizeof(text), "CHUA CHAY ME");
  }
  lcd.drawStr(0, 53, text);

  snprintf(text, sizeof(text), "SSR %3u%%",
           static_cast<unsigned>(lroundf(currentRuntime.heaterPower)));
  const int16_t ssrX = max(0, 127 - static_cast<int16_t>(lcd.getStrWidth(text)));
  lcd.drawStr(ssrX, 53, text);

  drawHomeStatusBar();
}

// ---------------------------------------------------------------------------
// TRANG TRANG THAI
// Da bo dong "LAN DAO TIEP" (da co o trang chinh) va bo dong huong dan
// "MO MENU". Cho trong duoc dung cho cong suat SSR va trang thai ket noi.
// ---------------------------------------------------------------------------
void drawSsrBar(int16_t x, int16_t y, int16_t width, int16_t height, float percent) {
  const int16_t clamped = static_cast<int16_t>(
      constrain(static_cast<int>(lroundf(percent)), 0, 100));
  lcd.drawFrame(x, y, width, height);
  const int16_t inner = static_cast<int16_t>(width - 2);
  const int16_t fill = static_cast<int16_t>((inner * clamped) / 100);
  if (fill > 0) lcd.drawBox(x + 1, y + 1, fill, height - 2);
}

void drawHomeOutputs() {
  char text[40];
  drawHeader("TRANG THAI");
  lcd.setFont(u8g2_font_5x8_tf);

  // 1. Cong suat SSR: chu so + thanh do de nhin thay ngay muc cap nhiet.
  snprintf(text, sizeof(text), "SSR THANH NHIET");
  lcd.drawStr(0, 19, text);
  snprintf(text, sizeof(text), "%u%%",
           static_cast<unsigned>(lroundf(currentRuntime.heaterPower)));
  const int16_t pctX = max(0, 127 - static_cast<int16_t>(lcd.getStrWidth(text)));
  lcd.drawStr(pctX, 19, text);
  drawSsrBar(0, 21, 128, 7, currentRuntime.heaterPower);

  // 2. Trang thai co cau chap hanh.
  snprintf(text, sizeof(text), "SSR %-3s   QUAT TH %-3s",
           currentRuntime.heaterOn ? "ON" : "OFF",
           currentRuntime.circulationFanOn ? "ON" : "OFF");
  lcd.drawStr(0, 37, text);

  snprintf(text, sizeof(text), "HUT %-3s   DAO %s",
           currentRuntime.ventFanOn ? "ON" : "OFF",
           currentRuntime.turnState == TurnState::Left ? "TRAI"
             : currentRuntime.turnState == TurnState::Right ? "PHAI"
             : currentRuntime.turnState == TurnState::Fault ? "LOI"
             : currentRuntime.turnState == TurnState::Waiting ? "CHO" : "DUNG");
  lcd.drawStr(0, 46, text);

  // 3. So lan dao + ket noi. Neu co loi thi nhuong dong duoi cho thanh canh bao.
  if (currentRuntime.activeFaultCount) {
    snprintf(text, sizeof(text), "DAO NAY %u / ME %lu",
             currentRuntime.turnCountToday,
             static_cast<unsigned long>(currentRuntime.turnCountBatch));
    lcd.drawStr(0, 54, text);
    drawHomeStatusBar();
    return;
  }
  snprintf(text, sizeof(text), "DAO NAY %u / ME %lu",
           currentRuntime.turnCountToday,
           static_cast<unsigned long>(currentRuntime.turnCountBatch));
  lcd.drawStr(0, 55, text);
  if (mayapCloudRestartPending()) {
    snprintf(text, sizeof(text), "MANG: CAN KHOI DONG LAI");
  } else {
    snprintf(text, sizeof(text), "MANG: %s",
             currentConfig.cloudEnabled ? netStateText(currentRuntime.netState)
                                        : "OFFLINE");
  }
  lcd.drawStr(0, 63, text);
}

void drawHome() {
  if (homePage == 0) drawHomeMain();
  else drawHomeOutputs();
}

// Thanh cuon ben phai: danh sach nhom/thong so nay da dai hon 4 dong nen
// nguoi van hanh can biet minh dang o dau.
void drawScrollbar(uint8_t topIndex, uint8_t visible, uint8_t total) {
  if (total <= visible) return;
  const int16_t trackTop = 12, trackHeight = 42;
  lcd.drawVLine(126, trackTop, trackHeight);
  int16_t barHeight = static_cast<int16_t>((trackHeight * visible) / total);
  if (barHeight < 4) barHeight = 4;
  const uint8_t maxTop = static_cast<uint8_t>(total - visible);
  const int16_t travel = static_cast<int16_t>(trackHeight - barHeight);
  const int16_t barTop = static_cast<int16_t>(
      trackTop + (maxTop ? (travel * topIndex) / maxTop : 0));
  lcd.drawBox(125, barTop, 3, barHeight);
}

// Bon dong menu + mot dong goi y phim o day man hinh.
constexpr int16_t MENU_ROW_Y[4] = {20, 31, 42, 53};

void drawMenuRow(uint8_t row, const char *label, bool selected) {
  const int16_t y = MENU_ROW_Y[row];
  if (selected) {
    lcd.drawBox(0, y - 9, 124, 11);
    lcd.setDrawColor(0);
    lcd.drawStr(2, y, ">");
    lcd.drawStr(12, y, label);
    lcd.setDrawColor(1);
  } else {
    lcd.drawStr(12, y, label);
  }
}

void drawMenuHint(const char *hint) {
  lcd.setFont(u8g2_font_5x8_tf);
  lcd.drawStr(0, 63, hint);
}

void drawMainMenu() {
  drawHeader("MENU CHINH");
  lcd.setFont(u8g2_font_6x12_tf);
  for (uint8_t row = 0; row < 4 && row < MAIN_COUNT; ++row) {
    drawMenuRow(row, mainItemLabel(row), row == mainIndex);
  }
  drawMenuHint("GIU: VE MAN HINH CHINH");
}

void drawGeneralMenu() {
  drawHeader("CAI DAT CHUNG");
  lcd.setFont(u8g2_font_6x12_tf);
  for (uint8_t row = 0; row < 4 && listTop + row < GENERAL_GROUP_COUNT; ++row) {
    const uint8_t local = static_cast<uint8_t>(listTop + row);
    drawMenuRow(row, GROUPS[GENERAL_GROUP_FIRST + local].label,
                local == listIndex);
  }
  drawScrollbar(listTop, 4, GENERAL_GROUP_COUNT);
  drawMenuHint("GIU: QUAY LAI MENU");
}

void drawWifiPortal() {
  char text[34];
  drawHeader("CAU HINH WI-FI");
  lcd.setFont(u8g2_font_5x8_tf);

  snprintf(text, sizeof(text), "TRANG THAI: %s",
           !mayapNetHooksInstalled() ? "BAN OFFLINE"
           : mayapCloudRestartPending() ? "CHO KHOI DONG LAI"
           : netStateText(currentRuntime.netState));
  lcd.drawStr(0, 21, text);

  if (mayapCloudRestartPending()) {
    lcd.drawStr(0, 31, "DA DOI CHE DO KET NOI.");
    lcd.drawStr(0, 40, "TAT/BAT NGUON MAY DE AP");
    lcd.drawStr(0, 49, "DUNG CHE DO VUA CHON.");
    drawActionBar("GIU: QUAY LAI");
    return;
  }

  const char *portal = portalStateText(currentRuntime.portalState);
  if (portal[0]) {
    lcd.drawStr(0, 31, portal);
    if (currentRuntime.portalState == PortalState::Active) {
      lcd.drawStr(0, 41, "MO TRINH DUYET:");
      lcd.setFont(u8g2_font_6x12_tf);
      lcd.drawStr(0, 52, "192.168.4.1");
      lcd.setFont(u8g2_font_5x8_tf);
    }
  } else {
    lcd.drawStr(0, 31, "NHAN DE XOA WI-FI CU VA");
    lcd.drawStr(0, 41, "MO DIEM PHAT MAYAP-XXXX");
    lcd.drawStr(0, 50, "DE CHON MANG MOI.");
  }
  drawActionBar(currentRuntime.portalState == PortalState::Active
                    ? "GIU: QUAY LAI"
                    : "NHAN: MO AP CAU HINH");
}

void drawSettingList() {
  const SettingGroup &group = GROUPS[selectedGroup];
  const uint8_t itemCount = settingListItemCount(selectedGroup);
  const uint8_t exitIndex = settingListExitIndex(selectedGroup);
  drawHeader(group.label);
  lcd.setFont(u8g2_font_6x12_tf);
  char value[18];
  for (uint8_t row = 0; row < 4 && listTop + row < itemCount; ++row) {
    const uint8_t local = static_cast<uint8_t>(listTop + row);
    const int16_t y = static_cast<int16_t>(22 + row * 12);
    const bool selected = local == listIndex;
    if (selected) {
      lcd.drawBox(0, y - 9, 124, 11);
      lcd.setDrawColor(0);
    }

    if (local < group.count) {
      const uint8_t settingIndex = GROUP_SETTING_INDEXES[group.first + local];
      const SettingItem &item = SETTINGS[settingIndex];
      formatSettingValue(item, readSetting(currentConfig, item), value, sizeof(value));
      lcd.drawStr(2, y, item.label);
      const int16_t x = max(78, 122 - static_cast<int16_t>(lcd.getStrWidth(value)));
      lcd.drawStr(x, y, value);
    } else if (local == group.count &&
               groupExtra(selectedGroup) != GroupExtra::None) {
      // Dong muc phu can trai va viet thuong dong bo voi cac muc cai dat.
      lcd.drawStr(2, y, groupExtraLabel(selectedGroup));
    } else if (local == exitIndex) {
      // Nut thoat can trai nhu cac dong con lai.
      lcd.drawStr(2, y, "Thoat");
    }

    if (selected) lcd.setDrawColor(1);
  }
  drawScrollbar(listTop, 4, itemCount);
}

void drawTurnStats() {
  char text[28];
  drawHeader("SO LAN DAO");
  lcd.setFont(u8g2_font_6x12_tf);

  snprintf(text, sizeof(text), "HOM NAY: %u LAN",
           currentRuntime.turnCountToday);
  lcd.drawStr(6, 29, text);

  snprintf(text, sizeof(text), "TONG ME: %lu LAN",
           static_cast<unsigned long>(currentRuntime.turnCountBatch));
  lcd.drawStr(6, 45, text);

  drawActionBar("NHAN: QUAY LAI");
}

void drawEditSetting() {
  const SettingItem &item = SETTINGS[editSettingIndex];
  char value[24];
  drawHeader("CHINH THONG SO");
  lcd.setFont(u8g2_font_6x12_tf);
  const int16_t labelX = max(0, (128 - static_cast<int16_t>(lcd.getStrWidth(item.label))) / 2);
  lcd.drawStr(labelX, 23, item.label);
  formatSettingValue(item, editValue, value, sizeof(value));
  lcd.setFont(u8g2_font_helvB14_tf);
  const int16_t x = max(0, (128 - static_cast<int16_t>(lcd.getStrWidth(value))) / 2);
  lcd.drawStr(x, 46, value);
  drawActionBar("LUU THAY DOI");
}

const char *autoTuneStateText(AutoTuneState state) {
  switch (state) {
    case AutoTuneState::Running: return "DANG TU CHINH";
    case AutoTuneState::Success: return "DA HOAN THANH";
    case AutoTuneState::Failed: return "THAT BAI";
    default: return "SAN SANG";
  }
}

void drawAutoTune() {
  char text[28];
  drawHeader("TU CHINH PID");
  lcd.setFont(u8g2_font_helvB12_tf);
  const char *state = autoTuneStateText(currentRuntime.autoTuneState);
  const int16_t stateX = max(
      0, (128 - static_cast<int16_t>(lcd.getStrWidth(state))) / 2);
  lcd.drawStr(stateX, 29, state);

  lcd.setFont(u8g2_font_6x12_tf);
  if (currentRuntime.autoTuneState == AutoTuneState::Running) {
    snprintf(text, sizeof(text), "TIEN DO %u%%",
             currentRuntime.autoTuneProgress);
  } else if (currentRuntime.batchRunning) {
    snprintf(text, sizeof(text), "HAY DUNG ME TRUOC");
  } else if (!currentRuntime.sensorOnline) {
    snprintf(text, sizeof(text), "CAM BIEN DANG LOI");
  } else {
    snprintf(text, sizeof(text), "MAY SE TU TIM PID");
  }
  const int16_t textX = max(
      0, (128 - static_cast<int16_t>(lcd.getStrWidth(text))) / 2);
  lcd.drawStr(textX, 46, text);
  drawActionBar(currentRuntime.autoTuneState == AutoTuneState::Running
                    ? "VUI LONG CHO"
                    : "NHAN: BAT DAU");
}

void formatEventAge(uint32_t ageSec, char *out, size_t size) {
  if (ageSec < 5U) {
    snprintf(out, size, "VUA XONG");
  } else if (ageSec < 60U) {
    snprintf(out, size, "CACH %lus", static_cast<unsigned long>(ageSec));
  } else {
    snprintf(out, size, "CACH %lup%02lus",
             static_cast<unsigned long>(ageSec / 60UL),
             static_cast<unsigned long>(ageSec % 60UL));
  }
}

const char *inputEventName(uint16_t code) {
  switch (code - 100U) {
    case 0: return "HT TRAI";
    case 1: return "HT PHAI";
    case 2: return "CHE DO AUTO";
    case 3: return "CONG TAC NHIET";
    case 4: return "CONG TAC QUAT";
    case 5: return "CONG TAC DEN";
    case 6: return "LENH DAO TRAI";
    case 7: return "LENH DAO PHAI";
    default: return "INPUT";
  }
}

const char *outputEventName(uint16_t code) {
  switch (code - 200U) {
    case 1: return "CONTACTOR NHIET";
    case 2: return "DAO TRAI";
    case 3: return "DAO PHAI";
    case 4: return "QUAT HUT";
    case 5: return "DEN";
    case 6: return "QUAT TUAN HOAN";
    case 7: return "COI KHAN";
    default: return "OUTPUT";
  }
}

void eventText(const HmiEventItem &e, char *title, size_t titleSize,
               char *detail, size_t detailSize) {
  title[0] = detail[0] = '\0';
  if (e.code >= 1100U && e.code < 1500U) {
    const uint16_t faultCode = static_cast<uint16_t>(e.code - 1000U);
    snprintf(title, titleSize, "E%03u %s", faultCode, faultTitle(faultCode));
    switch (e.type) {
      case 4: snprintf(detail, detailSize, "LOI XUAT HIEN"); break;
      case 5: snprintf(detail, detailSize, "LOI DA HET"); break;
      case 6: snprintf(detail, detailSize, "NGUOI DUNG DA XN"); break;
      default: snprintf(detail, detailSize, "CHI TIET %d", e.value); break;
    }
    return;
  }
  if (e.code >= 100U && e.code < 200U) {
    snprintf(title, titleSize, "%s", inputEventName(e.code));
    snprintf(detail, detailSize, "%s", e.value ? "BAT" : "TAT");
    return;
  }
  if (e.code >= 200U && e.code < 300U) {
    snprintf(title, titleSize, "%s", outputEventName(e.code));
    snprintf(detail, detailSize, "%s", e.value ? "BAT" : "TAT");
    return;
  }
  switch (e.code) {
    case 1: snprintf(title, titleSize, "KHOI DONG NGUON"); break;
    case 2: snprintf(title, titleSize, "RESET NGOAI"); break;
    case 3: snprintf(title, titleSize, "RESET PHAN MEM"); break;
    case 4: snprintf(title, titleSize, "RESET PANIC"); break;
    case 5: snprintf(title, titleSize, "RESET WATCHDOG"); break;
    case 6: snprintf(title, titleSize, "RESET BROWNOUT"); break;
    case 20: snprintf(title, titleSize, "BAT DAU ME AP"); break;
    case 21: snprintf(title, titleSize, "DUNG ME AP"); break;
    case 22: snprintf(title, titleSize, "PHUC HOI ME"); break;
    case 23: snprintf(title, titleSize, "CHO XN SAU MAT DIEN"); break;
    case 24: snprintf(title, titleSize, "DA TIEP TUC ME CU"); break;
    case 25: snprintf(title, titleSize, "DA HUY ME CU"); break;
    case 30: snprintf(title, titleSize, "CHUYEN SANG AUTO"); break;
    case 31: snprintf(title, titleSize, "CHUYEN SANG MANUAL"); break;
    case 40: snprintf(title, titleSize, "CAM BIEN PHUC HOI"); break;
    case 41: snprintf(title, titleSize, "MAT CAM BIEN"); break;
    case 50: snprintf(title, titleSize, "DA LUU CAU HINH"); break;
    case 51: snprintf(title, titleSize, "BAT AUTO TUNE"); break;
    case 52: snprintf(title, titleSize, "AUTO TUNE OK"); break;
    case 53: snprintf(title, titleSize, "AUTO TUNE LOI"); break;
    case 60: snprintf(title, titleSize, "BAT DAO TRAI"); break;
    case 61: snprintf(title, titleSize, "BAT DAO PHAI"); break;
    case 62: snprintf(title, titleSize, "TIM GOC TRAI"); break;
    case 63: snprintf(title, titleSize, "TIM GOC PHAI"); break;
    case 64: snprintf(title, titleSize, "DA DEN BEN TRAI"); break;
    case 65: snprintf(title, titleSize, "DA DEN BEN PHAI"); break;
    case 80:
      snprintf(title, titleSize, "LENH BI TU CHOI");
      snprintf(detail, detailSize, "MA LENH %d", e.value);
      return;
    case 90:
      snprintf(title, titleSize, "DOI TRANG THAI MANG");
      snprintf(detail, detailSize, "%s",
               netStateText(static_cast<NetState>(e.value)));
      return;
    case 91: snprintf(title, titleSize, "MO AP CAU HINH"); break;
    case 92: snprintf(title, titleSize, "MO AP THAT BAI"); break;
    default: snprintf(title, titleSize, "SU KIEN %u", e.code); break;
  }
  if (e.value) snprintf(detail, detailSize, "GIA TRI %d", e.value);
  else snprintf(detail, detailSize, "DA GHI NHAN");
}

bool hmiLeapYear(uint16_t year) {
  return (year % 4U == 0U && year % 100U != 0U) || (year % 400U == 0U);
}

uint8_t hmiDaysInMonth(uint16_t year, uint8_t month) {
  static const uint8_t days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  if (month < 1U || month > 12U) return 30U;
  return static_cast<uint8_t>(days[month - 1U] +
      ((month == 2U && hmiLeapYear(year)) ? 1U : 0U));
}

void formatEventDateTime(uint32_t epoch, char *out, size_t size) {
  if (!out || size == 0U) return;
  if (epoch == 0U) {
    snprintf(out, size, "--/-- --:--:--");
    return;
  }
  uint32_t days = epoch / 86400UL;
  uint32_t seconds = epoch % 86400UL;
  uint16_t year = 1970U;
  // uint32_t epoch khong the vuot nam 2106; gioi han 137 vong de khong co
  // bat ky vong lap khong bien tren duong ve LCD.
  for (uint16_t guard = 0U; guard < 137U; ++guard) {
    const uint16_t yearDays = hmiLeapYear(year) ? 366U : 365U;
    if (days < yearDays) break;
    days -= yearDays;
    ++year;
  }
  uint8_t month = 1U;
  while (month <= 12U) {
    const uint8_t monthDays = hmiDaysInMonth(year, month);
    if (days < monthDays) break;
    days -= monthDays;
    ++month;
  }
  const uint8_t day = static_cast<uint8_t>(days + 1U);
  const uint8_t hour = static_cast<uint8_t>(seconds / 3600UL);
  const uint8_t minute = static_cast<uint8_t>((seconds % 3600UL) / 60UL);
  const uint8_t second = static_cast<uint8_t>(seconds % 60UL);
  snprintf(out, size, "%02u/%02u %02u:%02u:%02u",
           static_cast<unsigned>(day), static_cast<unsigned>(month),
           static_cast<unsigned>(hour), static_cast<unsigned>(minute),
           static_cast<unsigned>(second));
}

void drawEventLog() {
  drawHeader("NHAT KY 1 GIO");
  if (!currentEventLog.count) {
    lcd.setFont(u8g2_font_6x12_tf);
    lcd.drawStr(18, 35, "CHUA CO SU KIEN");
    drawActionBar("NHAN: QUAY LAI");
    return;
  }
  if (eventLogIndex >= currentEventLog.count) eventLogIndex = 0U;
  const HmiEventItem &e = currentEventLog.items[eventLogIndex];
  char age[18];
  char timestamp[24];
  char title[28];
  char detail[28];
  char footer[48];
  formatEventAge(e.ageSec, age, sizeof(age));
  formatEventDateTime(e.epoch, timestamp, sizeof(timestamp));
  eventText(e, title, sizeof(title), detail, sizeof(detail));

  lcd.setFont(u8g2_font_5x8_tf);
  char top[40];
  snprintf(top, sizeof(top), "#%04lu %s",
           static_cast<unsigned long>(e.sequence % 10000UL), timestamp);
  lcd.drawStr(1, 20, top);
  lcd.setFont(u8g2_font_6x12_tf);
  lcd.drawStr(1, 34, title);
  lcd.setFont(u8g2_font_5x8_tf);
  lcd.drawStr(1, 46, detail);
  snprintf(footer, sizeof(footer), "%u/%u %s - NHAN:RA",
           eventLogIndex + 1U, currentEventLog.totalInWindow, age);
  drawActionBar(footer);
}

void drawConfirm() {
  char text[24];
  const bool autoTune = confirmAction == ConfirmAction::AutoTuneStart;
  const bool resume = confirmAction == ConfirmAction::ResumeBatch;
  const bool wifi = confirmAction == ConfirmAction::WifiPortal;
  drawHeader(wifi ? "CAU HINH WI-FI" :
             resume ? "PHUC HOI ME" : autoTune ? "XAC NHAN" :
             (currentRuntime.batchRunning ? "DUNG ME AP" : "BAT DAU ME"));
  lcd.setFont(u8g2_font_6x12_tf);
  if (wifi) {
    lcd.setFont(u8g2_font_5x8_tf);
    lcd.drawStr(4, 22, "SE XOA WI-FI DA LUU VA");
    lcd.drawStr(4, 32, "MO DIEM PHAT CAU HINH.");
    lcd.drawStr(4, 42, "MAY VAN AP BINH THUONG.");
    lcd.setFont(u8g2_font_6x12_tf);
  } else if (resume) {
    lcd.drawStr(4, 24, "PHAT HIEN MAT DIEN");
    lcd.drawStr(4, 38, "TIEP TUC ME DANG AP?");
  } else if (autoTune) {
    lcd.drawStr(4, 24, "BAT TU CHINH PID?");
    lcd.drawStr(4, 38, "MAY SE TU GIA NHIET");
  } else if (currentRuntime.batchRunning) {
    snprintf(text, sizeof(text), "DANG O NGAY %u/%u", currentRuntime.currentDay, currentConfig.totalIncubationDays);
    lcd.drawStr(4, 24, text);
    lcd.drawStr(4, 38, "DUNG SE NGAT CHU TRINH");
  } else {
    snprintf(text, sizeof(text), "SV %.1fC  %u NGAY", currentConfig.targetTemp, currentConfig.totalIncubationDays);
    lcd.drawStr(4, 24, text);
    snprintf(text, sizeof(text), "DAO MOI %u PHUT", currentConfig.turnIntervalMin);
    lcd.drawStr(4, 38, text);
  }
  lcd.setFont(u8g2_font_6x12_tf);
  if (confirmYes) {
    lcd.drawBox(0, 51, 63, 13);
    lcd.setDrawColor(0); lcd.drawStr(8, 62, "DONG Y"); lcd.setDrawColor(1);
    lcd.drawStr(83, 62, "HUY");
  } else {
    lcd.drawStr(8, 62, "DONG Y");
    lcd.drawBox(65, 51, 63, 13);
    lcd.setDrawColor(0); lcd.drawStr(83, 62, "HUY"); lcd.setDrawColor(1);
  }
}

void drawToast(uint32_t now) {
  if (!toastText[0] || timeReached(now, toastUntil)) return;
  const int16_t top = toastCompact ? 34 : 49;
  const int16_t height = toastCompact ? 30 : 15;
  lcd.setDrawColor(1);
  lcd.drawBox(0, top, 128, height);
  lcd.setDrawColor(0);
  lcd.setFont(toastCompact ? u8g2_font_5x8_tf : u8g2_font_6x12_tf);
  if (toastCompact) {
    for (uint8_t i = 0; i < toastLineCount; ++i) {
      lcd.drawStr(3, static_cast<int16_t>(43 + i * 10), toastLines[i]);
    }
  } else if (toastLineCount) {
    lcd.drawStr(3, 61, toastLines[0]);
  }
  lcd.setDrawColor(1);
  if (toastError) lcd.drawFrame(0, top, 128, height);
}

void render(uint32_t now) {
  if (!lcdReady) return;
  const bool viewChanged = !hasRenderedView || view != lastRenderedView;
  const bool periodicHome = view == View::Home &&
                            now - lastHomeDrawAt >= HOME_REFRESH_MS;
  const bool periodicAlarm = view == View::Alarm &&
                             now - lastAlarmDrawAt >= ALARM_REFRESH_MS;
  const bool periodic = periodicHome || periodicAlarm;
  if (!dirty && !periodic && !viewChanged) return;
  // Doi trang phai ve ngay, khong bat nguoi dung cho chu ky refresh cu.
  // Cac cap nhat trong cung mot trang van duoc gioi han tan so de giam tai I2C.
  if (!viewChanged && now - lastDrawAt < DISPLAY_MIN_DRAW_MS) return;

  // Luon khoi tao lai draw state truoc moi frame. Dieu nay tranh drawColor/font
  // cua popup truoc ro ri sang menu tiep theo khi nguoi dung bam nhanh.
  lcd.setDrawColor(1);
  lcd.setFontMode(0);
  lcd.clearBuffer();
  switch (view) {
    case View::Home: drawHome(); break;
    case View::MainMenu: drawMainMenu(); break;
    case View::GeneralMenu: drawGeneralMenu(); break;
    case View::WifiPortal: drawWifiPortal(); break;
    case View::SettingList: drawSettingList(); break;
    case View::EditSetting: drawEditSetting(); break;
    case View::TurnStats: drawTurnStats(); break;
    case View::AutoTune: drawAutoTune(); break;
    case View::EventLog: drawEventLog(); break;
    case View::Confirm: drawConfirm(); break;
    case View::Alarm: drawAlarm(); break;
  }
  drawToast(now);

  if (i2cLockCallback && !i2cLockCallback(I2C_TIMEOUT_MS)) {
    dirty = true;
    return;
  }

  // Full frame da duoc ve hoan chinh trong RAM. Gui duy nhat mot lan va giu
  // panel luon sang; setPowerSave() chi danh cho che do tiet kiem dien, khong
  // dung lam "chuyen canh" vi no tao chop den moi lan bam menu.
  lcd.sendBuffer();
  if (i2cUnlockCallback) i2cUnlockCallback();

  dirty = false;
  hasRenderedView = true;
  lastRenderedView = view;
  lastDrawAt = now;
  if (view == View::Home) lastHomeDrawAt = now;
  if (view == View::Alarm) lastAlarmDrawAt = now;
}

// ============================================================
// 7. API HMI CHO FIRMWARE TONG
// ============================================================
bool probeLcdUnlocked() {
  Wire.beginTransmission(LCD_I2C_ADDRESS);
  const bool ok = Wire.endTransmission(true) == 0;
  mayapI2cRecordResult(ok);
  return ok;
}

void recoverI2cBusUnlocked() {
#if MAYAP_HMI_OWNS_I2C_BUS
  Wire.end();
  pinMode(PIN_I2C_SDA, INPUT_PULLUP);
  pinMode(PIN_I2C_SCL, OUTPUT_OPEN_DRAIN);
  digitalWrite(PIN_I2C_SCL, HIGH);
  if (digitalRead(PIN_I2C_SDA) == LOW) {
    for (uint8_t i = 0; i < 9 && digitalRead(PIN_I2C_SDA) == LOW; ++i) {
      digitalWrite(PIN_I2C_SCL, LOW);
      delayMicroseconds(5);
      digitalWrite(PIN_I2C_SCL, HIGH);
      delayMicroseconds(5);
    }
  }
  pinMode(PIN_I2C_SDA, OUTPUT_OPEN_DRAIN);
  digitalWrite(PIN_I2C_SDA, LOW);
  delayMicroseconds(5);
  digitalWrite(PIN_I2C_SCL, HIGH);
  delayMicroseconds(5);
  digitalWrite(PIN_I2C_SDA, HIGH);
  pinMode(PIN_I2C_SDA, INPUT_PULLUP);
  pinMode(PIN_I2C_SCL, INPUT_PULLUP);
#endif
}

bool beginLcd() {
  if (i2cLockCallback && !i2cLockCallback(I2C_TIMEOUT_MS)) return false;
#if MAYAP_HMI_OWNS_I2C_BUS
  recoverI2cBusUnlocked();
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_CLOCK_HZ);
#endif
  Wire.setTimeOut(I2C_TIMEOUT_MS);
  if (!probeLcdUnlocked()) {
    if (i2cUnlockCallback) i2cUnlockCallback();
    return false;
  }
  lcd.setBusClock(I2C_CLOCK_HZ);
  lcd.setI2CAddress(static_cast<uint8_t>(LCD_I2C_ADDRESS << 1));
  lcd.begin();
  lcd.setContrast(DEFAULT_CONTRAST);
  lcd.setDrawColor(1);
  lcd.setFontMode(0);
  lcd.clearBuffer();
  lcd.sendBuffer();
  const bool ok = probeLcdUnlocked();
  if (i2cUnlockCallback) i2cUnlockCallback();
  hasRenderedView = false;
  return ok;
}

void serviceLcd(uint32_t now) {
  if (!lcdReady) {
    if (now - lastLcdRetryAt < LCD_RETRY_INTERVAL_MS) return;
    lastLcdRetryAt = now;
    lcdReady = beginLcd();
    if (lcdReady) {
      lastLcdHealthCheckAt = now;
      dirty = true;
      showToast("LCD DA TU PHUC HOI");
#if MAYAP_DIAGNOSTIC_SERIAL
      mayapSerialPrintf(false, "[HMI] LCD recovered\n");
#endif
    } else if (now - lastLcdFaultLogAt >= LCD_FAULT_LOG_INTERVAL_MS) {
      lastLcdFaultLogAt = now;
#if MAYAP_DIAGNOSTIC_SERIAL
      mayapSerialPrintf(false, "[HMI] LCD 0x%02X van mat\n", LCD_I2C_ADDRESS);
#endif
    }
    return;
  }

  if (now - lastLcdHealthCheckAt < LCD_HEALTH_CHECK_MS) return;
  lastLcdHealthCheckAt = now;
  if (i2cLockCallback && !i2cLockCallback(I2C_TIMEOUT_MS)) return;
  const bool ok = probeLcdUnlocked();
  if (i2cUnlockCallback) i2cUnlockCallback();
  if (!ok) {
    lcdReady = false;
    lastLcdRetryAt = now;
    dirty = true;
#if MAYAP_DIAGNOSTIC_SERIAL
    mayapSerialPrintf(false, "[HMI] LCD/I2C lost, scheduling recovery\n");
#endif
  }
}

void hmiOnI2cBusReset() {
  lcdReady = false;
  dirty = true;
  hasRenderedView = false;
  lastLcdRetryAt = 0U;
  lastLcdHealthCheckAt = 0U;
}

void hmiBegin() {
  buzzerBegin();
  beginRotary();
  lcdReady = beginLcd();
  const uint32_t now = millis();
  lastInteractionAt = now;
  lastCommandPollAt = now;
  lastLcdRetryAt = now;
  lastLcdHealthCheckAt = now;
  dirty = true;
#if MAYAP_DIAGNOSTIC_SERIAL
  mayapSerialPrintf(false, "[HMI] LCD=%s profile=%u contrast=%u\n", lcdReady ? "OK" : "FAIL",
                   LCD_PROFILE, DEFAULT_CONTRAST);
#endif
}

void sanitizeRuntime(MachineRuntime &runtime) {
  runtime.dateText[sizeof(runtime.dateText) - 1U] = '\0';
  runtime.machineState[sizeof(runtime.machineState) - 1U] = '\0';
  runtime.alarmMask &= ALARM_KNOWN_MASK;
  if (!isfinite(runtime.temperature) || !isfinite(runtime.humidity)) {
    runtime.sensorOnline = false;
  }
  if (!isfinite(runtime.temperature)) runtime.temperature = 0.0f;
  if (!isfinite(runtime.humidity)) runtime.humidity = 0.0f;
  runtime.temperature = constrain(runtime.temperature, -40.0f, 125.0f);
  runtime.humidity = constrain(runtime.humidity, 0.0f, 100.0f);
  if (!isfinite(runtime.heaterPower)) runtime.heaterPower = 0.0f;
  runtime.heaterPower = constrain(runtime.heaterPower, 0.0f, 100.0f);
  if (static_cast<uint8_t>(runtime.turnState) >
      static_cast<uint8_t>(TurnState::Fault)) {
    runtime.turnState = TurnState::Fault;
  }
  if (static_cast<uint8_t>(runtime.autoTuneState) >
      static_cast<uint8_t>(AutoTuneState::Failed)) {
    runtime.autoTuneState = AutoTuneState::Failed;
  }
  runtime.autoTuneProgress = static_cast<uint8_t>(constrain(
      static_cast<int>(runtime.autoTuneProgress), 0, 100));
  if (runtime.activeFaultDisplayCount > HMI_FAULT_DISPLAY_CAPACITY) {
    runtime.activeFaultDisplayCount = HMI_FAULT_DISPLAY_CAPACITY;
  }
  if (runtime.activeFaultCount < runtime.activeFaultDisplayCount) {
    runtime.activeFaultCount = runtime.activeFaultDisplayCount;
  }
  if (runtime.autoTuneState == AutoTuneState::Success) {
    runtime.autoTuneProgress = 100;
  } else if (runtime.autoTuneState == AutoTuneState::Idle ||
             runtime.autoTuneState == AutoTuneState::Failed) {
    runtime.autoTuneProgress = 0;
  }
  if (!runtime.dateText[0]) snprintf(runtime.dateText, sizeof(runtime.dateText),
                                     "--/--/----");
  if (!runtime.machineState[0]) {
    snprintf(runtime.machineState, sizeof(runtime.machineState),
             runtime.batchRunning ? "DANG AP" : "SAN SANG");
  }
}

int16_t displayTenths(float value) {
  return static_cast<int16_t>(lroundf(value * 10.0f));
}

int16_t displayWhole(float value) {
  return static_cast<int16_t>(lroundf(value));
}

bool fixedTextChanged(const char *a, const char *b, size_t size) {
  return strncmp(a, b, size) != 0;
}

bool runtimeHeaderChanged(const MachineRuntime &before,
                          const MachineRuntime &after) {
  return before.alarmMask != after.alarmMask ||
         before.primaryFaultCode != after.primaryFaultCode ||
         before.activeFaultCount != after.activeFaultCount ||
         before.faultNotificationSequence != after.faultNotificationSequence ||
         fixedTextChanged(before.dateText, after.dateText,
                          sizeof(before.dateText));
}

// Chi danh dau ve lai khi du lieu dang nhin thay tren trang hien tai thay doi.
// Chi danh dau dirty khi noi dung dang hien thi thuc su thay doi; khong
// lam LCD gui lai 1024 byte neu trang hien tai khong hien chung.
bool runtimeVisibleChanged(const MachineRuntime &before,
                           const MachineRuntime &after) {
  if (runtimeHeaderChanged(before, after)) return true;

  switch (view) {
    case View::Home:
      if (homePage == 0) {
        return before.sensorOnline != after.sensorOnline ||
               (after.sensorOnline &&
                displayTenths(before.temperature) !=
                    displayTenths(after.temperature)) ||
               (after.sensorOnline &&
                displayWhole(before.humidity) != displayWhole(after.humidity)) ||
               before.batchRunning != after.batchRunning ||
               before.currentDay != after.currentDay ||
               before.nextTurnMinutes != after.nextTurnMinutes ||
               before.turnState != after.turnState ||
               displayWhole(before.heaterPower) !=
                   displayWhole(after.heaterPower) ||
               fixedTextChanged(before.machineState, after.machineState,
                                sizeof(before.machineState));
      }
      return before.heaterOn != after.heaterOn ||
             before.circulationFanOn != after.circulationFanOn ||
             before.ventFanOn != after.ventFanOn ||
             before.turnState != after.turnState ||
             displayWhole(before.heaterPower) !=
                 displayWhole(after.heaterPower) ||
             before.netState != after.netState ||
             before.turnCountToday != after.turnCountToday ||
             before.turnCountBatch != after.turnCountBatch;

    case View::TurnStats:
      return before.turnCountToday != after.turnCountToday ||
             before.turnCountBatch != after.turnCountBatch;

    case View::AutoTune:
      return before.autoTuneState != after.autoTuneState ||
             before.autoTuneProgress != after.autoTuneProgress ||
             before.batchRunning != after.batchRunning ||
             before.sensorOnline != after.sensorOnline;

    case View::Confirm:
      return before.batchRunning != after.batchRunning ||
             before.currentDay != after.currentDay ||
             before.resumeConfirmationRequired != after.resumeConfirmationRequired;

    case View::EventLog:
      return before.eventSequence != after.eventSequence;

    case View::Alarm:
      return displayTenths(before.temperature) !=
                 displayTenths(after.temperature) ||
             displayWhole(before.humidity) != displayWhole(after.humidity) ||
             before.primaryFaultCode != after.primaryFaultCode ||
             before.activeFaultDisplayCount != after.activeFaultDisplayCount ||
             before.faultNotificationSequence != after.faultNotificationSequence;

    case View::WifiPortal:
      // Trang thai mang/portal la thu duy nhat dong tren man hinh nay.
      return before.netState != after.netState ||
             before.portalState != after.portalState;

    case View::MainMenu:
    case View::GeneralMenu:
    case View::SettingList:
    case View::EditSetting:
      return false;
  }
  return false;
}

void applyRuntime(MachineRuntime runtime) {
  sanitizeRuntime(runtime);
  const AutoTuneState previousAutoTuneState = currentRuntime.autoTuneState;
  const bool newFaultOccurrence =
      runtime.faultNotificationSequence != currentRuntime.faultNotificationSequence &&
      runtime.lastRaisedFaultCode != 0U;
  const uint32_t newFaultAlarmBit = newFaultOccurrence
      ? alarmBitForFaultCode(runtime.lastRaisedFaultCode) : AlarmNone;
  const uint32_t newAlarmBits = runtime.alarmMask & ~alarmPresentedMask;
  alarmPresentedMask &= runtime.alarmMask;

  const bool visibleChange = runtimeVisibleChanged(currentRuntime, runtime);
  currentRuntime = runtime;
  buzzer.acknowledgedAlarmMask &= currentRuntime.alarmMask;
  // Moi ma loi moi deu duoc keu lai, ke ca no dung chung AlarmBit voi mot loi
  // cu da duoc nguoi dung ACK truoc do.
  if (newFaultAlarmBit != AlarmNone) buzzerUnacknowledge(newFaultAlarmBit);

  if (currentRuntime.autoTuneState != previousAutoTuneState) {
    if (currentRuntime.autoTuneState == AutoTuneState::Running) {
      showToast("AUTO TUNE DA BAT DAU");
    } else if (currentRuntime.autoTuneState == AutoTuneState::Success) {
      showToast("AUTO TUNE THANH CONG");
      buzzerPlayCue(BuzzerCue::Ok);
    } else if (currentRuntime.autoTuneState == AutoTuneState::Failed) {
      showToast("AUTO TUNE THAT BAI", true);
      buzzerPlayCue(BuzzerCue::Error);
    }
  }

  if (!currentRuntime.resumeConfirmationRequired) resumeDecisionSubmitted = false;

  if (newFaultOccurrence || newAlarmBits) {
    alarmPresentedMask |= newAlarmBits | newFaultAlarmBit;
    if (view != View::Alarm) alarmReturnView = view;
    alarmIndex = 0;
    view = View::Alarm;
    dirty = true;
  } else if (!runtime.alarmMask && view == View::Alarm) {
    view = alarmReturnView;
    dirty = true;
  }

  // Sau khi xu ly man hinh loi, neu day la khoi dong lai sau mat dien thi
  // luon dua nguoi dung den man hinh xac nhan. Khong de trang Alarm cu che
  // mat yeu cau tiep tuc/huy me.
  if (currentRuntime.resumeConfirmationRequired && !resumeDecisionSubmitted &&
      confirmAction != ConfirmAction::ResumeBatch && view != View::Alarm) {
    openResumeConfirm();
  }
  if (visibleChange) dirty = true;
}

void applyHostConfig(MachineConfig config) {
  sanitizeConfig(config);
  portENTER_CRITICAL(&hmiApiMux);
  configSave.active = false;
  configSave.readyForHost = false;
  portEXIT_CRITICAL(&hmiApiMux);
  currentConfig = config;
  if (view == View::EditSetting || view == View::SettingList ||
      view == View::TurnStats || view == View::WifiPortal) {
    returnToGroupOwner();
    showToast("CAU HINH DA DONG BO");
  }
  dirty = true;
}

void processConfigAck(const ConfigAckInbox &ack) {
  MachineConfig rollback;
  MachineConfig accepted;
  bool matched = false;
  portENTER_CRITICAL(&hmiApiMux);
  if (configSave.active && configSave.id == ack.transactionId) {
    rollback = configSave.rollback;
    accepted = ack.hasStoredConfig ? ack.storedConfig : configSave.candidate;
    configSave.active = false;
    configSave.readyForHost = false;
    matched = true;
  }
  portEXIT_CRITICAL(&hmiApiMux);
  if (!matched) return;

  if (ack.ok) {
    sanitizeConfig(accepted);
    currentConfig = accepted;
      showToast("DA LUU CAU HINH");
    buzzerPlayCue(BuzzerCue::Save);
  } else {
    currentConfig = rollback;
      showToast("LOI LUU - DA HOAN TAC", true);
    buzzerPlayCue(BuzzerCue::Error);
  }
  dirty = true;
}

void processCommandAcks() {
  for (uint8_t budget = 0U; budget < COMMAND_ACK_QUEUE_SIZE; ++budget) {
    CommandAck ack;
    HmiCommand command;
    bool haveAck = false;
    bool matched = false;
    portENTER_CRITICAL(&hmiApiMux);
    if (commandAckCount) {
      ack = commandAckQueue[commandAckHead];
      commandAckHead = static_cast<uint8_t>(
          (commandAckHead + 1U) % COMMAND_ACK_QUEUE_SIZE);
      --commandAckCount;
      haveAck = true;
      for (uint8_t i = 0; i < COMMAND_QUEUE_SIZE; ++i) {
        if (activeCommands[i].used &&
            activeCommands[i].command.id == ack.commandId) {
          command = activeCommands[i].command;
          activeCommands[i].used = false;
          if (commandOutstandingCount) --commandOutstandingCount;
          matched = true;
          break;
        }
      }
    }
    portEXIT_CRITICAL(&hmiApiMux);
    if (!haveAck) break;
    if (!matched) continue;

    if (command.type == HmiCommandType::AlarmAck) {
      if (!ack.ok) buzzerUnacknowledge(command.alarmMask);
    } else {
      buzzerPlayCue(ack.ok ? BuzzerCue::Ok : BuzzerCue::Error);
    }
    showToast(ack.message[0] ? ack.message :
              (ack.ok ? "LENH DA THUC HIEN" : "LENH BI TU CHOI"), !ack.ok);
  }
}

void serviceApiMailboxes() {
  // hmiUpdate() co the duoc goi rat nhanh. Khong vao critical section neu
  // firmware tong khong gui du lieu/ACK moi.
  if (!apiHasPendingWork()) return;

  // Scratch tinh: HMI chi cho phep mot task goi hmiUpdate(), tranh khoi tao
  // lai MachineConfig/MachineRuntime lon tren stack o moi vong loop.
  static MachineRuntime runtime;
  static MachineConfig config;
  static HmiEventSnapshot eventLog;
  static ConfigAckInbox configAck;
  static char date[11];
  bool hasRuntime = false;
  bool hasConfig = false;
  bool hasEventLog = false;
  bool hasConfigAck = false;
  bool hasDate = false;
  bool hasCue = false;
  BuzzerCue cue = BuzzerCue::None;

  portENTER_CRITICAL(&hmiApiMux);
  if (runtimeInboxPending) {
    runtime = runtimeInbox;
    runtimeInboxPending = false;
    hasRuntime = true;
  }
  if (configInboxPending) {
    config = configInbox;
    configInboxPending = false;
    hasConfig = true;
  }
  if (eventLogInboxPending) {
    eventLog = eventLogInbox;
    eventLogInboxPending = false;
    hasEventLog = true;
  }
  if (configAckInbox.pending) {
    configAck = configAckInbox;
    configAckInbox.pending = false;
    hasConfigAck = true;
  }
  if (dateInboxPending) {
    memcpy(date, dateInbox, sizeof(date));
    dateInboxPending = false;
    hasDate = true;
  }
  if (cueInboxPending) {
    cue = cueInbox;
    cueInboxPending = false;
    hasCue = true;
  }
  portEXIT_CRITICAL(&hmiApiMux);

  if (hasConfigAck) processConfigAck(configAck);
  if (hasConfig) applyHostConfig(config);
  if (hasRuntime) applyRuntime(runtime);
  if (hasEventLog) {
    currentEventLog = eventLog;
    if (eventLogIndex >= currentEventLog.count) eventLogIndex = 0U;
    if (view == View::EventLog) dirty = true;
  }
  if (hasCue) buzzerPlayCue(cue);
  if (hasDate &&
      strncmp(date, currentRuntime.dateText, sizeof(currentRuntime.dateText)) != 0) {
    memcpy(currentRuntime.dateText, date, sizeof(currentRuntime.dateText));
    currentRuntime.dateText[sizeof(currentRuntime.dateText) - 1U] = '\0';
    dirty = true;
  }
  processCommandAcks();

  portENTER_CRITICAL(&hmiApiMux);
  const bool stillPending = runtimeInboxPending || configInboxPending ||
                            eventLogInboxPending || configAckInbox.pending ||
                            dateInboxPending || cueInboxPending ||
                            commandAckCount != 0;
  setApiWorkPending(stillPending);
  portEXIT_CRITICAL(&hmiApiMux);
}

void serviceCommandTimeouts(uint32_t now) {
  uint32_t alarmMaskToUnack = AlarmNone;
  bool anyTimeout = false;
  portENTER_CRITICAL(&hmiApiMux);

  HmiCommand retained[COMMAND_QUEUE_SIZE];
  uint8_t retainedCount = 0;
  while (commandCount) {
    const HmiCommand command = commandQueue[commandHead];
    commandHead = static_cast<uint8_t>((commandHead + 1U) % COMMAND_QUEUE_SIZE);
    --commandCount;
    if (now - command.createdAt >= command.validForMs) {
      if (commandOutstandingCount) --commandOutstandingCount;
      if (command.type == HmiCommandType::AlarmAck) {
        alarmMaskToUnack |= command.alarmMask;
      }
      anyTimeout = true;
    } else {
      retained[retainedCount++] = command;
    }
  }
  commandHead = 0;
  commandTail = retainedCount % COMMAND_QUEUE_SIZE;
  commandCount = retainedCount;
  for (uint8_t i = 0; i < retainedCount; ++i) commandQueue[i] = retained[i];

  for (uint8_t i = 0; i < COMMAND_QUEUE_SIZE; ++i) {
    if (!activeCommands[i].used) continue;
    if (now - activeCommands[i].takenAt < COMMAND_CONFIRM_TIMEOUT_MS) {
      continue;
    }
    if (activeCommands[i].command.type == HmiCommandType::AlarmAck) {
      alarmMaskToUnack |= activeCommands[i].command.alarmMask;
    }
    activeCommands[i].used = false;
    if (commandOutstandingCount) --commandOutstandingCount;
    anyTimeout = true;
  }
  if (expiredAlarmAckMask) anyTimeout = true;
  alarmMaskToUnack |= expiredAlarmAckMask;
  expiredAlarmAckMask = AlarmNone;
  portEXIT_CRITICAL(&hmiApiMux);

  if (alarmMaskToUnack) buzzerUnacknowledge(alarmMaskToUnack);
  if (anyTimeout) {
    showToast("LENH QUA THOI GIAN", true);
    buzzerPlayCue(BuzzerCue::Error);
  }
}

void serviceConfigSaveTimeout(uint32_t now) {
  MachineConfig rollback;
  bool timedOut = false;
  portENTER_CRITICAL(&hmiApiMux);
  if (configSave.active &&
      now - configSave.startedAt >= SAVE_CONFIRM_TIMEOUT_MS) {
    rollback = configSave.rollback;
    configSave.active = false;
    configSave.readyForHost = false;
    timedOut = true;
  }
  portEXIT_CRITICAL(&hmiApiMux);
  if (!timedOut) return;
  currentConfig = rollback;
  showToast("LUU QUA THOI GIAN - DA HOAN TAC", true);
  buzzerPlayCue(BuzzerCue::Error);
}

void hmiUpdate(uint32_t now) {
  serviceApiMailboxes();
  if (now - lastCommandPollAt >= HMI_COMMAND_POLL_MS) {
    lastCommandPollAt = now;
    serviceCommandTimeouts(now);
    serviceConfigSaveTimeout(now);
  }

  updateRotary(now);
  if (rotary.step || rotary.button != ButtonEvent::None) {
    lastInteractionAt = now;
  }
  handleInput();

  if (view != View::Home && view != View::Alarm &&
      now - lastInteractionAt >= MENU_IDLE_TIMEOUT_MS) {
    view = View::Home;
    homePage = 0;
    showToast("TU DONG VE MAN HINH CHINH");
    lastInteractionAt = now;
  }

  buzzerUpdate(now);
  if (toastText[0] && timeReached(now, toastUntil)) {
    toastText[0] = '\0';
    toastLineCount = 0;
    toastCompact = false;
    dirty = true;
  }
  serviceLcd(now);
  render(now);
}

void hmiSetConfig(const MachineConfig &config) {
  portENTER_CRITICAL(&hmiApiMux);
  configInbox = config;
  configInboxPending = true;
  markApiWorkPending();
  portEXIT_CRITICAL(&hmiApiMux);
}

void hmiSetDate(const char *dateText) {
  if (!dateText || !dateText[0]) dateText = "--/--/----";
  portENTER_CRITICAL(&hmiApiMux);
  snprintf(dateInbox, sizeof(dateInbox), "%.10s", dateText);
  dateInboxPending = true;
  markApiWorkPending();
  portEXIT_CRITICAL(&hmiApiMux);
}

const MachineConfig &hmiGetConfig() { return currentConfig; }

void hmiSetRuntime(const MachineRuntime &runtime) {
  portENTER_CRITICAL(&hmiApiMux);
  const uint32_t accumulatedAlarmMask =
      runtimeInboxPending ? runtimeInbox.alarmMask : AlarmNone;
  runtimeInbox = runtime;
  runtimeInbox.alarmMask |= accumulatedAlarmMask;
  runtimeInboxPending = true;
  markApiWorkPending();
  portEXIT_CRITICAL(&hmiApiMux);
}

void hmiSetEventLog(const HmiEventSnapshot &snapshot) {
  portENTER_CRITICAL(&hmiApiMux);
  eventLogInbox = snapshot;
  eventLogInboxPending = true;
  markApiWorkPending();
  portEXIT_CRITICAL(&hmiApiMux);
}

bool hmiTakeSavedConfig(MachineConfig &out, uint32_t &transactionId) {
  bool available = false;
  portENTER_CRITICAL(&hmiApiMux);
  if (configSave.active && configSave.readyForHost) {
    out = configSave.candidate;
    transactionId = configSave.id;
    configSave.readyForHost = false;
    available = true;
  }
  portEXIT_CRITICAL(&hmiApiMux);
  return available;
}

bool hmiConfirmConfigSave(uint32_t transactionId, bool ok,
                          const MachineConfig *storedConfig) {
  bool accepted = false;
  portENTER_CRITICAL(&hmiApiMux);
  if (!configAckInbox.pending && configSave.active &&
      configSave.id == transactionId) {
    configAckInbox.pending = true;
    configAckInbox.transactionId = transactionId;
    configAckInbox.ok = ok;
    configAckInbox.hasStoredConfig = storedConfig != nullptr;
    if (storedConfig) configAckInbox.storedConfig = *storedConfig;
    markApiWorkPending();
    accepted = true;
  }
  portEXIT_CRITICAL(&hmiApiMux);
  return accepted;
}

bool hmiTakeCommand(HmiCommand &out) {
  const uint32_t now = millis();
  bool available = false;
  portENTER_CRITICAL(&hmiApiMux);
  while (commandCount && !available) {
    const HmiCommand command = commandQueue[commandHead];
    commandHead = static_cast<uint8_t>((commandHead + 1U) % COMMAND_QUEUE_SIZE);
    --commandCount;
    if (now - command.createdAt >= command.validForMs) {
      if (commandOutstandingCount) --commandOutstandingCount;
      if (command.type == HmiCommandType::AlarmAck) {
        expiredAlarmAckMask |= command.alarmMask;
      }
      continue;
    }
    for (uint8_t i = 0; i < COMMAND_QUEUE_SIZE; ++i) {
      if (activeCommands[i].used) continue;
      activeCommands[i].used = true;
      activeCommands[i].takenAt = now;
      activeCommands[i].command = command;
      out = command;
      available = true;
      break;
    }
    if (!available) {
      if (commandOutstandingCount) --commandOutstandingCount;
      if (command.type == HmiCommandType::AlarmAck) {
        expiredAlarmAckMask |= command.alarmMask;
      }
    }
  }
  portEXIT_CRITICAL(&hmiApiMux);
  return available;
}

bool hmiConfirmCommand(uint32_t commandId, bool ok, const char *message) {
  bool exists = false;
  bool accepted = false;
  portENTER_CRITICAL(&hmiApiMux);
  for (uint8_t i = 0; i < COMMAND_QUEUE_SIZE; ++i) {
    if (activeCommands[i].used &&
        activeCommands[i].command.id == commandId) {
      exists = true;
      break;
    }
  }
  if (exists && commandAckCount < COMMAND_ACK_QUEUE_SIZE) {
    CommandAck &ack = commandAckQueue[commandAckTail];
    ack.commandId = commandId;
    ack.ok = ok;
    snprintf(ack.message, sizeof(ack.message), "%s", message ? message : "");
    commandAckTail = static_cast<uint8_t>(
        (commandAckTail + 1U) % COMMAND_ACK_QUEUE_SIZE);
    ++commandAckCount;
    markApiWorkPending();
    accepted = true;
  }
  portEXIT_CRITICAL(&hmiApiMux);
  return accepted;
}

void hmiSetI2cLockCallbacks(HmiI2cLockFn lockFn,
                            HmiI2cUnlockFn unlockFn) {
  portENTER_CRITICAL(&hmiApiMux);
  if (lockFn && unlockFn) {
    i2cLockCallback = lockFn;
    i2cUnlockCallback = unlockFn;
  } else {
    i2cLockCallback = nullptr;
    i2cUnlockCallback = nullptr;
  }
  portEXIT_CRITICAL(&hmiApiMux);
}

void hmiPlayCue(BuzzerCue cue) {
  portENTER_CRITICAL(&hmiApiMux);
  cueInbox = cue;
  cueInboxPending = true;
  markApiWorkPending();
  portEXIT_CRITICAL(&hmiApiMux);
}
