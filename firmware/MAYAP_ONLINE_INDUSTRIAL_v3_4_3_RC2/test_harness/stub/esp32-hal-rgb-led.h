#pragma once
#include <stdint.h>
inline void rgbLedWrite(uint8_t, uint8_t, uint8_t, uint8_t) {}
inline void neopixelWrite(uint8_t, uint8_t, uint8_t, uint8_t) {}
typedef enum { LED_COLOR_ORDER_RGB = 0, LED_COLOR_ORDER_BGR, LED_COLOR_ORDER_BRG,
               LED_COLOR_ORDER_RBG, LED_COLOR_ORDER_GBR, LED_COLOR_ORDER_GRB } rgb_led_color_order_t;
inline void rgbLedWriteOrdered(uint8_t, rgb_led_color_order_t, uint8_t, uint8_t, uint8_t) {}
