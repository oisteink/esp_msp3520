# Project Overview

## Hardware

- **Board**: ESP32-S3 DevKitC-1 (WROOM-2 N32R16V) — dual-core, 32MB octal flash, 16MB octal PSRAM. See [docs/devkits/esp32-s3-devkitc-1.md](devkits/esp32-s3-devkitc-1.md).
- **Display**: MSP3520 — 3.5" ILI9488 480x320 with XPT2046 resistive touch. See [docs/modules/msp3520.md](modules/msp3520.md).

## Structure

```
├── components/
│   └── msp3520/                  # Reusable component
│       ├── include/msp3520.h     # Public API
│       ├── src/                  # Implementation (display, touch, LVGL, calibration, CLI)
│       ├── CMakeLists.txt
│       └── Kconfig               # Pin and config menu
├── examples/
│   ├── basic/                    # Simple tap-counter demo
│   │   ├── main/main.c
│   │   └── sdkconfig.defaults
│   └── finger-paint/             # Full-screen drawing app
│       ├── main/finger-paint.c
│       └── sdkconfig.defaults
├── iteration/                    # Current iteration stage docs
│   └── history/                  # Archived iteration docs
└── docs/
    ├── devkits/                  # Board documentation
    └── modules/                  # Screen/peripheral documentation
```

## How it fits together

The **msp3520 component** (`components/msp3520/`) wraps all hardware and LVGL integration into a single reusable unit. Example projects in `examples/` consume it via `EXTRA_COMPONENT_DIRS`.

- **`components/msp3520/`** — Initializes SPI buses, ILI9488 display driver (RGB888), XPT2046 touch driver, LVGL (manual integration, no esp_lvgl_port), 3-point affine touch calibration with NVS persistence, and optional REPL commands. Pin assignments configurable via Kconfig.

- **`examples/basic/`** — Minimal example: button with tap counter, coordinate display, REPL console.

- **`examples/finger-paint/`** — Full-screen canvas drawing app with color picker and clear button. Border grid shows touch edge-reach dead zones. LVGL perf monitor togglable via CLI.

## External Components

Third-party components come from the [ESP Component Registry](https://components.espressif.com/). Dependencies are declared in `idf_component.yml` and downloaded into `managed_components/` on build.

Current dependencies:
- **`lvgl/lvgl^9.5.0`** — Graphics library
- **`espressif/esp_lcd_touch`** — Touch interface (pulled in by msp3520 component)

## Wiring: ESP32-S3 DevKitC-1 ↔ MSP3520

VCC powered from 5V (3.3V too weak for backlight). All IO is 3.3V TTL.
Display on SPI2, touch on dedicated SPI3. All direct wires, no breadboard.

| Pin | Screen Label | Function | GPIO |
|-----|-------------|----------|------|
| 1 | VCC | 5V power | - |
| 2 | GND | Ground | - |
| 3 | CS | Display CS (SPI2) | 3 |
| 4 | RESET | Display reset | 46 |
| 5 | DC/RS | Display data/command | 9 |
| 6 | SDI (MOSI) | Display MOSI (SPI2) | 10 |
| 7 | SCK | Display SCK (SPI2) | 11 |
| 8 | LED | Backlight | 12 |
| 9 | SDO (MISO) | Display MISO (SPI2) | 13 |
| 10 | T_CLK | Touch SCK (SPI3) | 6 |
| 11 | T_CS | Touch CS (SPI3) | 4 |
| 12 | T_DIN | Touch MOSI (SPI3) | 7 |
| 13 | T_DO | Touch MISO (SPI3) | 8 |
| 14 | T_IRQ | Touch interrupt | 5 |

## sdkconfig.defaults

Key non-default settings persisted in each example's `sdkconfig.defaults`:

- Octal flash (32MB) and octal PSRAM (16MB, 80MHz)
- SPI2 display pins and SPI3 touch pins
- `CONFIG_LOG_MAXIMUM_LEVEL=4` (DEBUG compiled in)
- Idle task watchdog disabled on both cores (LVGL blocks core 1)
- LVGL: 24-bit color depth, Montserrat 28 font

### Input latency tuning (both examples)

These settings reduce worst-case touch-to-pixel latency from ~40ms to ~12ms with negligible idle overhead:

- `CONFIG_FREERTOS_HZ=1000` — 1ms LVGL task sleep minimum (default 100 = 10ms)
- `CONFIG_LV_DEF_REFR_PERIOD=10` — 10ms indev read / display refresh (default 33ms)
- `CONFIG_MSP3520_TOUCH_SPI_CLOCK_KHZ=2000` — 2 MHz touch SPI (default 1 MHz, max 2.5 MHz)

### finger-paint extras

- `LV_USE_SYSMON`, `LV_USE_PERF_MONITOR` — LVGL performance overlay (toggled via `display perf on`)
- Uses direct buffer drawing with partial invalidation instead of the LVGL canvas layer API, which invalidates the entire canvas on every `lv_canvas_finish_layer()` call. This keeps FPS at 100 even while drawing.

## Console Commands

The msp3520 component provides optional REPL commands (registered via `msp3520_register_console_commands()`).

| Command | Description |
|---------|-------------|
| `touch` | Show touch status (z_threshold, calibration, flags) |
| `touch z <val>` | Set Z-pressure threshold (saved to NVS) |
| `touch rate <ms>` | Set touch read period in ms (1-100, runtime only) |
| `touch cal start` | Start 3-point crosshair calibration screen |
| `touch cal show` | Show calibration coefficients |
| `touch cal clear` | Clear calibration from NVS |
| `touch swap_xy\|mirror_x\|mirror_y <0\|1>` | Set touch coordinate flags |
| `display` | Show display info and usage |
| `display backlight <0-100>` | Set backlight brightness |
| `display rotation [swap_xy\|mirror_x\|mirror_y] [0\|1]` | Set display rotation flags |
| `display perf <on\|off>` | Toggle LVGL performance and memory monitor overlays (requires `LV_USE_PERF_MONITOR` / `LV_USE_MEM_MONITOR`) |

## Build

```sh
source ~/esp/v5.5.3/esp-idf/export.sh
cd examples/basic          # or examples/finger-paint
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```
