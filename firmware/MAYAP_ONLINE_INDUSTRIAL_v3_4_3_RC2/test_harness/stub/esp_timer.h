#pragma once
#include <stdint.h>
#include <Arduino.h>
inline int64_t esp_timer_get_time() { return (int64_t)g_fakeMillis * 1000; }
