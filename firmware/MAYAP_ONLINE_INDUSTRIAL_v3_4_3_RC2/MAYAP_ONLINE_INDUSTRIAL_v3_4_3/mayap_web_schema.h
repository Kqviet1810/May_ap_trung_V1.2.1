#pragma once

// ============================================================================
// MAYAP WEB SCHEMA - MOT NGUON DUY NHAT
// Thu vien MAYAP_WebBridge chi van chuyen JSON raw, khong include file nay.
// ============================================================================
// v2: bao cao them cloudEnabled (chi doc), netState/portalState va mang
// faults[] trong snapshot. Website doc "v" de biet co the tin cac truong moi.
constexpr uint16_t MAYAP_WEB_PROTOCOL_VERSION = 2U;

// Phien ban INBOUND thap nhat con chap nhan. Cac truong v2 deu la THEM MOI o
// chieu di len, nen website v1 cu van dieu khien duoc may binh thuong (no chi
// bo qua cac khoa JSON no khong biet). Neu bat buoc trung khop tuyet doi thi
// nap firmware moi se lam chet toan bo dieu khien tu web cho toi khi website
// duoc cap nhat - dieu khong duoc phep xay ra tren may dang chay.
constexpr uint16_t MAYAP_WEB_PROTOCOL_MIN_ACCEPTED = 1U;

// Khi them bien MachineConfig can dua len web: them dung mot dong vao nhom
// dung kieu. Parser va serializer duoc sinh tu cung danh sach, khong sua thu
// vien giao tiep.
#define MAYAP_WEB_CONFIG_FLOAT_FIELDS(X) \
  X(targetTemp) \
  X(tempHysteresis) \
  X(lowTempAlarm) \
  X(highTempAlarm) \
  X(emergencyTemp) \
  X(kp) \
  X(ki) \
  X(kd) \
  X(lowHumidityAlarm) \
  X(ventOnTemp) \
  X(ventOffTemp) \
  X(tempOffset) \
  X(humidityOffset)

#define MAYAP_WEB_CONFIG_U16_FIELDS(X) \
  X(pidCycleSec) \
  X(humidityAlarmDelaySec) \
  X(turnIntervalMin) \
  X(turnMaxRunSec) \
  X(powerRestoreDelaySec) \
  X(sensorTimeoutSec)

#define MAYAP_WEB_CONFIG_U8_FIELDS(X) \
  X(maxHeaterPower) \
  X(totalIncubationDays)

#define MAYAP_WEB_CONFIG_BOOL_FIELDS(X) \
  X(circulationFanEnabled) \
  X(turningEnabled) \
  X(autoResumeAfterPower) \
  X(allowHeatWithoutBatch) \
  X(alarmEnabled)

// Truong CHI DOC: bao cao len web nhung web khong duoc ghi.
// cloudEnabled la cong tac ONLINE/OFFLINE. Neu cho web tat no thi may se
// offline o lan khoi dong sau va KHONG THE bat lai tu xa - phai den tan may.
// Vi vay day la thiet lap chi thao tac duoc tai HMI.
// Ngoai ra config-set bat buoc phai co DU moi truong; de cloudEnabled trong
// nhom ghi duoc se lam moi lenh luu cau hinh cua website v1 bi tu choi.
#define MAYAP_WEB_CONFIG_READONLY_BOOL_FIELDS(X) \
  X(cloudEnabled)

#define MAYAP_WEB_CONFIG_ENUM_FIELDS(X) \
  X(controlMode, ControlMode, 1U) \
  X(nextDirection, TurnDirection, 1U)

// requireFresh=true: web bat buoc gui bootId hien tai va expiresAt (epoch giay).
// Them nut web moi:
//   1) them enum HmiCommandType va case trong executeMachineCommand();
//   2) them dung mot dong vao bang nay.
// Thu vien MAYAP_WebBridge va mayap_web_adapter.h khong phai sua.
#define MAYAP_WEB_COMMAND_TABLE(X) \
  X("batch_start",    BatchStart,    5000U, 0U,    true)  \
  X("batch_stop",     BatchStop,     5000U, 0U,    false) \
  X("turn_left",      TurnLeft,      2000U, 1800U, true)  \
  X("turn_stop",      TurnStop,      2000U, 0U,    false) \
  X("turn_right",     TurnRight,     2000U, 1800U, true)  \
  X("alarm_ack",      AlarmAck,      5000U, 0U,    false) \
  X("autotune_start", AutoTuneStart, 5000U, 0U,    true)  \
  X("resume_yes",     ResumeYes,    10000U, 0U,    true)  \
  X("resume_no",      ResumeNo,     10000U, 0U,    true)
