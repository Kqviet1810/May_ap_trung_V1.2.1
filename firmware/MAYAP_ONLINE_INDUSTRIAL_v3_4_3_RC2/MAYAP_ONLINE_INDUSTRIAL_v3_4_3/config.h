#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <stddef.h>
#include <math.h>

// ============================================================================
// MAY AP TRUNG ONLINE INDUSTRIAL v3.4.2 TEST - CAU HINH DUY NHAT CAN SUA
// MCU: ESP32-S3-WROOM-1U-N8 (8 MB Quad Flash, khong PSRAM)
// ============================================================================

constexpr char MAYAP_FIRMWARE_VERSION[] = "3.4.3-RC2";
constexpr char MAYAP_HARDWARE_REVISION[] = "CTRL-S3-N8-R1";
constexpr char HMI_FIRMWARE_VERSION[] = "3.4.3-RC2";
constexpr char HMI_HARDWARE_REVISION[] = "HMI-S3-R2";

// ----------------------------- BUILD -----------------------------------------
// 1: mo phong 8 input bang Serial. 0: doc input 12 V that qua opto.
#ifndef MAYAP_SERIAL_INPUT_SIM
#define MAYAP_SERIAL_INPUT_SIM 0
#endif

// Log chan doan khong tham gia dieu khien. Nen de 0 o ban giao thuong mai.
#ifndef MAYAP_DIAGNOSTIC_SERIAL
#define MAYAP_DIAGNOSTIC_SERIAL 1
#endif

// HMI chi duoc bien dich trong firmware tong; da loai bo demo doc lap.
#define MAYAP_HMI_OWNS_I2C_BUS 0
#define MAYAP_HMI_ENCODER_INTERRUPT 1

// ----------------------------- GPIO ------------------------------------------
// Output HIGH = ON.
constexpr uint8_t PIN_OUT_HEATER_SSR   = 1;   // KAO3400 - SSR thanh nhiet
constexpr uint8_t PIN_OUT_PULSE_SPARE  = 2;   // KAO3400 du phong
constexpr uint8_t PIN_OUT_TURN_RIGHT   = 10;
constexpr uint8_t PIN_OUT_TURN_LEFT    = 11;
constexpr uint8_t PIN_OUT_VENT_FAN     = 12;
constexpr uint8_t PIN_OUT_LIGHT        = 13;
constexpr uint8_t PIN_OUT_HEAT_MASTER  = 14;
constexpr uint8_t PIN_OUT_CIRC_FAN     = 21;
constexpr uint8_t PIN_OUT_SIREN        = 47;
constexpr uint8_t PIN_OUT_RELAY_SPARE  = 48;
constexpr uint8_t PIN_STATUS_RGB       = 42;  // SK6812MINI-C

// Input opto ACTIVE-LOW: kich 12 V => ngo ra opto keo GPIO xuong GND.
constexpr uint8_t PIN_IN_LIMIT_LEFT    = 4;
constexpr uint8_t PIN_IN_LIMIT_RIGHT   = 5;
constexpr uint8_t PIN_IN_AUTO          = 6;
constexpr uint8_t PIN_IN_HEATER_ENABLE = 7;
constexpr uint8_t PIN_IN_CIRC_FAN      = 15;
constexpr uint8_t PIN_IN_LIGHT         = 16;
constexpr uint8_t PIN_IN_TURN_LEFT     = 17;
constexpr uint8_t PIN_IN_TURN_RIGHT    = 18;  // da doi tu GPIO8 de tranh SDA

constexpr bool INPUT_ACTIVE_LOW = true;
constexpr bool OUTPUT_ACTIVE_HIGH = true;

// MAX3485 / Modbus RTU.
constexpr uint8_t SHT_UART_PORT = 1;
constexpr uint8_t PIN_RS485_RX = 37;     // RO
constexpr uint8_t PIN_RS485_DE_RE = 36;  // DE + /RE
constexpr uint8_t PIN_RS485_TX = 35;     // DI

// HMI ST7567 + rotary + coi phu duy nhat. GPIO41 bao phim, trang thai, dao va loi.
// GPIO47 chi danh cho coi lon qua nhiet cap cao nhat.
constexpr uint8_t LCD_I2C_ADDRESS = 0x3F;
constexpr uint8_t PIN_I2C_SDA = 8;
constexpr uint8_t PIN_I2C_SCL = 9;
constexpr uint8_t PIN_ENCODER_CLK = 38;
constexpr uint8_t PIN_ENCODER_DT  = 39;
constexpr uint8_t PIN_ENCODER_SW  = 40;
constexpr uint8_t PIN_BUZZER      = 41;
constexpr bool BUZZER_ACTIVE_HIGH = true;

// Kiem tra toan bo GPIO tai compile-time.
constexpr uint8_t MAYAP_USED_PINS[] = {
  PIN_OUT_HEATER_SSR, PIN_OUT_PULSE_SPARE, PIN_OUT_TURN_RIGHT,
  PIN_OUT_TURN_LEFT, PIN_OUT_VENT_FAN, PIN_OUT_LIGHT,
  PIN_OUT_HEAT_MASTER, PIN_OUT_CIRC_FAN, PIN_OUT_SIREN,
  PIN_OUT_RELAY_SPARE, PIN_STATUS_RGB,
  PIN_IN_LIMIT_LEFT, PIN_IN_LIMIT_RIGHT, PIN_IN_AUTO,
  PIN_IN_HEATER_ENABLE, PIN_IN_CIRC_FAN, PIN_IN_LIGHT,
  PIN_IN_TURN_LEFT, PIN_IN_TURN_RIGHT,
  PIN_RS485_RX, PIN_RS485_DE_RE, PIN_RS485_TX,
  PIN_I2C_SDA, PIN_I2C_SCL, PIN_ENCODER_CLK, PIN_ENCODER_DT,
  PIN_ENCODER_SW, PIN_BUZZER
};

constexpr bool mayapPinsValidAndUnique() {
  constexpr size_t count = sizeof(MAYAP_USED_PINS) / sizeof(MAYAP_USED_PINS[0]);
  for (size_t i = 0; i < count; ++i) {
    if (MAYAP_USED_PINS[i] > 48U) return false;
    for (size_t j = i + 1; j < count; ++j) {
      if (MAYAP_USED_PINS[i] == MAYAP_USED_PINS[j]) return false;
    }
  }
  return true;
}
static_assert(mayapPinsValidAndUnique(), "MAYAP: GPIO trung nhau/ngoai pham vi");

// ----------------------------- HMI -------------------------------------------
constexpr uint8_t HMI_MAX_VALID_GPIO = 48;
constexpr uint8_t HMI_USED_PINS[] = {
  PIN_I2C_SDA, PIN_I2C_SCL, PIN_ENCODER_CLK, PIN_ENCODER_DT,
  PIN_ENCODER_SW, PIN_BUZZER
};
constexpr bool hmiPinsAreValidAndUnique() {
  constexpr size_t count = sizeof(HMI_USED_PINS) / sizeof(HMI_USED_PINS[0]);
  for (size_t i = 0; i < count; ++i) {
    if (HMI_USED_PINS[i] > HMI_MAX_VALID_GPIO) return false;
    for (size_t j = i + 1; j < count; ++j) {
      if (HMI_USED_PINS[i] == HMI_USED_PINS[j]) return false;
    }
  }
  return true;
}
static_assert(hmiPinsAreValidAndUnique(), "HMI: GPIO trung nhau/ngoai pham vi");

// DS3231 va AT24C32 deu ho tro Fast-mode 400 kHz. Toc do nay rut ngan
// truyen full-buffer LCD 128x64, giam ro hien tuong ve xong tung manh khi vao menu.
constexpr uint32_t I2C_CLOCK_HZ = 400000UL;
constexpr uint16_t I2C_TIMEOUT_MS = 25;          // timeout phan cung moi giao dich
constexpr uint8_t DEFAULT_CONTRAST = 230;
constexpr bool REVERSE_ENCODER = false;
constexpr uint32_t LCD_RETRY_INTERVAL_MS = 3000UL;
constexpr uint32_t LCD_HEALTH_CHECK_MS = 5000UL;
constexpr uint32_t LCD_FAULT_LOG_INTERVAL_MS = 30000UL;
#ifndef LCD_PROFILE
#define LCD_PROFILE 1
#endif
constexpr uint8_t ENCODER_STEPS_PER_DETENT = 4;
constexpr uint8_t ENCODER_MAX_STEPS_PER_UPDATE = 3;
constexpr uint32_t BUTTON_DEBOUNCE_MS = 30UL;
constexpr uint32_t BUTTON_LONG_PRESS_MS = 900UL;
// Full-buffer 128x64 duoc ve xong trong RAM, sau do gui mot lan bang sendBuffer().
// Khong tat/bat panel khi doi trang vi se tao chop den kho chiu.
constexpr uint32_t DISPLAY_MIN_DRAW_MS = 30UL;
constexpr uint8_t LCD_I2C_ERROR_THRESHOLD = 3U;
constexpr uint32_t I2C_RECOVERY_COOLDOWN_MS = 250UL;
constexpr uint8_t I2C_RECOVERY_MAX_ATTEMPTS = 3U;
constexpr uint32_t I2C_RECOVERY_RETRY_MS = 3000UL;
constexpr uint32_t HOME_REFRESH_MS = 5000UL;
constexpr uint32_t ALARM_REFRESH_MS = 5000UL;
constexpr uint32_t HMI_COMMAND_POLL_MS = 100UL;
constexpr uint32_t MENU_IDLE_TIMEOUT_MS = 60000UL;
constexpr uint32_t SAVE_CONFIRM_TIMEOUT_MS = 8000UL;
constexpr uint32_t COMMAND_CONFIRM_TIMEOUT_MS = 8000UL;
constexpr uint16_t COMMAND_DEFAULT_VALID_MS = 5000U;
constexpr uint16_t COMMAND_AUTOTUNE_VALID_MS = 5000U;
constexpr uint32_t TOAST_INFO_MS = 2600UL;
constexpr uint32_t TOAST_ERROR_MS = 5000UL;
constexpr uint8_t COMMAND_QUEUE_SIZE = 4;
constexpr uint8_t COMMAND_ACK_QUEUE_SIZE = 4;
constexpr uint32_t EMERGENCY_RESOUND_MS = 60000UL;
constexpr uint32_t CRITICAL_RESOUND_MS = 300000UL;
// Nhac nguoi van hanh xac nhan phuc hoi me sau mat dien. Bat buoc keu,
// khong bi vo hieu boi tuy chon tat coi nhac nho.
constexpr uint32_t RESUME_PROMPT_ON_MS = 220UL;
constexpr uint32_t RESUME_PROMPT_OFF_MS = 780UL;

// --------------------- CAU HINH LOGIC DE CHINH -------------------------------
// Huong tim goc khi khay nam giua. true = trai; false = phai.
constexpr bool HOME_TO_LEFT = true;

// Quyen cho phep nhiet ngoai me nam trong MachineConfig va chinh tren LCD.
// Mac dinh false; du bat van bat buoc quat tuan hoan da chay on dinh.
// Neu muon bat buoc cong tac nhiet ON moi duoc START, doi thanh true.
constexpr bool REQUIRE_HEATER_ENABLE_TO_START = false;

// MANUAL chi trao quyen quat tuan hoan va dao cho cong tac cung.
// Tat quat trong MANUAL se khoa ca D1 va D14.
constexpr bool MANUAL_FAN_CAN_DISABLE_HEATING = true;

// WDT/panic/software reset: tu phuc hoi me neu snapshot EEPROM hop le.
// POWERON/BROWNOUT: bat buoc hoi nguoi van hanh co tiep tuc me hay khong.
constexpr uint8_t RESET_STORM_LIMIT = 3U;
constexpr uint32_t RESET_STORM_STABLE_CLEAR_MS = 600000UL; // 10 phut chay on dinh xoa dem

// Cac moc thoi gian an toan.
constexpr uint32_t INPUT_SCAN_MS = 5UL;
constexpr uint32_t INPUT_DEBOUNCE_MS = 30UL;
constexpr uint32_t LIMIT_DEBOUNCE_MS = 20UL;
constexpr uint32_t FAN_PRESTART_MS = 5000UL;
constexpr uint32_t HEAT_MASTER_PICKUP_MS = 500UL;
constexpr uint32_t HEAT_MASTER_DROP_DELAY_MS = 120UL; // tat SSR truoc, roi nha contactor
constexpr uint32_t HEAT_RESTART_LOCKOUT_MS = 30000UL;
constexpr uint32_t POST_COOL_MS = 60000UL;
constexpr uint32_t TURN_DIRECTION_DEADTIME_MS = 500UL;
constexpr uint32_t TURN_LIMIT_RELEASE_TIMEOUT_MS = 2500UL;
constexpr uint32_t TURN_INPUT_CONFLICT_MS = 500UL;
constexpr uint32_t SENSOR_RECOVERY_GOOD_SAMPLES = 3UL;
constexpr uint32_t SENSOR_STARTUP_GRACE_MS = 20000UL; // chua bao coi trong 20 s dau
// Buoc nhay giam dot ngot nguy hiem vi co the lam PID tang cong suat sai.
// Buoc nhay tang van duoc dua ngay vao nhanh bao ve qua nhiet.
constexpr float SENSOR_MAX_DOWN_STEP_C = 1.5f;
constexpr uint8_t SENSOR_PLAUSIBILITY_CLEAR_SAMPLES = 3U;
constexpr uint32_t LOW_TEMP_STARTUP_GRACE_MS = 300000UL; // 5 phut
constexpr uint32_t LOW_TEMP_CONFIRM_MS = 300000UL;       // thap lien tuc 5 phut
constexpr uint32_t HIGH_TEMP_CONFIRM_MS = 1000UL;
constexpr float HIGH_TEMP_CLEAR_HYSTERESIS_C = 0.2f;
constexpr uint32_t HIGH_TEMP_CLEAR_CONFIRM_MS = 10000UL;
constexpr float EMERGENCY_CLEAR_HYSTERESIS_C = 0.3f;
constexpr uint32_t EMERGENCY_CLEAR_CONFIRM_MS = 30000UL;
// --------- BAO VE THANH NHIET (khong can them phan cung) --------------------
// 1) RUNAWAY: SSR dinh/chap. Thanh nhiet da tat hoan toan ma PV van tang.
//    Cua so 180 s va 1.0 C: mot buong ap dong kin, quat tuan hoan chay, khong
//    cap nhiet thi khong the tang 1 C trong 3 phut. Nguong nay tranh false
//    positive khi nguoi van hanh vua dong cua sau khi mo.
constexpr uint32_t HEATER_RUNAWAY_WINDOW_MS = 180000UL;
constexpr float HEATER_RUNAWAY_RISE_C = 1.0f;
// 2) NO RESPONSE: dut dien tro, hong contactor, mat pha. Cong suat gan toi da
//    suot 15 phut ma PV gan nhu dung yen. Khoi dong nguoi tu 20 C len 37.5 C
//    van tang hon 0.3 C trong 15 phut, nen nguong nay an toan.
constexpr uint32_t HEATER_STALL_WINDOW_MS = 900000UL;
constexpr float HEATER_STALL_MIN_POWER = 90.0f;
constexpr float HEATER_STALL_RISE_C = 0.3f;
// 3) SENSOR STUCK: cam bien treo o mot gia tri. Yeu cau CA nhiet do VA do am
//    giong het nhau o muc dem tho suot 30 phut. Cam bien song luon dao dong
//    chu so cuoi, dac biet la do am, nen thuc te khong the false positive.
constexpr uint32_t SENSOR_STUCK_TIMEOUT_MS = 1800000UL;

constexpr uint32_t SIREN_TEMPORARY_MUTE_MS = 300000UL;
constexpr uint32_t BATCH_CHECKPOINT_MS = 300000UL;
constexpr uint32_t RUNTIME_TO_HMI_MS = 200UL;
constexpr uint32_t DIAGNOSTIC_STATUS_MS = 10000UL;
constexpr bool SERIAL_DEBUG_DEFAULT_ON = false;
constexpr uint32_t CONTROL_TASK_PERIOD_MS = 5UL;
constexpr uint32_t HMI_TASK_PERIOD_MS = 5UL;
constexpr uint32_t SUPERVISOR_TASK_PERIOD_MS = 50UL;
// ESP-IDF tinh stack task theo byte. Hai buffer nay cap phat tinh, khong phan manh heap.
constexpr size_t CONTROL_TASK_STACK_BYTES = 8192U;
constexpr size_t HMI_TASK_STACK_BYTES = 10240U;
constexpr size_t SUPERVISOR_TASK_STACK_BYTES = 4096U;
constexpr uint32_t TASK_STACK_MONITOR_MS = 30000UL;
// 5 s: du bien cho giao dich I2C huu han nhung van phat hien task bi treo.
constexpr uint32_t CONTROL_WDT_TIMEOUT_MS = 5000UL;
constexpr uint32_t CONTROL_HEARTBEAT_TIMEOUT_MS = 500UL;
constexpr uint32_t HMI_HEARTBEAT_TIMEOUT_MS = 2000UL;
constexpr uint32_t SUPERVISOR_RESTART_FALLBACK_MS = 7000UL; // TWDT 5 s duoc uu tien; day la fallback

// Deadline/jitter cua Control Task. Sau khi tach I2C, chu ky dieu khien phai
// ngan va xac dinh; qua nguong lap lai se hard-trip, khong chi ghi log.
constexpr uint32_t CONTROL_CYCLE_WARN_US = 20000UL;
constexpr uint32_t CONTROL_CYCLE_TRIP_US = 100000UL;
constexpr uint8_t CONTROL_CYCLE_TRIP_CONSECUTIVE = 3U;
constexpr uint32_t KERNEL_HEALTH_CHECK_MS = 30000UL;
constexpr uint32_t HEAP_INTEGRITY_CHECK_MS = 60000UL;
constexpr size_t MIN_FREE_INTERNAL_HEAP_BYTES = 40000U;
constexpr UBaseType_t CONTROL_STACK_MIN_BYTES = 1400U;
constexpr UBaseType_t HMI_STACK_MIN_BYTES = 1400U;
constexpr UBaseType_t SUPERVISOR_STACK_MIN_BYTES = 900U;

// ----------------------- KERNEL / EVENT -------------------------------------
constexpr uint8_t INPUT_EVENT_QUEUE_SIZE = 16U;
constexpr uint8_t OUTPUT_EVENT_QUEUE_SIZE = 16U;
constexpr uint8_t EVENT_LOG_RAM_SIZE = 64U;
// HMI chi nhan toi da 24 su kien moi nhat trong cua so 1 gio.
// Bo nho tinh nho, khong ghi EEPROM lien tuc nen khong gay hao mon.
constexpr uint8_t HMI_EVENT_DISPLAY_CAPACITY = 24U;
constexpr uint8_t HMI_FAULT_DISPLAY_CAPACITY = 16U;
constexpr uint32_t HMI_EVENT_WINDOW_SEC = 3600UL;
constexpr uint32_t RELAY_GENERAL_MIN_SWITCH_MS = 250UL;
constexpr uint32_t RELAY_FAN_MIN_ON_MS = 2000UL;
constexpr uint32_t RELAY_LIGHT_MIN_SWITCH_MS = 150UL;
constexpr uint32_t DIAGNOSTIC_FAST_STATUS_MS = 1000UL;
constexpr uint16_t MAX_RELAY_TRANSITIONS_PER_HOUR = 1800U;

// SSR zero-cross: cua so cham, co xung toi thieu de tranh dap lien tuc.
constexpr uint32_t SSR_MIN_ON_MS = 300UL;
constexpr uint32_t SSR_MIN_OFF_MS = 300UL;

// Auto Tune relay co gioi han, khong chay khi dang co me.
constexpr uint8_t AUTOTUNE_RELAY_POWER_PERCENT = 30;
constexpr float AUTOTUNE_BAND_C = 0.20f;
constexpr uint8_t AUTOTUNE_REQUIRED_CYCLES = 3;
constexpr uint32_t AUTOTUNE_MAX_MS = 2700000UL; // 45 phut
constexpr uint32_t AUTOTUNE_PHASE_MAX_MS = 900000UL; // moi pha toi da 15 phut
constexpr uint32_t AUTOTUNE_MIN_PERIOD_MS = 10000UL;
constexpr float AUTOTUNE_MIN_AMPLITUDE_C = 0.10f;

// LED RGB - do sang thap de khong nong/khong choi trong tu dien.
constexpr uint8_t RGB_BRIGHTNESS_LOW = 6;
constexpr uint8_t RGB_BRIGHTNESS_NORMAL = 14;
constexpr uint8_t RGB_BRIGHTNESS_ALARM = 30;

// Khong gia mao ngay thuc. Chi bat neu chap nhan hien ngay bien dich.
constexpr bool DISPLAY_BUILD_DATE_WHEN_RTC_MISSING = false;

// ----------------------- DS3231 + AT24C32 -----------------------------------
// Dia chi co dinh de code gon va xac dinh. Hay quet I2C mot lan roi sua
// EEPROM_I2C_ADDRESS neu module cua ban khong phai 0x57.
constexpr bool EXTERNAL_EEPROM_ENABLED = true;
constexpr bool EXTERNAL_EEPROM_REQUIRED = true;
constexpr uint8_t RTC_I2C_ADDRESS = 0x68U;
constexpr uint8_t EEPROM_I2C_ADDRESS = 0x57U;
constexpr uint16_t EEPROM_CAPACITY_BYTES = 4096U;
constexpr uint8_t EEPROM_PAGE_SIZE = 32U;
constexpr uint16_t EEPROM_WRITE_TIMEOUT_MS = 20U;
constexpr uint32_t RTC_READ_PERIOD_MS = 1000UL;
constexpr uint32_t RTC_STUCK_TIMEOUT_MS = 4000UL;
constexpr uint16_t RTC_VALID_YEAR_MIN = 2024U;
constexpr uint16_t RTC_VALID_YEAR_MAX = 2099U;
constexpr uint32_t MAX_RTC_RECOVERY_GAP_SEC = 45UL * 86400UL;

// Ban do AT24C32, dia chi o nho 16-bit:
// Config A/B 256 byte; Batch A/B 128 byte; phan con lai du phong.
constexpr uint16_t EEPROM_ADDR_CONFIG_A = 0x0000U;
constexpr uint16_t EEPROM_ADDR_CONFIG_B = 0x0100U;
constexpr uint16_t EEPROM_ADDR_BATCH_A  = 0x0200U;
constexpr uint16_t EEPROM_ADDR_BATCH_B  = 0x0280U;
constexpr uint16_t EEPROM_CONFIG_SLOT_BYTES = 0x0100U;
constexpr uint16_t EEPROM_BATCH_SLOT_BYTES = 0x0080U;

static_assert(EEPROM_PAGE_SIZE == 32U, "AT24C32 page phai 32 byte");
static_assert(EEPROM_ADDR_CONFIG_A + EEPROM_CONFIG_SLOT_BYTES <= EEPROM_ADDR_CONFIG_B, "Config A de len B");
static_assert(EEPROM_ADDR_CONFIG_B + EEPROM_CONFIG_SLOT_BYTES <= EEPROM_ADDR_BATCH_A, "Config B de len Batch A");
static_assert(EEPROM_ADDR_BATCH_A + EEPROM_BATCH_SLOT_BYTES <= EEPROM_ADDR_BATCH_B, "Batch A de len B");
static_assert(EEPROM_ADDR_BATCH_B + EEPROM_BATCH_SLOT_BYTES <= EEPROM_CAPACITY_BYTES,
              "Ban do EEPROM vuot 4KB");

// -------------------- RANG BUOC HMI/AN TOAN ---------------------------------
constexpr float TARGET_TEMP_MIN_C = 30.0f;
constexpr float TARGET_TEMP_MAX_C = 40.0f;
constexpr float LOW_ALARM_GAP_C = 0.1f;
constexpr float HIGH_ALARM_GAP_C = 0.1f;
constexpr float EMERGENCY_ABOVE_HIGH_C = 0.1f;
constexpr float VENT_OFF_ABOVE_SV_C = 0.0f;
constexpr float VENT_ON_ABOVE_SV_C = 0.1f;
constexpr float VENT_HYSTERESIS_C = 0.1f;
constexpr float HIGH_ALARM_MAX_C = 42.0f;
constexpr float EMERGENCY_MAX_C = 45.0f;

// ============================================================================
// GIOI HAN CAU HINH - NGUON DUY NHAT CHO FIRMWARE, HMI VA WEB
// Truoc v3.4.3 moi giao dien tu dat gioi han rieng (vi du sensorTimeoutSec:
// HMI 2..120 s nhung firmware clamp 5..30 s). Tu day chi con MOT bang so.
// Bang SETTINGS cua HMI va mayapSanitizeConfig() deu tham chieu cac hang nay.
// ============================================================================
constexpr float CFG_HYSTERESIS_MIN = 0.1f;
constexpr float CFG_HYSTERESIS_MAX = 1.0f;
constexpr float CFG_LOW_ALARM_MIN_C = 25.0f;
constexpr float CFG_KP_MIN = 0.0f,  CFG_KP_MAX = 100.0f;
constexpr float CFG_KI_MIN = 0.0f,  CFG_KI_MAX = 20.0f;
constexpr float CFG_KD_MIN = 0.0f,  CFG_KD_MAX = 200.0f;
constexpr uint16_t CFG_PID_CYCLE_MIN_S = 1U,   CFG_PID_CYCLE_MAX_S = 60U;
constexpr uint8_t  CFG_MAX_POWER_MIN = 10U,    CFG_MAX_POWER_MAX = 100U;
constexpr float CFG_HUM_ALARM_MIN = 10.0f,     CFG_HUM_ALARM_MAX = 90.0f;
constexpr uint16_t CFG_HUM_DELAY_MIN_S = 0U,   CFG_HUM_DELAY_MAX_S = 600U;
constexpr uint16_t CFG_TURN_INTERVAL_MIN_M = 1U,  CFG_TURN_INTERVAL_MAX_M = 720U;
constexpr uint16_t CFG_TURN_RUN_MIN_S = 5U,       CFG_TURN_RUN_MAX_S = 600U;
constexpr uint8_t  CFG_DAYS_MIN = 1U,             CFG_DAYS_MAX = 40U;
constexpr uint16_t CFG_POWER_DELAY_MIN_S = 0U,    CFG_POWER_DELAY_MAX_S = 600U;
constexpr float CFG_TEMP_OFFSET_MIN = -5.0f,   CFG_TEMP_OFFSET_MAX = 5.0f;
constexpr float CFG_HUM_OFFSET_MIN = -20.0f,   CFG_HUM_OFFSET_MAX = 20.0f;
// Cua so mat du lieu Modbus truoc khi coi la mat cam bien. Gioi han duoi phai
// lon hon POLL_PERIOD_MS (2 s) x 2 chu ky, gioi han tren giu me an toan.
// Gioi han tren la 15 s vi processSensor() clamp hieu luc 5..15 s.
// De 30 s nhu truoc se cho phep dat mot gia tri ma firmware bo qua.
constexpr uint16_t CFG_SENSOR_TIMEOUT_MIN_S = 5U, CFG_SENSOR_TIMEOUT_MAX_S = 15U;

// ============================================================================
// HOP DONG DU LIEU HMI <-> FIRMWARE
// ============================================================================
enum class ControlMode : uint8_t { OnOff = 0, Pid = 1 };
enum class TurnDirection : uint8_t { Left = 0, Right = 1 };
enum class TurnState : uint8_t {
  Stopped = 0, Left = 1, Right = 2, Waiting = 3, Fault = 4
};
enum class AutoTuneState : uint8_t {
  Idle = 0, Running = 1, Success = 2, Failed = 3
};

enum class MachineStateCode : uint8_t {
  Boot = 0, ReadyAuto, ReadyManual, ResumeWait, Prestart, Homing,
  RunningAuto, RunningManual, AutoTune,
  SensorFault, TurningFault, SystemFault, Emergency
};

// Trang thai mang thuc te do lop web bao xuong. Mat mang KHONG BAO GIO chan
// dieu khien va khong keu coi: may ap phai chay duoc hoan toan offline.
enum class NetState : uint8_t {
  Disabled = 0,   // Nguoi dung chon OFFLINE
  Starting = 1,   // Dang bat Wi-Fi
  NoWifi = 2,     // Chua co Wi-Fi/chua cau hinh
  WifiOnly = 3,   // Co Wi-Fi nhung chua len broker
  Online = 4      // Wi-Fi + MQTT deu tot
};

// Trang thai captive portal (AP cau hinh Wi-Fi), giong thao tac giu nut BOOT.
enum class PortalState : uint8_t {
  Idle = 0, Starting = 1, Active = 2, Failed = 3
};

enum AlarmBit : uint32_t {
  AlarmNone        = 0,
  AlarmSensor      = 1UL << 0,
  AlarmTempLow     = 1UL << 1,
  AlarmTempHigh    = 1UL << 2,
  AlarmEmergency   = 1UL << 3,
  AlarmHumidityLow = 1UL << 4,
  AlarmTurning     = 1UL << 5,
  AlarmSystem      = 1UL << 7
};
constexpr uint32_t ALARM_KNOWN_MASK =
    AlarmSensor | AlarmTempLow | AlarmTempHigh | AlarmEmergency |
    AlarmHumidityLow | AlarmTurning | AlarmSystem;

struct MachineConfig {
  float targetTemp = 37.5f;
  float tempHysteresis = 0.2f;
  float lowTempAlarm = 36.5f;
  float highTempAlarm = 38.2f;
  float emergencyTemp = 39.0f;
  ControlMode controlMode = ControlMode::Pid;
  float kp = 18.0f;
  float ki = 0.8f;
  float kd = 45.0f;
  uint16_t pidCycleSec = 10;
  uint8_t maxHeaterPower = 100;

  float lowHumidityAlarm = 45.0f;
  uint16_t humidityAlarmDelaySec = 60;

  bool circulationFanEnabled = true;
  float ventOnTemp = 38.0f;
  float ventOffTemp = 37.6f;

  bool turningEnabled = true;
  uint16_t turnIntervalMin = 120;
  uint16_t turnMaxRunSec = 300;
  TurnDirection nextDirection = TurnDirection::Left;

  uint8_t totalIncubationDays = 21;
  bool autoResumeAfterPower = false;  // Tu tiep tuc me sau mat dien neu dieu kien an toan dat
  bool allowHeatWithoutBatch = false;
  uint16_t powerRestoreDelaySec = 30;

  float tempOffset = 0.0f;
  float humidityOffset = 0.0f;
  uint16_t sensorTimeoutSec = 10;
  bool alarmEnabled = true;

  // ONLINE/OFFLINE do nguoi van hanh chon tren HMI (Cai dat chung).
  // false = may chay hoan toan doc lap, khong bat Wi-Fi/MQTT.
  // Day chi la Y DINH cua nguoi dung; trang thai ket noi thuc te nam o
  // MachineRuntime::netState, khong bao gio tron lan hai khai niem nay.
  bool cloudEnabled = true;
};

// ============================================================================
// SANITIZE DUY NHAT. Moi duong vao cau hinh (HMI, Web, EEPROM, Auto Tune) deu
// phai di qua ham nay truoc khi cham vao PID hoac hien thi.
// ============================================================================
inline float mayapClampF(float value, float low, float high) {
  if (!isfinite(value)) return low;
  if (value < low) return low;
  if (value > high) return high;
  return value;
}
inline int32_t mayapClampI(int32_t value, int32_t low, int32_t high) {
  return value < low ? low : (value > high ? high : value);
}

// Ep mot o bool ve dung 0 hoac 1 ma khong doc no duoi dang bool. Byte hong tu
// EEPROM/Web nam trong o bool tao ra gia tri khac 0/1; doc mot bool nhu vay la
// UB, va memcmp trong machineConfigEqual() se khac gia tri logic khien may luu
// di luu lai mai mot ban ghi bi coi la "khac nhau".
inline void mayapNormalizeBool(bool &field) {
  uint8_t raw = 0U;
  memcpy(&raw, &field, sizeof(raw));
  const bool value = raw != 0U;
  memcpy(&field, &value, sizeof(field));
}

inline void mayapSanitizeConfig(MachineConfig &cfg) {
  const MachineConfig defaults{};
  mayapNormalizeBool(cfg.circulationFanEnabled);
  mayapNormalizeBool(cfg.turningEnabled);
  mayapNormalizeBool(cfg.autoResumeAfterPower);
  mayapNormalizeBool(cfg.allowHeatWithoutBatch);
  mayapNormalizeBool(cfg.alarmEnabled);
  mayapNormalizeBool(cfg.cloudEnabled);
  if (!isfinite(cfg.targetTemp)) cfg.targetTemp = defaults.targetTemp;
  if (!isfinite(cfg.tempHysteresis)) cfg.tempHysteresis = defaults.tempHysteresis;
  if (!isfinite(cfg.lowTempAlarm)) cfg.lowTempAlarm = defaults.lowTempAlarm;
  if (!isfinite(cfg.highTempAlarm)) cfg.highTempAlarm = defaults.highTempAlarm;
  if (!isfinite(cfg.emergencyTemp)) cfg.emergencyTemp = defaults.emergencyTemp;
  if (!isfinite(cfg.kp)) cfg.kp = defaults.kp;
  if (!isfinite(cfg.ki)) cfg.ki = defaults.ki;
  if (!isfinite(cfg.kd)) cfg.kd = defaults.kd;
  if (!isfinite(cfg.lowHumidityAlarm)) cfg.lowHumidityAlarm = defaults.lowHumidityAlarm;
  if (!isfinite(cfg.ventOnTemp)) cfg.ventOnTemp = defaults.ventOnTemp;
  if (!isfinite(cfg.ventOffTemp)) cfg.ventOffTemp = defaults.ventOffTemp;
  if (!isfinite(cfg.tempOffset)) cfg.tempOffset = defaults.tempOffset;
  if (!isfinite(cfg.humidityOffset)) cfg.humidityOffset = defaults.humidityOffset;

  cfg.targetTemp = mayapClampF(cfg.targetTemp, TARGET_TEMP_MIN_C, TARGET_TEMP_MAX_C);
  cfg.tempHysteresis = mayapClampF(cfg.tempHysteresis,
                                   CFG_HYSTERESIS_MIN, CFG_HYSTERESIS_MAX);
  cfg.lowTempAlarm = mayapClampF(cfg.lowTempAlarm, CFG_LOW_ALARM_MIN_C,
                                 cfg.targetTemp - LOW_ALARM_GAP_C);
  cfg.highTempAlarm = mayapClampF(cfg.highTempAlarm,
                                  cfg.targetTemp + HIGH_ALARM_GAP_C,
                                  HIGH_ALARM_MAX_C);
  cfg.emergencyTemp = mayapClampF(cfg.emergencyTemp,
                                  cfg.highTempAlarm + EMERGENCY_ABOVE_HIGH_C,
                                  EMERGENCY_MAX_C);

  cfg.controlMode = ControlMode::Pid;  // San pham chi chay PID.
  cfg.kp = mayapClampF(cfg.kp, CFG_KP_MIN, CFG_KP_MAX);
  cfg.ki = mayapClampF(cfg.ki, CFG_KI_MIN, CFG_KI_MAX);
  cfg.kd = mayapClampF(cfg.kd, CFG_KD_MIN, CFG_KD_MAX);
  cfg.pidCycleSec = static_cast<uint16_t>(mayapClampI(
      cfg.pidCycleSec, CFG_PID_CYCLE_MIN_S, CFG_PID_CYCLE_MAX_S));
  cfg.maxHeaterPower = static_cast<uint8_t>(mayapClampI(
      cfg.maxHeaterPower, CFG_MAX_POWER_MIN, CFG_MAX_POWER_MAX));

  cfg.lowHumidityAlarm = mayapClampF(cfg.lowHumidityAlarm,
                                     CFG_HUM_ALARM_MIN, CFG_HUM_ALARM_MAX);
  cfg.humidityAlarmDelaySec = static_cast<uint16_t>(mayapClampI(
      cfg.humidityAlarmDelaySec, CFG_HUM_DELAY_MIN_S, CFG_HUM_DELAY_MAX_S));

  const float ventOnMin = cfg.targetTemp + VENT_ON_ABOVE_SV_C;
  const float ventOnMax = cfg.highTempAlarm;
  cfg.ventOnTemp = mayapClampF(cfg.ventOnTemp, ventOnMin, ventOnMax);
  const float ventOffMin = cfg.targetTemp + VENT_OFF_ABOVE_SV_C;
  const float ventOffMax = cfg.ventOnTemp - VENT_HYSTERESIS_C;
  cfg.ventOffTemp = mayapClampF(cfg.ventOffTemp, ventOffMin, ventOffMax);

  cfg.turnIntervalMin = static_cast<uint16_t>(mayapClampI(
      cfg.turnIntervalMin, CFG_TURN_INTERVAL_MIN_M, CFG_TURN_INTERVAL_MAX_M));
  cfg.turnMaxRunSec = static_cast<uint16_t>(mayapClampI(
      cfg.turnMaxRunSec, CFG_TURN_RUN_MIN_S, CFG_TURN_RUN_MAX_S));
  if (static_cast<uint8_t>(cfg.nextDirection) >
      static_cast<uint8_t>(TurnDirection::Right)) {
    cfg.nextDirection = defaults.nextDirection;
  }

  cfg.totalIncubationDays = static_cast<uint8_t>(mayapClampI(
      cfg.totalIncubationDays, CFG_DAYS_MIN, CFG_DAYS_MAX));
  cfg.powerRestoreDelaySec = static_cast<uint16_t>(mayapClampI(
      cfg.powerRestoreDelaySec, CFG_POWER_DELAY_MIN_S, CFG_POWER_DELAY_MAX_S));
  cfg.tempOffset = mayapClampF(cfg.tempOffset,
                               CFG_TEMP_OFFSET_MIN, CFG_TEMP_OFFSET_MAX);
  cfg.humidityOffset = mayapClampF(cfg.humidityOffset,
                                   CFG_HUM_OFFSET_MIN, CFG_HUM_OFFSET_MAX);
  cfg.sensorTimeoutSec = static_cast<uint16_t>(mayapClampI(
      cfg.sensorTimeoutSec, CFG_SENSOR_TIMEOUT_MIN_S, CFG_SENSOR_TIMEOUT_MAX_S));
  cfg.alarmEnabled = true;  // Coi bao dong an toan khong duoc phep tat.
}

// Doi SV keo theo ca chuoi nguong. Dung o CA duong HMI va duong Web de hai
// giao dien khong tao ra hai ket qua khac nhau tu cung mot thao tac.
inline void mayapShiftThresholdsWithTarget(MachineConfig &cfg, float oldTarget) {
  if (!isfinite(oldTarget) || !isfinite(cfg.targetTemp)) return;
  const float delta = cfg.targetTemp - oldTarget;
  if (fabsf(delta) < 0.0001f) return;
  cfg.lowTempAlarm += delta;
  cfg.highTempAlarm += delta;
  cfg.emergencyTemp += delta;
  cfg.ventOnTemp += delta;
  cfg.ventOffTemp += delta;
}

// ============================================================================
// BANG LOI DUNG CHUNG
// Truoc day hmi.h chep tay faultTitle() va alarmBitForFaultCode(). Them mot ma
// loi vao firmware ma quen sua HMI se hien "LOI KHONG XAC DINH" va gan sai
// AlarmBit. Tu day CHI CO MOT bang; HMI doc truc tiep tu day.
// ============================================================================
enum class FaultSeverity : uint8_t { Info = 0, Warning = 1, Stop = 2, Emergency = 3 };

enum class FaultCode : uint16_t {
  None = 0,
  SensorLost = 101,
  SensorInvalid = 102,
  SensorSuspect = 103,
  SensorStuck = 104,
  LowTemperature = 110,
  HighTemperature = 111,
  EmergencyTemperature = 112,
  HumidityLow = 120,
  TurnLimitConflict = 201,
  TurnTimeout = 202,
  TurnLimitStuck = 203,
  TurnCommandConflict = 204,
  StorageUnavailable = 301,
  StorageDegraded = 302,
  AbnormalReset = 303,
  OutputConflict = 304,
  RelayRateExceeded = 305,
  RtcFailure = 306,
  ControlDeadline = 307,
  StackLow = 308,
  HeapLow = 309,
  HeapCorrupt = 310,
  I2cBusFailure = 311,
  StorageQueueOverflow = 312,
  HeaterRunaway = 401,
  HeaterNoResponse = 402
};

struct FaultDescriptor {
  FaultCode code;
  FaultSeverity severity;
  uint8_t displayPriority;
  uint32_t alarmBit;
  bool latching;
  bool inhibitsHeat;
  bool inhibitsTurning;
  const char *text;      // Serial/log, ASCII ngan
  const char *title;     // Tieu de tren HMI
  const char *cause;     // Nguyen nhan, hien khi khong co chi tiet dong
  const char *action;    // Hanh dong nguoi van hanh phai lam
};

inline const FaultDescriptor &faultDescriptor(FaultCode code) {
  static const FaultDescriptor unknown{FaultCode::None, FaultSeverity::Info, 0U,
      AlarmNone, false, false, false, "NONE", "LOI KHONG XAC DINH",
      "MA LOI NGOAI BANG", "GHI MA LOI VA BAO KY THUAT"};
  static const FaultDescriptor table[] = {
    {FaultCode::SensorLost, FaultSeverity::Stop, 235U, AlarmSensor, false, true, false,
     "SENSOR LOST", "MAT CAM BIEN", "KHONG CO DU LIEU RS485",
     "KIEM TRA DAY A/B VA NGUON CAM BIEN"},
    {FaultCode::SensorInvalid, FaultSeverity::Stop, 230U, AlarmSensor, false, true, false,
     "SENSOR INVALID", "CAM BIEN SAI", "DU LIEU NGOAI PHAM VI",
     "KIEM TRA DAU DO VA NHIEU DUONG RS485"},
    {FaultCode::SensorSuspect, FaultSeverity::Stop, 225U, AlarmSensor, false, true, false,
     "SENSOR SUSPECT", "CAM BIEN BAT THUONG", "GIA TRI GIAM DOT NGOT",
     "KIEM TRA TIEP XUC DAU DO VA NOI DAT"},
    {FaultCode::SensorStuck, FaultSeverity::Stop, 228U, AlarmSensor, false, true, false,
     "SENSOR STUCK", "CAM BIEN DUNG YEN", "NHIET VA AM KHONG DOI QUA LAU",
     "CAM BIEN CO THE TREO - KHOI DONG LAI CAM BIEN"},
    {FaultCode::LowTemperature, FaultSeverity::Warning, 55U, AlarmTempLow, false, false, false,
     "TEMP LOW", "NHIET DO THAP", "PV DUOI NGUONG BAO THAP",
     "KIEM TRA THANH NHIET VA CUA BUONG AP"},
    {FaultCode::HighTemperature, FaultSeverity::Stop, 240U, AlarmTempHigh, false, true, true,
     "TEMP HIGH", "NHIET DO CAO", "PV VUOT NGUONG BAO CAO",
     "DA CAT NHIET - MO CUA HA NHIET, KIEM TRA QUAT"},
    {FaultCode::EmergencyTemperature, FaultSeverity::Emergency, 255U, AlarmEmergency, false, true, true,
     "TEMP EMERGENCY", "QUA NHIET KHAN CAP", "PV VUOT NGUONG NGAT KHAN",
     "MO CUA NGAY - KIEM TRA SSR VA CONTACTOR NHIET"},
    {FaultCode::HumidityLow, FaultSeverity::Warning, 40U, AlarmHumidityLow, false, false, false,
     "HUM LOW", "DO AM THAP - HET NUOC", "AM DUOI NGUONG BAO HET NUOC",
     "CHAM THEM NUOC VAO KHAY TAO AM"},
    {FaultCode::TurnLimitConflict, FaultSeverity::Stop, 160U, AlarmTurning, true, false, true,
     "LIMIT CONFLICT", "LOI 2 HANH TRINH", "HAI CTHT CUNG TAC DONG",
     "KIEM TRA CTHT TRAI/PHAI VA DAY TIN HIEU"},
    {FaultCode::TurnTimeout, FaultSeverity::Stop, 150U, AlarmTurning, true, false, true,
     "TURN TIMEOUT", "DAO QUA THOI GIAN", "CHUA CHAM CTHT DICH",
     "KIEM TRA MOTOR, XICH DAO VA KHAY BI KET"},
    {FaultCode::TurnLimitStuck, FaultSeverity::Stop, 155U, AlarmTurning, true, false, true,
     "LIMIT STUCK", "HANH TRINH BI KET", "CTHT GOC KHONG NHA",
     "VE SINH/CHINH LAI CONG TAC HANH TRINH"},
    {FaultCode::TurnCommandConflict, FaultSeverity::Stop, 145U, AlarmTurning, true, false, true,
     "TURN CMD CONFLICT", "XUNG DOT LENH DAO", "TRAI VA PHAI CUNG ON",
     "NHA CA HAI NUT DAO TAY ROI THU LAI"},
    {FaultCode::StorageUnavailable, FaultSeverity::Stop, 205U, AlarmSystem, true, true, false,
     "EEPROM UNAVAILABLE", "MAT EEPROM", "KHONG DOC/GHI DUOC AT24C32",
     "KIEM TRA I2C/EEPROM - KHONG CHAY ME MOI"},
    {FaultCode::StorageDegraded, FaultSeverity::Warning, 90U, AlarmSystem, false, false, false,
     "EEPROM DEGRADED", "EEPROM SUY GIAM", "DANG CHAY BANG CAU HINH RAM",
     "HOAN TAT ME ROI THAY EEPROM"},
    {FaultCode::AbnormalReset, FaultSeverity::Stop, 210U, AlarmSystem, true, true, false,
     "ABNORMAL RESET", "RESET BAT THUONG", "WDT/PANIC/BROWNOUT",
     "XAC NHAN DE TIEP TUC - KIEM TRA NGUON 12V"},
    {FaultCode::OutputConflict, FaultSeverity::Emergency, 250U, AlarmSystem, true, true, true,
     "OUTPUT CONFLICT", "XUNG DOT OUTPUT", "LENH RA MAU THUAN NHAU",
     "DA CAT OUTPUT - BAO KY THUAT KIEM TRA"},
    {FaultCode::RelayRateExceeded, FaultSeverity::Warning, 70U, AlarmSystem, true, false, false,
     "RELAY RATE", "RELAY DONG CAT NHIEU", "VUOT GIOI HAN LAN/GIO",
     "KIEM TRA DAO DONG PID VA TIEP DIEM RELAY"},
    {FaultCode::RtcFailure, FaultSeverity::Warning, 80U, AlarmSystem, false, false, false,
     "RTC FAILURE", "LOI DONG HO RTC", "RTC MAT/OSF/SAI GIO",
     "THAY PIN CR2032 VA DAT LAI NGAY GIO"},
    {FaultCode::ControlDeadline, FaultSeverity::Warning, 120U, AlarmSystem, false, false, false,
     "CONTROL DEADLINE", "VONG DIEU KHIEN CHAM", "CHU KY VUOT NGUONG CANH BAO",
     "GHI NHAN VA BAO KY THUAT NEU LAP LAI"},
    {FaultCode::StackLow, FaultSeverity::Warning, 115U, AlarmSystem, false, false, false,
     "STACK LOW", "STACK SAP CAN", "TASK CON IT BO NHO STACK",
     "BAO KY THUAT - CAN NAP LAI FIRMWARE"},
    {FaultCode::HeapLow, FaultSeverity::Warning, 100U, AlarmSystem, false, false, false,
     "HEAP LOW", "THIEU BO NHO", "HEAP TRONG DUOI NGUONG",
     "BAO KY THUAT - CAN NAP LAI FIRMWARE"},
    {FaultCode::HeapCorrupt, FaultSeverity::Emergency, 252U, AlarmSystem, true, true, true,
     "HEAP CORRUPT", "LOI BO NHO RAM", "HEAP KHONG TOAN VEN",
     "DA CAT OUTPUT - TAT NGUON VA BAO KY THUAT"},
    {FaultCode::I2cBusFailure, FaultSeverity::Warning, 110U, AlarmSystem, false, false, false,
     "I2C BUS", "LOI BUS I2C", "LOI LIEN TIEP TREN SDA/SCL",
     "KIEM TRA DAY VA DIEN TRO KEO I2C"},
    {FaultCode::StorageQueueOverflow, FaultSeverity::Warning, 105U, AlarmSystem, false, false, false,
     "STORAGE QUEUE", "HANG DOI LUU DAY", "LENH GHI EEPROM QUA NHIEU",
     "NGUNG THAY DOI CAU HINH LIEN TUC"},
    // --- Bao ve thanh nhiet: hai che do hong quan trong nhat cua may ap ---
    {FaultCode::HeaterRunaway, FaultSeverity::Emergency, 254U, AlarmEmergency, true, true, true,
     "HEATER RUNAWAY", "NHIET TANG KHI DA TAT", "NGHI SSR BI DINH/CHAP",
     "DA CAT CONTACTOR - NGAT APTOMAT NHIET NGAY"},
    {FaultCode::HeaterNoResponse, FaultSeverity::Stop, 215U, AlarmSystem, true, false, false,
     "HEATER NO RESPONSE", "THANH NHIET KHONG LEN", "CONG SUAT DAY MA NHIET KHONG TANG",
     "KIEM TRA THANH NHIET, CONTACTOR VA PHA DIEN"}
  };
  for (const auto &item : table) if (item.code == code) return item;
  return unknown;
}

// Phai bang so dong cua bang trong faultDescriptor(). faultDescriptor() dung
// bien 'static' cuc bo nen khong the constexpr; hang nay duoc kiem tra lai o
// runtime trong bo test va dung cho static_assert cua FaultManager.
constexpr uint16_t FAULT_CODE_COUNT = 26U;

inline uint32_t faultAlarmBit(uint16_t rawCode) {
  return faultDescriptor(static_cast<FaultCode>(rawCode)).alarmBit;
}

struct HmiFaultItem {
  uint16_t code = 0;
  int16_t detail = 0;
  uint8_t severity = 0;
  uint8_t flags = 0;  // bit0: condition con ton tai, bit1: da ACK
};

struct HmiEventItem {
  uint32_t sequence = 0;
  uint32_t epoch = 0;       // ngay gio DS3231 tai luc su kien; 0 = chua hop le
  uint32_t ageSec = 0;
  uint16_t code = 0;
  int16_t value = 0;
  uint8_t type = 0;
  uint8_t flags = 0;
};

struct HmiEventSnapshot {
  uint32_t sourceSequence = 0;
  uint8_t count = 0;
  uint8_t totalInWindow = 0;
  HmiEventItem items[HMI_EVENT_DISPLAY_CAPACITY]{};
};

struct MachineRuntime {
  float temperature = NAN;
  float humidity = NAN;
  bool sensorOnline = false;
  bool batchRunning = false;
  uint8_t currentDay = 0;
  float heaterPower = 0.0f;
  bool heaterOn = false;
  bool circulationFanOn = false;
  bool ventFanOn = false;
  TurnState turnState = TurnState::Stopped;
  uint16_t nextTurnMinutes = 0;
  uint16_t turnCountToday = 0;
  uint32_t turnCountBatch = 0;
  uint32_t alarmMask = AlarmNone;
  AutoTuneState autoTuneState = AutoTuneState::Idle;
  uint8_t autoTuneProgress = 0;
  MachineStateCode stateCode = MachineStateCode::Boot;
  uint16_t primaryFaultCode = 0;
  uint8_t activeFaultCount = 0;
  uint8_t activeFaultDisplayCount = 0;
  HmiFaultItem activeFaults[HMI_FAULT_DISPLAY_CAPACITY]{};
  uint32_t faultNotificationSequence = 0;
  uint16_t lastRaisedFaultCode = 0;
  uint8_t lastRaisedFaultSeverity = 0;
  uint32_t eventSequence = 0;
  uint16_t relayTransitionsHour = 0;
  bool sensorStartupGrace = true;
  bool resumeConfirmationRequired = false;
  bool timeValid = false;
  // Trang thai mang THUC TE (khac voi MachineConfig::cloudEnabled la y dinh).
  NetState netState = NetState::Disabled;
  PortalState portalState = PortalState::Idle;
  char dateText[11] = "--/--/----";
  char machineState[20] = "KHOI DONG";
};

enum class HmiCommandType : uint8_t {
  None, BatchStart, BatchStop, TurnLeft, TurnStop, TurnRight,
  AlarmAck, AutoTuneStart, ResumeYes, ResumeNo
};
struct HmiCommand {
  uint32_t id = 0;
  HmiCommandType type = HmiCommandType::None;
  uint32_t createdAt = 0;
  uint16_t validForMs = 0;
  uint16_t actuatorLeaseMs = 0;
  uint32_t alarmMask = AlarmNone;

  // Tham so chung cho lenh mo rong. Them nut web co tham so khong can sua
  // thu vien MAYAP_WebBridge; chi them enum, case core va mot dong schema.
  int32_t arg0 = 0;
  int32_t arg1 = 0;
  float value = 0.0f;
};
enum class BuzzerCue : uint8_t { None, Key, Save, Ok, Error };

using HmiI2cLockFn = bool (*)(uint32_t timeoutMs);
using HmiI2cUnlockFn = void (*)();

struct KernelHealthSnapshot {
  uint32_t lastControlCycleUs = 0U;
  uint32_t maxControlCycleUs = 0U;
  uint32_t deadlineMissCount = 0U;
  uint16_t hardTripReason = 0U;
  uint16_t i2cRecoveryCount = 0U;
  uint16_t i2cConsecutiveErrors = 0U;
  UBaseType_t controlStackFree = 0U;
  UBaseType_t hmiStackFree = 0U;
  UBaseType_t supervisorStackFree = 0U;
  size_t freeInternalHeap = 0U;
  bool deadlineWarning = false;
  bool stackLow = false;
  bool heapLow = false;
  bool heapCorrupt = false;
  bool i2cFailed = false;
};

// ============================================================================
// CONG GIAO TIEP WEB ON DINH
// Thu vien MQTT khong biet cac struct nay. Toan bo anh xa nam trong adapter cua
// du an, vi vay them nut/lenh moi khong phai sua MAYAP_WebBridge.
// ============================================================================
enum class ConfigOrigin : uint8_t { Hmi = 0, Web = 1, AutoTune = 2 };

constexpr uint8_t WEB_COMMAND_QUEUE_CAPACITY = 8U;
constexpr uint8_t WEB_RESULT_QUEUE_CAPACITY = 8U;
constexpr uint8_t WEB_EVENT_QUEUE_CAPACITY = 32U;
constexpr size_t WEB_REQUEST_ID_CAPACITY = 40U;
constexpr size_t WEB_RESULT_MESSAGE_CAPACITY = 64U;

struct WebConfigRequest {
  uint32_t transactionId = 0U;
  uint32_t revision = 0U;
  char requestId[WEB_REQUEST_ID_CAPACITY]{};
  MachineConfig config{};
};

struct WebConfigResult {
  uint32_t transactionId = 0U;
  uint32_t revision = 0U;
  char requestId[WEB_REQUEST_ID_CAPACITY]{};
  bool ok = false;
  MachineConfig config{};
  char message[WEB_RESULT_MESSAGE_CAPACITY]{};
};

struct WebCommandRequest {
  uint32_t sequence = 0U;
  char requestId[WEB_REQUEST_ID_CAPACITY]{};
  HmiCommand command{};
};

struct WebCommandResult {
  uint32_t sequence = 0U;
  char requestId[WEB_REQUEST_ID_CAPACITY]{};
  HmiCommandType type = HmiCommandType::None;
  bool ok = false;
  char message[WEB_RESULT_MESSAGE_CAPACITY]{};
};

struct WebMachineSnapshot {
  uint32_t sequence = 0U;
  MachineConfig config{};
  MachineRuntime runtime{};
  KernelHealthSnapshot kernel{};

  bool autoMode = false;
  bool manualMode = false;
  bool systemHealthy = false;
  bool sensorHealthy = false;
  bool rtcHealthy = false;
  bool storageHealthy = false;
  bool heaterLocked = false;
  bool turningLocked = false;
  bool waitingPowerResume = false;

  uint16_t activeFaultCount = 0U;
  uint16_t highestFaultCode = 0U;
  uint32_t uptimeSec = 0U;
};

struct WebEventRecord {
  uint32_t sequence = 0U;
  uint32_t epoch = 0U;
  uint16_t code = 0U;
  int16_t value = 0;
  uint8_t type = 0U;
  uint8_t flags = 0U;
};

// ============================================================================
// CAU NOI MANG - HMI KHONG PHU THUOC THU VIEN MAYAP_WebBridge
// .ino dang ky hai callback nay khi MAYAP_WEB_ENABLED=1. Khi bien dich ban
// OFFLINE thi khong ai dang ky, hook tra ve false va HMI bao "KHONG CO MANG".
// Nho vay hmi.h/machine_control.h van bien dich duoc khong can thu vien web.
// ============================================================================
using MayapPortalStartFn = bool (*)();
using MayapNetStatusFn = void (*)(NetState &net, PortalState &portal);

void mayapSetNetHooks(MayapPortalStartFn portalStart, MayapNetStatusFn status);
// Doi ONLINE/OFFLINE chi co hieu luc sau khi khoi dong lai. HMI doc co nay de
// nhac nguoi van hanh, thay vi tu y reset mot may dang ap trung.
void mayapSetCloudRestartPending(bool pending);
bool mayapCloudRestartPending();
bool mayapRequestWifiPortal();
void mayapReadNetStatus(NetState &net, PortalState &portal);
bool mayapNetHooksInstalled();

// Dung chung giua HMI, EEPROM va firmware tong.
bool mayapI2cLock(uint32_t timeoutMs);
void mayapI2cUnlock();
void mayapI2cRecordResult(bool ok);
bool mayapI2cRecoveryDue(uint32_t now);
void mayapI2cRecoveryFinished(uint32_t now, bool ok);
uint16_t mayapI2cRecoveryCount();
uint16_t mayapI2cConsecutiveErrors();
bool mayapSystemTripActive();
uint16_t mayapSystemTripReason();
void mayapRequestSystemTrip(uint16_t reason);
KernelHealthSnapshot mayapKernelHealthSnapshot();
bool mayapSerialDebugEnabled();
void mayapSetSerialDebugEnabled(bool enabled);
void mayapSerialPrintf(bool force, const char *format, ...);

void hmiBegin();
void hmiUpdate(uint32_t now);
void hmiSetConfig(const MachineConfig &config);
void hmiSetDate(const char *dateText);
const MachineConfig &hmiGetConfig();
void hmiSetRuntime(const MachineRuntime &runtime);
void hmiSetEventLog(const HmiEventSnapshot &snapshot);
bool hmiTakeSavedConfig(MachineConfig &out, uint32_t &transactionId);
bool hmiConfirmConfigSave(uint32_t transactionId, bool ok,
                          const MachineConfig *storedConfig = nullptr);
bool hmiTakeCommand(HmiCommand &out);
bool hmiConfirmCommand(uint32_t commandId, bool ok,
                       const char *message = nullptr);
void hmiSetI2cLockCallbacks(HmiI2cLockFn lockFn, HmiI2cUnlockFn unlockFn);
void hmiPlayCue(BuzzerCue cue);
void hmiOnI2cBusReset();
