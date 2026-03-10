# Plan: Finger Paint Example

Builds on [spec.md](spec.md) and [research.md](research.md).

## Part 1: Component Enhancement — Perf Monitor CLI

### 1.1 Add `display perf [on|off]` subcommand

**File: `components/msp3520/src/console_commands.c`**

Add a `perf` subcommand to the existing `cmd_display` handler:

```c
if (strcmp(sub, "perf") == 0) {
    if (argc != 3) {
        printf("Usage: display perf <on|off>\n");
        return 1;
    }
    msp3520_lvgl_lock(h, 0);
    if (strcmp(argv[2], "on") == 0) {
        lv_sysmon_show_performance(h->display);
        printf("Perf monitor: on\n");
    } else {
        lv_sysmon_hide_performance(h->display);
        printf("Perf monitor: off\n");
    }
    msp3520_lvgl_unlock(h);
    return 0;
}
```

Also update the help/hint text and the `argc == 1` usage output to mention `perf`.

### 1.2 Add `#include "lv_sysmon.h"` guard

The sysmon API is only available when `LV_USE_PERF_MONITOR` is enabled. Wrap the perf subcommand in:
```c
#if LV_USE_PERF_MONITOR
    ...
#endif
```

This way the command only compiles in when the consumer enables it.

### 1.3 No component Kconfig changes needed

The perf monitor Kconfig options (`LV_USE_SYSMON`, `LV_USE_PERF_MONITOR`) belong to LVGL's own Kconfig. Consumers enable them in their `sdkconfig.defaults`.

---

## Part 2: Finger Paint Example Project

### 2.1 Scaffold

```bash
cd /home/ok/src/esp-msp3520/examples
source ~/esp/v5.5.3/esp-idf/export.sh
idf.py create-project finger-paint
```

This creates `examples/finger-paint/` with `CMakeLists.txt`, `main/CMakeLists.txt`, and `main/finger_paint.c`.

### 2.2 Edit CMakeLists.txt

**`examples/finger-paint/CMakeLists.txt`** — add `EXTRA_COMPONENT_DIRS`:
```cmake
cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS ../../components)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(finger-paint)
```

**`examples/finger-paint/main/CMakeLists.txt`** — add dependencies:
```cmake
idf_component_register(
    SRCS "finger_paint.c"
    INCLUDE_DIRS "."
    REQUIRES msp3520 nvs_flash console
)
```

### 2.3 Create sdkconfig.defaults

Copy from `examples/basic/sdkconfig.defaults` and add sysmon/perf options:
```
# Performance monitor
CONFIG_LV_USE_SYSMON=y
CONFIG_LV_USE_PERF_MONITOR=y
```

### 2.4 Create idf_component.yml

Same as basic example — pull in LVGL dependency:
```yaml
dependencies:
  lvgl/lvgl: "~9.5.0"
```

Wait — check if basic has one.

### 2.5 Write `main/finger_paint.c`

**Structure:**

```
app_main()
  ├── nvs_flash_init()
  ├── msp3520_create()
  ├── create_paint_ui()       ← LVGL mutex held
  │   ├── toolbar (top bar)
  │   │   ├── "Clear" button
  │   │   └── color swatches (R, G, B, W, Black)
  │   └── canvas (fills remaining area)
  │       └── draw border/grid at init
  ├── msp3520_register_console_commands()
  └── esp_console_start_repl()
```

**Canvas setup:**
- Create `lv_canvas` sized to fill below toolbar (320 × ~440)
- Allocate buffer from PSRAM: `heap_caps_malloc(320 * 440 * 3, MALLOC_CAP_SPIRAM)`
- Color format: `LV_COLOR_FORMAT_RGB888`
- Fill with white, then draw 1px border rectangle and optional light grid

**Drawing logic:**
- Add `LV_EVENT_PRESSING` callback on the canvas
- On each event: get current touch point via `lv_indev_get_point()`
- If previous point exists and distance is small: draw line from prev→current using canvas layer API
- If distance is large (finger lifted and re-pressed): just record point, don't draw line
- Store previous point in a static variable; reset on `LV_EVENT_RELEASED`

**Line drawing on canvas:**
```c
lv_layer_t layer;
lv_canvas_init_layer(canvas, &layer);
lv_draw_line_dsc_t dsc;
lv_draw_line_dsc_init(&dsc);
dsc.color = current_color;
dsc.width = 3;  // brush width
dsc.round_start = 1;
dsc.round_end = 1;
dsc.p1 = (lv_point_precise_t){prev_x, prev_y};
dsc.p2 = (lv_point_precise_t){curr_x, curr_y};
lv_draw_line(&layer, &dsc);
lv_canvas_finish_layer(canvas, &layer);
```

**Clear button:** calls `lv_canvas_fill_bg()` then redraws the border.

**Color swatches:** small colored buttons in the toolbar. Tap sets `current_color`.

**Border/grid drawing:**
- 1px border rectangle at canvas edges (shows screen boundary)
- Optional: light gray grid every 40px for spatial reference

### 2.6 Build and verify

```bash
cd /home/ok/src/esp-msp3520/examples/finger-paint
idf.py build
```

---

## File change summary

| File | Action |
|------|--------|
| `components/msp3520/src/console_commands.c` | Add `display perf` subcommand |
| `examples/finger-paint/CMakeLists.txt` | Edit scaffolded file |
| `examples/finger-paint/main/CMakeLists.txt` | Edit scaffolded file |
| `examples/finger-paint/main/finger_paint.c` | Rewrite scaffolded file |
| `examples/finger-paint/sdkconfig.defaults` | New file (based on basic) |

## Risks

- **Canvas layer performance**: Drawing a line per touch event through the layer API may be slow. If so, fall back to `lv_canvas_set_px()` for individual pixels or use Bresenham's line algorithm directly on the buffer.
- **Touch event rate**: LVGL may not fire `PRESSING` fast enough for smooth strokes at quick finger movement. If strokes look choppy, we could interpolate between points.
- **PSRAM allocation**: 320×440×3 = ~412KB. Should be fine with 8MB PSRAM.
