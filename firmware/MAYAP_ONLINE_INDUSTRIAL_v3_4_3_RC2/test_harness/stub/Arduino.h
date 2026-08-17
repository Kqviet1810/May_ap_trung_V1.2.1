#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <algorithm>
#include <string>

class String {
 public:
  String() {}
  String(const char *s) : v_(s ? s : "") {}
  String(const std::string &s) : v_(s) {}
  String(int n) { char b[24]; snprintf(b,sizeof(b),"%d",n); v_ = b; }
  String(unsigned n) { char b[24]; snprintf(b,sizeof(b),"%u",n); v_ = b; }
  const char *c_str() const { return v_.c_str(); }
  size_t length() const { return v_.size(); }
  bool isEmpty() const { return v_.empty(); }
  int toInt() const { return atoi(v_.c_str()); }
  String &operator=(const char *s) { v_ = s ? s : ""; return *this; }
  bool operator==(const char *s) const { return v_ == (s ? s : ""); }
  bool operator==(const String &s) const { return v_ == s.v_; }
  bool operator!=(const char *s) const { return !(*this == s); }
  String operator+(const String &o) const { return String(v_ + o.v_); }
  String &operator+=(const String &o) { v_ += o.v_; return *this; }
  char operator[](size_t i) const { return i < v_.size() ? v_[i] : 0; }
  void reserve(size_t n) { v_.reserve(n); }
  void replace(const String &a, const String &b) {
    if (a.v_.empty()) return;
    size_t p = 0;
    while ((p = v_.find(a.v_, p)) != std::string::npos) {
      v_.replace(p, a.v_.size(), b.v_); p += b.v_.size();
    }
  }
  void replace(const char *a, const char *b) { replace(String(a), String(b)); }
  void replace(const char *a, const String &b) { replace(String(a), b); }
  std::string v_;
};
inline String operator+(const char *a, const String &b) { return String(std::string(a) + b.v_); }

#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define INPUT_PULLDOWN 4
#define OUTPUT_OPEN_DRAIN 3
#define SERIAL_8N1 0x800001c
#define CHANGE 3
#define RISING 1
#define FALLING 2
#define PI 3.1415926535897932384626433832795f
#define DRAM_ATTR
#define IRAM_ATTR
inline int digitalPinToInterrupt(int p) { return p; }
#define F(x) (x)
// Stub PHAN CUNG: dong bo NTP. Tren host coi nhu da co gio hop le.
inline void configTime(long, int, const char *, const char * = nullptr, const char * = nullptr) {}
inline void attachInterrupt(int, void (*)(), int) {}
inline void detachInterrupt(int) {}

extern uint32_t g_fakeMillis;
inline uint32_t millis() { return g_fakeMillis; }
inline uint32_t micros() { return g_fakeMillis * 1000UL; }
inline void delayMicroseconds(uint32_t) {}
inline void delay(uint32_t) {}
inline void pinMode(uint8_t, uint8_t) {}
extern uint8_t g_pinLevel[64];
inline void digitalWrite(uint8_t p, uint8_t v) { if (p < 64) g_pinLevel[p] = v; }
inline int digitalRead(uint8_t p) { return p < 64 ? g_pinLevel[p] : 1; }

template <typename T, typename L, typename H>
inline T constrain(T v, L lo, H hi) {
  return v < static_cast<T>(lo) ? static_cast<T>(lo)
       : (v > static_cast<T>(hi) ? static_cast<T>(hi) : v);
}
using std::min;
using std::max;

struct FakeSerial {
  void begin(uint32_t) {}
  void begin(uint32_t, uint32_t, int, int) {}
  void end() {}
  int available() { return 0; }
  int availableForWrite() { return 4096; }
  int read() { return -1; }
  size_t write(const uint8_t *b, size_t n) { fwrite(b, 1, n, stdout); return n; }
  void setTimeout(uint32_t) {}
  void println(const char *s = "") { printf("%s\n", s ? s : ""); }
  void println(const String &s) { printf("%s\n", s.c_str()); }
  void print(const char *s) { printf("%s", s ? s : ""); }
  void printf(const char *f, ...) { (void)f; }
  void setRxBufferSize(size_t) {}
  void flush() {}
  int peek() { return -1; }
};
extern FakeSerial Serial;

struct HardwareSerial {
  explicit HardwareSerial(int) {}
  void begin(uint32_t, uint32_t, int, int) {}
  void end() {}
  int available() { return 0; }
  int availableForWrite() { return 128; }
  int read() { return -1; }
  size_t write(const uint8_t *, size_t n) { return n; }
  void flush() {}
  void setRxBufferSize(size_t) {}
  void setTxBufferSize(size_t) {}
};

// ---- FreeRTOS-ish ----
typedef uint32_t TickType_t;
typedef uint32_t UBaseType_t;
typedef void *TaskHandle_t;
typedef struct { int dummy; } StaticTask_t;
typedef uint32_t StackType_t;
typedef struct { volatile int owner; } portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED {0}
inline void portENTER_CRITICAL(portMUX_TYPE *) {}
inline void portEXIT_CRITICAL(portMUX_TYPE *) {}
inline void portENTER_CRITICAL_ISR(portMUX_TYPE *) {}
inline void portEXIT_CRITICAL_ISR(portMUX_TYPE *) {}
struct EspClass {
  uint32_t getFreeHeap() { return 200000; }
  uint32_t getMinFreeHeap() { return 150000; }
  uint64_t getEfuseMac() { return 0xFC012CCC5FB8ULL; }
  void restart() { }
};
extern EspClass ESP;
#define pdMS_TO_TICKS(x) (x)
#define pdTRUE 1
#define pdFALSE 0
inline TickType_t xTaskGetTickCount() { return g_fakeMillis; }
inline void vTaskDelayUntil(TickType_t *, TickType_t) {}
inline void vTaskDelay(TickType_t) {}
inline void vTaskSuspend(TaskHandle_t) {}
inline UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t) { return 4096; }
#define BIT(n) (1UL << (n))
#define PROGMEM
inline TaskHandle_t xTaskCreateStaticPinnedToCore(void (*)(void *), const char *,
    uint32_t, void *, UBaseType_t, StackType_t *, StaticTask_t *, int) {
  return (TaskHandle_t)1;
}
