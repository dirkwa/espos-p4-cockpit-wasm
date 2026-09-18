/* Shim for ESP-IDF's esp_err.h: the handful of codes the firmware
 * headers we compile against return. */
#pragma once

typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_NOT_FOUND 0x105
