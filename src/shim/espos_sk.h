/* Shim for espOS's espos_sk.h, the SignalK client component. The widget
 * factory asks it for the discovered server when a stream widget has no
 * host of its own. There is no SignalK client in the preview, so the
 * stub answers "not found" and the stream code takes its no-server path
 * (which never runs here anyway: the designer previews stream widgets
 * as static labels). Only what widget_factory.cpp touches is declared. */
#pragma once
#include <cstdint>

typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_NOT_FOUND 0x105

#define ESPOS_SK_HOST_MAX 64
#define ESPOS_SK_SELF_MAX 64

typedef struct {
  char host[ESPOS_SK_HOST_MAX]; /* IPv4 dotted or hostname */
  uint16_t port;
  char self[ESPOS_SK_SELF_MAX];
} espos_sk_server_t;

extern "C" {
/** Copy the current server; ESP_ERR_NOT_FOUND if none. */
esp_err_t espos_sk_get_server(espos_sk_server_t* out);
}
