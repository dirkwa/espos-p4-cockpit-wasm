# espos-p4-cockpit-wasm

LVGL + JLP `widget_factory.cpp` compiled to WebAssembly via emscripten
+ SDL2, so the
[signalk-hmi-designer](https://github.com/dirkwa/signalk-hmi-designer)
canvas can render layouts pixel-identically to what the
[espos-p4-cockpit](https://github.com/dirkwa/espos-p4-cockpit)
firmware draws — no device required.

## Architecture invariants

1. **No source forks.** `CMakeLists.txt` pulls LVGL and
   `widget_factory.cpp` straight out of the firmware's PIO checkout.
   Any time we edit them in the firmware repo this build picks the
   changes up automatically. Don't copy files in here.
2. **Two files DO need lockstep maintenance** with the firmware:
   - `src/lv_conf.h` mirrors the firmware's
     `sensesp-cockpit-display/lv_conf.h`. Anything pixel-affecting
     (color depth, theme, fonts, `LV_USE_FLOAT`) must match.
   - `src/wasm_stubs.cpp::color_for_state` must match firmware
     `zone_registry.cpp::color_for_state` — the maritime palette
     lives in two places, by design (no shared dep).
3. **Not a runtime.** No SK websocket, no PUTs, no notification
   registry consumer. JS feeds simulated values via the `jlp_set_*`
   bridge functions.

## Where to start reading

| File                              | Why                                                  |
|-----------------------------------|------------------------------------------------------|
| [README.md](README.md)            | JS API + build / smoke-test instructions             |
| [src/main.cpp](src/main.cpp)      | JS bridge: `jlp_init`, `jlp_apply_layout`, `jlp_set_value_*`, `jlp_set_path_meta`, `jlp_set_notifications` |
| [src/wasm_stubs.cpp](src/wasm_stubs.cpp) | In-process stand-ins for SubjectRegistry / ZoneRegistry / NotificationsRegistry / sk_put |
| [src/lv_conf.h](src/lv_conf.h)    | LVGL config — must mirror firmware                   |
| [CMakeLists.txt](CMakeLists.txt)  | Source paths into the firmware's PIO checkout        |

## Build

```bash
source ~/emsdk/emsdk_env.sh
emcmake cmake -B build -S .
cmake --build build -j
```

Build the firmware first (`idf.py build` in
`../espos-p4-cockpit`) so the ESP-IDF component manager
fetches the LVGL + ArduinoJson checkouts this CMakeLists points at into
`managed_components/`.

## Repo conventions

- **Commits**: focused, atomic. Subject ≤ 50 chars, imperative.
- **Never auto-commit, never auto-push.** Only when the user asks.
- **No release-flow work** unless the user says release.
- **No AI attribution** anywhere.
- The generated `public/jlp_wasm.{js,wasm}` are committed so the
  designer's `copy-wasm.sh` can pull them without an emscripten
  toolchain — keep them in sync after any firmware widget change.
- CI builds the bundle against `dirkwa/espos-p4-cockpit@main` on every
  pull request, without publishing. A red build means the shims in
  `src/shim` and `src/wasm_stubs.cpp` drifted from what
  `widget_factory.cpp` now includes or calls; fix the shims, never fork
  the firmware source. The manual `rebuild-wasm` run is what publishes.
