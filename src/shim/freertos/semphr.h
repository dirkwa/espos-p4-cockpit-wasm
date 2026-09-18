// WASM/emscripten shim — see FreeRTOS.h in this directory. A binary
// semaphore reduced to a flag: with one thread, Take() after Give()
// succeeds at once and Take() on an empty semaphore cannot block, so it
// reports a timeout instead.
#pragma once
#include "freertos/FreeRTOS.h"

struct ShimBinarySemaphore {
  bool given;
};
typedef ShimBinarySemaphore* SemaphoreHandle_t;

inline SemaphoreHandle_t xSemaphoreCreateBinary() {
  return new ShimBinarySemaphore{false};
}
inline void vSemaphoreDelete(SemaphoreHandle_t s) { delete s; }
inline BaseType_t xSemaphoreGive(SemaphoreHandle_t s) {
  if (!s || s->given) return pdFALSE;
  s->given = true;
  return pdTRUE;
}
inline BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t /*ticks*/) {
  if (!s || !s->given) return pdFALSE;
  s->given = false;
  return pdTRUE;
}
