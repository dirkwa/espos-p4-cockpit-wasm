// JLP WASM entry point. Mirrors the firmware's layout-apply pipeline
// (parse JSON, build under a staging parent, swap atomically) but
// runs in the browser instead of on the P4. The widget LVGL code path
// is the firmware's widget_factory.cpp compiled byte-for-byte.
//
// JS bridge:
//   jlp_init(w, h)                — set up LVGL + SDL window (canvas).
//   jlp_apply_layout(json)        — render the layout, return "" on
//                                   success or a UTF-8 error string.
//   jlp_set_value_float(p, v)     — push a simulated SK float.
//   jlp_set_value_int  (p, v)     — push a simulated SK int.
//   jlp_set_value_bool (p, b)     — push a simulated SK bool.
//
// We deliberately don't import LayoutManager itself: the firmware's
// version pulls in LittleFS persistence and the post-swap WS restart
// hook, neither of which makes sense in a browser. The pipeline here
// is the same shape — staging parent, build, atomic swap, delete old
// — but everything else is intentionally trimmed.

#include <emscripten.h>
#include <emscripten/html5.h>
#include <emscripten/bind.h>

#include <set>
#include <string>

#include "lvgl.h"
#include "src/drivers/sdl/lv_sdl_window.h"

// SDL2 is statically linked through emscripten's port. We only need
// SDL_EventState here to tell SDL "don't watch for keyboard / mouse"
// — the designer drives LVGL from JS, not from page input. Without
// this, SDL's keydown handler captures every key on the page (the
// canvas + document target) and the inspector inputs / F12 / Ctrl-R
// stop working because preventDefault eats every keystroke.
#include <SDL2/SDL_events.h>

#include <ArduinoJson.h>

#include "subject_registry.h"
#include "zone_registry.h"
#include "notifications_registry.h"
#include "widgets/widget_factory.h"

static lv_display_t* disp = nullptr;
static lv_obj_t*     current_root = nullptr;

static EM_BOOL frame(double t, void*) {
  (void)t;
  lv_tick_inc(16);
  lv_timer_handler();
  return EM_TRUE;
}

extern "C" {

EMSCRIPTEN_KEEPALIVE
int jlp_init(int w, int h) {
  if (disp) return 0;  // idempotent
  lv_init();
  disp = lv_sdl_window_create(w, h);
  if (!disp) return -1;
  // Stop SDL from intercepting keyboard + mouse on the page. Each
  // SDL_EventState(..., SDL_DISABLE) call removes the corresponding
  // event class from SDL's processing AND tells emscripten's SDL2
  // port to stop installing its document-level listeners for it.
  // Without these, the embedded LVGL canvas grabs every keystroke
  // (F12, Ctrl-Shift-R, typing in the designer's inspector inputs)
  // because SDL's default keydown handler calls preventDefault.
  SDL_EventState(SDL_KEYDOWN,     SDL_DISABLE);
  SDL_EventState(SDL_KEYUP,       SDL_DISABLE);
  SDL_EventState(SDL_TEXTINPUT,   SDL_DISABLE);
  SDL_EventState(SDL_TEXTEDITING, SDL_DISABLE);
  SDL_EventState(SDL_MOUSEMOTION, SDL_DISABLE);
  SDL_EventState(SDL_MOUSEBUTTONDOWN, SDL_DISABLE);
  SDL_EventState(SDL_MOUSEBUTTONUP,   SDL_DISABLE);
  SDL_EventState(SDL_MOUSEWHEEL,      SDL_DISABLE);
  lv_obj_t* scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x0d1117), LV_PART_MAIN);
  emscripten_request_animation_frame_loop(frame, nullptr);
  return 0;
}

EMSCRIPTEN_KEEPALIVE
const char* jlp_apply_layout(const char* json) {
  static std::string err_storage;
  err_storage.clear();
  if (!disp) { err_storage = "jlp_init not called"; return err_storage.c_str(); }

  JsonDocument doc;
  DeserializationError de = deserializeJson(doc, json);
  if (de) {
    err_storage = std::string("parse: ") + de.c_str();
    return err_storage.c_str();
  }

  lv_obj_t* screen = lv_screen_active();
  lv_obj_t* staging = lv_obj_create(screen);
  lv_obj_set_size(staging, lv_pct(100), lv_pct(100));
  lv_obj_set_pos(staging, 0, 0);
  lv_obj_set_style_bg_opa(staging, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(staging, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(staging, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(staging, 0, LV_PART_MAIN);
  lv_obj_clear_flag(staging, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(staging, LV_OBJ_FLAG_HIDDEN);

  // Pull the first screen's widgets only — the designer's WASM canvas
  // is single-screen by definition (the tab strip lives outside the
  // canvas surface). Switching tabs in the designer re-calls
  // jlp_apply_layout with the active screen's spec wrapped in the
  // same top-level shape.
  JsonArrayConst screens = doc["screens"];
  if (screens.isNull() || screens.size() == 0) {
    lv_obj_delete(staging);
    err_storage = "no screens";
    return err_storage.c_str();
  }
  // The layout's `theme`, handled as layout_manager.cpp does on the
  // device: `bg` paints the root (left transparent when omitted, so the
  // dark default shows through), and apply_theme() sets the default
  // fg/accent before any widget is built so they pick it up. A failed
  // build puts the previous theme back, since the previous layout stays
  // on screen and its widgets read the globals at update time.
  JsonObjectConst theme = doc["theme"];
  uint32_t theme_bg = 0;
  if (!theme.isNull() &&
      jlp::parse_hex_color(theme["bg"] | (const char*)nullptr, &theme_bg)) {
    lv_obj_set_style_bg_color(staging, lv_color_hex(theme_bg), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(staging, LV_OPA_COVER, LV_PART_MAIN);
  }
  const jlp::ThemeColors prev_theme = jlp::current_theme();
  jlp::apply_theme(theme);

  JsonArrayConst widgets = screens[0]["widgets"];
  std::set<std::string> live_paths;
  jlp::BuildCtx ctx{ staging, jlp::registry(), live_paths };
  if (!widgets.isNull()) {
    for (JsonObjectConst spec : widgets) {
      std::string werr;
      lv_obj_t* w = jlp::build_widget(ctx, spec, &werr);
      if (!w) {
        jlp::restore_theme(prev_theme);
        lv_obj_delete(staging);
        err_storage = std::string("widget: ") + werr;
        return err_storage.c_str();
      }
    }
  }

  // Atomic swap: unhide, then delete the previous root.
  lv_obj_clear_flag(staging, LV_OBJ_FLAG_HIDDEN);
  if (current_root) lv_obj_delete(current_root);
  current_root = staging;

  return "";  // success
}

EMSCRIPTEN_KEEPALIVE
void jlp_set_value_float(const char* path, float v) {
  lv_subject_t* s = jlp::registry().lookup(path);
  if (!s) return;
  lv_subject_set_float(s, v);
}

EMSCRIPTEN_KEEPALIVE
void jlp_set_value_int(const char* path, int v) {
  lv_subject_t* s = jlp::registry().lookup(path);
  if (!s) return;
  lv_subject_set_int(s, v);
}

EMSCRIPTEN_KEEPALIVE
void jlp_set_value_bool(const char* path, int v) {
  lv_subject_t* s = jlp::registry().lookup(path);
  if (!s) return;
  lv_subject_set_int(s, v ? 1 : 0);
}

// Feed a SignalK meta object for a path. Designer fetches meta via
// the SK REST tree (zones, description, displayUnits, units) and
// pushes the relevant shape here so wasm widgets can do zone-colored
// renders + description-over-value labels, matching the device.
//
// Expected JSON shape (subset of SK meta):
//   { "description": "...", "zones": [{"lower":N,"upper":N,"state":"alarm"}, ...] }
EMSCRIPTEN_KEEPALIVE
void jlp_set_path_meta(const char* path, const char* meta_json) {
  JsonDocument doc;
  if (deserializeJson(doc, meta_json)) return;
  jlp::zones().apply_meta(path, doc.as<JsonObjectConst>());
}

// Replace the entire notifications snapshot from a JSON array of
// `{path, state, message}` rows. Removes any previously-known
// notification whose path isn't in the new array (so cleared
// notifications disappear from list widgets and the alert overlay
// dismisses). Fires the registry's on_change observers so listeners
// (list widget rows, alert overlay) re-render.
//
// `path` is the suffix after `notifications.` — same convention the
// firmware NotificationsRegistry uses, and what designer's
// fetchNotifications() returns.
EMSCRIPTEN_KEEPALIVE
void jlp_set_notifications(const char* snapshot_json) {
  JsonDocument doc;
  if (deserializeJson(doc, snapshot_json)) return;
  JsonArrayConst arr = doc.as<JsonArrayConst>();
  if (arr.isNull()) return;

  // Build the new key set so we can clear removals first.
  std::set<std::string> incoming;
  for (JsonObjectConst row : arr) {
    const char* p = row["path"] | (const char*)nullptr;
    if (p) incoming.insert(p);
  }

  // Clear any prior path not in the new snapshot. snapshot(true)
  // returns the registry's current set including cleared rows.
  auto current = jlp::notifications().snapshot(true);
  for (const auto& n : current) {
    if (!incoming.count(n.path)) {
      JsonDocument null_doc;  // a default-constructed variant reads as null
      jlp::notifications().apply(n.path, null_doc.as<JsonVariantConst>());
    }
  }

  // Apply each row. The registry treats this as upsert.
  for (JsonObjectConst row : arr) {
    const char* p = row["path"] | (const char*)nullptr;
    if (!p) continue;
    // Build a fresh value object the registry's apply() expects
    // (it reads `state` and `message`).
    JsonDocument val_doc;
    JsonObject val = val_doc.to<JsonObject>();
    val["state"]   = row["state"]   | "normal";
    val["message"] = row["message"] | "";
    jlp::notifications().apply(p, val_doc.as<JsonVariantConst>());
  }
}

}  // extern "C"
