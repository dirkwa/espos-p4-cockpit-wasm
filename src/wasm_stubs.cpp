// WASM stubs for the firmware modules that widget_factory.cpp links
// against. Each function satisfies the public interface declared in
// the corresponding firmware header (subject_registry.h,
// zone_registry.h, notifications_registry.h, net/sk_put.h) but
// substitutes in-process behavior for the SensESP / SignalK plumbing
// the firmware uses.
//
// What's preserved:
//   - The exact LVGL behavior (subjects feed values into lv_subject_*,
//     observers fire when values change, zone colors map via the same
//     color_for_state).
//   - The exact memory model (subjects survive layout swaps via the
//     same get_or_create cache).
//
// What's replaced:
//   - SensESP SKValueListener / SKPutRequest -> no-op (PUTs are routed
//     back to the JS bridge via puts_emit; SK reads come from
//     subject_registry::feed_value which the bridge calls when the
//     designer wants to simulate a new SK value).
//   - WS notifications.* delta consumer -> in-memory snapshot the JS
//     bridge populates from a JSON blob.
//   - Zone meta delivery -> JS-supplied table per path.

#include "subject_registry.h"
#include "zone_registry.h"
#include "notifications_registry.h"
#include "net/sk_put.h"
#include "audio/voice_control.h"
#include "audio/chime.h"
#include "net/drop_here.h"
#include "cockpit_hal/ui.h"
#include "esp_timer.h"
#include "espos_sk.h"
#include "espos_cfg_keys.h"
#include "espos_config.h"
#include "net/stream_client.h"

#include <emscripten.h>

#include "lvgl.h"

#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include <unordered_map>

namespace jlp {

// ----- SubjectRegistry --------------------------------------------------

lv_subject_t* SubjectRegistry::get_or_create(const std::string& path,
                                             SubjectKind kind) {
  auto it = map_.find(path);
  if (it != map_.end()) {
    if (it->second.entry->kind != kind) return nullptr;
    return &it->second.entry->subject;
  }
  auto entry = std::make_unique<SubjectEntry>();
  entry->path = path;
  entry->kind = kind;
  std::memset(entry->str_buf,  0, sizeof(entry->str_buf));
  std::memset(entry->str_prev, 0, sizeof(entry->str_prev));
  switch (kind) {
    case SubjectKind::Float:
      lv_subject_init_float(&entry->subject, 0.f);
      break;
    case SubjectKind::Int:
    case SubjectKind::Bool:
      lv_subject_init_int(&entry->subject, 0);
      break;
    case SubjectKind::String:
      lv_subject_init_string(&entry->subject,
                             entry->str_buf, entry->str_prev,
                             sizeof(entry->str_buf), "");
      break;
  }
  lv_subject_t* out = &entry->subject;
  map_.emplace(path, Slot{std::move(entry)});
  return out;
}

lv_subject_t* SubjectRegistry::lookup(const std::string& path) const {
  auto it = map_.find(path);
  if (it == map_.end()) return nullptr;
  return const_cast<lv_subject_t*>(&it->second.entry->subject);
}

std::vector<std::string> SubjectRegistry::paths() const {
  std::vector<std::string> out;
  out.reserve(map_.size());
  for (const auto& kv : map_) out.push_back(kv.first);
  return out;
}

void SubjectRegistry::garbage_collect(const std::set<std::string>& live) {
  for (auto it = map_.begin(); it != map_.end();) {
    if (live.count(it->first) == 0) it = map_.erase(it);
    else ++it;
  }
}

SubjectRegistry& registry() {
  static SubjectRegistry r;
  return r;
}

// ----- ZoneRegistry -----------------------------------------------------

void ZoneRegistry::hook_sk_ws() { /* no SK in WASM */ }

const Zone* ZoneRegistry::match(const std::string& path,
                                float raw_value) const {
  auto it = map_.find(path);
  if (it == map_.end()) return nullptr;
  const auto& zs = it->second;
  // Mirror firmware semantics: half-open [lower, upper) for all zones
  // except the topmost, which is inclusive so a value sitting exactly
  // on the upper boundary (e.g. full tank 1.0) still tints.
  float top_edge = -1e30f;
  for (const auto& z : zs) if (z.upper > top_edge) top_edge = z.upper;
  for (const auto& z : zs) {
    if (z.lower == z.upper) {
      if (raw_value == z.lower) return &z;
    } else if (raw_value >= z.lower && raw_value < z.upper) {
      return &z;
    } else if (raw_value == z.upper && z.upper == top_edge) {
      return &z;
    }
  }
  return nullptr;
}

const std::string& ZoneRegistry::description(const std::string& path) const {
  static const std::string empty;
  auto it = descriptions_.find(path);
  return it == descriptions_.end() ? empty : it->second;
}

void ZoneRegistry::apply_meta(const std::string& path,
                              const JsonObjectConst& meta) {
  std::vector<Zone> zones;
  JsonArrayConst arr = meta["zones"];
  if (!arr.isNull()) {
    for (JsonObjectConst z : arr) {
      Zone zone;
      zone.lower = z["lower"] | -1e30f;
      zone.upper = z["upper"] |  1e30f;
      const char* s = z["state"] | "nominal";
      if      (!std::strcmp(s, "alert"))     zone.state = ZoneState::Alert;
      else if (!std::strcmp(s, "warn"))      zone.state = ZoneState::Warn;
      else if (!std::strcmp(s, "warning"))   zone.state = ZoneState::Warn;
      else if (!std::strcmp(s, "alarm"))     zone.state = ZoneState::Alarm;
      else if (!std::strcmp(s, "emergency")) zone.state = ZoneState::Emergency;
      else if (!std::strcmp(s, "normal"))    zone.state = ZoneState::Normal;
      else                                   zone.state = ZoneState::Nominal;
      zones.push_back(zone);
    }
  }
  map_[path] = std::move(zones);
  const char* desc = meta["description"] | (const char*)nullptr;
  if (desc) descriptions_[path] = desc;
  // Match firmware: notify the bound subject so observers re-fire
  // and pick up the just-loaded description/zones. build_label
  // pre-seeds its text from description() at construction time,
  // so observer-fire-on-meta no longer overwrites a stale-but-
  // correct rendering with a stale zero — the worry behind the
  // earlier revert.
  lv_subject_t* sub = registry().lookup(path);
  if (sub) lv_subject_notify(sub);
}

ZoneRegistry& zones() {
  static ZoneRegistry r;
  return r;
}

uint32_t color_for_state(ZoneState s) {
  // Maritime-helm palette: one step warmer than SK spec defaults.
  // KEEP IN LOCKSTEP with firmware zone_registry.cpp:color_for_state.
  switch (s) {
    case ZoneState::Nominal:
    case ZoneState::Normal:    return 0x3fb950;  // green
    case ZoneState::Alert:     return 0xd29922;  // yellow
    case ZoneState::Warn:      return 0xfb8500;  // orange
    case ZoneState::Alarm:     return 0xf85149;  // red
    case ZoneState::Emergency: return 0xa371f7;  // purple
  }
  return 0x8b949e;
}

// ----- NotificationsRegistry --------------------------------------------

void NotificationsRegistry::hook_sk_ws() { /* no SK in WASM */ }

void NotificationsRegistry::apply(const std::string& path_after_prefix,
                                  const JsonVariantConst& value) {
  if (value.isNull()) {
    map_.erase(path_after_prefix);
  } else {
    Notification n;
    n.path = path_after_prefix;
    n.message = (const char*)(value["message"] | "");
    n.state = parse_not_state(value["state"] | "normal");
    map_[path_after_prefix] = std::move(n);
  }
  fire_observers();
}

const Notification* NotificationsRegistry::most_severe() const {
  const Notification* best = nullptr;
  uint8_t best_sev = 0;
  for (const auto& kv : map_) {
    if (acked_.count(kv.first)) continue;
    uint8_t sev = (uint8_t)kv.second.state;
    if (sev > best_sev) { best_sev = sev; best = &kv.second; }
  }
  return best;
}

std::vector<Notification>
NotificationsRegistry::snapshot(bool include_cleared) const {
  std::vector<Notification> out;
  out.reserve(map_.size());
  for (const auto& kv : map_) {
    // Match firmware: ack suppresses the alert-overlay popup only,
    // not the list. The bus condition is still live.
    if (!include_cleared) {
      if (kv.second.state == NotState::Normal ||
          kv.second.state == NotState::Nominal) continue;
    }
    out.push_back(kv.second);
  }
  // Sort by severity descending so the worst floats to the top —
  // mirrors firmware behaviour.
  std::sort(out.begin(), out.end(),
            [](const Notification& a, const Notification& b) {
              return (uint8_t)a.state > (uint8_t)b.state;
            });
  return out;
}

bool NotificationsRegistry::acknowledge(const std::string& path_after_prefix) {
  auto it = map_.find(path_after_prefix);
  if (it == map_.end()) return false;
  acked_[path_after_prefix] = it->second.state;
  fire_observers();
  // The firmware answers whether the caller should also send the SK
  // ack delta, and says no for a path it has seen re-assert from the
  // bus. Nothing re-asserts in the preview, so a known path always
  // gets the ack.
  return true;
}

bool NotificationsRegistry::is_acknowledged(
    const std::string& path_after_prefix) const {
  return acked_.count(path_after_prefix) > 0;
}

void NotificationsRegistry::fire_observers() {
  for (auto& s : observers_) {
    if (s.cb) s.cb();
  }
}

NotificationsRegistry& notifications() {
  static NotificationsRegistry r;
  return r;
}

// ----- voice ------------------------------------------------------------
// The voice widgets (voice/PTT, mute_mic, mute_speaker, volume) are
// panel-local: on the device they drive the Wyoming satellite and the audio
// codec. Neither exists here, so the preview keeps the state the widgets read
// back and does nothing else -- enough for the designer to render a tile in
// its correct on/off position and move a slider.

void VoiceControl::set_ptt_held(bool /*held*/) {
  // Forwards to the Wyoming satellite on the device; there is nothing to
  // stream to here, and VoiceControl keeps no PTT state of its own.
}

int VoiceControl::state_code() const {
  // 0 = no orchestrator. The preview has none, and the PTT caption renders
  // its disconnected form, which is the honest thing to show.
  return 0;
}

void VoiceControl::set_speaker_muted(bool muted) { speaker_muted_.store(muted); }
void VoiceControl::set_mic_muted(bool muted) { mic_muted_.store(muted); }

void VoiceControl::set_volume(uint8_t pct, bool /*persist*/) {
  volume_.store(pct > 100 ? 100 : pct);
}

VoiceControl& voice() {
  static VoiceControl v;
  return v;
}

// ----- chime / drop_here ------------------------------------------------
// Both are device actions with no meaning in a preview: there is no speaker
// to chime and no SignalK server to drop an anchor on. The mute state is kept
// because a widget reads it back to draw itself.

void Chime::set_muted(bool muted) { muted_ = muted; }

Chime& chime() {
  static Chime c;
  return c;
}

void drop_anchor_here() {}

// ----- sk_put -----------------------------------------------------------
// In firmware these PUT to the SignalK server. In WASM there's no SK to
// talk to — we just remember the last PUT per path so the JS bridge
// can surface "what would the device send right now?" for the
// inspector. A future iteration could call back into JS via
// EM_ASM/EM_JS to forward the PUT to the connected device.

namespace {
struct LastPut { std::string kind; std::string value; };
std::map<std::string, LastPut>& last_puts() {
  static std::map<std::string, LastPut> m;
  return m;
}
}  // namespace

void put_bool(const std::string& p, bool v) {
  last_puts()[p] = {"bool", v ? "true" : "false"};
}
void put_int(const std::string& p, int v) {
  last_puts()[p] = {"int", std::to_string(v)};
}
void put_float(const std::string& p, float v) {
  last_puts()[p] = {"float", std::to_string(v)};
}
void put_string(const std::string& p, const std::string& v) {
  last_puts()[p] = {"string", v};
}
void put_null(const std::string& p) {
  last_puts()[p] = {"null", "null"};
}
void put_notification_ack(const std::string& path_after_prefix) {
  // In firmware this constructs an inbound SK delta. In WASM we
  // mirror the local-ack behavior so the alert overlay dismisses.
  notifications().acknowledge(path_after_prefix);
}

// ----- stream client ----------------------------------------------------
// The MJPEG stream widget's transport. The designer previews stream
// widgets as static labels, so nothing ever starts a stream here; the
// stubs exist so widget_factory.cpp links.

bool stream_client_start(const char* /*host*/, uint16_t /*port*/,
                         uint32_t /*width*/, uint32_t /*height*/,
                         StreamFrameCb /*cb*/) {
  return false;
}
void stream_client_stop() {}
void stream_client_set_paused(bool /*paused*/) {}
bool stream_client_stop_diagnostic() { return false; }
bool stream_client_peer(uint32_t* /*addr_be*/) { return false; }
StreamStats stream_client_stats() { return StreamStats{}; }

}  // namespace jlp

// ----- ESP-IDF / espOS ---------------------------------------------------

extern "C" int64_t esp_timer_get_time(void) {
  return static_cast<int64_t>(emscripten_get_now() * 1000.0);
}

extern "C" esp_err_t espos_sk_get_server(espos_sk_server_t* /*out*/) {
  return ESP_ERR_NOT_FOUND;  // no SignalK client in the preview
}

// The settings store behind the @brightness slider: answer with the
// descriptor default, accept writes, never fire a change. The designer
// feeds the real panel's level through the "@brightness" subject.
extern "C" esp_err_t espos_config_get_i32(const char* /*ns*/, const char* /*key*/,
                                          int32_t* out) {
  if (out) *out = 95;
  return ESP_OK;
}
extern "C" esp_err_t espos_config_set_i32(const char* /*ns*/, const char* /*key*/,
                                          int32_t /*v*/) {
  return ESP_OK;
}
extern "C" esp_err_t espos_config_subscribe(espos_config_change_cb_t /*cb*/,
                                            void* /*arg*/) {
  return ESP_OK;
}

// ----- cockpit_hal::ui --------------------------------------------------
// The firmware marshals work onto its UI task; the preview has one
// thread and LVGL's own timers, so a periodic callback is an lv_timer
// keyed by a handle the caller can cancel.

namespace cockpit_hal {
namespace ui {

namespace {
struct Periodic {
  lv_timer_t* timer;
  std::function<void()> fn;
};
std::map<uint32_t, Periodic>& periodics() {
  static std::map<uint32_t, Periodic> m;
  return m;
}
uint32_t next_handle = 1;
}  // namespace

void post(std::function<void()> fn) {
  if (fn) fn();  // one thread: this is the UI thread
}

DisplayDriver* display() { return nullptr; }  // no backlight to preview on

uint32_t every(uint32_t ms, std::function<void()> fn) {
  const uint32_t h = next_handle++;
  auto& slot = periodics()[h];
  slot.fn = std::move(fn);
  slot.timer = lv_timer_create(
      [](lv_timer_t* t) {
        auto* p = static_cast<Periodic*>(lv_timer_get_user_data(t));
        if (p && p->fn) p->fn();
      },
      ms, &slot);
  return h;
}

void cancel(uint32_t handle) {
  auto& m = periodics();
  auto it = m.find(handle);
  if (it == m.end()) return;
  lv_timer_delete(it->second.timer);
  m.erase(it);
}

}  // namespace ui
}  // namespace cockpit_hal
