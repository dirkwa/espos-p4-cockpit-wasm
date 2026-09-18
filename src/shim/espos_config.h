/* Shim for espOS's espos_config.h, the persistent settings store. The
 * @brightness slider reads, writes and follows one integer setting; the
 * preview has no store, so wasm_stubs.cpp answers with the descriptor
 * default, accepts writes, and never fires a change callback. */
#pragma once
#include <cstdint>

#include "esp_err.h"

extern "C" {
esp_err_t espos_config_get_i32(const char* ns, const char* key, int32_t* out);
esp_err_t espos_config_set_i32(const char* ns, const char* key, int32_t v);
typedef void (*espos_config_change_cb_t)(const char* ns, const char* key, void* arg);
esp_err_t espos_config_subscribe(espos_config_change_cb_t cb, void* arg);
}
