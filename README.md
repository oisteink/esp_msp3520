# esp-msp3520

Reusable ESP-IDF component for the MSP3520 display module (3.5" ILI9488 SPI TFT + XPT2046 resistive touch), with LVGL v9.5 integration.

## Hardware

- **Display**: 3.5" ILI9488 SPI TFT (MSP3520) with resistive touch
- **Target**: ESP32-S3-DevKitC-1 (tested)

See `docs/` for per-component details.

## Stack

- ESP-IDF v5.5.3
- LVGL v9.5 (RGB888, manual integration — no esp_lvgl_port)

## Structure

```
components/msp3520/     Reusable component (display, touch, LVGL, calibration, CLI)
examples/basic/         Simple tap-counter demo
examples/paint/         Full-screen drawing app (stylus recommended)
docs/                   Hardware and project documentation
```

## Quick Start

```sh
source ~/esp/v5.5.3/esp-idf/export.sh
cd examples/basic        # or examples/paint
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

## Examples

### basic
Minimal example: button with tap counter and coordinate display. Demonstrates component init, UI creation, and REPL.

### paint
Full-screen canvas drawing app. Draw with a stylus/pointer for best results, pick colors, clear the canvas. Includes a border grid to visualize touch edge-reach dead zones. Uses direct buffer drawing with partial invalidation for 100 FPS while drawing. LVGL performance monitor available via `display perf on`.

> **Tip**: This example uses Bresenham line algorithm with round brush (3px). For smooth lines, use a stylus/pointer — finger touch creates "straws" due to tracking jitter.

Both examples include input latency tuning (1kHz FreeRTOS tick, 10ms LVGL refresh, 2MHz touch SPI) — see `docs/project-overview.md` for details.
