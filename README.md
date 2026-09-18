# espos-p4-cockpit-wasm

LVGL + JLP widget factory compiled to WebAssembly. Renders the
**same `widget_factory.cpp`** the device runs, byte-for-byte, into
an HTML `<canvas>` via SDL2 (emscripten port).

Purpose: pixel-perfect designer preview without a connected device.
The designer drops layout JSON in and gets back the same pixels the
P4 panel would draw — no SVG approximation, no font drift, no
LVGL-version skew.

This is the default preview mode in
[signalk-hmi-designer](https://github.com/dirkwa/signalk-hmi-designer);
the live-mirror mode (HTTP-polled `/screenshot?fmt=jpeg` from a real
device) is the fallback when a panel happens to be connected.

## Layout

- `src/main.cpp` — JS bridge: `jlp_init(w, h)`,
  `jlp_apply_layout(json)`, `jlp_set_value_float/int/bool(path, v)`,
  `jlp_set_path_meta(path, meta_json)` (zones + description),
  `jlp_set_notifications(snapshot_json)`. Disables SDL's
  global keyboard / mouse capture so the canvas doesn't fight the
  designer page for input.
- `src/wasm_stubs.cpp` — in-process implementations of the firmware's
  `SubjectRegistry`, `ZoneRegistry`, `NotificationsRegistry`,
  `net/sk_put.h`. Identical interfaces, no SensESP / SignalK
  plumbing — values come from JS via `jlp_set_value_*`.
- `src/lv_conf.h` — LVGL config mirroring the firmware's
  `sensesp-cockpit-display/lv_conf.h`. Anything pixel-affecting
  (color depth, theme, fonts, `LV_USE_FLOAT`) must stay in lockstep.
- `public/` — build output (`jlp_wasm.js`, `jlp_wasm.wasm`) + a
  standalone `index.html` smoke test.
- `CMakeLists.txt` — pulls LVGL + `widget_factory.cpp` from the
  firmware's local PIO checkout, so versions never drift.

## Build

```bash
source ~/emsdk/emsdk_env.sh
emcmake cmake -B build -S .
cmake --build build -j
```

Outputs `public/jlp_wasm.js` (175 KB) + `public/jlp_wasm.wasm`
(~1.6 MB).

## Smoke test

```bash
cd public && python3 -m http.server 8090
# open http://localhost:8090
```

The page calls `jlp_init(1024, 600)`, then `jlp_apply_layout(...)`
with a tiny multi-widget layout covering each supported kind
(`label`, `value`, `toggle`, `arc`, `bar`, `bargroup`, `button`,
`notifications`).

## JS API

```js
import createJlpWasm from './jlp_wasm.js'

const mod = await createJlpWasm({ canvas: document.querySelector('#mycanvas') })

mod._jlp_init(1024, 600)

// allocate UTF-8 for a C string
const layout = JSON.stringify({ schema: 1, name: 'x', screens: [{ ... }] })
const len = mod.lengthBytesUTF8(layout) + 1
const ptr = mod._malloc(len)
mod.stringToUTF8(layout, ptr, len)

const errPtr = mod._jlp_apply_layout(ptr)
const err = mod.UTF8ToString(errPtr)
mod._free(ptr)
if (err) console.error('apply failed:', err)
```

Feed simulated SignalK values:

```js
const path = 'tanks.fuel'
const p = mod._malloc(mod.lengthBytesUTF8(path) + 1)
mod.stringToUTF8(path, p, mod.lengthBytesUTF8(path) + 1)
mod._jlp_set_value_float(p, 0.73)  // 73 %
mod._free(p)
```

## Dependencies

The build pulls source from the firmware checkout next door:
- LVGL → `../espos-p4-cockpit/managed_components/lvgl__lvgl`
- widget_factory.cpp → `../espos-p4-cockpit/main/jlp/widgets/widget_factory.cpp`
- ArduinoJson → `../espos-p4-cockpit/managed_components/bblanchon__arduinojson`

So **build the firmware once first** before building this:

```bash
cd ../espos-p4-cockpit && idf.py build
```

## What this is not

- **Not a runtime.** No SK websocket, no PUTs, no notifications
  registry consumer. Stubs are in-process only.
- **Not pixel-perfect against arbitrary browsers.** Anti-aliasing,
  GPU compositor, and sub-pixel rounding can shift a pixel or two
  across Chrome/Firefox/Safari. But the LVGL output is identical to
  the device.
- **Not the only preview path.** The designer also has a "live
  mirror" mode that polls the actual device's `/screenshot?fmt=jpeg`
  for true WYSIWYG when a panel is connected. WASM is the
  offline-pixel-perfect fallback.

## Keeping in sync with firmware

When `widget_factory.cpp` changes upstream, this repo's build picks
it up automatically (same source path). But two things must be kept
in lockstep manually:

1. **`src/lv_conf.h`** vs **firmware `lv_conf.h`** —
   any pixel-affecting LVGL knob (color depth, theme, fonts,
   `LV_USE_FLOAT`) must match. Check whenever the firmware's
   `lv_conf.h` changes.
2. **`src/wasm_stubs.cpp::color_for_state`** must match firmware
   `zone_registry.cpp::color_for_state` — the maritime palette
   lives in two places.

Pixel-perfect parity depends on both.

## License

espos-p4-cockpit-wasm (sensesp-p4-cockpit-wasm before the espOS port)
1.0.0 and later is **source available, not open source**. See [LICENSE.md](LICENSE.md).

**You may**, free of charge: run it on your own boat or fleet, private or
commercial; use it for internal company operations; modify it for your own use;
use it in education and research; and provide professional services around it.

**You may not**: redistribute it, or publish a modified version of it — as
source, wasm bundle or otherwise. Verbatim copies of official releases may be
mirrored and cached.

Earlier commits carried no license file. Third-party code linked into the
prebuilt wasm (LVGL, SDL2, Emscripten) keeps its own license — see
[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md).
