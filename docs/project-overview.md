# Project Overview

## Structure

```
├── CMakeLists.txt              # Top-level ESP-IDF project file
├── main/                       # Application
│   ├── CMakeLists.txt
│   ├── idf_component.yml       # Managed component dependencies
│   ├── Kconfig.projbuild       # Pin and config menu
│   └── ili9488-test.c          # Entry point (app_main)
├── components/
│   └── esp_lcd_ili9488/        # ILI9488 display driver (local component)
│       ├── CMakeLists.txt
│       ├── esp_lcd_ili9488.c
│       └── include/
│           └── esp_lcd_ili9488.h
├── managed_components/         # Downloaded by ESP Component Manager
│   ├── atanisoft__esp_lcd_touch_xpt2046/
│   ├── espressif__esp_lcd_touch/
│   └── lvgl__lvgl/
├── iteration/                  # Current iteration stage docs
│   └── history/                # Archived iteration docs
│       ├── iteration-1/
│       ├── iteration-2/
│       └── iteration-3/
└── docs/                       # Hardware datasheets and project docs
```

## How it fits together

This is a standard ESP-IDF project targeting the **ESP32-S3**.

- **`main/`** — The application. Sets up SPI, initializes display and touch, runs LVGL with an interactive UI. Pin assignments are configurable via Kconfig.

- **`components/esp_lcd_ili9488/`** — Local ILI9488 driver using the `esp_lcd` panel interface. RGB888 passthrough (no color conversion), 3-param API. Only depends on ESP-IDF APIs so it stays reusable.

## External Components

Third-party components come from the [ESP Component Registry](https://components.espressif.com/). Dependencies are declared in `idf_component.yml` and downloaded into `managed_components/` on build.

To add a dependency:
```sh
idf.py add-dependency "namespace/component^version"
idf.py reconfigure
```

Current dependencies:
- **`lvgl/lvgl^9.5.0`** — Graphics library, LVGL v9.5 with RGB888 color format
- **`atanisoft/esp_lcd_touch_xpt2046^1.0.0`** — XPT2046 resistive touch driver (pulls in `espressif/esp_lcd_touch` as transitive dependency)

## Wiring: ESP32-S3 DevKitC-1 ↔ MSP3520

VCC powered from 5V (3.3V too weak for backlight). All IO is 3.3V TTL.
Display and touch share SPI bus (SPI2) via breadboard, with separate CS lines.

| Pin | Screen Label | Wire To | GPIO |
|-----|-------------|---------|------|
| 1 | VCC | 5V | - |
| 2 | GND | GND | - |
| 3 | CS | S3 | 3 |
| 4 | RESET | S3 | 46 |
| 5 | DC/RS | S3 | 9 |
| 6 | SDI (MOSI) | breadboard | 10 |
| 7 | SCK | breadboard | 11 |
| 8 | LED | S3 | 12 |
| 9 | SDO (MISO) | breadboard | 13 |
| 10 | T_CLK | breadboard | (11) |
| 11 | T_CS | S3 | 4 |
| 12 | T_DIN | breadboard | (10) |
| 13 | T_DO | breadboard | (13) |
| 14 | T_IRQ | S3 | 5 |

## Build

```sh
source ~/esp/v5.5.3/esp-idf/export.sh
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```
