# Paint Example

Full-screen canvas drawing application with color picker and clear button. Uses direct buffer drawing with partial invalidation for 100 FPS performance.

## Usage

1. **Display**: Connect to ESP32-S3 DevKitC-1 (MSP3520 display module)
2. **Operation**:
   - Touch and drag on the canvas to draw
   - Select colors from the toolbar (black, red, green, blue, white/eraser)
   - Tap the clear button to reset the canvas
3. **Performance Monitor**: Use CLI command `display perf on` to toggle LVGL performance overlay

## Stylus Pointer Recommended

This example uses Bresenham line algorithm with round brush (3px). **For best results, use a stylus/pointer instead of a finger** — finger touch tracking creates "straws" (gaps between line segments) due to jitter and large touch area.

## Requirements

- ESP-IDF v5.5.3
- ESP32-S3 DevKitC-1
- MSP3520 display module (ILI9488 + XPT2046 touch)
- LVGL v9.5 (included as managed component)

## Build and Run

```sh
source ~/esp/v5.5.3/esp-idf/export.sh
cd examples/paint
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

## Features

- Direct buffer drawing with partial invalidation (100 FPS)
- Color picker with eraser (white brush)
- Border grid shows touch edge-reach dead zones
- LVGL performance monitor (via `display perf on`)