# Iteration 1: ILI9488 SPI Driver + Color Fill Demo

## What we're building

An ESP-IDF component driver for the ILI9488 (SPI interface) and a demo app that cycles the screen through solid red, green, and blue fills.

## Scope

- **In scope**: ILI9488 SPI driver component, color fill demo app
- **Out of scope**: touch (XPT2046 — next iteration), parallel 8080 interface, LVGL integration

## Hardware

- **Display**: MSP3520 — 3.5" 480x320 ILI9488, SPI, resistive touch (touch unused this iteration)
- **Board**: NanoESP32-C6 (RISC-V, 160 MHz, no PSRAM, ESP-IDF v5.5.3)
- **Next board**: ESP32-S3-DevKitC-1 (arriving soon; driver must not be board-specific)

## Driver requirements

### Interface

The driver implements the `esp_lcd_panel_t` interface so it plugs into ESP-IDF's `esp_lcd` framework. Single public function:

```c
esp_err_t esp_lcd_new_panel_ili9488(
    const esp_lcd_panel_io_handle_t io,
    const esp_lcd_panel_dev_config_t *panel_dev_config,
    esp_lcd_panel_handle_t *ret_panel);
```

### Panel operations

All standard `esp_lcd_panel_t` callbacks:

| Callback | Required |
|----------|----------|
| `del` | yes |
| `reset` | yes (HW reset via GPIO, SW reset fallback) |
| `init` | yes (full ILI9488 init sequence) |
| `draw_bitmap` | yes |
| `invert_color` | yes |
| `mirror` | yes |
| `swap_xy` | yes |
| `set_gap` | yes |
| `disp_on_off` | yes |

### Color mode

- **18-bit color (RGB666)** — 3 bytes per pixel over SPI. This is the only color mode the ILI9488 supports over SPI (16-bit pixel format is parallel-only).
- `bits_per_pixel` = 18 or 24 both map to RGB666 (24 = 3 bytes/pixel on the wire, upper 6 bits of each byte used).

### Portability

- The driver depends only on ESP-IDF APIs (`esp_lcd`, `driver/gpio`, `freertos`). No board-specific code.
- Pin assignments and SPI bus config live in the app, not the driver.

## Demo app requirements

- Configure SPI bus and LCD panel IO for the NanoESP32-C6
- Initialize the ILI9488 driver
- Fill the entire screen with solid red, then green, then blue, cycling continuously
- **Pin assignments and SPI config via Kconfig** (`main/Kconfig.projbuild`), accessible in code as `CONFIG_*` symbols. Provide `sdkconfig.defaults` with working values for the NanoESP32-C6.
- Configurable items: SPI MOSI, SCLK, CS, DC, RST, backlight GPIOs; SPI clock speed

Note: Espressif's own `spi_lcd_touch` example (ESP-IDF v5.5.3, `examples/peripherals/lcd/spi_lcd_touch/`) uses hardcoded `#define`s for pins and Kconfig only for controller selection. We go further by making pins configurable too — better for switching boards.

## Acceptance criteria

1. `idf.py build` succeeds with no warnings for the `esp32c6` target
2. Flashing to the NanoESP32-C6 shows solid color fills cycling on the display
3. The driver component has no board-specific code
4. The driver implements all `esp_lcd_panel_t` callbacks listed above

## Reference

- Existing working driver: `~/src/zenith/zenith_components/esp_lcd_ili9488/` (usage in `~/src/zenith/zenit_core`)
- Display datasheet/pinout: `docs/msp3520.md`
- Board pinout: `docs/nanoesp32-c6.md`
