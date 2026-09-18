/* Shim for the firmware's display driver interface. The @brightness
 * slider previews its level on the backlight while dragging; the
 * preview has no backlight, so ui::display() returns null and the call
 * is skipped. Only the member widget_factory.cpp touches is declared. */
#pragma once
#include <cstdint>

namespace cockpit_hal {

struct DisplayDriver {
  virtual ~DisplayDriver() = default;
  virtual void set_brightness(uint8_t /*pct*/) {}
};

}  // namespace cockpit_hal
