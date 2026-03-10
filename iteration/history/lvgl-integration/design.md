# Iteration 2: LVGL Integration (Display Only)

## What we're building

Replace the color fill demo with an LVGL-driven UI. Minimal custom screen — a label ("Hello LVGL") and a colored rectangle — proving LVGL renders correctly on the ILI9488 over SPI in landscape (480x320).

## Architecture

Same SPI + ILI9488 driver stack from iteration 1. New layer on top:

```
LVGL (lv_timer_handler task)
  → flush callback
    → esp_lcd_panel_draw_bitmap
      → SPI DMA
```

- **Color format**: `LV_COLOR_FORMAT_RGB888` (3 bytes/pixel) — matches ILI9488's RGB666 over SPI (driver already handles this)
- **Buffers**: Dual DMA buffers, partial rendering mode. Size tuned for C6 RAM — 32 lines per buffer (320 * 32 * 3 = ~30KB each, ~60KB total)
- **Tick**: 2ms ESP periodic timer calling `lv_tick_inc()`
- **LVGL task**: FreeRTOS task running `lv_timer_handler()` in a loop, mutex-protected
- **Orientation**: Landscape via `esp_lcd_panel_swap_xy()` + `esp_lcd_panel_mirror()`
- **Flush completion**: SPI `on_color_trans_done` callback calls `lv_display_flush_ready()`

## Scope

- **In scope**: LVGL init, display driver glue, tick timer, LVGL task, simple UI (label + rectangle), landscape orientation
- **Out of scope**: touch (iteration 3), custom widgets, `lv_conf.h` customization beyond what's needed, animations

## Changes

- `main/ili9488-test.c` — rewrite: LVGL init, flush callback, tick timer, task, simple UI
- `main/CMakeLists.txt` — add `lvgl` dependency
- No changes to the ILI9488 driver component

## Acceptance criteria

1. `idf.py build` succeeds with no warnings for `esp32c6`
2. Display shows "Hello LVGL" label and a colored rectangle in landscape orientation
3. No crashes, no memory exhaustion on the C6
4. LVGL tick and task run continuously without watchdog triggers

## References

- Design validated from reference project: `~/src/zenith/zenith_components/zenith_display/`
- Iteration 1 spec: `iteration/spec.md`
- LVGL v9.5.0 (managed component, already added)
