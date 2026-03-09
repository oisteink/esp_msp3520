# Project Overview

## Structure

```
├── CMakeLists.txt              # Top-level ESP-IDF project file
├── main/                       # Application
│   ├── CMakeLists.txt
│   └── ili9488-test.c          # Entry point (app_main)
├── components/
│   └── esp_lcd_ili9488/        # ILI9488 display driver (local component)
│       ├── CMakeLists.txt
│       ├── esp_lcd_ili9488.c
│       └── include/
│           └── esp_lcd_ili9488.h
└── docs/                       # Hardware datasheets and project docs
```

## How it fits together

This is a standard ESP-IDF project with two build units:

- **`main/`** — The application. Contains `app_main()` and will eventually set up SPI, initialize the display, and run demo/UI code. This is where we wire everything together.

- **`components/esp_lcd_ili9488/`** — A local ESP-IDF component providing the ILI9488 driver. ESP-IDF automatically discovers components in the `components/` directory and makes them available to `main/`. The component exposes its public API through `include/esp_lcd_ili9488.h`.

The app depends on the driver, not the other way around. The driver component should only depend on ESP-IDF APIs (SPI, GPIO, esp_lcd) so it stays reusable.

## External Components

Third-party components come from the [ESP Component Registry](https://components.espressif.com/). Dependencies are declared in `idf_component.yml` and downloaded into `managed_components/` on build.

To add a dependency:
```sh
idf.py add-dependency "namespace/component^version"
idf.py reconfigure
```

Current dependencies:
- **`lvgl/lvgl^9.5.0`** — graphics library (for future iterations)

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
