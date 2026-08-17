#pragma once
#include <stdint.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NO_MEM 0x101
typedef enum {
  ESP_RST_UNKNOWN = 0, ESP_RST_POWERON, ESP_RST_EXT, ESP_RST_SW,
  ESP_RST_PANIC, ESP_RST_INT_WDT, ESP_RST_TASK_WDT, ESP_RST_WDT,
  ESP_RST_DEEPSLEEP, ESP_RST_BROWNOUT, ESP_RST_SDIO
} esp_reset_reason_t;
extern esp_reset_reason_t g_fakeResetReason;
inline esp_reset_reason_t esp_reset_reason() { return g_fakeResetReason; }
inline uint32_t esp_random() { return 0x12345678UL; }
inline void esp_restart() {}
#define RTC_DATA_ATTR
