# LVGL-WASM JLP preview. Builds widget_factory.cpp (from the firmware
# repo) + LVGL via emscripten so the designer can render layouts
# pixel-perfectly against the same code path the device runs.
#
# This spike only compiles a smoke-test main.c with a single arc. Once
# the toolchain is verified we extend it to pull in widget_factory.cpp
# and the JSON apply pipeline.
#
# Build:
#   source ~/emsdk/emsdk_env.sh
#   cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=$EMSDK/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake
#   cmake --build build

cmake_minimum_required(VERSION 3.20)
project(jlp_wasm C CXX)

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 17)

# Point LVGL at our lv_conf.h (sits in src/).
add_compile_definitions(LV_CONF_INCLUDE_SIMPLE)
include_directories(${CMAKE_SOURCE_DIR}/src)

# The firmware checkout we build against. Overridable so this does not depend
# on a particular directory name next door.
set(FW_CHECKOUT "${CMAKE_SOURCE_DIR}/../espos-p4-cockpit" CACHE PATH
    "espos-p4-cockpit checkout to take LVGL, ArduinoJson and the JLP sources from")

# LVGL source — share with the firmware checkout so we stay in lockstep.
# ESP-IDF's component manager puts it in managed_components/; the old
# .pio/libdeps path is from the PlatformIO era and no longer exists.
set(LVGL_ROOT "${FW_CHECKOUT}/managed_components/lvgl__lvgl")
if(NOT EXISTS "${LVGL_ROOT}/lvgl.h")
  message(FATAL_ERROR
    "LVGL not found at ${LVGL_ROOT}. "
    "Build the firmware once first so the component manager fetches it.")
endif()

file(GLOB_RECURSE LVGL_SOURCES
  "${LVGL_ROOT}/src/*.c"
  "${LVGL_ROOT}/src/*.cpp")

# Drop arch-specific .S asm (helium etc.); emscripten doesn't speak it.
list(FILTER LVGL_SOURCES EXCLUDE REGEX "/helium/")
list(FILTER LVGL_SOURCES EXCLUDE REGEX "_neon\\.c$")

add_library(lvgl STATIC ${LVGL_SOURCES})
target_include_directories(lvgl PUBLIC "${LVGL_ROOT}" "${LVGL_ROOT}/src")

# Firmware sources we reuse verbatim (single source of truth: any
# widget code change must benefit both target and preview).
# main/jlp since the espOS port; it was src/jlp under PlatformIO.
set(FW_ROOT     "${FW_CHECKOUT}/main/jlp")
set(JSON_ROOT   "${FW_CHECKOUT}/managed_components/bblanchon__arduinojson/src")
if(NOT EXISTS "${FW_ROOT}/widgets/widget_factory.cpp")
  message(FATAL_ERROR "widget_factory.cpp not found at ${FW_ROOT}")
endif()
if(NOT EXISTS "${JSON_ROOT}/ArduinoJson.h")
  message(FATAL_ERROR "ArduinoJson not found at ${JSON_ROOT} — build the firmware once.")
endif()

add_library(jlp_widgets STATIC
  "${FW_ROOT}/widgets/widget_factory.cpp"
  "${CMAKE_SOURCE_DIR}/src/wasm_stubs.cpp")
target_include_directories(jlp_widgets PUBLIC
  # Shim dir first: satisfies notifications_registry.h's vestigial
  # freertos/*.h includes (it uses no FreeRTOS types in the header; the
  # real dependency is in the firmware .cpp we don't compile) without
  # forking the firmware header.
  "${CMAKE_SOURCE_DIR}/src/shim"
  "${FW_ROOT}"
  "${FW_ROOT}/widgets"
  "${FW_ROOT}/audio"
  # chime.h pulls espos_audio/audio_driver.h since the espOS port. Only the
  # header is needed here -- the driver itself belongs to the device.
  "${FW_CHECKOUT}/espos/components/espos_audio/include"
  "${JSON_ROOT}")
target_link_libraries(jlp_widgets PUBLIC lvgl)
# ArduinoJson uses some Arduino macros around String which we don't
# need; the header is otherwise pure C++17.
target_compile_definitions(jlp_widgets PRIVATE
  ARDUINOJSON_ENABLE_STD_STRING=1
  ARDUINOJSON_ENABLE_ARDUINO_STRING=0)

# JLP-WASM entry: layout JSON in, LVGL pixels out.
add_executable(jlp_wasm src/main.cpp)
target_link_libraries(jlp_wasm PRIVATE lvgl jlp_widgets)
target_compile_definitions(jlp_wasm PRIVATE
  ARDUINOJSON_ENABLE_STD_STRING=1
  ARDUINOJSON_ENABLE_ARDUINO_STRING=0)

# Emscripten link flags:
#  - USE_SDL=2          : pull in SDL2 (emscripten port; talks to <canvas>)
#  - INITIAL_MEMORY     : LVGL + our buffers fit in 32 MB
#  - EXPORTED_RUNTIME   : ccall/cwrap so JS can drive us
#  - MODULARIZE         : emit a factory function we can import as a module
#  - EXPORT_ES6 / ENVIRONMENT=web : ES module suitable for Vite import
target_link_options(jlp_wasm PRIVATE
  "-sUSE_SDL=2"
  "-sINITIAL_MEMORY=33554432"
  "-sALLOW_MEMORY_GROWTH=1"
  "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','HEAPU8','UTF8ToString','stringToUTF8','lengthBytesUTF8','allocateUTF8']"
  "-sEXPORTED_FUNCTIONS=['_jlp_init','_jlp_apply_layout','_jlp_set_value_float','_jlp_set_value_int','_jlp_set_value_bool','_jlp_set_path_meta','_jlp_set_notifications','_malloc','_free']"
  "-sNO_EXIT_RUNTIME=1"
  "-sINVOKE_RUN=0"
  "-sMODULARIZE=1"
  "-sEXPORT_ES6=1"
  "-sENVIRONMENT=web"
  "-sEXPORT_NAME=createJlpWasm"
  "-O2")
target_compile_options(jlp_wasm PRIVATE -sUSE_SDL=2)
target_compile_options(lvgl PRIVATE -sUSE_SDL=2)

set_target_properties(jlp_wasm PROPERTIES
  SUFFIX ".js"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/public")
