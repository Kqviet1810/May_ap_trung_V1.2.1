#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <esp_system.h>
uint32_t g_fakeMillis = 0;
uint8_t g_pinLevel[64] = {};
FakeSerial Serial;
EspClass ESP;
#include <WiFi.h>
WiFiStub WiFi;
TwoWire Wire;
esp_reset_reason_t g_fakeResetReason = ESP_RST_POWERON;
const int u8g2_font_5x8_tf = FONT_5x8;
const int u8g2_font_6x12_tf = FONT_6x12;
const int u8g2_font_helvB10_tf = FONT_HELVB10;
const int u8g2_font_helvB12_tf = FONT_HELVB12;
const int u8g2_font_helvB14_tf = FONT_HELVB14;
const int u8g2_font_logisoso20_tn = FONT_LOGISOSO20;
#include <mqtt_client.h>
MqttStubState g_mqtt;
