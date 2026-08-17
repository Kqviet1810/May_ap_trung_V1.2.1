#pragma once
#include <esp_system.h>
typedef struct { uint32_t timeout_ms; uint32_t idle_core_mask; bool trigger_panic; } esp_task_wdt_config_t;
inline esp_err_t esp_task_wdt_add(void *) { return ESP_OK; }
inline esp_err_t esp_task_wdt_reset() { return ESP_OK; }
inline esp_err_t esp_task_wdt_init(esp_task_wdt_config_t *) { return ESP_OK; }
inline esp_err_t esp_task_wdt_reconfigure(esp_task_wdt_config_t *) { return ESP_OK; }
