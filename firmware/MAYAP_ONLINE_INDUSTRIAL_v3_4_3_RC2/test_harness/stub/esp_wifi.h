#pragma once
#include <Arduino.h>
#include <esp_system.h>
typedef struct { uint8_t dummy; } wifi_config_t;
typedef int wifi_ps_type_t;
inline esp_err_t esp_wifi_set_ps(wifi_ps_type_t) { return ESP_OK; }
inline esp_err_t esp_wifi_get_mac(int, uint8_t *m) {
  static const uint8_t v[6] = {0xB8,0x5F,0xCC,0x2C,0x01,0xFC};
  memcpy(m, v, 6); return ESP_OK;
}
#define WIFI_PS_NONE 0
#define WIFI_PS_MIN_MODEM 1
#define WIFI_IF_STA 0
