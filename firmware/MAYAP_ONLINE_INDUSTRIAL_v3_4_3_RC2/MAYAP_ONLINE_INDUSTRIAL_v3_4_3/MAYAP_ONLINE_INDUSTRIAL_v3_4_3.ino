#include "config.h"

#include <Wire.h>
#include <algorithm>
#include <esp_heap_caps.h>
#include <esp_task_wdt.h>
#include <esp_timer.h>

// ============================================================================
// KERNEL SHARED STATE
// Sau setup, chi HMI Task duoc phep goi Wire. Hai ham lock duoc giu de tuong
// thich API cu, nhung khong con mutex/priority inversion tren duong dieu khien.
// ============================================================================
static volatile bool gSystemTripLatched = false;
static volatile uint16_t gSystemTripReason = 0U;

static volatile uint16_t gI2cConsecutiveErrors = 0U;
static volatile uint16_t gI2cRecoveryCount = 0U;
static volatile uint8_t gI2cBurstAttempts = 0U;
static volatile uint32_t gI2cNextRecoveryAt = 0U;
static volatile bool gI2cFailed = false;

static volatile uint32_t gLastControlCycleUs = 0U;
static volatile uint32_t gMaxControlCycleUs = 0U;
static volatile uint32_t gDeadlineMissCount = 0U;
static volatile bool gDeadlineWarning = false;
static volatile bool gStackLow = false;
static volatile bool gHeapLow = false;
static volatile bool gHeapCorrupt = false;
static volatile UBaseType_t gControlStackFree = 0U;
static volatile UBaseType_t gHmiStackFree = 0U;
static volatile UBaseType_t gSupervisorStackFree = 0U;
static volatile size_t gFreeInternalHeap = 0U;

bool mayapI2cLock(uint32_t timeoutMs) {
  (void)timeoutMs;
  return !mayapSystemTripActive();
}

void mayapI2cUnlock() {}

void mayapI2cRecordResult(bool ok) {
  if (ok) {
    __atomic_store_n(&gI2cConsecutiveErrors, 0U, __ATOMIC_RELEASE);
    __atomic_store_n(&gI2cBurstAttempts, 0U, __ATOMIC_RELEASE);
    __atomic_store_n(&gI2cFailed, false, __ATOMIC_RELEASE);
    return;
  }
  uint16_t errors = __atomic_load_n(&gI2cConsecutiveErrors, __ATOMIC_ACQUIRE);
  if (errors < UINT16_MAX) ++errors;
  __atomic_store_n(&gI2cConsecutiveErrors, errors, __ATOMIC_RELEASE);
}

bool mayapI2cRecoveryDue(uint32_t now) {
  const uint16_t errors = __atomic_load_n(&gI2cConsecutiveErrors, __ATOMIC_ACQUIRE);
  const uint32_t next = __atomic_load_n(&gI2cNextRecoveryAt, __ATOMIC_ACQUIRE);
  return errors >= LCD_I2C_ERROR_THRESHOLD &&
         static_cast<int32_t>(now - next) >= 0;
}

void mayapI2cRecoveryFinished(uint32_t now, bool ok) {
  uint16_t count = __atomic_load_n(&gI2cRecoveryCount, __ATOMIC_ACQUIRE);
  if (count < UINT16_MAX) ++count;
  __atomic_store_n(&gI2cRecoveryCount, count, __ATOMIC_RELEASE);

  if (ok) {
    __atomic_store_n(&gI2cConsecutiveErrors, 0U, __ATOMIC_RELEASE);
    __atomic_store_n(&gI2cBurstAttempts, 0U, __ATOMIC_RELEASE);
    __atomic_store_n(&gI2cFailed, false, __ATOMIC_RELEASE);
    __atomic_store_n(&gI2cNextRecoveryAt,
                     now + I2C_RECOVERY_COOLDOWN_MS, __ATOMIC_RELEASE);
    return;
  }

  uint8_t attempts = __atomic_load_n(&gI2cBurstAttempts, __ATOMIC_ACQUIRE);
  if (attempts < UINT8_MAX) ++attempts;
  if (attempts >= I2C_RECOVERY_MAX_ATTEMPTS) {
    __atomic_store_n(&gI2cFailed, true, __ATOMIC_RELEASE);
    attempts = 0U;
    __atomic_store_n(&gI2cNextRecoveryAt,
                     now + I2C_RECOVERY_RETRY_MS, __ATOMIC_RELEASE);
  } else {
    __atomic_store_n(&gI2cNextRecoveryAt,
                     now + I2C_RECOVERY_COOLDOWN_MS, __ATOMIC_RELEASE);
  }
  __atomic_store_n(&gI2cBurstAttempts, attempts, __ATOMIC_RELEASE);
}

uint16_t mayapI2cRecoveryCount() {
  return __atomic_load_n(&gI2cRecoveryCount, __ATOMIC_ACQUIRE);
}

uint16_t mayapI2cConsecutiveErrors() {
  return __atomic_load_n(&gI2cConsecutiveErrors, __ATOMIC_ACQUIRE);
}

bool mayapSystemTripActive() {
  return __atomic_load_n(&gSystemTripLatched, __ATOMIC_ACQUIRE);
}

uint16_t mayapSystemTripReason() {
  return __atomic_load_n(&gSystemTripReason, __ATOMIC_ACQUIRE);
}

void mayapRequestSystemTrip(uint16_t reason) {
  bool expected = false;
  if (__atomic_compare_exchange_n(&gSystemTripLatched, &expected, true, false,
                                  __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
    __atomic_store_n(&gSystemTripReason, reason, __ATOMIC_RELEASE);
  }
}

KernelHealthSnapshot mayapKernelHealthSnapshot() {
  KernelHealthSnapshot out{};
  out.lastControlCycleUs = __atomic_load_n(&gLastControlCycleUs, __ATOMIC_ACQUIRE);
  out.maxControlCycleUs = __atomic_load_n(&gMaxControlCycleUs, __ATOMIC_ACQUIRE);
  out.deadlineMissCount = __atomic_load_n(&gDeadlineMissCount, __ATOMIC_ACQUIRE);
  out.hardTripReason = mayapSystemTripReason();
  out.i2cRecoveryCount = mayapI2cRecoveryCount();
  out.i2cConsecutiveErrors = mayapI2cConsecutiveErrors();
  out.controlStackFree = __atomic_load_n(&gControlStackFree, __ATOMIC_ACQUIRE);
  out.hmiStackFree = __atomic_load_n(&gHmiStackFree, __ATOMIC_ACQUIRE);
  out.supervisorStackFree = __atomic_load_n(&gSupervisorStackFree, __ATOMIC_ACQUIRE);
  out.freeInternalHeap = __atomic_load_n(&gFreeInternalHeap, __ATOMIC_ACQUIRE);
  out.deadlineWarning = __atomic_load_n(&gDeadlineWarning, __ATOMIC_ACQUIRE);
  out.stackLow = __atomic_load_n(&gStackLow, __ATOMIC_ACQUIRE);
  out.heapLow = __atomic_load_n(&gHeapLow, __ATOMIC_ACQUIRE);
  out.heapCorrupt = __atomic_load_n(&gHeapCorrupt, __ATOMIC_ACQUIRE);
  out.i2cFailed = __atomic_load_n(&gI2cFailed, __ATOMIC_ACQUIRE);
  return out;
}

#include "hmi.h"
#include "machine_control.h"
#include "mayap_web_credentials.h"

#if MAYAP_WEB_ENABLED
#include <WiFi.h>
#include <MAYAP_WebBridge.h>
#include "mayap_web_adapter.h"
#endif

using namespace Mayap;

#if MAYAP_WEB_ENABLED
static mayap::WebBridge WebBridge;
static MayapWebAdapter WebAdapter;
static bool webBridgeStarted = false;
// Che do luc khoi dong. Doi ONLINE/OFFLINE chi co hieu luc sau khi khoi dong
// lai: bat/tat radio khi dang chay se lam thu vien MQTT o trang thai nua voi.
static bool cloudEnabledAtBoot = false;

// ============================================================================
// CAU HINH WI-FI TU HMI
// MAYAP_WebBridge v2.0.1 co san API clearNetworkAndOpenPortal(). Day CHINH LA
// ham ma serviceResetButton() goi khi nha nut BOOT sau khi giu 3 giay, nen bam
// tren HMI va giu nut BOOT di qua dung mot duong code - khong the lech hanh vi.
//
// LUU Y QUAN TRONG: ham nay XOA Wi-Fi da luu roi moi mo portal (dung nhu thao
// tac BOOT). HMI phai hoi xac nhan truoc, xem ConfirmAction::WifiPortal.
// ============================================================================
static bool mayapPortalStartHook() {
  if (!webBridgeStarted) return false;
  WebBridge.clearNetworkAndOpenPortal();
  return true;
}

// Trang thai mang doc THANG tu thu vien. Chi de hien thi/ghi log, khong bao gio
// tham gia vao quyet dinh dieu khien.
static void mayapNetStatusHook(NetState &net, PortalState &portal) {
  if (!webBridgeStarted) {
    net = NetState::Disabled;
    portal = PortalState::Idle;
    return;
  }
  portal = WebBridge.portalOpen() ? PortalState::Active : PortalState::Idle;

  switch (WebBridge.state()) {
    case mayap::ConnectionState::Online:
      net = NetState::Online; break;
    case mayap::ConnectionState::MqttConnecting:
    case mayap::ConnectionState::TimeSync:
      net = NetState::WifiOnly; break;
    case mayap::ConnectionState::WifiConnecting:
      net = NetState::Starting; break;
    case mayap::ConnectionState::Portal:
      net = NetState::Starting;
      portal = PortalState::Active;
      break;
    default:
      net = WebBridge.wifiOnline() ? NetState::WifiOnly : NetState::NoWifi;
      break;
  }
}
#endif

static StaticTask_t controlTaskTcb;
static StackType_t controlTaskStack[
    (CONTROL_TASK_STACK_BYTES + sizeof(StackType_t) - 1U) / sizeof(StackType_t)];
static TaskHandle_t controlTaskHandle = nullptr;

static StaticTask_t hmiTaskTcb;
static StackType_t hmiTaskStack[
    (HMI_TASK_STACK_BYTES + sizeof(StackType_t) - 1U) / sizeof(StackType_t)];
static TaskHandle_t hmiTaskHandle = nullptr;

static StaticTask_t supervisorTaskTcb;
static StackType_t supervisorTaskStack[
    (SUPERVISOR_TASK_STACK_BYTES + sizeof(StackType_t) - 1U) / sizeof(StackType_t)];
static TaskHandle_t supervisorTaskHandle = nullptr;

static volatile uint32_t controlHeartbeatMs = 0U;
static volatile uint32_t hmiHeartbeatMs = 0U;

static_assert(sizeof(controlTaskStack) >= CONTROL_TASK_STACK_BYTES,
              "Control stack buffer qua nho");
static_assert(sizeof(hmiTaskStack) >= HMI_TASK_STACK_BYTES,
              "HMI stack buffer qua nho");
static_assert(sizeof(supervisorTaskStack) >= SUPERVISOR_TASK_STACK_BYTES,
              "Supervisor stack buffer qua nho");

[[noreturn]] static void fatalRestart(const char *stage, esp_err_t error) {
  mayapRequestSystemTrip(900U);
  mayapSafeOutputsEarly();
  mayapSerialPrintf(true, "[FATAL] %s err=%d -> RESTART\n",
                    stage ? stage : "SYSTEM", static_cast<int>(error));
  esp_restart();
  abort();
}

static void subscribeCurrentTaskToWdt(const char *name) {
  const esp_err_t result = esp_task_wdt_add(nullptr);
  if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
    fatalRestart(name, result);
  }
}

static bool reinitializeI2cBus(uint32_t now) {
  Wire.end();
  pinMode(PIN_I2C_SDA, INPUT_PULLUP);
  pinMode(PIN_I2C_SCL, OUTPUT_OPEN_DRAIN);
  digitalWrite(PIN_I2C_SCL, HIGH);
  if (digitalRead(PIN_I2C_SDA) == LOW) {
    for (uint8_t i = 0U; i < 9U && digitalRead(PIN_I2C_SDA) == LOW; ++i) {
      digitalWrite(PIN_I2C_SCL, LOW);
      delayMicroseconds(5);
      digitalWrite(PIN_I2C_SCL, HIGH);
      delayMicroseconds(5);
    }
  }
  // STOP co gioi han de giai phong slave dang giu SDA.
  pinMode(PIN_I2C_SDA, OUTPUT_OPEN_DRAIN);
  digitalWrite(PIN_I2C_SDA, LOW);
  delayMicroseconds(5);
  digitalWrite(PIN_I2C_SCL, HIGH);
  delayMicroseconds(5);
  digitalWrite(PIN_I2C_SDA, HIGH);
  pinMode(PIN_I2C_SDA, INPUT_PULLUP);
  pinMode(PIN_I2C_SCL, INPUT_PULLUP);

  const bool ok = Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_CLOCK_HZ);
  Wire.setTimeOut(I2C_TIMEOUT_MS);
  Machine.onI2cBusReset(now);
  hmiOnI2cBusReset();
  mayapI2cRecoveryFinished(now, ok);
  mayapSerialPrintf(false, "[I2C] BUS RECOVERY %s count=%u\n",
                    ok ? "OK" : "FAIL", mayapI2cRecoveryCount());
  return ok;
}

void controlTask(void *parameter) {
  (void)parameter;
  subscribeCurrentTaskToWdt("CTRL WDT ADD");
  TickType_t lastWake = xTaskGetTickCount();
  uint8_t consecutiveTrips = 0U;

  for (;;) {
    if (mayapSystemTripActive()) {
      mayapSafeOutputsEarly();
      vTaskSuspend(nullptr);
    }

    const int64_t startedUs = esp_timer_get_time();
    const uint32_t now = millis();
    Machine.update(now);
    const uint32_t cycleUs = static_cast<uint32_t>(
        std::max<int64_t>(0, esp_timer_get_time() - startedUs));

    __atomic_store_n(&gLastControlCycleUs, cycleUs, __ATOMIC_RELEASE);
    uint32_t maximum = __atomic_load_n(&gMaxControlCycleUs, __ATOMIC_ACQUIRE);
    while (cycleUs > maximum &&
           !__atomic_compare_exchange_n(&gMaxControlCycleUs, &maximum, cycleUs,
                                        false, __ATOMIC_ACQ_REL,
                                        __ATOMIC_ACQUIRE)) {}

    const bool warning = cycleUs > CONTROL_CYCLE_WARN_US;
    __atomic_store_n(&gDeadlineWarning, warning, __ATOMIC_RELEASE);
    if (warning) {
      uint32_t misses = __atomic_load_n(&gDeadlineMissCount, __ATOMIC_ACQUIRE);
      if (misses < UINT32_MAX) ++misses;
      __atomic_store_n(&gDeadlineMissCount, misses, __ATOMIC_RELEASE);
    }

    if (cycleUs > CONTROL_CYCLE_TRIP_US) {
      if (consecutiveTrips < UINT8_MAX) ++consecutiveTrips;
    } else {
      consecutiveTrips = 0U;
    }
    if (consecutiveTrips >= CONTROL_CYCLE_TRIP_CONSECUTIVE) {
      mayapRequestSystemTrip(307U);
      mayapSafeOutputsEarly();
      vTaskSuspend(nullptr);
    }

    __atomic_store_n(&controlHeartbeatMs, now, __ATOMIC_RELEASE);
    const esp_err_t result = esp_task_wdt_reset();
    if (result != ESP_OK) fatalRestart("CTRL WDT RESET", result);
    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(CONTROL_TASK_PERIOD_MS));
  }
}

void hmiTask(void *parameter) {
  (void)parameter;
  TickType_t lastWake = xTaskGetTickCount();
  for (;;) {
    const uint32_t now = millis();
    if (mayapI2cRecoveryDue(now)) (void)reinitializeI2cBus(now);
    Machine.serviceI2c(now);       // Chu so huu duy nhat cua RTC/EEPROM sau boot.
    hmiUpdate(now);                // LCD cung chay tren cung task, khong mutex.
    __atomic_store_n(&hmiHeartbeatMs, now, __ATOMIC_RELEASE);
    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(HMI_TASK_PERIOD_MS));
  }
}

void supervisorTask(void *parameter) {
  (void)parameter;
  subscribeCurrentTaskToWdt("SUP WDT ADD");
  TickType_t lastWake = xTaskGetTickCount();
  uint32_t lastKernelCheckAt = 0U;
  uint32_t lastHeapCheckAt = 0U;
  bool hmiWasSlow = false;

  for (;;) {
    const uint32_t now = millis();
    const uint32_t ctrlBeat = __atomic_load_n(&controlHeartbeatMs, __ATOMIC_ACQUIRE);
    const uint32_t hmiBeat = __atomic_load_n(&hmiHeartbeatMs, __ATOMIC_ACQUIRE);

    const bool controlHealthy = ctrlBeat != 0U &&
        elapsedMs(now, ctrlBeat) <= CONTROL_HEARTBEAT_TIMEOUT_MS;
    const bool hmiHealthy = hmiBeat != 0U &&
        elapsedMs(now, hmiBeat) <= HMI_HEARTBEAT_TIMEOUT_MS;

    if (!controlHealthy) mayapRequestSystemTrip(901U);

    if (!hmiHealthy && hmiBeat != 0U && !hmiWasSlow) {
      hmiWasSlow = true;
      mayapSerialPrintf(false, "[SUPERVISOR] HMI HEARTBEAT CHAM\n");
    } else if (hmiHealthy && hmiWasSlow) {
      hmiWasSlow = false;
      mayapSerialPrintf(false, "[SUPERVISOR] HMI DA PHUC HOI\n");
    }

    if (elapsedMs(now, lastKernelCheckAt) >= KERNEL_HEALTH_CHECK_MS) {
      lastKernelCheckAt = now;
      const UBaseType_t ctrl = uxTaskGetStackHighWaterMark(controlTaskHandle);
      const UBaseType_t hmi = uxTaskGetStackHighWaterMark(hmiTaskHandle);
      const UBaseType_t sup = uxTaskGetStackHighWaterMark(supervisorTaskHandle);
      const size_t heap = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
      const bool stackLow = ctrl < CONTROL_STACK_MIN_BYTES ||
                            sup < SUPERVISOR_STACK_MIN_BYTES ||
                            hmi < HMI_STACK_MIN_BYTES;
      const bool heapLow = heap < MIN_FREE_INTERNAL_HEAP_BYTES;
      __atomic_store_n(&gControlStackFree, ctrl, __ATOMIC_RELEASE);
      __atomic_store_n(&gHmiStackFree, hmi, __ATOMIC_RELEASE);
      __atomic_store_n(&gSupervisorStackFree, sup, __ATOMIC_RELEASE);
      __atomic_store_n(&gFreeInternalHeap, heap, __ATOMIC_RELEASE);
      __atomic_store_n(&gStackLow, stackLow, __ATOMIC_RELEASE);
      __atomic_store_n(&gHeapLow, heapLow, __ATOMIC_RELEASE);
      if (ctrl < CONTROL_STACK_MIN_BYTES || sup < SUPERVISOR_STACK_MIN_BYTES) {
        mayapRequestSystemTrip(308U);
      }
    }

    if (elapsedMs(now, lastHeapCheckAt) >= HEAP_INTEGRITY_CHECK_MS) {
      lastHeapCheckAt = now;
      const bool ok = heap_caps_check_integrity_all(false);
      __atomic_store_n(&gHeapCorrupt, !ok, __ATOMIC_RELEASE);
      if (!ok) mayapRequestSystemTrip(310U);
    }

    if (mayapSystemTripActive()) {
      if (controlTaskHandle) vTaskSuspend(controlTaskHandle);
      mayapSafeOutputsEarly();
      mayapSerialPrintf(true, "[SUPERVISOR] HARD TRIP reason=%u\n",
                        mayapSystemTripReason());
      const uint32_t tripAt = now;
      // Khong feed TWDT nua. Fallback chi dung neu TWDT khong reset nhu du kien.
      while (elapsedMs(millis(), tripAt) < SUPERVISOR_RESTART_FALLBACK_MS) {
        mayapSafeOutputsEarly();
        vTaskDelay(pdMS_TO_TICKS(20));
      }
      esp_restart();
      abort();
    }

    const esp_err_t result = esp_task_wdt_reset();
    if (result != ESP_OK) fatalRestart("SUP WDT RESET", result);
    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(SUPERVISOR_TASK_PERIOD_MS));
  }
}

void setup() {
  mayapSafeOutputsEarly();
  Serial.begin(115200);

  if (!Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_CLOCK_HZ)) {
    fatalRestart("I2C BEGIN", ESP_FAIL);
  }
  Wire.setTimeOut(I2C_TIMEOUT_MS);
  hmiSetI2cLockCallbacks(nullptr, nullptr);

  esp_task_wdt_config_t wdtConfig{};
  wdtConfig.timeout_ms = CONTROL_WDT_TIMEOUT_MS;
  wdtConfig.idle_core_mask = BIT(0) | BIT(1);
  wdtConfig.trigger_panic = true;
  esp_err_t result = esp_task_wdt_reconfigure(&wdtConfig);
  if (result == ESP_ERR_INVALID_STATE) result = esp_task_wdt_init(&wdtConfig);
  if (result != ESP_OK) fatalRestart("WDT INIT", result);

  // Boot con don luong: duoc phep doc RTC/EEPROM truc tiep truoc khi task chay.
  Machine.begin();
  hmiSetConfig(Machine.config());
  hmiSetRuntime(Machine.runtime());
  hmiBegin();

  const uint32_t now = millis();
  __atomic_store_n(&controlHeartbeatMs, now, __ATOMIC_RELEASE);
  __atomic_store_n(&hmiHeartbeatMs, now, __ATOMIC_RELEASE);

  controlTaskHandle = xTaskCreateStaticPinnedToCore(
      controlTask, "mayap_ctrl", sizeof(controlTaskStack), nullptr, 5,
      controlTaskStack, &controlTaskTcb, 1);

  supervisorTaskHandle = xTaskCreateStaticPinnedToCore(
      supervisorTask, "mayap_supervisor", sizeof(supervisorTaskStack), nullptr, 6,
      supervisorTaskStack, &supervisorTaskTcb, 1);

  hmiTaskHandle = xTaskCreateStaticPinnedToCore(
      hmiTask, "mayap_hmi_i2c", sizeof(hmiTaskStack), nullptr, 2,
      hmiTaskStack, &hmiTaskTcb, 0);

  if (!controlTaskHandle || !hmiTaskHandle || !supervisorTaskHandle) {
    fatalRestart("TASK CREATE", ESP_ERR_NO_MEM);
  }

#if MAYAP_WEB_ENABLED
  // Mang khoi dong sau cung. Loi Wi-Fi/MQTT khong duoc anh huong may OFFLINE.
  mayap::BridgeOptions webOptions{};
  webOptions.apPrefix = "MAYAP";
  webOptions.brokerUri = MAYAP_MQTT_BROKER_URI;
  webOptions.mqttUsername = MAYAP_MQTT_USERNAME;
  webOptions.mqttPassword = MAYAP_MQTT_PASSWORD;
  webOptions.rootCaPem = MAYAP_MQTT_ROOT_CA;
  webOptions.topicRoot = "mayap/v1";
  webOptions.firmwareVersion = MAYAP_FIRMWARE_VERSION;
  webOptions.mqttTaskPriority = 1U;
  webOptions.mqttTaskStackBytes = 7168U;
  webOptions.allowInsecureTls = false;
  webOptions.enableSerialLog = MAYAP_WEB_ENABLE_SERIAL_LOG != 0;
  webOptions.resetButtonPin = 0U;
  webOptions.resetButtonActiveLow = true;

  // ONLINE/OFFLINE do nguoi van hanh chon trong "Cai dat chung". Machine.begin()
  // da nap cau hinh tu EEPROM o tren, nen o day da biet y dinh cua nguoi dung.
  // OFFLINE = KHONG bat radio chut nao: khong ton dien, khong phat song, khong
  // co be mat tan cong nao. Day moi la y nghia that cua mot nut Offline tren
  // thiet bi cong nghiep.
  cloudEnabledAtBoot = Machine.config().cloudEnabled;
  if (cloudEnabledAtBoot) {
    (void)WebAdapter.begin(WebBridge);
    webBridgeStarted = WebBridge.begin(webOptions, WebAdapter.callbacks());
  } else {
    WiFi.mode(WIFI_OFF);
  }
  // HMI chi biet hai con tro ham nay, khong biet gi ve MAYAP_WebBridge.
  mayapSetNetHooks(&mayapPortalStartHook, &mayapNetStatusHook);
  mayapSerialPrintf(false, "[WEB] mode=%s bridge=%s device=%s broker=%s\n",
                    cloudEnabledAtBoot ? "ONLINE" : "OFFLINE",
                    webBridgeStarted ? "READY" : "DISABLED",
                    WebBridge.deviceId(),
                    MAYAP_MQTT_BROKER_URI[0] ? "CONFIGURED" : "EMPTY");
#else
  // Ban OFFLINE: khong dang ky hook. HMI se bao "BAN OFFLINE - KHONG CO MANG".
  mayapSetNetHooks(nullptr, nullptr);
#endif
}

void loop() {
#if MAYAP_WEB_ENABLED
  // loopTask uu tien thap. Control/Supervisor/HMI van chay bang task rieng.
  if (webBridgeStarted) {
    WebBridge.loop();
    WebAdapter.loop();
  }
  // Nguoi van hanh doi ONLINE/OFFLINE tren HMI ma chua khoi dong lai: bao cho
  // HMI biet de hien "CAN KHOI DONG LAI", khong tu y reset may dang ap trung.
  mayapSetCloudRestartPending(Machine.config().cloudEnabled != cloudEnabledAtBoot);
  vTaskDelay(pdMS_TO_TICKS(2));
#else
  vTaskDelay(pdMS_TO_TICKS(1000));
#endif
}
