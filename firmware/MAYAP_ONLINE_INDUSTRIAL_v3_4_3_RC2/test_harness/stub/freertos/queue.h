#pragma once
// Stub PHAN CUNG: hang doi FreeRTOS. Cai bang vong tron don gian, du dung cho
// mot producer (MQTT task) - mot consumer (loop) nhu thu vien dung.
#include <Arduino.h>
#include <vector>
#include <string>
typedef void *QueueHandle_t;
typedef struct { int dummy; } StaticQueue_t;
struct FakeQueue {
  size_t itemSize = 0, capacity = 0;
  std::vector<std::string> items;
};
inline QueueHandle_t xQueueCreateStatic(uint32_t len, uint32_t itemSize,
                                        uint8_t *, StaticQueue_t *) {
  FakeQueue *q = new FakeQueue(); q->itemSize = itemSize; q->capacity = len;
  return (QueueHandle_t)q;
}
inline QueueHandle_t xQueueCreate(uint32_t len, uint32_t itemSize) {
  return xQueueCreateStatic(len, itemSize, nullptr, nullptr);
}
inline int xQueueSend(QueueHandle_t h, const void *item, TickType_t) {
  FakeQueue *q = (FakeQueue *)h;
  if (!q || q->items.size() >= q->capacity) return 0;
  q->items.push_back(std::string((const char *)item, q->itemSize));
  return 1;
}
inline int xQueueSendToBack(QueueHandle_t h, const void *i, TickType_t t) { return xQueueSend(h, i, t); }
inline int xQueueReceive(QueueHandle_t h, void *out, TickType_t) {
  FakeQueue *q = (FakeQueue *)h;
  if (!q || q->items.empty()) return 0;
  memcpy(out, q->items.front().data(), q->itemSize);
  q->items.erase(q->items.begin());
  return 1;
}
inline uint32_t uxQueueMessagesWaiting(QueueHandle_t h) {
  FakeQueue *q = (FakeQueue *)h; return q ? q->items.size() : 0;
}
inline void vQueueDelete(QueueHandle_t h) { delete (FakeQueue *)h; }
#define portMAX_DELAY 0xFFFFFFFFUL
