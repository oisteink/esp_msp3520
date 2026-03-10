# Project Overview

## Hardware

- **Board**: ESP32-S3 DevKitC-1 (WROOM-2 N32R16V) — dual-core, 32MB octal flash, 16MB octal PSRAM. See [docs/devkits/esp32-s3-devkitc-1.md](devkits/esp32-s3-devkitc-1.md).
- **Display**: MSP3520 — 3.5" ILI9488 480x320 with XPT2046 resistive touch. See [docs/modules/msp3520.md](modules/msp3520.md).

## Structure

```
├── CMakeLists.txt              # Top-level ESP-IDF project file
├── sdkconfig.defaults          # Build defaults (octal flash/PSRAM, pins, etc.)
├── main/                       # Application
│   ├── CMakeLists.txt
│   ├── idf_component.yml       # Managed component dependencies
│   ├── Kconfig.projbuild       # Pin and config menu
│   ├── ili9488-test.c          # Entry point (app_main, HW init, LVGL setup)
│   ├── console.c               # REPL commands, calibration UI screen
│   ├── console.h               # Console API, app_context_t
│   ├── touch_calibration.c     # Affine calibration math, NVS persistence
│   └── touch_calibration.h     # Calibration types and API
├── components/
│   ├── esp_lcd_ili9488/        # ILI9488 display driver (local component)
│   └── xpt2046/                # Forked XPT2046 touch driver (from atanisoft v1.0.6)
├── managed_components/         # Downloaded by ESP Component Manager
│   ├── espressif__esp_lcd_touch/
│   └── lvgl__lvgl/
├── iteration/                  # Current iteration stage docs
│   └── history/                # Archived iteration docs
├── docs/
│   ├── devkits/                # Board documentation
│   ├── modules/                # Screen/peripheral documentation
│   └── plans/                  # Design documents per iteration
```

## How it fits together

This is a standard ESP-IDF v5.5.3 project targeting the **ESP32-S3 WROOM-2 N32R16V**.

- **`main/`** — The application. Initializes two SPI buses (display + touch), runs LVGL with an interactive UI and a REPL console. Pin assignments are configurable via Kconfig.

- **`components/esp_lcd_ili9488/`** — Local ILI9488 driver using the `esp_lcd` panel interface. RGB888 passthrough (no color conversion). Only depends on ESP-IDF APIs.

- **`components/xpt2046/`** — Forked XPT2046 touch driver (from atanisoft v1.0.6). Adds median filtering with outlier rejection, IRQ-driven detection, runtime Z-threshold control, and PENIRQ support.

- **LVGL** — v9.5, RGB888 color format, full-screen double-buffered rendering from PSRAM. LVGL task pinned to core 1.

## External Components

Third-party components come from the [ESP Component Registry](https://components.espressif.com/). Dependencies are declared in `idf_component.yml` and downloaded into `managed_components/` on build.

Current dependencies:
- **`lvgl/lvgl^9.5.0`** — Graphics library
- **`espressif/esp_lcd_touch`** — Touch interface (pulled in by local `xpt2046` component)

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

Key non-default settings persisted in `sdkconfig.defaults`:

- Octal flash (32MB) and octal PSRAM (16MB, 80MHz)
- SPI2 display pins and SPI3 touch pins
- `CONFIG_LOG_MAXIMUM_LEVEL=4` (DEBUG compiled in)
- Idle task watchdog disabled on both cores (LVGL blocks core 1)
- LVGL: 24-bit color depth, Montserrat 28 font

## Console Commands

REPL on UART (`tft>` prompt). Commands defined in `main/console.c`.

| Command | Description |
|---------|-------------|
| `touch` | Show touch status (z_threshold, calibration, flags) |
| `touch z <val>` | Set Z-pressure threshold (saved to NVS) |
| `touch cal start` | Start 3-point crosshair calibration screen |
| `touch cal show` | Show calibration coefficients |
| `touch cal clear` | Clear calibration from NVS |
| `touch swap_xy\|mirror_x\|mirror_y <0\|1>` | Set touch coordinate flags |
| `rotation [swap_xy\|mirror_x\|mirror_y] [0\|1]` | Get/set display rotation flags |
| `log_level <tag> <level>` | Set log level for a tag |
| `debug` | Toggle debug logging for app/driver tags |
| `info` | Show chip info, heap, uptime |

## Build

```sh
source ~/esp/v5.5.3/esp-idf/export.sh
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```
