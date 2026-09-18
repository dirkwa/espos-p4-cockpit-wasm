/* Shim for ESP-IDF's esp_timer.h. widget_factory.cpp uses only
 * esp_timer_get_time() (stream frame pacing); wasm_stubs.cpp answers it
 * from the browser clock. */
#pragma once
#include <cstdint>

extern "C" {
/** Microseconds since start, monotonic. */
int64_t esp_timer_get_time(void);
}
