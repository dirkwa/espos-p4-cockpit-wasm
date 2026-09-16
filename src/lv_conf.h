/* lv_conf.h for the LVGL-WASM JLP preview.
 *
 * MUST stay visually compatible with the firmware's lv_conf.h
 * (sensesp-cockpit-display/lv_conf.h) — color depth, theme, font
 * sizes. Anything that affects pixel output. Backend/platform
 * knobs (color format alignment, tick source, draw cache size) are
 * tuned for browser/SDL instead of the P4 panel.
 *
 * Keep the firmware copy as the source of truth; review this file
 * any time that one changes.
 */
#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/* ---- color depth: must match firmware (RGB565 on the P4 MIPI panel) ----
 * LV_COLOR_DEPTH is deprecated as of LVGL 9.4; LV_COLOR_FORMAT_DEFAULT is
 * what current lvgl actually reads (see espos-p4-cockpit lv_conf.h). */
#define LV_COLOR_FORMAT_DEFAULT LV_COLOR_FORMAT_RGB565

/* ---- memory ---- */
#define LV_USE_STDLIB_MALLOC LV_STDLIB_BUILTIN
#define LV_MEM_SIZE          (1024U * 1024U)  /* 1 MB for a 1024x600 canvas */

/* ---- types ---- */
#define LV_USE_FLOAT         1   /* widget_factory uses float subjects */

/* ---- HAL ---- */
#define LV_DEF_REFR_PERIOD   33                /* ~30 fps */
#define LV_TICK_CUSTOM       0                 /* lv_tick_inc from JS RAF */
#define LV_DPI_DEF           130

/* ---- drawing ---- */
#define LV_DRAW_SW_COMPLEX           1
#define LV_DRAW_SW_GRADIENT          1
#define LV_DRAW_SW_DRAW_UNIT_CNT     1
#define LV_USE_DRAW_SW               1

/* ---- file system / logging / asserts — off in browser ---- */
#define LV_USE_FS_FATFS              0
#define LV_USE_FS_STDIO              0
#define LV_USE_FS_POSIX              0
#define LV_USE_FS_WIN32              0
#define LV_USE_FS_MEMFS              0
#define LV_USE_LZ4_INTERNAL          0
#define LV_USE_LZ4_EXTERNAL          0
#define LV_USE_PNG                   0
#define LV_USE_BMP                   0
#define LV_USE_JPG                   0
#define LV_USE_RLOTTIE               0
#define LV_USE_VECTOR_GRAPHIC        0
#define LV_USE_GIF                   0
#define LV_USE_QRCODE                0
#define LV_USE_BARCODE               0
#define LV_USE_LOG                   0
#define LV_USE_ASSERT_NULL           1
#define LV_USE_ASSERT_MALLOC         1
#define LV_USE_PERF_MONITOR          0
#define LV_USE_MEM_MONITOR           0

/* ---- widgets — keep parity with firmware ---- */
#define LV_USE_ARC          1
#define LV_USE_BAR          1
#define LV_USE_BUTTON       1
#define LV_USE_BUTTONMATRIX 0
#define LV_USE_CALENDAR     0
#define LV_USE_CANVAS       0
#define LV_USE_CHART        0
#define LV_USE_CHECKBOX     0
#define LV_USE_DROPDOWN     0
#define LV_USE_IMAGE        1
#define LV_USE_KEYBOARD     0
#define LV_USE_LABEL        1
#define LV_USE_LED          0
#define LV_USE_LINE         1
#define LV_USE_LIST         0
#define LV_USE_MENU         0
#define LV_USE_MSGBOX       0
#define LV_USE_ROLLER       0
#define LV_USE_SCALE        0
/* The volume widget is an lv_slider; the firmware's lv_conf enables it,
 * and a preview that cannot build the same widgets defeats the point. */
#define LV_USE_SLIDER       1
#define LV_USE_SPAN         0
#define LV_USE_SPINBOX      0
#define LV_USE_SPINNER      0
#define LV_USE_SWITCH       1
#define LV_USE_TABLE        0
#define LV_USE_TABVIEW      0
#define LV_USE_TEXTAREA     0
#define LV_USE_TILEVIEW     0
#define LV_USE_WIN          0

/* ---- theme — must match firmware ---- */
#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 1
#define LV_THEME_DEFAULT_GROW 1

/* ---- fonts ---- */
#define LV_FONT_MONTSERRAT_8  0
#define LV_FONT_MONTSERRAT_10 0
#define LV_FONT_MONTSERRAT_12 0
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_18 0
#define LV_FONT_MONTSERRAT_20 1  /* widget_factory uses this */
#define LV_FONT_MONTSERRAT_22 0
#define LV_FONT_MONTSERRAT_24 0
#define LV_FONT_MONTSERRAT_26 0
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_30 0
#define LV_FONT_MONTSERRAT_32 0
#define LV_FONT_MONTSERRAT_36 1
#define LV_FONT_DEFAULT       &lv_font_montserrat_14

/* ---- driver: SDL — emscripten will glue this to <canvas> ---- */
#define LV_USE_SDL          1
#define LV_SDL_INCLUDE_PATH <SDL2/SDL.h>
#define LV_SDL_RENDER_MODE  LV_DISPLAY_RENDER_MODE_DIRECT
#define LV_SDL_BUF_COUNT    1
#define LV_SDL_FULLSCREEN   0
#define LV_SDL_DIRECT_EXIT  0
#define LV_SDL_MOUSEWHEEL_MODE  LV_SDL_MOUSEWHEEL_MODE_CROP

#endif /* LV_CONF_H */
