# Iteration 2: LVGL Integration (Display Only)

## What we're building

Replace the color fill demo with an LVGL-driven UI showing a minimal custom screen — a label ("Hello LVGL") and a colored rectangle — proving LVGL renders correctly on the ILI9488 over SPI in landscape (480x320).

## Scope

- **In scope**: LVGL init, display driver glue (flush callback, DMA buffers), tick timer, LVGL task, simple UI, landscape orientation
- **Out of scope**: touch input (XPT2046 — next iteration), custom widgets, animations, `lv_conf.h` customization beyond defaults

## Hardware

- **Display**: MSP3520 — 3.5" 480x320 ILI9488, SPI (same as iteration 1)
- **Board**: NanoESP32-C6 (RISC-V, 160 MHz, no PSRAM, ~512KB SRAM)
- **Orientation**: Landscape (480x320) via MADCTL swap_xy + mirror

## Architecture

Iteration 1's SPI + ILI9488 driver stack unchanged. New LVGL layer on top:

```
LVGL (lv_timer_handler task)
  → flush callback
    → esp_lcd_panel_draw_bitmap
      → SPI DMA
```

### Color format

`LV_COLOR_FORMAT_RGB888` — 3 bytes/pixel. LVGL renders RGB888, ILI9488 accepts RGB666 over SPI (upper 6 bits of each byte used). No conversion needed in the flush callback.

### Buffers

Dual DMA buffers, partial rendering mode. Sized for C6 RAM constraints:
- 320 pixels wide × 32 lines × 3 bytes = ~30KB per buffer
- Two buffers = ~60KB total
- Allocated via `spi_bus_dma_memory_alloc()` for DMA alignment

### Tick and task

- 2ms ESP periodic timer calling `lv_tick_inc(2)`
- FreeRTOS task running `lv_timer_handler()` in a loop
- Mutex protects LVGL API calls

### Flush completion

`esp_lcd_panel_io_register_event_callbacks()` registers a callback that calls `lv_display_flush_ready()` when SPI DMA transfer completes.

## Changes from iteration 1

- `main/ili9488-test.c` — full rewrite: LVGL init, flush/tick/task, simple UI
- `main/CMakeLists.txt` — add `lvgl` to REQUIRES
- ILI9488 driver component — no changes

## Demo UI

- White or light background
- Centered label: "Hello LVGL"
- Colored rectangle (e.g., blue filled box) to prove rendering works

## Acceptance criteria

1. `idf.py build` succeeds with no warnings for `esp32c6`
2. Display shows "Hello LVGL" label and a colored rectangle in landscape
3. No crashes or memory exhaustion on the C6
4. LVGL tick and task run continuously without watchdog triggers

## References

- Design doc: `docs/plans/2026-03-08-lvgl-integration-design.md`
- Iteration 1 spec/plan: git history
- Reference implementation: `~/src/zenith/zenith_components/zenith_display/`
- LVGL v9.5.0 (managed component, already added as dependency)
