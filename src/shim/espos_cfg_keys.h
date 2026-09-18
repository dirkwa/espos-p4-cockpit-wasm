/* Shim for the header espOS generates from the firmware's config
 * descriptors (main/config/cockpit.json). widget_factory.cpp reads one
 * setting, the backlight level behind the @brightness slider. */
#pragma once

#define ESPOS_CFG_NS_COCKPIT "cockpit"
#define ESPOS_CFG_COCKPIT_BRIGHTNESS "brightness"
