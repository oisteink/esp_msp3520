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

## Build

```sh
source ~/esp/v5.5.3/esp-idf/export.sh
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```
