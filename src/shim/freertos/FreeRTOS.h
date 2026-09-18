// WASM/emscripten shim. notifications_registry.h and widget_factory.cpp
// come unmodified from the firmware (this repo's no-source-forks rule).
// The former only includes FreeRTOS headers; the latter, since the
// stream widget, uses a binary semaphore to pace frame hand-off between
// its network task and the UI thread. The preview has one thread, so
// the semaphore in semphr.h is a flag and tick conversion is identity.
#pragma once
#include <cstdint>

typedef uint32_t TickType_t;
typedef int BaseType_t;
#define pdTRUE 1
#define pdFALSE 0
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#define portMAX_DELAY ((TickType_t)0xffffffffu)
