# Research: Finger Paint Example

Builds on [spec.md](spec.md).

## LVGL Performance Monitor (Component Enhancement)

LVGL 9.5.0 exposes the perf monitor through the **sysmon** subsystem.

### Compile-time requirements
Both Kconfig options must be enabled by any project that wants perf monitor support:
- `CONFIG_LV_USE_SYSMON=y`
- `CONFIG_LV_USE_PERF_MONITOR=y`

Position is configurable via `CONFIG_LV_USE_PERF_MONITOR_POS` (default: bottom-right).

### Runtime API (`lv_sysmon.h`)
```c
void lv_sysmon_show_performance(lv_display_t *disp);
void lv_sysmon_hide_performance(lv_display_t *disp);
void lv_sysmon_performance_pause(lv_display_t *disp);
void lv_sysmon_performance_resume(lv_display_t *disp);
```

### Integration into component
- Add a CLI command (e.g. `display perf [on|off]`) in `console_commands.c`.
- Call `lv_sysmon_show/hide_performance()` with the display handle from the msp3520 handle.
- Must be called with the LVGL mutex held.
- Projects that want this must enable the two Kconfig options in their `sdkconfig.defaults`.

## Drawing on Canvas (Finger Paint)

### lv_canvas widget
- `lv_canvas_create(parent)` — creates a canvas object
- `lv_canvas_set_buffer(obj, buf, w, h, cf)` — attach a pixel buffer
- Supports `LV_COLOR_FORMAT_RGB888` (matches our display)
- `lv_canvas_fill_bg(obj, color, opa)` — clear/fill entire canvas
- `lv_canvas_set_px(obj, x, y, color, opa)` — set individual pixels

### Drawing lines (for strokes)
Canvas supports a layer API for LVGL draw primitives:
```c
lv_layer_t layer;
lv_canvas_init_layer(canvas, &layer);
// draw lines, rects, arcs etc. on the layer
lv_canvas_finish_layer(canvas, &layer);
```

Line drawing descriptor (`lv_draw_line_dsc_t`):
- `p1`, `p2` — endpoints
- `color`, `width`, `opa` — appearance
- `round_start`, `round_end` — rounded caps

**Approach**: On each touch move event, draw a line from previous point to current point on the canvas layer. This gives smooth strokes without needing to track every pixel.

### Memory considerations
Canvas buffer = `320 * 480 * 3` (RGB888) = **460,800 bytes**. Must come from PSRAM (ESP32-S3 has 8MB octal PSRAM configured). Not DMA-capable, but that's fine — canvas is a regular LVGL widget rendered by the display flush pipeline.

## Edge Visualization

Draw a thin border rectangle and optional grid lines on the canvas at startup, before user draws. This shows the screen boundaries so touch dead zones become visible as the gap between the drawn border and where strokes can actually reach.

## Example Project Scaffolding

### Create
```
cd examples && idf.py create-project finger-paint
```
Generates: `CMakeLists.txt`, `main/CMakeLists.txt`, `main/finger_paint.c`

### Configure
1. Edit root `CMakeLists.txt`: add `set(EXTRA_COMPONENT_DIRS ../../components)` before the `include()` line.
2. Edit `main/CMakeLists.txt`: add `REQUIRES msp3520 nvs_flash console`.
3. Copy `sdkconfig.defaults` from `examples/basic/`, add sysmon/perf monitor Kconfig options.

### Pattern (from existing basic example)
Root CMakeLists.txt:
```cmake
cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS ../../components)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(finger-paint)
```

## UI Design Sketch

```
┌──────────────────────────────────┐
│ [Clear] [R] [G] [B] [W]         │  ← toolbar (top, ~40px)
├──────────────────────────────────┤
│┌────────────────────────────────┐│
││  border line (1px)             ││
││                                ││
││     canvas / drawing area      ││
││                                ││
││                                ││
│└────────────────────────────────┘│
└──────────────────────────────────┘
│                    [perf monitor]│  ← if enabled via CLI
```

- Toolbar at top with clear button and color swatches
- Canvas fills remaining screen area
- Border drawn on canvas at init
- Perf monitor overlay (bottom-right) toggled via CLI
