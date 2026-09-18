/* Shim for the firmware's cockpit_hal/ui.h, the UI-thread marshalling
 * layer of espOS. widget_factory.cpp uses the periodic timer pair (the
 * stream widget's watchdog) and post() (its frame hand-off), so that is
 * all this declares. The preview has a single thread and LVGL's own
 * timers: post() runs the function at once and every() is an lv_timer,
 * see wasm_stubs.cpp. */
#pragma once
#include <cstdint>
#include <functional>

namespace cockpit_hal {
namespace ui {

/** Run fn on the UI thread; here, right away. */
void post(std::function<void()> fn);
/** Run fn every ms. Returns a handle for cancel(); 0 is never returned. */
uint32_t every(uint32_t ms, std::function<void()> fn);
void cancel(uint32_t handle);

}  // namespace ui
}  // namespace cockpit_hal
